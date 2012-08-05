/****************************************************************************
 ****************************************************************************
 *
 * format/raw.c
 *
 ****************************************************************************
 ****************************************************************************/





#include <stdio.h>

#include "raw.h"
#include "../error.h"
#include "../debug.h"
#include "../verbose.h"
#include "../global.h"
#include "../disk.h"
#include "../fifo.h"
#include "../format.h"
#include "container.h"
#include "histogram.h"
#include "bounds.h"




/****************************************************************************
 *
 * misc helper functions
 *
 ****************************************************************************/




/****************************************************************************
 * raw_read_lookup2
 ****************************************************************************/
static int
raw_read_lookup2(
	struct bounds			*bnd,
	int				bnd_size,
	int				*lookup,
	int				*error)

	{
	int				i, j, l, h, w;

	/*
	 * find highest valid count increment it by one and fill lookup
	 * with it (areas out of bounds will generate invalid bit patterns)
	 */

	for (i = j = 0; i < bnd_size; i++) if (bnd[i].count > j) j = bnd[i].count;
	for (i = 0, j++; i < GLOBAL_NR_PULSE_LENGTHS; i++)
		{
		lookup[i] = j;
		if (error != NULL) error[i] = 0xff;
		}

	/* fill lookup */

	for (i = 0; i < bnd_size; i++)
		{
		debug_error_condition((bnd[i].read_low > 0x7fff) || (bnd[i].read_high > 0x7fff));
		debug_error_condition(bnd[i].read_low > bnd[i].read_high);
		l = (bnd[i].read_low  + 0xff) >> 8;
		w = (bnd[i].write     + 0x80) >> 8;
		h = (bnd[i].read_high + 0xff) >> 8;
		for (j = l; (j <= h) && (j < GLOBAL_NR_PULSE_LENGTHS); j++)
			{
			lookup[j] = bnd[i].count;
			if (error == NULL) continue;
			if (j < w) error[j] = w - j;
			else error[j] = j - w;
			}
		}
	return (0);
	}




/****************************************************************************
 *
 * functions for track handling
 *
 ****************************************************************************/




/****************************************************************************
 * raw_read_write_track
 ****************************************************************************/
static int
raw_read_write_track(
	struct fifo			*ffo_src,
	struct fifo			*ffo_dst)

	{
	int				size = fifo_get_wr_ofs(ffo_src);

	debug_error_condition(fifo_get_limit(ffo_dst) < size);
	if (fifo_copy_block(ffo_src, ffo_dst, size) == -1) return (0);
	fifo_set_flags(ffo_dst, fifo_get_flags(ffo_src));
	return (1);
	}



/****************************************************************************
 * raw_statistics
 ****************************************************************************/
static int
raw_statistics(
	union format			*fmt,
	struct fifo			*ffo_l0,
	int				track)

	{
	histogram_normal(ffo_l0, track, track);
	return (1);
	}



/****************************************************************************
 * raw_read_track
 ****************************************************************************/
static int
raw_read_track(
	union format			*fmt,
	struct container		*con,
	struct fifo			*ffo_src,
	struct fifo			*ffo_dst,
	struct disk_sector		*dsk_sct,
	int				track)

	{
	return (raw_read_write_track(ffo_src, ffo_dst));
	}



/****************************************************************************
 * raw_write_track
 ****************************************************************************/
static int
raw_write_track(
	union format			*fmt,
	struct fifo			*ffo_src,
	struct disk_sector		*dsk_sct,
	struct fifo			*ffo_dst,
	unsigned char			*data,
	int				track)

	{
	return (raw_read_write_track(ffo_src, ffo_dst));
	}




/****************************************************************************
 *
 * functions for configuration
 *
 ****************************************************************************/




/****************************************************************************
 * raw_set_defaults
 ****************************************************************************/
static void
raw_set_defaults(
	union format			*fmt)

	{
	debug(2, "setting defaults");
	}



/****************************************************************************
 * raw_set_dummy_option
 ****************************************************************************/
static int
raw_set_dummy_option(
	union format			*fmt,
	int				magic,
	int				val,
	int				ofs)

	{
	debug_error();
	return (0);
	}



/****************************************************************************
 * raw_get_sector_size
 ****************************************************************************/
static int
raw_get_sector_size(
	union format			*fmt,
	int				sector)

	{
	debug_error_condition(sector >= 0);
	return (GLOBAL_MAX_TRACK_SIZE);
	}



/****************************************************************************
 * raw_get_sectors
 ****************************************************************************/
static int
raw_get_sectors(
	union format			*fmt)

	{
	return (0);
	}



/****************************************************************************
 * raw_get_flags
 ****************************************************************************/
static int
raw_get_flags(
	union format			*fmt)

	{
	return (FORMAT_FLAG_GREEDY);
	}



/****************************************************************************
 * raw_get_data_offset
 ****************************************************************************/
static int
raw_get_data_offset(
	union format			*fmt)

	{
	return (-1);
	}



/****************************************************************************
 * raw_get_data_size
 ****************************************************************************/
static int
raw_get_data_size(
	union format			*fmt)

	{
	return (-1);
	}



/****************************************************************************
 * raw_dummy_options
 ****************************************************************************/
static struct format_option		raw_dummy_options[] =
	{
	FORMAT_OPTION_END
	};




/****************************************************************************
 *
 * used by external callers
 *
 ****************************************************************************/




/****************************************************************************
 * raw_format_desc
 ****************************************************************************/
struct format_desc			raw_format_desc =
	{
	.name             = "raw",
	.level            = 0,
	.set_defaults     = raw_set_defaults,
	.set_read_option  = raw_set_dummy_option,
	.set_write_option = raw_set_dummy_option,
	.set_rw_option    = raw_set_dummy_option,
	.get_sectors      = raw_get_sectors,
	.get_sector_size  = raw_get_sector_size,
	.get_flags        = raw_get_flags,
	.get_data_offset  = raw_get_data_offset,
	.get_data_size    = raw_get_data_size,
	.track_statistics = raw_statistics,
	.track_read       = raw_read_track,
	.track_write      = raw_write_track,
	.fmt_opt_rd       = raw_dummy_options,
	.fmt_opt_wr       = raw_dummy_options,
	.fmt_opt_rw       = raw_dummy_options
	};



/****************************************************************************
 * raw_read_lookup
 ****************************************************************************/
int
raw_read_lookup(
	struct bounds			*bnd,
	int				bnd_size,
	int				*lookup)

	{
	return (raw_read_lookup2(bnd, bnd_size, lookup, NULL));
	}



/****************************************************************************
 * raw_read_counter
 ****************************************************************************/
int
raw_read_counter(
	struct fifo			*ffo_l0,
	int				*lookup)

	{
	int				i = fifo_read_byte(ffo_l0);

	if (i == -1) return (-1);
	return (lookup[i & GLOBAL_PULSE_LENGTH_MASK]);
	}



/****************************************************************************
 * raw_write_counter
 ****************************************************************************/
int
raw_write_counter(
	struct fifo			*ffo_l0,
	struct raw_counter		*raw_cnt,
	int				i)

	{
	int				clip, val, precomp;

	if ((i < 0) || (i >= raw_cnt->bnd_size))
		{
		verbose(3, "could not convert invalid bit pattern at offset %d", fifo_get_wr_ofs(ffo_l0));
		raw_cnt->invalid++;
		i = raw_cnt->bnd_size - 1;
		}
	raw_cnt->this += raw_cnt->bnd[i].write;
	if (raw_cnt->last > 0)
		{
		precomp       = raw_cnt->precomp[raw_cnt->bnd_size * raw_cnt->last_i + i];
		raw_cnt->last -= precomp;
		raw_cnt->this += precomp;
		clip = 0, val = raw_cnt->last;
		if (val < 0x0300) clip = 1, val = 0x0300;
		if (val > 0x7fff) clip = 1, val = 0x7fff;
		if (clip) error_warning("precompensation lead to invalid catweasel counter 0x%04x at offset %d", raw_cnt->last, fifo_get_wr_ofs(ffo_l0));
		if (fifo_write_byte(ffo_l0, val >> 8) == -1) return (-1);
		}
	raw_cnt->last   = raw_cnt->this;
	raw_cnt->this   &= 0xff;
	raw_cnt->last_i = i; 
	return (0);
	}



/****************************************************************************
 * raw_read
 ****************************************************************************/
int
raw_read(
	struct fifo			*ffo_l0,
	struct fifo			*ffo_l1,
	struct bounds			*bnd,
	int				bnd_size)

	{
	int				i, lookup[GLOBAL_NR_PULSE_LENGTHS];

	/* create lookup table */

	raw_read_lookup(bnd, bnd_size, lookup);

	/* convert raw counter values to raw bits */

	debug(3, "raw_read ffo_l0->wr_ofs = %d, ffo_l1->limit = %d", fifo_get_wr_ofs(ffo_l0), fifo_get_limit(ffo_l1));
	while (1)
		{
		i = raw_read_counter(ffo_l0, lookup);
		if (i == -1) break;
		if (fifo_write_count(ffo_l1, i) == -1) debug_error();
		}
	fifo_write_flush(ffo_l1);
	debug(3, "raw_read ffo_l0->wr_ofs = %d, ffo_l1->wr_bitofs = %d", fifo_get_wr_ofs(ffo_l0), fifo_get_wr_bitofs(ffo_l1));
	return (0);
	}



/****************************************************************************
 * raw_read_map
 ****************************************************************************/
int
raw_read_map(
	struct fifo			*ffo_l0,
	struct fifo			*ffo_l1,
	struct bounds			*bnd,
	int				bnd_size,
	struct raw_map			*raw_map,
	int				raw_map_size)

	{
	int				b, e, i, j, s;
	int				lookup[GLOBAL_NR_PULSE_LENGTHS];
	int				error[GLOBAL_NR_PULSE_LENGTHS];

	/* create lookup table */

	raw_read_lookup2(bnd, bnd_size, lookup, error);

	/* convert raw counter values to raw bits */

	debug(3, "raw_read_map ffo_l0->wr_ofs = %d", fifo_get_wr_ofs(ffo_l0));
	if (ffo_l1 != NULL) debug(3, "raw_read_map ffo_l1->limit = %d", fifo_get_limit(ffo_l1));
	for (j = 0, s = 0; j < raw_map_size; j++)
		{
		b = fifo_read_byte(ffo_l0);
		if (b == -1) break;
		e = error[b & GLOBAL_PULSE_LENGTH_MASK];
		i = lookup[b & GLOBAL_PULSE_LENGTH_MASK];
		s += i + 1;
		raw_map[j] = (struct raw_map)
			{
			.length     = i + 1,
			.length_sum = s,
			.error      = e
			};
		if (ffo_l1 != NULL)
			{
			if (fifo_write_count(ffo_l1, i) == -1) debug_error();
			}
		}
	debug(3, "raw_read_map ffo_l0->wr_ofs = %d", fifo_get_wr_ofs(ffo_l0));
	if (ffo_l1 != NULL)
		{
		fifo_write_flush(ffo_l1);
		debug(3, "raw_read_map ffo_l1->wr_bitofs = %d", fifo_get_wr_bitofs(ffo_l1));
		}
	return (j);
	}



/****************************************************************************
 * raw_write
 ****************************************************************************/
int
raw_write(
	struct fifo			*ffo_l1,
	struct fifo			*ffo_l0,
	struct bounds			*bnd,
	short				*precomp,
	int				bnd_size)

	{
	struct raw_counter		raw_cnt = RAW_COUNTER_INIT(bnd, precomp, bnd_size);
	int				i, j, lookup[GLOBAL_NR_PULSE_LENGTHS];

	/* create lookup table */

	for (i = 0; i < GLOBAL_NR_PULSE_LENGTHS; i++) lookup[i] = -1;
	for (i = 0; i < bnd_size; i++)
		{
		j = bnd[i].count;
		debug_error_condition(j >= 128);
		lookup[j] = i;
		}

	/* convert raw bits to raw counter values and do precompensation */

	debug(3, "raw_write ffo_l1->wr_bitofs = %d, ffo_l0->limit = %d", fifo_get_wr_bitofs(ffo_l1), fifo_get_limit(ffo_l0));
	fifo_set_flags(ffo_l0, FIFO_FLAG_WRITABLE);
	while (1)
		{
		i = fifo_read_count(ffo_l1);
		if (i == -1) break;
		if (i >= 128) i = 127;
		if (raw_write_counter(ffo_l0, &raw_cnt, lookup[i]) == -1) return (-1);
		}
	if (raw_cnt.invalid > 0) error_warning("could not convert %d invalid bit patterns", raw_cnt.invalid);
	debug(3, "raw_write ffo_l1->rd_bitofs = %d, ffo_l0->wr_ofs = %d", fifo_get_rd_bitofs(ffo_l1), fifo_get_wr_ofs(ffo_l0));

	return (0);
	}
/******************************************************** Karsten Scheibler */
