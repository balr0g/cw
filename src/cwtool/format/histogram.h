/****************************************************************************
 ****************************************************************************
 *
 * format/histogram.h
 *
 ****************************************************************************
 ****************************************************************************/





#ifndef CWTOOL_FORMAT_HISTOGRAM_H
#define CWTOOL_FORMAT_HISTOGRAM_H

#include "types.h"
#include "../global.h"

typedef cw_count_t			cw_hist_t[GLOBAL_NR_PULSE_LENGTHS];
typedef cw_hist_t			cw_hist2_t[GLOBAL_NR_PULSE_LENGTHS];

struct fifo;
struct bounds;

extern cw_void_t			histogram_calculate(cw_raw8_t *, cw_size_t, cw_hist_t, cw_hist2_t);
extern cw_void_t			histogram_blur(cw_hist_t, cw_hist_t, cw_count_t);
extern cw_void_t			histogram_long(cw_hist_t, cw_count_t, cw_count_t);
extern cw_void_t			histogram_short(cw_hist_t, cw_count_t, cw_count_t);
extern cw_void_t			histogram_normal(struct fifo *, cw_count_t, cw_count_t);
extern cw_void_t			histogram_postcomp_simple(struct fifo *, struct bounds *, cw_size_t, cw_count_t, cw_count_t);
extern cw_void_t			histogram_analyze_gcr(struct fifo *, struct bounds *, cw_size_t, cw_count_t, cw_count_t);
extern cw_void_t			histogram_analyze_mfm(struct fifo *, struct bounds *, cw_size_t, cw_count_t, cw_count_t);



#endif /* !CWTOOL_FORMAT_HISTOGRAM_H */
/******************************************************** Karsten Scheibler */
