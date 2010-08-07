/****************************************************************************
 ****************************************************************************
 *
 * gcr_apple.c
 *
 ****************************************************************************
 *
 * - EXPERIMENTAL, currently not used by the default config
 * - implements 6-and-2 encoding
 *
 ****************************************************************************
 ****************************************************************************/





#include <stdio.h>

#include "gcr_apple.h"
#include "error.h"
#include "debug.h"
#include "verbose.h"
#include "cwtool.h"
#include "disk.h"
#include "fifo.h"
#include "format.h"
#include "raw.h"
#include "setvalue.h"




/****************************************************************************
 *
 * low level gcr functions
 *
 ****************************************************************************/




/****************************************************************************
 * gcr_read_sync
 ****************************************************************************/
static int
gcr_read_sync(
	struct fifo			*ffo_l1,
	int				val)

	{
	int				i, bits;
	int				reg = fifo_read_bits(ffo_l1, 15);

	if (reg == -1) return (-1);
	bits = fifo_read_bits(ffo_l1, 8);
	if (bits == -1) return (-1);
	bits = reg = (reg << 8) | bits;
	while (1)
		{
		bits = fifo_read_bits(ffo_l1, 8);
		if (bits == -1) return (-1);
		bits = reg = (reg << 8) | bits;
		for (i = 0; i < 8; i++, bits >>= 1) if ((bits & 0xffffff) == val) goto found;
		}
found:
	fifo_set_rd_bitofs(ffo_l1, fifo_get_rd_bitofs(ffo_l1) - i);
	verbose(2, "got sync at bit offset %d with value 0x%06x", fifo_get_rd_bitofs(ffo_l1) - 24, val);
	return (1);
	}



/****************************************************************************
 * gcr_write_sync
 ****************************************************************************/
static int
gcr_write_sync(
	struct fifo			*ffo_l1,
	int				val)

	{
	verbose(2, "writing sync at bit offset %d with value 0x%06x", fifo_get_wr_bitofs(ffo_l1), val);
	if (fifo_write_bits(ffo_l1, val >> 16, 8) == -1) return (-1);
	if (fifo_write_bits(ffo_l1, val & 0xffff, 16) == -1) return (-1);
	return (0);
	}



/****************************************************************************
 * gcr_write_fill
 ****************************************************************************/
static int
gcr_write_fill(
	struct fifo			*ffo_l1,
	int				val,
	int				size)

	{
	verbose(2, "writing fill at bit offset %d with value 0x%04x", fifo_get_wr_bitofs(ffo_l1), val);
	while (size-- > 0) if (fifo_write_bits(ffo_l1, val, 10) == -1) return (-1);
	return (0);
	}



/****************************************************************************
 * gcr_read_8header_bits
 ****************************************************************************/
static int
gcr_read_8header_bits(
	struct fifo			*ffo_l1,
	struct disk_error		*dsk_err,
	int				ofs)

	{
	int				val = fifo_read_bits(ffo_l1, 16);

	if (val == -1) return (-1);
	if ((val & 0xaaaa) != 0xaaaa)
		{
		verbose(3, "wrong header clock bit around bit offset %d (byte %d)", fifo_get_rd_bitofs(ffo_l1), ofs);
		disk_error_add(dsk_err, DISK_ERROR_FLAG_ENCODING, 1);
		}
	return (val & (val >> 7) & 0xff);
	}



/****************************************************************************
 * gcr_write_8header_bits
 ****************************************************************************/
static int
gcr_write_8header_bits(
	struct fifo			*ffo_l1,
	int				val)

	{
	return (fifo_write_bits(ffo_l1, val | (val << 7) | 0xaaaa, 16));
	}



/****************************************************************************
 * gcr_read_header_bytes
 ****************************************************************************/
static int
gcr_read_header_bytes(
	struct fifo			*ffo_l1,
	struct disk_error		*dsk_err,
	unsigned char			*data,
	int				size)

	{
	int				bitofs = fifo_get_rd_bitofs(ffo_l1);
	int				d, i;

	for (i = 0; i < size; i++)
		{
		d = gcr_read_8header_bits(ffo_l1, dsk_err, i);
		if (d == -1) return (-1);
		data[i] = d;
		}
	verbose(2, "read %d header bytes at bit offset %d with", i, bitofs);
	return (0);
	}



/****************************************************************************
 * gcr_write_header_bytes
 ****************************************************************************/
static int
gcr_write_header_bytes(
	struct fifo			*ffo_l1,
	unsigned char			*data,
	int				size)

	{
	int				i;

	verbose(2, "writing %d header bytes at bit offset %d", size, fifo_get_wr_bitofs(ffo_l1));
	for (i = 0; i < size; i++) gcr_write_8header_bits(ffo_l1, data[i]);
	return (0);
	}



/****************************************************************************
 * gcr_read_data_bytes
 ****************************************************************************/
static int
gcr_read_data_bytes(
	struct fifo			*ffo_l1,
	struct disk_error		*dsk_err,
	unsigned char			*data,
	int				size)

	{
	const static unsigned char	decode[0x100 - 0x96] =
		{
		0x00, 0x01, 0xff, 0xff, 0x02, 0x03, 0xff, 0x04,
		0x05, 0x06, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0x07, 0x08, 0xff, 0xff, 0xff, 0x09, 0x0a, 0x0b,
		0x0c, 0x0d, 0xff, 0xff, 0x0e, 0x0f, 0x10, 0x11,
		0x12, 0x13, 0xff, 0x14, 0x15, 0x16, 0x17, 0x18,
		0x19, 0x1a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0x1b, 0xff, 0x1c,
		0x1d, 0x1e, 0xff, 0xff, 0xff, 0x1f, 0xff, 0xff,
		0x20, 0x21, 0xff, 0x22, 0x23, 0x24, 0x25, 0x26,
		0x27, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0x29,
		0x2a, 0x2b, 0xff, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
		0x31, 0x32, 0xff, 0xff, 0x33, 0x34, 0x35, 0x36,
		0x37, 0x38, 0xff, 0x39, 0x3a, 0x3b, 0x3c, 0x3d,
		0x3e, 0x3f
		};
	int				i, j, r;
	int				bitofs = fifo_get_rd_bitofs(ffo_l1);

	for (i = 0; i < size; i++)
		{
		r = fifo_read_bits(ffo_l1, 8);
		if (r == -1) return (-1);
		if (r < 0x96) j = 0xff;
		else j = decode[r - 0x96];
		if (j == 0xff)
			{
			verbose(3, "data decode error around bit offset %d (byte %d), got 0x%02x(0x%02x)", fifo_get_rd_bitofs(ffo_l1) - 8, i, r, j);
			disk_error_add(dsk_err, DISK_ERROR_FLAG_ENCODING, 1);
			}
		data[i] = j;
		}
	verbose(2, "read %d data bytes at bit offset %d", i, bitofs);
	return (0);
	}



/****************************************************************************
 * gcr_write_data_bytes
 ****************************************************************************/
static int
gcr_write_data_bytes(
	struct fifo			*ffo_l1,
	unsigned char			*data,
	int				size)

	{
	const static unsigned char	encode[0x40] =
		{
		0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
		0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
		0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
		0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
		0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
		0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
		0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
		0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
		};
	int				i;

	verbose(2, "writing %d data bytes at bit offset %d", size, fifo_get_wr_bitofs(ffo_l1));
	for (i = 0; i < size; i++)
		{
		debug_error_condition(data[i] >= 0x40);
		if (fifo_write_bits(ffo_l1, encode[data[i]], 8) == -1) return (-1);
		}
	return (0);
	}




/****************************************************************************
 *
 * functions for sector and track handling
 *
 ****************************************************************************/




#define HEADER_SIZE			4
#define DATA_SIZE			343
#define FLAG_IGNORE_CHECKSUMS		(1 << 0)
#define FLAG_IGNORE_TRACK_MISMATCH	(1 << 1)
#define FLAG_IGNORE_VOLUME_ID		(1 << 2)



/****************************************************************************
 * gcr_apple_bit_swap
 ****************************************************************************/
static int
gcr_apple_bit_swap(
	int				val)

	{
	val &= 3;
	if (val == 1) return (2);
	if (val == 2) return (1);
	return (val);
	}



/****************************************************************************
 * gcr_apple_shuffle
 ****************************************************************************/
static void
gcr_apple_shuffle(
	unsigned char			*data)

	{
	int				i;

	for (i = 0x100; i-- > 0; ) data[i + 0x56] = data[i];
	for (i = 0; i < 0x54; i++) data[i] = gcr_apple_bit_swap(data[i + 0x102]), data[i + 0x102] >>= 2;
	data[0x54] = data[0x55] = 0;
	for (i = 0; i < 0x56; i++) data[i] = (data[i] << 2) | gcr_apple_bit_swap(data[i + 0xac]), data[i + 0xac] >>= 2;
	for (i = 0; i < 0x56; i++) data[i] = (data[i] << 2) | gcr_apple_bit_swap(data[i + 0x56]), data[i + 0x56] >>= 2;
	}



/****************************************************************************
 * gcr_apple_unshuffle
 ****************************************************************************/
static void
gcr_apple_unshuffle(
	unsigned char			*data)

	{
	int				i;

	for (i = 0; i < 0x56; i++) data[i + 0x056] = (data[i + 0x056] << 2) | gcr_apple_bit_swap(data[i]);
	for (i = 0; i < 0x56; i++) data[i + 0x0ac] = (data[i + 0x0ac] << 2) | gcr_apple_bit_swap(data[i] >> 2);
	for (i = 0; i < 0x54; i++) data[i + 0x102] = (data[i + 0x102] << 2) | gcr_apple_bit_swap(data[i] >> 4);
	for (i = 0; i < 0x100; i++) data[i] = data[i + 0x56];
	}



/****************************************************************************
 * gcr_apple_header_checksum
 ****************************************************************************/
static int
gcr_apple_header_checksum(
	unsigned char			*data,
	int				len)

	{
	int				c, i;

	for (c = i = 0; i < len; i++) c ^= data[i];
	return (c);
	}



/****************************************************************************
 * gcr_apple_read_data_checksum
 ****************************************************************************/
static int
gcr_apple_read_data_checksum(
	unsigned char			*data,
	int				len)

	{
	int				c, i;

	for (c = i = 0; i < len; i++) data[i] ^= c, c = data[i];
	return (c);
	}



/****************************************************************************
 * gcr_apple_write_data_checksum
 ****************************************************************************/
static int
gcr_apple_write_data_checksum(
	unsigned char			*data,
	int				len)

	{
	int				c, i;

	for (c = data[len - 1], i = len; --i > 0; ) data[i] ^= data[i - 1];
	return (c);
	}



/****************************************************************************
 * gcr_apple_read_sector2
 ****************************************************************************/
static int
gcr_apple_read_sector2(
	struct fifo			*ffo_l1,
	struct gcr_apple		*gcr_apl,
	struct disk_error		*dsk_err,
	unsigned char			*header,
	unsigned char			*data)

	{
	int				bitofs, epilog;

	*dsk_err = (struct disk_error) { };
	if (gcr_read_sync(ffo_l1, gcr_apl->rw.sync_value1) == -1) return (-1);
	bitofs = fifo_get_rd_bitofs(ffo_l1);
	if (gcr_read_header_bytes(ffo_l1, dsk_err, header, HEADER_SIZE) == -1) return (-1);
	epilog = fifo_read_bits(ffo_l1, 16);
	disk_error_add(dsk_err, DISK_ERROR_FLAG_ENCODING, format_compare2("header epilogue: got 0x%04x, expected 0x%04x", epilog, 0xdeaa));
	if (gcr_read_sync(ffo_l1, gcr_apl->rw.sync_value2) == -1) return (-1);

	/* seems there is always one zero bit between sync and data */

	if (fifo_read_bits(ffo_l1, 1) == -1) return (-1);
	if (gcr_read_data_bytes(ffo_l1, dsk_err, data, DATA_SIZE) == -1) return (-1);
	epilog = fifo_read_bits(ffo_l1, 16);
	disk_error_add(dsk_err, DISK_ERROR_FLAG_ENCODING, format_compare2("data epilogue: got 0x%04x, expected 0x%04x", epilog, 0xdeaa));
	verbose(2, "rewinding to bit offset %d", bitofs);
	fifo_set_rd_bitofs(ffo_l1, bitofs);
	return (1);
	}



/****************************************************************************
 * gcr_apple_write_sector2
 ****************************************************************************/
static int
gcr_apple_write_sector2(
	struct fifo			*ffo_l1,
	struct gcr_apple		*gcr_apl,
	unsigned char			*header,
	unsigned char			*data)

	{
	if (gcr_write_fill(ffo_l1, gcr_apl->wr.fill_value1, gcr_apl->wr.fill_length1) == -1) return (-1);
	if (gcr_write_sync(ffo_l1, gcr_apl->rw.sync_value1) == -1) return (-1);
	if (gcr_write_header_bytes(ffo_l1, header, HEADER_SIZE) == -1) return (-1);
	if (fifo_write_bits(ffo_l1, 0xde, 8) == -1) return (-1);
	if (fifo_write_bits(ffo_l1, 0xaaeb, 16) == -1) return (-1);
	if (gcr_write_fill(ffo_l1, gcr_apl->wr.fill_value2, gcr_apl->wr.fill_length2) == -1) return (-1);
	if (gcr_write_sync(ffo_l1, gcr_apl->rw.sync_value2) == -1) return (-1);

	/* seems there is always one zero bit between sync and data */

	if (fifo_write_bits(ffo_l1, 0, 1) == -1) return (-1);
	if (gcr_write_data_bytes(ffo_l1, data, DATA_SIZE) == -1) return (-1);
	if (fifo_write_bits(ffo_l1, 0xde, 8) == -1) return (-1);
	if (fifo_write_bits(ffo_l1, 0xaaeb, 16) == -1) return (-1);
	return (1);
	}



/****************************************************************************
 * gcr_apple_read_sector
 ****************************************************************************/
static int
gcr_apple_read_sector(
	struct fifo			*ffo_l1,
	struct gcr_apple		*gcr_apl,
	struct disk_sector		*dsk_sct,
	int				track)

	{
	struct disk_error		dsk_err;
	unsigned char			header[HEADER_SIZE];
	unsigned char			data[DATA_SIZE];
	int				result, sector;

	if (gcr_apple_read_sector2(ffo_l1, gcr_apl, &dsk_err, header, data) == -1) return (-1);

	/* accept only valid sector numbers */

	sector = header[2];
	track  = track / 4;
	if (sector >= gcr_apl->rw.sectors)
		{
		verbose(1, "sector %d out of range", sector);
		return (0);
		}
	verbose(1, "got sector %d", sector);

	/* check sector quality */

	result = format_compare2("header xor checksum: got 0x%02x, expected 0x%02x", header[3], gcr_apple_header_checksum(header, 3));
	result += format_compare2("data xor checksum: got 0x%02x, expected 0x%02x", data[342], gcr_apple_read_data_checksum(data, 342));
	if (result > 0) verbose(2, "checksum error on sector %d", sector);
	if (gcr_apl->rd.flags & FLAG_IGNORE_CHECKSUMS) disk_warning_add(&dsk_err, result);
	else disk_error_add(&dsk_err, DISK_ERROR_FLAG_CHECKSUM, result);

	result = format_compare2("track: got %d, expected %d", header[1], track);
	if (result > 0) verbose(2, "track mismatch on sector %d", sector);
	if (gcr_apl->rd.flags & FLAG_IGNORE_TRACK_MISMATCH) disk_warning_add(&dsk_err, result);
	else disk_error_add(&dsk_err, DISK_ERROR_FLAG_NUMBERING, result);

	result = format_compare2("volume_id: got 0x%02x, expected 0x%02x", header[0], gcr_apl->rw.volume_id);
	if (result > 0) verbose(2, "wrong volume_id on sector %d", sector);
	if (gcr_apl->rd.flags & FLAG_IGNORE_VOLUME_ID) disk_warning_add(&dsk_err, result);
	else disk_error_add(&dsk_err, DISK_ERROR_FLAG_ID, result);

	/* unshuffle sector */

	gcr_apple_unshuffle(data);

	/*
	 * if the found sector is of better quality than the current one
	 * then take the data
	 */

	disk_set_sector_number(&dsk_sct[sector], sector);
	disk_sector_read(&dsk_sct[sector], &dsk_err, data);
	return (1);
	}



/****************************************************************************
 * gcr_apple_write_sector
 ****************************************************************************/
static int
gcr_apple_write_sector(
	struct fifo			*ffo_l1,
	struct gcr_apple		*gcr_apl,
	struct disk_sector		*dsk_sct,
	int				track)

	{
	unsigned char			header[HEADER_SIZE];
	unsigned char			data[DATA_SIZE];
	int				sector = disk_get_sector_number(dsk_sct);

	verbose(1, "writing sector %d", sector);
	header[0] = gcr_apl->rw.volume_id;
	header[1] = track / 4;
	header[2] = sector;
	header[3] = gcr_apple_header_checksum(header, 3);
	disk_sector_write(data, dsk_sct);
	gcr_apple_shuffle(data);
	data[342] = gcr_apple_write_data_checksum(data, 342);
	return (gcr_apple_write_sector2(ffo_l1, gcr_apl, header, data));
	}



/****************************************************************************
 * gcr_apple_statistics
 ****************************************************************************/
static int
gcr_apple_statistics(
	union format			*fmt,
	struct fifo			*ffo_l0,
	int				track)

	{
	raw_histogram(ffo_l0, track);
	raw_precomp_statistics(ffo_l0, fmt->gcr_apl.rw.bnd, 3);
	return (1);
	}



/****************************************************************************
 * gcr_apple_read_track
 ****************************************************************************/
static int
gcr_apple_read_track(
	union format			*fmt,
	struct fifo			*ffo_l0,
	struct fifo			*ffo_l3,
	struct disk_sector		*dsk_sct,
	int				track)

	{
	unsigned char			data[CWTOOL_MAX_TRACK_SIZE];
	struct fifo			ffo_l1 = FIFO_INIT(data, sizeof (data));

	raw_read(ffo_l0, &ffo_l1, fmt->gcr_apl.rw.bnd, 3);
	while (gcr_apple_read_sector(&ffo_l1, &fmt->gcr_apl, dsk_sct, track) != -1) ;
	return (1);
	}



/****************************************************************************
 * gcr_apple_write_track
 ****************************************************************************/
static int
gcr_apple_write_track(
	union format			*fmt,
	struct fifo			*ffo_l3,
	struct disk_sector		*dsk_sct,
	struct fifo			*ffo_l0,
	int				track)

	{
	unsigned char			data[CWTOOL_MAX_TRACK_SIZE];
	struct fifo			ffo_l1 = FIFO_INIT(data, sizeof (data));
	int				i;

	if (gcr_write_fill(&ffo_l1, 0x3fc, fmt->gcr_apl.wr.prolog_length) == -1) return (0);
	for (i = 0; i < fmt->gcr_apl.rw.sectors; i++) if (gcr_apple_write_sector(&ffo_l1, &fmt->gcr_apl, &dsk_sct[i], track) == -1) return (0);
	fifo_set_rd_ofs(ffo_l3, fifo_get_wr_ofs(ffo_l3));
	if (gcr_write_fill(&ffo_l1, 0x3fc, fmt->gcr_apl.wr.epilog_length) == -1) return (0);
	fifo_write_flush(&ffo_l1);
	if (raw_write(&ffo_l1, ffo_l0, fmt->gcr_apl.rw.bnd, fmt->gcr_apl.wr.precomp, 3) == -1) return (0);
	return (1);
	}




/****************************************************************************
 *
 * functions for configuration
 *
 ****************************************************************************/




#define MAGIC_IGNORE_CHECKSUMS		1
#define MAGIC_IGNORE_TRACK_MISMATCH	2
#define MAGIC_IGNORE_VOLUME_ID		3
#define MAGIC_PROLOG_LENGTH		4
#define MAGIC_EPILOG_LENGTH		5
#define MAGIC_FILL_LENGTH1		6
#define MAGIC_FILL_VALUE1		7
#define MAGIC_FILL_LENGTH2		8
#define MAGIC_FILL_VALUE2		9
#define MAGIC_PRECOMP			10
#define MAGIC_SECTORS			11
#define MAGIC_VOLUME_ID			12
#define MAGIC_SYNC_VALUE1		13
#define MAGIC_SYNC_VALUE2		14
#define MAGIC_BOUNDS			15



/****************************************************************************
 * gcr_apple_set_defaults
 ****************************************************************************/
static void
gcr_apple_set_defaults(
	union format			*fmt)

	{
	const static struct gcr_apple	gcr_apl =
		{
		.rd =
			{
			.flags       = 0
			},
		.wr =
			{
			.prolog_length = 0,
			.epilog_length = 64,
			.fill_length1  = 54,
			.fill_value1   = 0x3fc,
			.fill_length2  = 8,
			.fill_value2   = 0x3fc,
			.precomp       = { }
			},
		.rw =
			{
			.sectors     = 16,
			.volume_id   = 0xfe,
			.sync_value1 = 0xd5aa96,
			.sync_value2 = 0xd5aaad,
			.bnd         =
				{
				BOUNDS(0x0800, 0x1600, 0x2200, 0),
				BOUNDS(0x2300, 0x2c00, 0x3800, 1),
				BOUNDS(0x3900, 0x4300, 0x5000, 2)
				}
			}
		};

	debug(2, "setting defaults");
	fmt->gcr_apl = gcr_apl;
	}



/****************************************************************************
 * gcr_apple_set_read_option
 ****************************************************************************/
static int
gcr_apple_set_read_option(
	union format			*fmt,
	int				magic,
	int				val,
	int				ofs)

	{
	debug(2, "setting read option magic = %d, val = %d, ofs = %d", magic, val, ofs);
	if (magic == MAGIC_IGNORE_CHECKSUMS)      return (setvalue_uchar_bit(&fmt->gcr_apl.rd.flags, val, FLAG_IGNORE_CHECKSUMS));
	if (magic == MAGIC_IGNORE_TRACK_MISMATCH) return (setvalue_uchar_bit(&fmt->gcr_apl.rd.flags, val, FLAG_IGNORE_TRACK_MISMATCH));
	debug_error_condition(magic != MAGIC_IGNORE_VOLUME_ID);
	return (setvalue_uchar_bit(&fmt->gcr_apl.rd.flags, val, FLAG_IGNORE_VOLUME_ID));
	}



/****************************************************************************
 * gcr_apple_set_write_option
 ****************************************************************************/
static int
gcr_apple_set_write_option(
	union format			*fmt,
	int				magic,
	int				val,
	int				ofs)

	{
	debug(2, "setting write option magic = %d, val = %d, ofs = %d", magic, val, ofs);
	if (magic == MAGIC_PROLOG_LENGTH) return (setvalue_ushort(&fmt->gcr_apl.wr.prolog_length, val, 0, 0xffff));
	if (magic == MAGIC_EPILOG_LENGTH) return (setvalue_ushort(&fmt->gcr_apl.wr.epilog_length, val, 8, 0xffff));
	if (magic == MAGIC_FILL_LENGTH1)  return (setvalue_uchar(&fmt->gcr_apl.wr.fill_length1, val, 8, 0xff));
	if (magic == MAGIC_FILL_VALUE1)   return (setvalue_ushort(&fmt->gcr_apl.wr.fill_value1, val, 0, 0x3ff));
	if (magic == MAGIC_FILL_LENGTH2)  return (setvalue_uchar(&fmt->gcr_apl.wr.fill_length2, val, 4, 0xff));
	if (magic == MAGIC_FILL_VALUE2)   return (setvalue_ushort(&fmt->gcr_apl.wr.fill_value2, val, 0, 0x3ff));
	debug_error_condition(magic != MAGIC_PRECOMP);
	return (setvalue_short(&fmt->gcr_apl.wr.precomp[ofs], val, -0x4000, 0x4000));
	}



/****************************************************************************
 * gcr_apple_set_rw_option
 ****************************************************************************/
static int
gcr_apple_set_rw_option(
	union format			*fmt,
	int				magic,
	int				val,
	int				ofs)

	{
	debug(2, "setting rw option magic = %d, val = %d, ofs = %d", magic, val, ofs);
	if (magic == MAGIC_SECTORS)     return (setvalue_uchar(&fmt->gcr_apl.rw.sectors, val, 1, CWTOOL_MAX_SECTOR));
	if (magic == MAGIC_VOLUME_ID)   return (setvalue_uchar(&fmt->gcr_apl.rw.volume_id, val, 0, 0xff));
	if (magic == MAGIC_SYNC_VALUE1) return (setvalue_uint(&fmt->gcr_apl.rw.sync_value1, val, 0, 0xffffff));
	if (magic == MAGIC_SYNC_VALUE2) return (setvalue_uint(&fmt->gcr_apl.rw.sync_value2, val, 0, 0xffffff));
	debug_error_condition(magic != MAGIC_BOUNDS);
	return (setvalue_bounds(fmt->gcr_apl.rw.bnd, val, ofs));
	}



/****************************************************************************
 * gcr_apple_get_sector_size
 ****************************************************************************/
static int
gcr_apple_get_sector_size(
	union format			*fmt,
	int				sector)

	{
	debug_error_condition(sector >= fmt->gcr_apl.rw.sectors);
	if (sector < 0) return (256 * fmt->gcr_apl.rw.sectors);
	return (256);
	}



/****************************************************************************
 * gcr_apple_get_sectors
 ****************************************************************************/
static int
gcr_apple_get_sectors(
	union format			*fmt)

	{
	return (fmt->gcr_apl.rw.sectors);
	}



/****************************************************************************
 * gcr_apple_read_options
 ****************************************************************************/
static struct format_option		gcr_apple_read_options[] =
	{
	FORMAT_OPTION("ignore_checksums",      MAGIC_IGNORE_CHECKSUMS,      1),
	FORMAT_OPTION("ignore_track_mismatch", MAGIC_IGNORE_TRACK_MISMATCH, 1),
	FORMAT_OPTION("ignore_volume_id",      MAGIC_IGNORE_VOLUME_ID,      1),
	FORMAT_OPTION(NULL, 0, 0)
	};



/****************************************************************************
 * gcr_apple_write_options
 ****************************************************************************/
static struct format_option		gcr_apple_write_options[] =
	{
	FORMAT_OPTION("prolog_length", MAGIC_PROLOG_LENGTH, 1),
	FORMAT_OPTION("epilog_length", MAGIC_EPILOG_LENGTH, 1),
	FORMAT_OPTION("fill_length1",  MAGIC_FILL_LENGTH1,  1),
	FORMAT_OPTION("fill_length2",  MAGIC_FILL_LENGTH2,  1),
	FORMAT_OPTION("precomp",       MAGIC_PRECOMP,       9),
	FORMAT_OPTION(NULL, 0, 0)
	};



/****************************************************************************
 * gcr_apple_rw_options
 ****************************************************************************/
static struct format_option		gcr_apple_rw_options[] =
	{
	FORMAT_OPTION("sectors",     MAGIC_SECTORS,     1),
	FORMAT_OPTION("volume_id",   MAGIC_VOLUME_ID,   1),
	FORMAT_OPTION("sync_value1", MAGIC_SYNC_VALUE1, 1),
	FORMAT_OPTION("sync_value2", MAGIC_SYNC_VALUE2, 1),
	FORMAT_OPTION("bounds",      MAGIC_BOUNDS,      9),
	FORMAT_OPTION(NULL, 0, 0)
	};




/****************************************************************************
 *
 * used by external callers
 *
 ****************************************************************************/




/****************************************************************************
 * gcr_apple_format_desc
 ****************************************************************************/
struct format_desc			gcr_apple_format_desc =
	{
	.name             = "gcr_apple",
	.level            = 3,
	.set_defaults     = gcr_apple_set_defaults,
	.set_read_option  = gcr_apple_set_read_option,
	.set_write_option = gcr_apple_set_write_option,
	.set_rw_option    = gcr_apple_set_rw_option,
	.get_sectors      = gcr_apple_get_sectors,
	.get_sector_size  = gcr_apple_get_sector_size,
	.track_statistics = gcr_apple_statistics,
	.track_read       = gcr_apple_read_track,
	.track_write      = gcr_apple_write_track,
	.fmt_opt_rd       = gcr_apple_read_options,
	.fmt_opt_wr       = gcr_apple_write_options,
	.fmt_opt_rw       = gcr_apple_rw_options
	};
/******************************************************** Karsten Scheibler */
