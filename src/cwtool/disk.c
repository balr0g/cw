/****************************************************************************
 ****************************************************************************
 *
 * disk.c
 *
 ****************************************************************************
 ****************************************************************************/





#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "error.h"
#include "debug.h"
#include "verbose.h"
#include "global.h"
#include "options.h"
#include "file.h"
#include "fifo.h"
#include "image.h"
#include "setvalue.h"
#include "string.h"



struct disk_track_buffer
	{
	unsigned char			*data;
	int				size;
	};




/****************************************************************************
 *
 * misc helper functions
 *
 ****************************************************************************/




/****************************************************************************
 * disk_sectors_init
 ****************************************************************************/
static int
disk_sectors_init(
	struct disk_sector		*dsk_sct,
	struct disk_track		*dsk_trk,
	struct fifo			*ffo,
	int				write)

	{
	struct disk_sector		dsk_sct2[GLOBAL_NR_SECTORS];
	unsigned char			*data      = fifo_get_data(ffo);
	int				skew       = dsk_trk->skew;
	int				interleave = dsk_trk->interleave + 1;
	int				errors     = 0x7fffffff;
	int				sectors, size, i, j;

	debug_error_condition(dsk_trk->fmt_dsc->get_sectors == NULL);
	debug_error_condition(dsk_trk->fmt_dsc->get_sector_size == NULL);
	sectors = dsk_trk->fmt_dsc->get_sectors(&dsk_trk->fmt);

	/* initialize sct2[] */

	if (write) errors = 0;
	for (i = j = 0; i < sectors; j += dsk_sct2[i++].size)
		{
		dsk_sct2[i] = (struct disk_sector)
			{
			.number = i,
			.data   = &data[j],
			.offset = j,
			.size   = dsk_trk->fmt_dsc->get_sector_size(&dsk_trk->fmt, i),
			.err    = { .flags = DISK_ERROR_FLAG_NOT_FOUND, .errors = errors }
			};
		}

	/* set fifo limits for write or read */

	size = dsk_trk->fmt_dsc->get_sector_size(&dsk_trk->fmt, -1);
	if (write) fifo_set_limit(ffo, size);
	else if (sectors > 0) fifo_set_wr_ofs(ffo, size);

	/*
	 * on write apply sector shuffling according to skew and interleave.
	 * current calculation will fail if (sectors / interleave < 2), so it
	 * is applied only if (sectors / interleave >= 2)
	 */

	for (i = 0; i < sectors; i++)
		{
		if ((write) && (sectors / interleave >= 2)) j = (skew + (i / interleave) + ((i % interleave) * sectors + interleave - 1) / interleave) % sectors;
		else j = i;
		dsk_sct[i] = dsk_sct2[j];
		}

	return (size);
	}



/****************************************************************************
 * disk_info_update
 ****************************************************************************/
static void
disk_info_update(
	struct disk_info		*dsk_nfo,
	struct disk_track		*dsk_trk,
	struct disk_sector		*dsk_sct,
	int				track,
	int				try,
	int				offset,
	int				summary)

	{
	int				sectors = dsk_trk->fmt_dsc->get_sectors(&dsk_trk->fmt);
	int				i, j;

	dsk_nfo->track        = track;
	dsk_nfo->try          = try;
	dsk_nfo->sectors_good = 0;
	dsk_nfo->sectors_weak = 0;
	dsk_nfo->sectors_bad  = 0;
	for (i = 0; i < sectors; i++)
		{
		j = dsk_sct[i].number;
		dsk_nfo->sct_nfo[track][j] = (struct disk_sector_info)
			{
			.flags  = dsk_sct[i].err.flags,
			.offset = offset + dsk_sct[i].offset
			};
		if (dsk_sct[i].err.errors > 0) dsk_nfo->sectors_bad++;
		else if (dsk_sct[i].err.warnings > 0) dsk_nfo->sectors_weak++;
		else dsk_nfo->sectors_good++;
		}
	if (! summary) return;
	dsk_nfo->sum.tracks++;
	dsk_nfo->sum.sectors_good += dsk_nfo->sectors_good;
	dsk_nfo->sum.sectors_weak += dsk_nfo->sectors_weak;
	dsk_nfo->sum.sectors_bad  += dsk_nfo->sectors_bad;
	}



/****************************************************************************
 * disk_info_update_path
 ****************************************************************************/
static void
disk_info_update_path(
	struct disk_info		*dsk_nfo,
	char				*path)

	{
	string_copy(dsk_nfo->path, GLOBAL_MAX_PATH_SIZE, path);
	}



/****************************************************************************
 * disk_dump_bad_sector_lines
 ****************************************************************************/
static cw_void_t
disk_dump_bad_sector_lines(
	struct file			*fil,
	struct container		*con,
	cw_index_t			index,
	cw_count_t			start,
	cw_count_t			end)

	{
	cw_raw8_t			*data;
	cw_raw8_t			*error;
	struct container_lookup		*lkp;
	cw_char_t			string[0x10000];
	cw_index_t			i, j;

	data  = container_get_data(con, index);
	error = container_get_error(con, index);
	lkp   = container_get_lookup(con, index);
	for (i = start, j = 0; i < end; i++)
		{
		j += string_snprintf(&string[j], sizeof (string) - j, "%02x # %2d %d\n", data[i] & 0x7f, error[i], lkp[i].length);
		if (j < sizeof (string) / 2) continue;
		file_write(fil, string, j);
		j = 0;
		}
	if (j > 0) file_write(fil, string, j);
	}



/****************************************************************************
 * disk_dump_bad_sector
 ****************************************************************************/
static cw_count_t
disk_dump_bad_sector(
	struct file			*fil,
	struct container		*con,
	cw_count_t			track,
	cw_mode_t			clock,
	cw_count_t			sector)

	{
	struct range_sector		*rng_sec;
	cw_flag_t			flags = 8;	/* UGLY: flag "no correction" hard coded */
	cw_count_t			entries, range_entries, start, end, limit;
	cw_index_t			i, j, k, l, t;

	entries = container_get_entries(con);
	for (i = j = t = 0; i < entries; i++)
		{
		limit = container_get_limit(con, i);
		range_entries = container_get_range_entries(con, i);
		for (k = l = 0; k < range_entries; k++)
			{
			rng_sec = container_get_range_sector(con, i, k);
			if (range_sector_get_number(rng_sec) == sector) l++;
			}
		if (l == 0) continue;
		file_write_sprintf(fil, "track_data_hex %d %d %d {\n", track, clock, flags);
		t++;
		for (k = 0; k < range_entries; k++)
			{
			rng_sec = container_get_range_sector(con, i, k);
			if (range_sector_get_number(rng_sec) != sector) continue;
			j++;
			file_write_sprintf(fil, "##### start track %d sector %d (%d) #####\n", track, sector, j);

			/* sector header and sector gap */

			start = container_lookup_position(con, i, range_get_start(range_sector_header(rng_sec)));
			end   = container_lookup_position(con, i, range_get_end(range_sector_header(rng_sec)));
			if (end > 0)
				{

				/* UGLY: fixup for incorrect start value */

				if (start > 128) start -= 128;
				else start = 0;
				file_write_string(fil, "### sector header ###\n");
				disk_dump_bad_sector_lines(fil, con, i, start, end);
				start = end;
				end   = container_lookup_position(con, i, range_get_start(range_sector_data(rng_sec)));
				file_write_string(fil, "### sector gap between header and data ###\n");
				disk_dump_bad_sector_lines(fil, con, i, start, end);
				}

			/* sector data */

			start = container_lookup_position(con, i, range_get_start(range_sector_data(rng_sec)));
			end   = container_lookup_position(con, i, range_get_end(range_sector_data(rng_sec)));

			/* UGLY: fixup for incorrect end value */

			if (end < limit - 128) end += 128;
			else end = limit;
			file_write_string(fil, "### sector data ###\n");
			disk_dump_bad_sector_lines(fil, con, i, start, end);

			file_write_sprintf(fil, "##### end track %d sector %d (%d) #####\n", track, sector, j);
			}
		file_write_string(fil, "}\n");
		}
	return (t);
	}



/****************************************************************************
 * disk_dump_bad_sectors
 ****************************************************************************/
static cw_void_t
disk_dump_bad_sectors(
	struct disk_track		*dsk_trk,
	struct disk_sector		*dsk_sct,
	struct file			*fil,
	struct container		*con,
	cw_count_t			track,
	cw_mode_t			clock)

	{
	static cw_count_t		t = 0;
	static cw_bool_t		known = CW_BOOL_FALSE;
	cw_count_t			sectors = dsk_trk->fmt_dsc->get_sectors(&dsk_trk->fmt);
	cw_count_t			i, j;

	if (track < options_get_output_track_start()) return;
	if (track > options_get_output_track_end()) return;
	if (fil == NULL) return;
	for (i = j = 0; i < sectors; i++) if (dsk_sct[i].err.errors != 0) j++;
	if (j == 0) return;
	file_write_string(fil, "# cwtool raw text 3\n");
	if (! (dsk_trk->fmt_dsc->get_flags(&dsk_trk->fmt) & FORMAT_FLAG_OUTPUT))
		{
		file_write_sprintf(fil, "# track %d: format '%s' does not support raw output of bad sectors\n", track, dsk_trk->fmt_dsc->name);
		return;
		}
	for (i = 0; i < sectors; i++)
		{
		if (dsk_sct[i].err.errors == 0) continue;
		t += disk_dump_bad_sector(fil, con, track, clock, dsk_sct[i].number);
		}

	/*
	 * UGLY: using local static variables to count overall number of
	 *       tracks and using IMAGE_RAW_NR_HINTS directly
	 */

	if ((t >= IMAGE_RAW_NR_HINTS) && (! known))
		{
		error_warning("created bad sector output has too many tracks to be read at once");
		known = CW_BOOL_TRUE;
		}
	}



/****************************************************************************
 * disk_statistics_track
 ****************************************************************************/
static void
disk_statistics_track(
	struct disk			*dsk,
	union image			*img,
	int				track)

	{
	struct disk_track		*dsk_trk = &dsk->trk[track];
	unsigned char			data[GLOBAL_MAX_TRACK_SIZE] = { };
	struct fifo			ffo = FIFO_INIT(data, sizeof (data));

	if (track < options_get_disk_track_start()) goto done;
	if (track > options_get_disk_track_end()) goto done;
	if (dsk_trk->fmt_dsc == NULL) goto done;
	debug_error_condition(dsk_trk->fmt_dsc->track_statistics == NULL);

	/* return if track is optional and we got no data */

	if (! dsk->img_dsc_l0->track_read(img, &dsk_trk->img_trk, &ffo, NULL, 0, track))
		{
		if (dsk_trk->img_trk.flags & IMAGE_TRACK_FLAG_OPTIONAL) goto done;
		error("no data available for track %d", track);
		}
	dsk_trk->fmt_dsc->track_statistics(&dsk_trk->fmt, &ffo, track);
done:
	dsk->img_dsc_l0->track_done(img, &dsk_trk->img_trk, track);
	}



/****************************************************************************
 * disk_read_track_greedy2
 ****************************************************************************/
static int
disk_read_track_greedy2(
	struct disk			*dsk,
	struct disk_sector		*dsk_sct,
	struct disk_option		*dsk_opt,
	struct disk_info		*dsk_nfo,
	union image			*img_src,
	union image			*img_dst,
	struct container		*con,
	struct fifo			*ffo_src,
	struct fifo			*ffo_dst,
	int				track)

	{
	struct disk_track		*dsk_trk = &dsk->trk[track];
	int				offset   = dsk->img_dsc->offset(img_dst);
	int				t;

	for (t = 0; t <= dsk_opt->retry; t++)
		{
		fifo_reset(ffo_src);

		/*
		 * currently greedy is only used with format "raw". because
		 * it uses fifo-functions to append the data, we need to
		 * reset the fifo pointers before doing another try
		 */

		fifo_reset(ffo_dst);

		/*
		 * if this track is optional and we could not read
		 * it, because the drive only supports double steps,
		 * we simply ignore this track
		 */

		if (! dsk->img_dsc_l0->track_read(img_src, &dsk_trk->img_trk, ffo_src, NULL, 0, track)) break;
		if (! dsk_trk->fmt_dsc->track_read(&dsk_trk->fmt, con, ffo_src, ffo_dst, dsk_sct, track)) error("data too long on track %d", track);
		disk_info_update(dsk_nfo, dsk_trk, dsk_sct, track, t, offset, 0);
		if (dsk_opt->info_func != NULL) dsk_opt->info_func(dsk_nfo, 0);
		dsk->img_dsc->track_write(img_dst, &dsk_trk->img_trk, ffo_dst, dsk_sct, dsk_trk->fmt_dsc->get_sectors(&dsk_trk->fmt), track);
		}
	return (t);
	}



/****************************************************************************
 * disk_read_track_greedy
 ****************************************************************************/
static void
disk_read_track_greedy(
	struct disk			*dsk,
	struct disk_option		*dsk_opt,
	struct disk_info		*dsk_nfo,
	char				**path_src,
	union image			**img_src,
	int				img_src_count,
	union image			*img_dst,
	struct file			*fil_output,
	int				track)

	{
	struct disk_track		*dsk_trk = &dsk->trk[track];
	struct disk_sector		dsk_sct[GLOBAL_NR_SECTORS] = { };
	unsigned char			data_src[GLOBAL_MAX_TRACK_SIZE] = { };
	unsigned char			data_dst[GLOBAL_MAX_TRACK_SIZE] = { };
	struct container		*con;
	struct fifo			ffo_src = FIFO_INIT(data_src, sizeof (data_src));
	struct fifo			ffo_dst = FIFO_INIT(data_dst, sizeof (data_dst));
	int				offset  = dsk->img_dsc->offset(img_dst);
	int				i, t = 0;

	/*
	 * if this track is not within the wanted range, ignore it. because
	 * the format is greedy there is no fixed size for the data to be
	 * written, so nothing is written to img_dst
	 */

	if (track < options_get_disk_track_start()) goto done;
	if (track > options_get_disk_track_end()) goto done;

	/*
	 * skip this track if ffo_dst would contain 0 bytes, this is the case
	 * with format fill
	 */

	if (disk_sectors_init(dsk_sct, dsk_trk, &ffo_dst, 0) == 0) goto done;
	debug_error_condition(dsk_trk->fmt_dsc->track_read == NULL);

	con = container_init(NULL);
	for (i = 0; i < img_src_count; i++)
		{
		disk_info_update_path(dsk_nfo, path_src[i]);
		t += disk_read_track_greedy2(dsk, dsk_sct, dsk_opt, dsk_nfo, img_src[i], img_dst, con, &ffo_src, &ffo_dst, track);
		}
	disk_dump_bad_sectors(dsk_trk, dsk_sct, fil_output, con, track, dsk_trk->img_trk.clock);
	container_deinit(con);
	if ((t == 0) && (! (dsk_trk->img_trk.flags & IMAGE_TRACK_FLAG_OPTIONAL))) error("no data available for track %d", track);
	disk_info_update(dsk_nfo, dsk_trk, dsk_sct, track, t, offset, 1);
done:
	for (i = 0; i < img_src_count; i++) dsk->img_dsc_l0->track_done(img_src[i], &dsk_trk->img_trk, track);
	}



/****************************************************************************
 * disk_read_track_nongreedy2
 ****************************************************************************/
static int
disk_read_track_nongreedy2(
	struct disk			*dsk,
	struct disk_sector		*dsk_sct,
	struct disk_option		*dsk_opt,
	struct disk_info		*dsk_nfo,
	union image			*img_src,
	struct container		*con,
	struct fifo			*ffo_src,
	struct fifo			*ffo_dst,
	int				offset,
	int				track)

	{
	struct disk_track		*dsk_trk = &dsk->trk[track];
	int				b, t;

	for (b = -1, t = 0; (b != 0) && (t <= dsk_opt->retry); t++)
		{
		fifo_reset(ffo_src);

		/*
		 * if this track is optional and we could not read
		 * it, because the drive only supports double steps,
		 * we simply ignore this track
		 */

		if (! dsk->img_dsc_l0->track_read(img_src, &dsk_trk->img_trk, ffo_src, NULL, 0, track)) break;
		if (! dsk_trk->fmt_dsc->track_read(&dsk_trk->fmt, con, ffo_src, ffo_dst, dsk_sct, track)) error("data too long on track %d", track);
		disk_info_update(dsk_nfo, dsk_trk, dsk_sct, track, t, offset, 0);
		if (dsk_opt->info_func != NULL) dsk_opt->info_func(dsk_nfo, 0);
		b = dsk_nfo->sectors_bad;
		}
	return (t);
	}



/****************************************************************************
 * disk_read_track_nongreedy
 ****************************************************************************/
static void
disk_read_track_nongreedy(
	struct disk			*dsk,
	struct disk_option		*dsk_opt,
	struct disk_info		*dsk_nfo,
	char				**path_src,
	union image			**img_src,
	int				img_src_count,
	union image			*img_dst,
	struct file			*fil_output,
	int				track)

	{
	struct disk_track		*dsk_trk = &dsk->trk[track];
	struct disk_sector		dsk_sct[GLOBAL_NR_SECTORS] = { };
	unsigned char			data_src[GLOBAL_MAX_TRACK_SIZE] = { };
	unsigned char			data_dst[GLOBAL_MAX_TRACK_SIZE] = { };
	struct container		*con;
	struct fifo			ffo_src = FIFO_INIT(data_src, sizeof (data_src));
	struct fifo			ffo_dst = FIFO_INIT(data_dst, sizeof (data_dst));
	int				offset  = dsk->img_dsc->offset(img_dst);
	int				i, t = 0;

	/*
	 * skip this track if ffo_dst would contain 0 bytes, this is the case
	 * with format fill
	 */

	if (disk_sectors_init(dsk_sct, dsk_trk, &ffo_dst, 0) == 0) goto done;
	debug_error_condition(dsk_trk->fmt_dsc->track_read == NULL);

	/*
	 * if this track is not within the wanted range, just write zeros
	 * to img_dst and skip all reading routines. thats why
	 * disk_sectors_init() can not be skipped
	 */

	if (track < options_get_disk_track_start()) goto done_write;
	if (track > options_get_disk_track_end()) goto done_write;

	con = container_init(NULL);
	for (i = 0; i < img_src_count; i++)
		{
		disk_info_update_path(dsk_nfo, path_src[i]);
		t += disk_read_track_nongreedy2(dsk, dsk_sct, dsk_opt, dsk_nfo, img_src[i], con, &ffo_src, &ffo_dst, offset, track);
		if ((t > 0) && (dsk_nfo->sectors_bad == 0)) break;
		}
	disk_dump_bad_sectors(dsk_trk, dsk_sct, fil_output, con, track, dsk_trk->img_trk.clock);
	container_deinit(con);
	if ((t == 0) && (! (dsk_trk->img_trk.flags & IMAGE_TRACK_FLAG_OPTIONAL))) error("no data available for track %d", track);
	disk_info_update(dsk_nfo, dsk_trk, dsk_sct, track, t, offset, 1);
done_write:
	dsk->img_dsc->track_write(img_dst, &dsk_trk->img_trk, &ffo_dst, dsk_sct, dsk_trk->fmt_dsc->get_sectors(&dsk_trk->fmt), track);
done:
	for (i = 0; i < img_src_count; i++) dsk->img_dsc_l0->track_done(img_src[i], &dsk_trk->img_trk, track);
	}



/****************************************************************************
 * disk_read_track
 ****************************************************************************/
static void
disk_read_track(
	struct disk			*dsk,
	struct disk_option		*dsk_opt,
	struct disk_info		*dsk_nfo,
	char				**path_src,
	union image			**img_src,
	int				img_src_count,
	union image			*img_dst,
	struct file			*fil_output,
	int				track)

	{
	struct disk_track		*dsk_trk = &dsk->trk[track];

	/* skip this track if no format is defined */

	if (dsk_trk->fmt_dsc == NULL) return;
	debug_error_condition(dsk_trk->fmt_dsc->get_flags == NULL);
	if (dsk_trk->fmt_dsc->get_flags(&dsk_trk->fmt) & FORMAT_FLAG_GREEDY) disk_read_track_greedy(dsk, dsk_opt, dsk_nfo, path_src, img_src, img_src_count, img_dst, fil_output, track);
	else disk_read_track_nongreedy(dsk, dsk_opt, dsk_nfo, path_src, img_src, img_src_count, img_dst, fil_output, track);
	}



/****************************************************************************
 * disk_write_data_size
 ****************************************************************************/
static cw_size_t
disk_write_data_size(
	struct disk			*dsk)

	{
	struct disk_track		*dsk_trk;
	cw_bool_t			needed = CW_BOOL_FALSE;
	cw_index_t			t;
	cw_count_t			o;
	cw_size_t			s, size;

	for (t = 0, size = 0; t < GLOBAL_NR_TRACKS; t++)
		{
		dsk_trk = &dsk->trk[t];
		if (dsk_trk->fmt_dsc == NULL) continue;
		if (dsk_trk->fmt_dsc->get_data_offset == NULL) continue;
		if (dsk_trk->fmt_dsc->get_data_size == NULL) continue;
		o = dsk_trk->fmt_dsc->get_data_offset(&dsk_trk->fmt);
		s = dsk_trk->fmt_dsc->get_data_size(&dsk_trk->fmt);
		if ((o >= 0) && (s >= 0)) needed = CW_BOOL_TRUE;
		size += dsk_trk->fmt_dsc->get_sector_size(&dsk_trk->fmt, -1);
		}
	if (! needed) size = 0;
	return (size);
	}



/****************************************************************************
 * disk_write_data_get
 ****************************************************************************/
static cw_void_t
disk_write_data_get(
	struct disk			*dsk,
	struct disk_track_buffer	*dsk_trk_buf,
	union image			*img)

	{
	unsigned char			*data = dsk_trk_buf[0].data;
	cw_size_t			size = dsk_trk_buf[0].size;
	cw_index_t			i, t;
	cw_size_t			s;

	for (i = t = 0; t < GLOBAL_NR_TRACKS; t++)
		{
		struct disk_track	*dsk_trk = &dsk->trk[t];
		struct disk_sector	dsk_sct[GLOBAL_NR_SECTORS] = { };
		unsigned char		data_tmp[GLOBAL_MAX_TRACK_SIZE] = { };
		struct fifo		ffo_tmp = FIFO_INIT(data_tmp, sizeof (data_tmp));

		if (dsk_trk->fmt_dsc == NULL) continue;
		disk_sectors_init(dsk_sct, dsk_trk, &ffo_tmp, 1);
		if (fifo_get_limit(&ffo_tmp) == 0) continue;
		dsk->img_dsc->track_read(
			img,
			&dsk_trk->img_trk,
			&ffo_tmp,
			dsk_sct,
			dsk_trk->fmt_dsc->get_sectors(&dsk_trk->fmt),
			t);
		s = fifo_get_wr_ofs(&ffo_tmp);
		if (s == 0) continue;
		if (i + s > size) error_internal();
		dsk_trk_buf[t + 1] = (struct disk_track_buffer)
			{
			.data = &data[i],
			.size = s
			};
		memcpy(&data[i], fifo_get_data(&ffo_tmp), s);
		i += s;
		}
	}



/****************************************************************************
 * disk_write_track
 ****************************************************************************/
static void
disk_write_track(
	struct disk			*dsk,
	struct disk_option		*dsk_opt,
	struct disk_info		*dsk_nfo,
	struct disk_track_buffer	*dsk_trk_buf,
	union image			*img_src,
	union image			*img_dst,
	int				track)

	{
	struct disk_track		*dsk_trk = &dsk->trk[track];
	struct disk_sector		dsk_sct[GLOBAL_NR_SECTORS] = { };
	unsigned char			data_src[GLOBAL_MAX_TRACK_SIZE] = { };
	unsigned char			data_dst[GLOBAL_MAX_TRACK_SIZE] = { };
	unsigned char			*data = NULL;
	struct fifo			ffo_src = FIFO_INIT(data_src, sizeof (data_src));
	struct fifo			ffo_dst = FIFO_INIT(data_dst, sizeof (data_dst));
	int				offset, size;

	/* skip this track if no format is defined */

	if (dsk_trk->fmt_dsc == NULL) return;
	disk_sectors_init(dsk_sct, dsk_trk, &ffo_src, 1);
	debug_error_condition(dsk_trk->fmt_dsc->track_write == NULL);

	/*
	 * skip this track if we got 0 bytes but expected more than
	 * that from the image reader, but this will currently
	 * not happen. with format fill also 0 bytes are
	 * expected, so in this case we do not skip this track
	 */

	if (fifo_get_limit(&ffo_src) > 0)
		{
		if (dsk_trk_buf != NULL)
			{

			/*
			 * get data from dsk_trk_buf. dsk_trk_buf[0]
			 * contains size for while image
			 */

			offset = dsk_trk->fmt_dsc->get_data_offset(&dsk_trk->fmt);
			size   = dsk_trk->fmt_dsc->get_data_size(&dsk_trk->fmt);
			if (offset + size > dsk_trk_buf[0].size) data = NULL;
			else data = &dsk_trk_buf[0].data[offset];
			fifo_write_block(
				&ffo_src,
				dsk_trk_buf[track + 1].data,
				dsk_trk_buf[track + 1].size);
			}
		else dsk->img_dsc->track_read(img_src, &dsk_trk->img_trk, &ffo_src, dsk_sct, dsk_trk->fmt_dsc->get_sectors(&dsk_trk->fmt), track);
		if (fifo_get_wr_ofs(&ffo_src) == 0) return;
		}

	/*
	 * if this track is not within the wanted range, do no encoding and
	 * do not write to img_dsc_l0
	 */
	
	if (track < options_get_disk_track_start()) return;
	if (track > options_get_disk_track_end()) return;

	/* encode the data */

	if (! dsk_trk->fmt_dsc->track_write(&dsk_trk->fmt, &ffo_src, dsk_sct, &ffo_dst, data, track)) error("data too long on track %d", track);

	/*
	 * if this track is optional and we could not write it,
	 * because the drive only supports double steps, we simply
	 * continue with the next track
	 */

	if (! dsk->img_dsc_l0->track_write(img_dst, &dsk_trk->img_trk, &ffo_dst, NULL, 0, track)) return;
	disk_info_update(dsk_nfo, dsk_trk, dsk_sct, track, 0, 0, 1);
	if (dsk_opt->info_func != NULL) dsk_opt->info_func(dsk_nfo, 0);
	}




/****************************************************************************
 *
 * used by external callers
 *
 ****************************************************************************/




/****************************************************************************
 * disk_get
 ****************************************************************************/
struct disk *
disk_get(
	int				i)

	{
	static struct disk		dsk[GLOBAL_NR_DISKS];
	static int			disks;

	if (i == -1)
		{
		if (disks >= GLOBAL_NR_DISKS) error_message("too many disks defined");
		debug(1, "request for unused disk struct, disks = %d", disks);
		return (&dsk[disks++]);
		}
	if ((i >= 0) && (i < disks)) return (&dsk[i]);
	return (NULL);
	}



/****************************************************************************
 * disk_search
 ****************************************************************************/
struct disk *
disk_search(
	const char			*name)

	{
	struct disk			*dsk;
	int				i;

	for (i = 0; (dsk = disk_get(i)) != NULL; i++) if (string_equal(name, dsk->name)) break;
	return (dsk);
	}



/****************************************************************************
 * disk_init_track_default
 ****************************************************************************/
struct disk_track *
disk_init_track_default(
	struct disk			*dsk)

	{
	struct disk_track		dsk_trk = DISK_TRACK_INIT;

	dsk->trk_def = dsk_trk;
	return (&dsk->trk_def);
	}



/****************************************************************************
 * disk_init
 ****************************************************************************/
int
disk_init(
	struct disk			*dsk,
	int				revision)

	{
	*dsk = (struct disk)
		{
		.revision   = revision,
		.img_dsc_l0 = image_search_desc("raw"),
		.img_dsc    = image_search_desc("plain")
		};
	debug_error_condition((dsk->img_dsc_l0 == NULL) || (dsk->img_dsc == NULL));
	return (1);
	}



/****************************************************************************
 * disk_insert
 ****************************************************************************/
int
disk_insert(
	struct disk			*dsk)

	{
	struct disk			*dsk2;

	dsk2 = disk_search(dsk->name);
	if (dsk2 != NULL) *dsk2 = *dsk;
	else *disk_get(-1) = *dsk;
	return (1);
	}



/****************************************************************************
 * disk_copy
 ****************************************************************************/
int
disk_copy(
	struct disk			*dsk_dst,
	struct disk			*dsk_src)

	{
	char				name[GLOBAL_MAX_NAME_SIZE];
	char				info[GLOBAL_MAX_NAME_SIZE];
	int				revision;

	string_copy(name, GLOBAL_MAX_NAME_SIZE, dsk_dst->name);
	string_copy(info, GLOBAL_MAX_NAME_SIZE, dsk_dst->info);
	revision = dsk_dst->revision;
	*dsk_dst = *dsk_src;
	string_copy(dsk_dst->name, GLOBAL_MAX_NAME_SIZE, name);
	string_copy(dsk_dst->info, GLOBAL_MAX_NAME_SIZE, info);
	dsk_dst->revision = revision;
	return (1);
	}



/****************************************************************************
 * disk_tracks_used
 ****************************************************************************/
int
disk_tracks_used(
	struct disk			*dsk)

	{
	struct disk_track		*dsk_trk;
	int				s, t, used;

	for (t = used = 0; t < GLOBAL_NR_TRACKS; t++)
		{
		dsk_trk = &dsk->trk[t];
		if (dsk_trk->fmt_dsc == NULL) continue;
		debug_error_condition(dsk_trk->fmt_dsc->get_sectors == NULL);
		debug_error_condition(dsk_trk->fmt_dsc->get_sector_size == NULL);
		s = dsk_trk->fmt_dsc->get_sectors(&dsk_trk->fmt);
		debug_error_condition((s < 0) || (s > GLOBAL_NR_SECTORS));
		s = dsk_trk->fmt_dsc->get_sector_size(&dsk_trk->fmt, -1);
		debug_error_condition((s < 0) || (s > GLOBAL_MAX_TRACK_SIZE));
		dsk->size += s;
		used++;
		}
	return ((used > 0) ? 1 : 0);
	}



/****************************************************************************
 * disk_image_ok
 ****************************************************************************/
int
disk_image_ok(
	struct disk			*dsk)

	{
	struct disk_track		*dsk_trk;
	int				t;

	debug_error_condition(dsk->img_dsc == NULL);
	for (t = 0; t < GLOBAL_NR_TRACKS; t++)
		{
		dsk_trk = &dsk->trk[t];
		if (dsk_trk->fmt_dsc == NULL) continue;
		if (dsk_trk->fmt_dsc->level == -1) continue;
		if (dsk_trk->fmt_dsc->level != dsk->img_dsc->level) return (0);
		}
	return (1);
	}



/****************************************************************************
 * disk_check_track
 ****************************************************************************/
int
disk_check_track(
	int				track)

	{
	if ((track < 0) || (track >= GLOBAL_NR_TRACKS)) return (0);
	return (1);
	}



/****************************************************************************
 * disk_check_sectors
 ****************************************************************************/
int
disk_check_sectors(
	int				sectors)

	{
	if ((sectors < 0) || (sectors > GLOBAL_NR_SECTORS)) return (0);
	return (1);
	}



/****************************************************************************
 * disk_set_image
 ****************************************************************************/
int
disk_set_image(
	struct disk			*dsk,
	struct image_desc		*img_dsc)

	{
	dsk->img_dsc = img_dsc;
	return (1);
	}



/****************************************************************************
 * disk_set_name
 ****************************************************************************/
int
disk_set_name(
	struct disk			*dsk,
	const char			*name)

	{
	struct disk			*dsk2 = disk_search(name);

	if (dsk2 != NULL) if (dsk->revision <= dsk2->revision) return (0);
	string_copy(dsk->name, GLOBAL_MAX_NAME_SIZE, name);
	return (1);
	}



/****************************************************************************
 * disk_set_info
 ****************************************************************************/
int
disk_set_info(
	struct disk			*dsk,
	const char			*info)

	{
	string_copy(dsk->info, GLOBAL_MAX_NAME_SIZE, info);
	return (1);
	}



/****************************************************************************
 * disk_set_track
 ****************************************************************************/
int
disk_set_track(
	struct disk			*dsk,
	struct disk_track		*dsk_trk,
	int				track)

	{
	if ((track < 0) || (track >= GLOBAL_NR_TRACKS)) return (0);
	debug(2, "setting track %d", track);
	dsk->trk[track] = *dsk_trk;
	return (1);
	}



/****************************************************************************
 * disk_set_format
 ****************************************************************************/
int
disk_set_format(
	struct disk_track		*dsk_trk,
	struct format_desc		*fmt_dsc)

	{
	dsk_trk->fmt_dsc = fmt_dsc;
	debug_error_condition(dsk_trk->fmt_dsc->set_defaults == NULL);
	dsk_trk->fmt_dsc->set_defaults(&dsk_trk->fmt);
	return (1);
	}



/****************************************************************************
 * disk_set_image_track_flag
 ****************************************************************************/
int
disk_set_image_track_flag(
	struct disk_track		*dsk_trk,
	int				val,
	int				bit)

	{
	return (setvalue_uchar_bit(&dsk_trk->img_trk.flags, val, bit));
	}



/****************************************************************************
 * disk_set_clock
 ****************************************************************************/
int
disk_set_clock(
	struct disk_track		*dsk_trk,
	int				clock)

	{
	if (clock == 14)      dsk_trk->img_trk.clock = 0;
	else if (clock == 28) dsk_trk->img_trk.clock = 1;
	else if (clock == 56) dsk_trk->img_trk.clock = 2;
	else return (0);
	return (1);
	}



/****************************************************************************
 * disk_set_side_offset
 ****************************************************************************/
int
disk_set_side_offset(
	struct disk_track		*dsk_trk,
	int				side_offset)

	{
	return (setvalue_uchar(&dsk_trk->img_trk.side_offset, side_offset, 0, GLOBAL_NR_TRACKS / 2));
	}



/****************************************************************************
 * disk_set_timeout_read
 ****************************************************************************/
int
disk_set_timeout_read(
	struct disk_track		*dsk_trk,
	int				timeout)

	{
	return (setvalue_ushort(&dsk_trk->img_trk.timeout_read, timeout, CW_MIN_TIMEOUT + 1, CW_MAX_TIMEOUT - 1));
	}



/****************************************************************************
 * disk_set_timeout_write
 ****************************************************************************/
int
disk_set_timeout_write(
	struct disk_track		*dsk_trk,
	int				timeout)

	{
	return (setvalue_ushort(&dsk_trk->img_trk.timeout_write, timeout, CW_MIN_TIMEOUT + 1, CW_MAX_TIMEOUT - 1));
	}



/****************************************************************************
 * disk_set_read_option
 ****************************************************************************/
int
disk_set_read_option(
	struct disk_track		*dsk_trk,
	struct format_option		*fmt_opt,
	int				num,
	int				ofs)

	{
	debug_error_condition(dsk_trk->fmt_dsc == NULL);
	debug_error_condition(dsk_trk->fmt_dsc->set_read_option == NULL);
	return (dsk_trk->fmt_dsc->set_read_option(&dsk_trk->fmt, fmt_opt->magic, num, ofs));
	}



/****************************************************************************
 * disk_set_write_option
 ****************************************************************************/
int
disk_set_write_option(
	struct disk_track		*dsk_trk,
	struct format_option		*fmt_opt,
	int				num,
	int				ofs)

	{
	debug_error_condition(dsk_trk->fmt_dsc == NULL);
	debug_error_condition(dsk_trk->fmt_dsc->set_write_option == NULL);
	return (dsk_trk->fmt_dsc->set_write_option(&dsk_trk->fmt, fmt_opt->magic, num, ofs));
	}



/****************************************************************************
 * disk_set_rw_option
 ****************************************************************************/
int
disk_set_rw_option(
	struct disk_track		*dsk_trk,
	struct format_option		*fmt_opt,
	int				num,
	int				ofs)

	{
	debug_error_condition(dsk_trk->fmt_dsc == NULL);
	debug_error_condition(dsk_trk->fmt_dsc->set_rw_option == NULL);
	return (dsk_trk->fmt_dsc->set_rw_option(&dsk_trk->fmt, fmt_opt->magic, num, ofs));
	}



/****************************************************************************
 * disk_set_skew
 ****************************************************************************/
int
disk_set_skew(
	struct disk_track		*dsk_trk,
	int				skew)

	{
	debug_error_condition(dsk_trk->fmt_dsc == NULL);
	debug_error_condition(dsk_trk->fmt_dsc->get_sectors == NULL);
	if (dsk_trk->fmt_dsc->get_sectors(&dsk_trk->fmt) < 2) return (0);
	return (setvalue_uchar(&dsk_trk->skew, skew, 0, GLOBAL_NR_SECTORS - 1));
	}



/****************************************************************************
 * disk_set_interleave
 ****************************************************************************/
int
disk_set_interleave(
	struct disk_track		*dsk_trk,
	int				interleave)

	{
	debug_error_condition(dsk_trk->fmt_dsc == NULL);
	debug_error_condition(dsk_trk->fmt_dsc->get_sectors == NULL);
	if (dsk_trk->fmt_dsc->get_sectors(&dsk_trk->fmt) < 2) return (0);
	return (setvalue_uchar(&dsk_trk->interleave, interleave, 0, GLOBAL_NR_SECTORS - 1));
	}



/****************************************************************************
 * disk_set_sector_number
 ****************************************************************************/
int
disk_set_sector_number(
	struct disk_sector		*dsk_sct,
	int				number)

	{
	debug_error_condition((number < 0) || (number >= GLOBAL_NR_SECTORS));
	dsk_sct->number = number;
	return (0);
	}



/****************************************************************************
 * disk_get_sector_number
 ****************************************************************************/
int
disk_get_sector_number(
	struct disk_sector		*dsk_sct)

	{
	return (dsk_sct->number);
	}



/****************************************************************************
 * disk_get_sectors
 ****************************************************************************/
int
disk_get_sectors(
	struct disk_track		*dsk_trk)

	{
	debug_error_condition(dsk_trk->fmt_dsc == NULL);
	debug_error_condition(dsk_trk->fmt_dsc->get_sectors == NULL);
	return (dsk_trk->fmt_dsc->get_sectors(&dsk_trk->fmt));
	}



/****************************************************************************
 * disk_get_name
 ****************************************************************************/
const char *
disk_get_name(
	struct disk			*dsk)

	{
	debug_error_condition(dsk == NULL);
	return (dsk->name);
	}



/****************************************************************************
 * disk_get_info
 ****************************************************************************/
const char *
disk_get_info(
	struct disk			*dsk)

	{
	debug_error_condition(dsk == NULL);
	return (dsk->info);
	}



/****************************************************************************
 * disk_get_format
 ****************************************************************************/
struct format_desc *
disk_get_format(
	struct disk_track		*dsk_trk)

	{
	debug_error_condition(dsk_trk == NULL);
	return (dsk_trk->fmt_dsc);
	}



/****************************************************************************
 * disk_get_read_options
 ****************************************************************************/
struct format_option *
disk_get_read_options(
	struct disk_track		*dsk_trk)

	{
	debug_error_condition(dsk_trk->fmt_dsc == NULL);
	debug_error_condition(dsk_trk->fmt_dsc->fmt_opt_rd == NULL);
	return (dsk_trk->fmt_dsc->fmt_opt_rd);
	}



/****************************************************************************
 * disk_get_write_options
 ****************************************************************************/
struct format_option *
disk_get_write_options(
	struct disk_track		*dsk_trk)

	{
	debug_error_condition(dsk_trk->fmt_dsc == NULL);
	debug_error_condition(dsk_trk->fmt_dsc->fmt_opt_wr == NULL);
	return (dsk_trk->fmt_dsc->fmt_opt_wr);
	}



/****************************************************************************
 * disk_get_rw_options
 ****************************************************************************/
struct format_option *
disk_get_rw_options(
	struct disk_track		*dsk_trk)

	{
	debug_error_condition(dsk_trk->fmt_dsc == NULL);
	debug_error_condition(dsk_trk->fmt_dsc->fmt_opt_rw == NULL);
	return (dsk_trk->fmt_dsc->fmt_opt_rw);
	}



/****************************************************************************
 * disk_error_add
 ****************************************************************************/
int
disk_error_add(
	struct disk_error		*dsk_err,
	int				flags,
	int				add)

	{
	if (add > 0) dsk_err->flags |= flags, dsk_err->errors += add;
	return (1);
	}



/****************************************************************************
 * disk_warning_add
 ****************************************************************************/
int
disk_warning_add(
	struct disk_error		*dsk_err,
	int				add)

	{
	if (add > 0) dsk_err->warnings += add;
	return (1);
	}



/****************************************************************************
 * disk_sector_read
 ****************************************************************************/
int
disk_sector_read(
	struct disk_sector		*dsk_sct,
	struct disk_error		*dsk_err,
	unsigned char			*data)

	{
	if (((dsk_sct->err.errors == dsk_err->errors) && (dsk_sct->err.warnings < dsk_err->warnings)) ||
		(dsk_sct->err.errors < dsk_err->errors)) return (0);
	dsk_sct->err = *dsk_err;
	if (data != NULL) memcpy(dsk_sct->data, data, dsk_sct->size);
	return (1);
	}



/****************************************************************************
 * disk_sector_write
 ****************************************************************************/
int
disk_sector_write(
	unsigned char			*data,
	struct disk_sector		*dsk_sct)

	{
	memcpy(data, dsk_sct->data, dsk_sct->size);
	return (1);
	}



/****************************************************************************
 * disk_statistics
 ****************************************************************************/
int
disk_statistics(
	struct disk			*dsk,
	char				*path)

	{
	union image			img;
	int				t;

	debug_error_condition(dsk->img_dsc_l0->open == NULL);
	debug_error_condition(dsk->img_dsc_l0->close == NULL);
	debug_error_condition(dsk->img_dsc_l0->track_read == NULL);

	/* open image */

	dsk->img_dsc_l0->open(&img, path, IMAGE_MODE_READ, IMAGE_FLAG_NONE);

	/* iterate over all defined tracks */

	for (t = 0; t < GLOBAL_NR_TRACKS; t++) disk_statistics_track(dsk, &img, t);

	/* close image */

	dsk->img_dsc_l0->close(&img);

	/* done */

	return (1);
	}



/****************************************************************************
 * disk_read
 ****************************************************************************/
int
disk_read(
	struct disk			*dsk,
	struct disk_option		*dsk_opt,
	char				**path_src,
	int				path_src_count,
	char				*path_dst,
	char				*path_output)

	{

	/*
	 * it would be no good idea to put img_src[] onto the stack, because
	 * it could exceed the allowed stack size (especially
	 * struct image_raw is large). so we just put an array of pointers
	 * on the stack and malloc each struct image later
	 */

	struct disk_info		dsk_nfo = { };
	union image			*img_src[GLOBAL_NR_IMAGES], img_dst;
	struct file			fil;
	struct file			*fil_output = NULL;
	int				i, t;

	debug_error_condition(dsk->img_dsc_l0->open == NULL);
	debug_error_condition(dsk->img_dsc_l0->close == NULL);
	debug_error_condition(dsk->img_dsc_l0->track_read == NULL);
	debug_error_condition(dsk->img_dsc->open == NULL);
	debug_error_condition(dsk->img_dsc->close == NULL);
	debug_error_condition(dsk->img_dsc->track_write == NULL);

	/* open images */

	for (i = 0; i < path_src_count; i++)
		{
		img_src[i] = (union image *) malloc(sizeof (union image));
		if (img_src[i] == NULL) error_oom();
		dsk->img_dsc_l0->open(img_src[i], path_src[i], IMAGE_MODE_READ, IMAGE_FLAG_NONE);
		}
	dsk->img_dsc->open(&img_dst, path_dst, IMAGE_MODE_WRITE, IMAGE_FLAG_NONE);

	/* open output file for raw bad sectors */

	if (path_output != NULL)
		{
		file_open(&fil, path_output, FILE_MODE_CREATE, FILE_FLAG_NONE);
		fil_output = &fil;
		}

	/* iterate over all tracks */

	for (t = 0; t < GLOBAL_NR_TRACKS; t++) disk_read_track(dsk, dsk_opt, &dsk_nfo, path_src, img_src, path_src_count, &img_dst, fil_output, t);
	if (dsk_opt->info_func != NULL) dsk_opt->info_func(&dsk_nfo, 1);

	/* close output file */

	if (fil_output != NULL) file_close(fil_output);

	/* close images */

	for (i = 0; i < path_src_count; i++)
		{
		dsk->img_dsc_l0->close(img_src[i]);
		free(img_src[i]);
		}
	dsk->img_dsc->close(&img_dst);

	/* done */

	return (1);
	}



/****************************************************************************
 * disk_write
 ****************************************************************************/
int
disk_write(
	struct disk			*dsk,
	struct disk_option		*dsk_opt,
	char				*path_src,
	char				*path_dst)

	{
	struct disk_info		dsk_nfo = { };
	struct disk_track_buffer	dsk_trk_buf[GLOBAL_NR_TRACKS + 1] = { };
	union image			img_src, img_dst;
	int				flags = (dsk_opt->flags & DISK_OPTION_FLAG_IGNORE_SIZE) ? IMAGE_FLAG_IGNORE_SIZE : IMAGE_FLAG_NONE;
	int				t;

	debug_error_condition(dsk->img_dsc->open == NULL);
	debug_error_condition(dsk->img_dsc->close == NULL);
	debug_error_condition(dsk->img_dsc->track_read == NULL);
	debug_error_condition(dsk->img_dsc_l0->open == NULL);
	debug_error_condition(dsk->img_dsc_l0->close == NULL);
	debug_error_condition(dsk->img_dsc_l0->track_write == NULL);

	/* open images */

	dsk->img_dsc->open(&img_src, path_src, IMAGE_MODE_READ, flags);
	dsk->img_dsc_l0->open(&img_dst, path_dst, IMAGE_MODE_WRITE, IMAGE_FLAG_NONE);

	/*
	 * if a format wants specific data from an image, the image is read
	 * first completely and stored into memory
	 */

	dsk_trk_buf[0].size = disk_write_data_size(dsk);
	if (dsk_trk_buf[0].size > 0)
		{
		dsk_trk_buf[0].data = malloc(dsk_trk_buf[0].size * sizeof (unsigned char));
		if (dsk_trk_buf[0].data == NULL) error_oom();
		disk_write_data_get(dsk, dsk_trk_buf, &img_src);
		for (t = 0; t < GLOBAL_NR_TRACKS; t++) disk_write_track(dsk, dsk_opt, &dsk_nfo, dsk_trk_buf, NULL, &img_dst, t);
		free(dsk_trk_buf[0].data);
		}
	else
		{
		for (t = 0; t < GLOBAL_NR_TRACKS; t++) disk_write_track(dsk, dsk_opt, &dsk_nfo, NULL, &img_src, &img_dst, t);
		}
	if (dsk_opt->info_func != NULL) dsk_opt->info_func(&dsk_nfo, 1);

	/* close images */

	dsk->img_dsc->close(&img_src);
	dsk->img_dsc_l0->close(&img_dst);

	/* done */

	return (1);
	}
/******************************************************** Karsten Scheibler */
