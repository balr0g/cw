/****************************************************************************
 ****************************************************************************
 *
 * format/range.h
 *
 ****************************************************************************
 ****************************************************************************/





#ifndef CWTOOL_FORMAT_RANGE_H
#define CWTOOL_FORMAT_RANGE_H

#include "types.h"

struct range
	{
	cw_count_t			start;
	cw_count_t			end;
	};

#define RANGE_SECTOR_INIT		(struct range_sector) { }

struct range_sector
	{
	struct range			rng_header;
	struct range			rng_data;
	cw_count_t			number;
	};

struct range *
range_sector_header(
	struct range_sector		*rng_sec);

struct range *
range_sector_data(
	struct range_sector		*rng_sec);

cw_void_t
range_sector_set_number(
	struct range_sector		*rng_sec,
	cw_count_t			number);

cw_count_t
range_sector_get_number(
	struct range_sector		*rng_sec);

cw_void_t
range_set_start(
	struct range			*rng,
	cw_count_t			start);

cw_void_t
range_set_end(
	struct range			*rng,
	cw_count_t			end);

cw_count_t
range_get_start(
	struct range			*rng);

cw_count_t
range_get_end(
	struct range			*rng);



#endif /* !CWTOOL_FORMAT_RANGE_H */
/******************************************************** Karsten Scheibler */
