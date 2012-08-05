/****************************************************************************
 ****************************************************************************
 *
 * format/match_simple.h
 *
 ****************************************************************************
 ****************************************************************************/





#ifndef CWTOOL_FORMAT_MATCH_SIMPLE_H
#define CWTOOL_FORMAT_MATCH_SIMPLE_H

#include "types.h"

union format;
struct fifo;
struct bounds;
struct container;
struct disk_sector;

struct match_simple_info
	{
	struct container		*con;
	union format			*fmt;
	struct fifo			*ffo_l0;
	struct fifo			*ffo_l3;
	struct disk_sector		*dsk_sct;
	cw_count_t			track;
	struct bounds			*bnd;
	cw_count_t			bnd_size;
	cw_void_t			(*callback)(union format *, struct container *, struct fifo *, struct fifo *, struct disk_sector *, cw_count_t);
	cw_bool_t			merge_two;
	cw_bool_t			merge_all;
	cw_bool_t			fixup;
	};

extern cw_void_t
match_simple(
	struct match_simple_info	*mat_sim_nfo);



#endif /* !CWTOOL_FORMAT_MATCH_SIMPLE_H */
/******************************************************** Karsten Scheibler */
