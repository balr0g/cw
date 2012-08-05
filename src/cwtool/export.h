/****************************************************************************
 ****************************************************************************
 *
 * export.h
 *
 ****************************************************************************
 ****************************************************************************/





#ifndef CWTOOL_EXPORT_H
#define CWTOOL_EXPORT_H

#include "types.h"

extern cw_u8_t *
export_u16_be(
	cw_u8_t				*data,
	cw_u16_t			val);

extern cw_u8_t *
export_u16_le(
	cw_u8_t				*data,
	cw_u16_t			val);

extern cw_u8_t *
export_u32_le(
	cw_u8_t				*data,
	cw_u32_t			val);



#endif /* !CWTOOL_EXPORT_H */
/******************************************************** Karsten Scheibler */
