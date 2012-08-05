/****************************************************************************
 ****************************************************************************
 *
 * string.h
 *
 ****************************************************************************
 ****************************************************************************/





#ifndef CWTOOL_STRING_H
#define CWTOOL_STRING_H

#include "types.h"

extern cw_bool_t
string_equal(
	const cw_char_t			*string1,
	const cw_char_t			*string2);

extern cw_bool_t
string_equal2(
	const cw_char_t			*string1,
	const cw_char_t			*string2,
	const cw_char_t			*string3);

extern cw_count_t
string_length(
	const cw_char_t			*string);

extern char *
string_copy(
	cw_char_t			*dst,
	cw_size_t			size,
	const cw_char_t			*src);

extern cw_count_t
string_snprintf(
	cw_char_t			*string,
	cw_size_t			size,
	const cw_char_t			*format,
	...);

extern char *
string_dot(
	cw_char_t			*dst,
	cw_size_t			size,
	cw_char_t			*src);



#endif /* !CWTOOL_STRING_H */
/******************************************************** Karsten Scheibler */
