/****************************************************************************
 ****************************************************************************
 *
 * format/fm_nec765.h
 *
 ****************************************************************************
 ****************************************************************************/





#ifndef CWTOOL_FM_NEC765_H
#define CWTOOL_FM_NEC765_H

#include "../cwtool.h"
#include "bounds.h"

struct fm_nec765
	{
	struct
		{
		unsigned char		flags;
		unsigned char		reserved[3];
		}			rd;
	struct
		{
		unsigned short		prolog_length;
		unsigned short		epilog_length;
		unsigned char		prolog_value;
		unsigned char		epilog_value;
		unsigned char		fill_length1;
		unsigned char		fill_value1;
		unsigned char		fill_length2;
		unsigned char		fill_value2;
		unsigned char		fill_length3;
		unsigned char		fill_value3;
		unsigned char		fill_length4;
		unsigned char		fill_value4;
		unsigned char		fill_length5;
		unsigned char		fill_value5;
		unsigned char		fill_length6;
		unsigned char		fill_value6;
		unsigned char		fill_length7;
		unsigned char		fill_value7;
		short			precomp[4];
		}			wr;
	struct
		{
		unsigned char		sectors;
		unsigned char		flags;
		unsigned short		sync_value1;
		unsigned short		sync_value2;
		unsigned short		sync_value3;
		unsigned short		crc16_init_value1;
		unsigned short		crc16_init_value2;
		unsigned short		crc16_init_value3;
		unsigned short		reserved;
		unsigned char		pshift[(CWTOOL_MAX_SECTOR + 1) / 2];
		struct bounds		bnd[2];
		}			rw;
	};

/*
 * do not put this include at the beginning of the file, because ../format.h
 * also includes this file to construct union format. so the above struct
 * has to be known at time of inclusion
 */

#include "../format.h"

extern struct format_desc		fm_nec765_format_desc;



#endif /* !CWTOOL_FM_NEC765_H */
/******************************************************** Karsten Scheibler */