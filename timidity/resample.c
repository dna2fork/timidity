/*
    TiMidity++ -- MIDI to WAVE converter and player
    Copyright (C) 1999-2002 Masanao Izumo <mo@goice.co.jp>
    Copyright (C) 1995 Tuukka Toivonen <tt@cgs.fi>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    resample.c
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef NO_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif

#include "timidity.h"
#include "common.h"
#include "instrum.h"
#include "playmidi.h"
#include "output.h"
#include "controls.h"
#include "tables.h"
#include "resample.h"
#include "recache.h"

#if defined(NEWTON_INTERPOLATION) || defined(GAUSS_INTERPOLATION)
double newt_coeffs[58][58];		/* for start/end of samples */
#endif


#if defined(CSPLINE_INTERPOLATION)
# define INTERPVARS      int32 ofsi, ofsf, v0, v1, v2, v3, temp;
# define RESAMPLATION \
		ofsi = ofs >> FRACTION_BITS;	\
        v1 = src[ofsi]; \
        v2 = src[ofsi + 1]; \
	if(reduce_quality_flag || \
	   (ofs<ls+(1L<<FRACTION_BITS))||((ofs+(2L<<FRACTION_BITS))>le)){ \
                *dest++ = (sample_t)(v1 + (((v2-v1) * (ofs & FRACTION_MASK)) >> FRACTION_BITS)); \
	}else{ \
        v0 = src[ofsi - 1]; \
        v3 = src[ofsi + 2]; \
        ofsf = ofs & FRACTION_MASK; \
	    temp = v2; \
 		v2 = (6 * v2 + ((((5 * v3 - 11 * v2 + 7 * v1 - v0) >> 2) * \
 		     (ofsf + (1L << FRACTION_BITS)) >> FRACTION_BITS) * \
 		     (ofsf - (1L << FRACTION_BITS)) >> FRACTION_BITS)) \
 		     * ofsf; \
 		v1 = (((6 * v1+((((5 * v0 - 11 * v1 + 7 * temp - v3) >> 2) * \
 		     ofsf >> FRACTION_BITS) * (ofsf - (2L << FRACTION_BITS)) \
 		     >> FRACTION_BITS)) * ((1L << FRACTION_BITS) - ofsf)) + v2) \
 		     / (6L << FRACTION_BITS); \
 		*dest++ = (v1 > 32767) ? 32767: ((v1 < -32768)? -32768: v1); \
	}
#elif defined(LAGRANGE_INTERPOLATION)	/* must be fixed for uint32 */
# define INTERPVARS      splen_t ofsd; int32 v0, v1, v2, v3;
# define RESAMPLATION \
	v1 = (int32)src[(ofs>>FRACTION_BITS)]; \
	v2 = (int32)src[(ofs>>FRACTION_BITS)+1]; \
	if(reduce_quality_flag || \
	   (ofs<ls+(1L<<FRACTION_BITS))||((ofs+(2L<<FRACTION_BITS))>le)){ \
		*dest++ = (sample_t)(v1 + (((v2-v1) * (ofs & FRACTION_MASK)) >> FRACTION_BITS)); \
	}else{ \
	    v0 = (int32)src[(ofs>>FRACTION_BITS)-1]; \
	    v3 = (int32)src[(ofs>>FRACTION_BITS)+2]; \
	    ofsd = ofs; \
	    ofs &= FRACTION_MASK; \
	    v3 += -3*v2 + 3*v1 - v0; \
	    v3 *= (ofs - (2<<FRACTION_BITS)) / 6; \
	    v3 >>= FRACTION_BITS; \
	    v3 += v2 - v1 - v1 + v0; \
	    v3 *= (ofs - (1<<FRACTION_BITS)) >> 1; \
	    v3 >>= FRACTION_BITS; \
	    v3 += v1 - v0; \
	    v3 *= ofs; \
	    v3 >>= FRACTION_BITS; \
	    v3 += v0; \
	    *dest++ = (v3 > 32767)? 32767: ((v3 < -32768)? -32768: v3); \
	    ofs = ofsd; \
	}
#elif defined(GAUSS_INTERPOLATION)
float *gauss_table[(1<<FRACTION_BITS)] = {0};	/* don't need doubles */
int gauss_n = 25;
# define INTERPVARS	int32 v1, v2; \
			sample_t *sptr; \
			double y, xd; \
			float *gptr, *gend; \
			int32 left, right, temp_n; \
			int ii, jj;
# define RESAMPLATION \
	v1 = src[(ofs>>FRACTION_BITS)]; \
	v2 = src[(ofs>>FRACTION_BITS) +1]; \
	if (reduce_quality_flag) { \
	    *dest++ = (sample_t)(v1 + (((v2-v1) * (ofs & FRACTION_MASK)) >> FRACTION_BITS)); \
	}else{ \
	    left = (ofs>>FRACTION_BITS); \
	    right = (vp->sample->data_length>>FRACTION_BITS)-(ofs>>FRACTION_BITS)-1; \
	    temp_n = (right<<1)-1; \
	    if (temp_n <= 0) \
		temp_n = 1; \
	    if (temp_n > (left<<1)+1) \
		temp_n = (left<<1)+1; \
	    if (temp_n < gauss_n) { \
		xd = ofs & FRACTION_MASK; \
		xd /= (1L<<FRACTION_BITS); \
		xd += temp_n>>1; \
		y = 0; \
		sptr = src + (ofs>>FRACTION_BITS) - (temp_n>>1); \
		for (ii = temp_n; ii;) { \
		    for (jj = 0; jj <= ii; jj++) \
			y += sptr[jj] * newt_coeffs[ii][jj]; \
		    y *= xd - --ii; \
		} y += *sptr; \
	    }else{ \
		y = 0; \
		gptr = gauss_table[ofs&FRACTION_MASK]; \
		gend = gptr + gauss_n; \
		sptr = src + (ofs>>FRACTION_BITS) - (gauss_n>>1); \
		do { \
		    y += *(sptr++) * *(gptr++); \
		} while (gptr <= gend); \
	    } \
	    *dest++ = (y > 32767)? 32767: ((y < -32768)? -32768: y); \
	}
#elif defined(NEWTON_INTERPOLATION)
int newt_n = 11;
int32 newt_old_trunc_x = -1;
int newt_grow = -1;
int newt_max = 13;
double newt_divd[60][60] = {0};
double newt_recip[60] = { 0, 1, 1.0/2, 1.0/3, 1.0/4, 1.0/5, 1.0/6, 1.0/7,
			1.0/8, 1.0/9, 1.0/10, 1.0/11, 1.0/12, 1.0/13, 1.0/14,
			1.0/15, 1.0/16, 1.0/17, 1.0/18, 1.0/19, 1.0/20, 1.0/21,
			1.0/22, 1.0/23, 1.0/24, 1.0/25, 1.0/26, 1.0/27, 1.0/28,
			1.0/29, 1.0/30, 1.0/31, 1.0/32, 1.0/33, 1.0/34, 1.0/35,
			1.0/36, 1.0/37, 1.0/38, 1.0/39, 1.0/40, 1.0/41, 1.0/42,
			1.0/43, 1.0/44, 1.0/45, 1.0/46, 1.0/47, 1.0/48, 1.0/49,
			1.0/50, 1.0/51, 1.0/52, 1.0/53, 1.0/54, 1.0/55, 1.0/56,
			1.0/57, 1.0/58, 1.0/59 };
sample_t *newt_old_src = NULL;
# define INTERPVARS	int n_new, n_old; \
			int32 v1, v2, diff; \
			sample_t *sptr; \
			double y, xd; \
			int32 left, right, temp_n; \
			int ii, jj;
# define RESAMPLATION \
	v1 = src[(ofs>>FRACTION_BITS)]; \
	v2 = src[(ofs>>FRACTION_BITS)+1]; \
	if (reduce_quality_flag) { \
	    *dest++ = (sample_t)(v1 + (((v2-v1) * (ofs & FRACTION_MASK)) >> FRACTION_BITS)); \
	}else{ \
	    left = (ofs>>FRACTION_BITS); \
	    right = (vp->sample->data_length>>FRACTION_BITS)-(ofs>>FRACTION_BITS)-1; \
	    temp_n = (right<<1)-1; \
	    if (temp_n <= 0) \
		temp_n = 1; \
	    if (temp_n > (left<<1)+1) \
		temp_n = (left<<1)+1; \
	    if (temp_n < newt_n) { \
		xd = ofs & FRACTION_MASK; \
		xd /= (1L<<FRACTION_BITS); \
		xd += temp_n>>1; \
		y = 0; \
		sptr = src + (ofs>>FRACTION_BITS) - (temp_n>>1); \
		for (ii = temp_n; ii;) { \
		    for (jj = 0; jj <= ii; jj++) \
			y += sptr[jj] * newt_coeffs[ii][jj]; \
		    y *= xd - --ii; \
		} y += *sptr; \
	    }else{ \
		if (newt_grow >= 0 && src == newt_old_src && \
		   (diff = (ofs>>FRACTION_BITS) - newt_old_trunc_x) > 0){ \
		    n_new = newt_n + ((newt_grow + diff)<<1); \
		    if (n_new <= newt_max){ \
			n_old = newt_n + (newt_grow<<1); \
			newt_grow += diff; \
			for (v1=(ofs>>FRACTION_BITS)+(n_new>>1)+1,v2=n_new; \
			     v2 > n_old; --v1, --v2){ \
				newt_divd[0][v2] = src[v1]; \
			}for (v1 = 1; v1 <= n_new; v1++) \
			    for (v2 = n_new; v2 > n_old; --v2) \
				newt_divd[v1][v2] = (newt_divd[v1-1][v2] - \
						     newt_divd[v1-1][v2-1]) * \
						     newt_recip[v1]; \
		    }else newt_grow = -1; \
		} \
		if (newt_grow < 0 || src != newt_old_src || diff < 0){ \
		    newt_grow = 0; \
		    for (v1=(ofs>>FRACTION_BITS)-(newt_n>>1),v2=0; \
			 v2 <= newt_n; v1++, v2++){ \
			    newt_divd[0][v2] = src[v1]; \
		    }for (v1 = 1; v1 <= newt_n; v1++) \
			for (v2 = newt_n; v2 >= v1; --v2) \
			     newt_divd[v1][v2] = (newt_divd[v1-1][v2] - \
						  newt_divd[v1-1][v2-1]) * \
						  newt_recip[v1]; \
		} \
		n_new = newt_n + (newt_grow<<1); \
		v2 = n_new; \
		y = newt_divd[v2][v2]; \
		xd = (double)(ofs&FRACTION_MASK) / (1L<<FRACTION_BITS) + \
			     (newt_n>>1) + newt_grow; \
		for (--v2; v2; --v2){ \
		    y *= xd - v2; \
		    y += newt_divd[v2][v2]; \
		}y = y*xd + **newt_divd; \
		newt_old_src = src; \
		newt_old_trunc_x = (ofs>>FRACTION_BITS); \
	    } \
	    *dest++ = (y > 32767)? 32767: ((y < -32768)? -32768: y); \
	}
#elif defined(LINEAR_INTERPOLATION)
# if defined(LOOKUP_HACK) && defined(LOOKUP_INTERPOLATION)
#   define RESAMPLATION \
	   ofsi = ofs >> FRACTION_BITS;\
       v1 = src[ofsi];\
       v2 = src[ofsi + 1];\
       *dest++ = (sample_t)(v1 + (iplookup[(((v2 - v1) << 5) & 0x03FE0) | \
           ((ofs & FRACTION_MASK) >> (FRACTION_BITS-5))]));
# else
#   define RESAMPLATION \
	  ofsi = ofs >> FRACTION_BITS;\
      v1 = src[ofsi];\
      v2 = src[ofsi + 1];\
      *dest++ = (sample_t)(v1 + (((v2 - v1) * (ofs & FRACTION_MASK)) >> FRACTION_BITS));
# endif
#  define INTERPVARS int32 v1, v2, ofsi;
#else
/* Earplugs recommended for maximum listening enjoyment */
#  define RESAMPLATION *dest++ = src[ofs >> FRACTION_BITS];
#  define INTERPVARS
#endif

/* #define FINALINTERP if (ofs < le) *dest++=src[(ofs>>FRACTION_BITS)-1]/2; */
#define FINALINTERP /* Nothing to do after TiMidity++ 2.9.0 */
/* So it isn't interpolation. At least it's final. */

static sample_t resample_buffer[AUDIO_BUFFER_SIZE];
static int resample_buffer_offset;
static sample_t *vib_resample_voice(int, int32 *, int);
static sample_t *normal_resample_voice(int, int32 *, int);

#ifdef PRECALC_LOOPS
#if SAMPLE_LENGTH_BITS == 32 && TIMIDITY_HAVE_INT64
#define PRECALC_LOOP_COUNT(start, end, incr) (int32)(((int64)((end) - (start) + (incr) - 1)) / (incr))
#else
#define PRECALC_LOOP_COUNT(start, end, incr) (int32)(((splen_t)((end) - (start) + (incr) - 1)) / (incr))
#endif
#endif /* PRECALC_LOOPS */

#ifdef GAUSS_INTERPOLATION
void initialize_gauss_table(int n)
{
    int m, i, k, n_half = (n>>1);
    double ck;
    double x, x_inc, xz;
    double z[35];
    float *gptr;

    for (i = 0; i <= n; i++)
    	z[i] = i / (4*M_PI);

    x_inc = 1.0 / (1<<FRACTION_BITS);
    for (m = 0, x = 0.0; m < (1<<FRACTION_BITS); m++, x += x_inc)
    {
    	xz = (x + n_half) / (4*M_PI);
    	gptr = gauss_table[m] = realloc(gauss_table[m], (n+1)*sizeof(float));

    	for (k = 0; k <= n; k++)
    	{
    	    ck = 1.0;

    	    for (i = 0; i <= n; i++)
    	    {
    	    	if (i == k)
    	    	    continue;
    	
    	    	ck *= (sin(xz - z[i])) / (sin(z[k] - z[i]));
    	    }
    	    
    	    *gptr++ = ck;
    	}
    }
}
#endif

#if defined(NEWTON_INTERPOLATION) || defined(GAUSS_INTERPOLATION)
void initialize_newton_coeffs(void)
{
    int i, j, n = 57;
    int sign;

    newt_coeffs[0][0] = 1;
    for (i = 0; i <= n; i++)
    {
    	newt_coeffs[i][0] = 1;
    	newt_coeffs[i][i] = 1;

	if (i > 1)
	{
	    newt_coeffs[i][0] = newt_coeffs[i-1][0] / i;
	    newt_coeffs[i][i] = newt_coeffs[i-1][0] / i;
	}

    	for (j = 1; j < i; j++)
    	{
    	    newt_coeffs[i][j] = newt_coeffs[i-1][j-1] + newt_coeffs[i-1][j];

	    if (i > 1)
	    	newt_coeffs[i][j] /= i;
	}
    }
    for (i = 0; i <= n; i++)
    	for (j = 0, sign = pow(-1, i); j <= i; j++, sign *= -1)
    	    newt_coeffs[i][j] *= sign;
}
#endif

/*************** resampling with fixed increment *****************/

static sample_t *rs_plain_c(int v, int32 *countptr)
{
    Voice *vp = &voice[v];
    sample_t
	*dest = resample_buffer + resample_buffer_offset,
	*src = vp->sample->data;
    int32 ofs, count = *countptr, i, le;

    le = (int32)(vp->sample->loop_end >> FRACTION_BITS);
    ofs = (int32)(vp->sample_offset >> FRACTION_BITS);

    i = ofs + count;
    if(i > le)
	i = le;
    count = i - ofs;

    memcpy(dest, src + ofs, count * sizeof(sample_t));
    ofs += count;
    if(ofs == le)
    {
	vp->timeout = 1;
	*countptr = count;
    }
    vp->sample_offset = ((splen_t)ofs << FRACTION_BITS);
    return resample_buffer + resample_buffer_offset;
}

static sample_t *rs_plain(int v, int32 *countptr)
{
  /* Play sample until end, then free the voice. */
  INTERPVARS
  Voice *vp = &voice[v];
  sample_t
    *dest = resample_buffer + resample_buffer_offset,
    *src = vp->sample->data;
  splen_t
    ofs = vp->sample_offset,
#if defined(LAGRANGE_INTERPOLATION) || defined(CSPLINE_INTERPOLATION) || defined(NEWTON_INTERPOLATION) || defined(GAUSS_INTERPOLATION)
    ls = 0,
#endif /* LAGRANGE_INTERPOLATION */
    le = vp->sample->data_length;
  int32 count = *countptr, incr = vp->sample_increment;
#ifdef PRECALC_LOOPS
  int32 i, j;
#endif

  if(vp->cache && incr == (1 << FRACTION_BITS))
      return rs_plain_c(v, countptr);

#ifdef PRECALC_LOOPS
  if (incr < 0) incr = -incr; /* In case we're coming out of a bidir loop */

  /* Precalc how many times we should go through the loop.
     NOTE: Assumes that incr > 0 and that ofs <= le */
  i = PRECALC_LOOP_COUNT(ofs, le, incr);

  if (i > count)
    {
      i = count;
      count = 0;
    }
  else count -= i;

  for(j = 0; j < i; j++)
    {
      RESAMPLATION;
      ofs += incr;
    }

  if (ofs >= le)
    {
      FINALINTERP;
      vp->timeout = 1;
      *countptr -= count;
    }
#else /* PRECALC_LOOPS */
    while (count--)
    {
      RESAMPLATION;
      ofs += incr;
      if (ofs >= le)
	{
	  FINALINTERP;
	  vp->timeout = 1;
	  *countptr -= count;
	  break;
	}
    }
#endif /* PRECALC_LOOPS */

  vp->sample_offset = ofs; /* Update offset */
  return resample_buffer + resample_buffer_offset;
}

static sample_t *rs_loop_c(Voice *vp, int32 count)
{
  int32
    ofs = (int32)(vp->sample_offset >> FRACTION_BITS),
    le = (int32)(vp->sample->loop_end >> FRACTION_BITS),
    ll = le - (int32)(vp->sample->loop_start >> FRACTION_BITS);
  sample_t
    *dest = resample_buffer + resample_buffer_offset,
    *src = vp->sample->data;
  int32 i;

  while(count)
  {
      while(ofs >= le)
	  ofs -= ll;
      /* Precalc how many times we should go through the loop */
      i = le - ofs;
      if(i > count)
	  i = count;
      count -= i;
      memcpy(dest, src + ofs, i * sizeof(sample_t));
      dest += i;
      ofs += i;
  }
  vp->sample_offset = ((splen_t)ofs << FRACTION_BITS);
  return resample_buffer + resample_buffer_offset;
}

static sample_t *rs_loop(Voice *vp, int32 count)
{
  /* Play sample until end-of-loop, skip back and continue. */
  INTERPVARS
  splen_t
    ofs = vp->sample_offset,
    le = vp->sample->loop_end,
#if defined(LAGRANGE_INTERPOLATION) || defined(CSPLINE_INTERPOLATION) || defined(NEWTON_INTERPOLATION) || defined(GAUSS_INTERPOLATION)
    ls = vp->sample->loop_start,
#endif /* LAGRANGE_INTERPOLATION */
    ll = le - vp->sample->loop_start;
  sample_t
    *dest = resample_buffer + resample_buffer_offset,
    *src = vp->sample->data;
#ifdef PRECALC_LOOPS
  int32 i, j;
#endif
  int32 incr = vp->sample_increment;

  if(vp->cache && incr == (1 << FRACTION_BITS))
      return rs_loop_c(vp, count);

#ifdef PRECALC_LOOPS
  while (count)
    {
      while (ofs >= le)
	ofs -= ll;
      /* Precalc how many times we should go through the loop */
      i = PRECALC_LOOP_COUNT(ofs, le, incr);
      if (i > count)
	{
	  i = count;
	  count = 0;
	}
      else count -= i;
      for(j = 0; j < i; j++)
	{
	  RESAMPLATION;
	  ofs += incr;
	}
    }
#else
  while (count--)
    {
      RESAMPLATION;
      ofs += incr;
      if (ofs >= le)
	ofs -= ll; /* Hopefully the loop is longer than an increment. */
    }
#endif

  vp->sample_offset = ofs; /* Update offset */
  return resample_buffer + resample_buffer_offset;
}

static sample_t *rs_bidir(Voice *vp, int32 count)
{
  INTERPVARS
#if SAMPLE_LENGTH_BITS == 32
  int32
#else
  splen_t
#endif
    ofs = vp->sample_offset,
    le = vp->sample->loop_end,
    ls = vp->sample->loop_start;
  sample_t
    *dest = resample_buffer + resample_buffer_offset,
    *src = vp->sample->data;
  int32 incr = vp->sample_increment;

#ifdef PRECALC_LOOPS
#if SAMPLE_LENGTH_BITS == 32
  int32
#else
  splen_t
#endif
    le2 = le << 1,
    ls2 = ls << 1;
  int32 i, j;
  /* Play normally until inside the loop region */

  if (incr > 0 && ofs < ls)
    {
      /* NOTE: Assumes that incr > 0, which is NOT always the case
	 when doing bidirectional looping.  I have yet to see a case
	 where both ofs <= ls AND incr < 0, however. */
      i = PRECALC_LOOP_COUNT(ofs, ls, incr);
      if (i > count)
	{
	  i = count;
	  count = 0;
	}
      else count -= i;
      for(j = 0; j < i; j++)
	{
	  RESAMPLATION;
	  ofs += incr;
	}
    }

  /* Then do the bidirectional looping */

  while(count)
    {
      /* Precalc how many times we should go through the loop */
      i = PRECALC_LOOP_COUNT(ofs, incr > 0 ? le : ls, incr);
      if (i > count)
	{
	  i = count;
	  count = 0;
	}
      else count -= i;
      for(j = 0; j < i; j++)
	{
	  RESAMPLATION;
	  ofs += incr;
	}
      if(ofs >= 0 && ofs >= le)
	{
	  /* fold the overshoot back in */
	  ofs = le2 - ofs;
	  incr *= -1;
	}
      else if (ofs <= 0 || ofs <= ls)
	{
	  ofs = ls2 - ofs;
	  incr *= -1;
	}
    }

#else /* PRECALC_LOOPS */
  /* Play normally until inside the loop region */

  if (ofs < ls)
    {
      while (count--)
	{
	  RESAMPLATION;
	  ofs += incr;
	  if (ofs >= ls)
	    break;
	}
    }

  /* Then do the bidirectional looping */

  if (count > 0)
    while (count--)
      {
	RESAMPLATION;
	ofs += incr;
	if (ofs >= le)
	  {
	    /* fold the overshoot back in */
	    ofs = le - (ofs - le);
	    incr = -incr;
	  }
	else if (ofs <= ls)
	  {
	    ofs = ls + (ls - ofs);
	    incr = -incr;
	  }
      }
#endif /* PRECALC_LOOPS */
  vp->sample_increment = incr;
  vp->sample_offset = ofs; /* Update offset */
  return resample_buffer + resample_buffer_offset;
}

/*********************** vibrato versions ***************************/

/* We only need to compute one half of the vibrato sine cycle */
static int vib_phase_to_inc_ptr(int phase)
{
  if (phase < VIBRATO_SAMPLE_INCREMENTS / 2)
    return VIBRATO_SAMPLE_INCREMENTS / 2 - 1 - phase;
  else if (phase >= 3 * VIBRATO_SAMPLE_INCREMENTS / 2)
    return 5 * VIBRATO_SAMPLE_INCREMENTS / 2 - 1 - phase;
  else
    return phase - VIBRATO_SAMPLE_INCREMENTS / 2;
}

static int32 update_vibrato(Voice *vp, int sign)
{
  int32 depth;
  int phase, pb;
  double a;

  if(vp->vibrato_delay > 0)
  {
      vp->vibrato_delay -= vp->vibrato_control_ratio;
      if(vp->vibrato_delay > 0)
	  return vp->sample_increment;
  }

  if (vp->vibrato_phase++ >= 2 * VIBRATO_SAMPLE_INCREMENTS - 1)
    vp->vibrato_phase = 0;
  phase = vib_phase_to_inc_ptr(vp->vibrato_phase);

  if (vp->vibrato_sample_increment[phase])
    {
      if (sign)
	return -vp->vibrato_sample_increment[phase];
      else
	return vp->vibrato_sample_increment[phase];
    }

  /* Need to compute this sample increment. */

  depth = vp->vibrato_depth;
  if(depth < vp->modulation_wheel)
      depth = vp->modulation_wheel;
  depth <<= 7;

  if (vp->vibrato_sweep && !vp->modulation_wheel)
    {
      /* Need to update sweep */
      vp->vibrato_sweep_position += vp->vibrato_sweep;
      if (vp->vibrato_sweep_position >= (1 << SWEEP_SHIFT))
	vp->vibrato_sweep=0;
      else
	{
	  /* Adjust depth */
	  depth *= vp->vibrato_sweep_position;
	  depth >>= SWEEP_SHIFT;
	}
    }

  if(vp->sample->inst_type == INST_SF2) {
  pb = (int)((lookup_triangular(vp->vibrato_phase *
			(SINE_CYCLE_LENGTH / (2 * VIBRATO_SAMPLE_INCREMENTS)))
	    * (double)(depth) * VIBRATO_AMPLITUDE_TUNING));
  } else {
  pb = (int)((lookup_sine(vp->vibrato_phase *
			(SINE_CYCLE_LENGTH / (2 * VIBRATO_SAMPLE_INCREMENTS)))
	    * (double)(depth) * VIBRATO_AMPLITUDE_TUNING));
  }

  a = TIM_FSCALE(((double)(vp->sample->sample_rate) *
		  (double)(vp->frequency)) /
		 ((double)(vp->sample->root_freq) *
		  (double)(play_mode->rate)),
		 FRACTION_BITS);

  if(pb < 0)
  {
      pb = -pb;
      a /= bend_fine[(pb >> 5) & 0xFF] * bend_coarse[pb >> 13];
  }
  else
      a *= bend_fine[(pb >> 5) & 0xFF] * bend_coarse[pb >> 13];
  a += 0.5;

  /* If the sweep's over, we can store the newly computed sample_increment */
  if (!vp->vibrato_sweep || vp->modulation_wheel)
    vp->vibrato_sample_increment[phase] = (int32) a;

  if (sign)
    a = -a; /* need to preserve the loop direction */

  return (int32) a;
}

static sample_t *rs_vib_plain(int v, int32 *countptr)
{
  /* Play sample until end, then free the voice. */
  INTERPVARS
  Voice *vp = &voice[v];
  sample_t
    *dest = resample_buffer + resample_buffer_offset,
    *src = vp->sample->data;
  splen_t
#if defined(LAGRANGE_INTERPOLATION) || defined(CSPLINE_INTERPOLATION) || defined(NEWTON_INTERPOLATION) || defined(GAUSS_INTERPOLATION)
    ls = 0,
#endif /* LAGRANGE_INTERPOLATION */
    le = vp->sample->data_length,
    ofs = vp->sample_offset;
    
  int32 count = *countptr, incr = vp->sample_increment;
  int cc = vp->vibrato_control_counter;

  /* This has never been tested */

  if (incr < 0) incr = -incr; /* In case we're coming out of a bidir loop */

  while (count--)
    {
      if (!cc--)
	{
	  cc = vp->vibrato_control_ratio;
	  incr = update_vibrato(vp, 0);
	}
      RESAMPLATION;
      ofs += incr;
      if (ofs >= le)
	{
	  FINALINTERP;
	  vp->timeout = 1;
	  *countptr -= count;
	  break;
	}
    }

  vp->vibrato_control_counter = cc;
  vp->sample_increment = incr;
  vp->sample_offset = ofs; /* Update offset */
  return resample_buffer + resample_buffer_offset;
}

static sample_t *rs_vib_loop(Voice *vp, int32 count)
{
  /* Play sample until end-of-loop, skip back and continue. */
  INTERPVARS
  splen_t
    ofs = vp->sample_offset,
#if defined(LAGRANGE_INTERPOLATION) || defined(CSPLINE_INTERPOLATION) || defined(NEWTON_INTERPOLATION) || defined(GAUSS_INTERPOLATION)
    ls = vp->sample->loop_start,
#endif /* LAGRANGE_INTERPOLATION */
    le = vp->sample->loop_end,
    ll = le - vp->sample->loop_start;
  sample_t
    *dest = resample_buffer + resample_buffer_offset,
    *src = vp->sample->data;
  int cc = vp->vibrato_control_counter;
  int32 incr = vp->sample_increment;

#ifdef PRECALC_LOOPS
  int32 i, j;
  int vibflag=0;

  while (count)
    {
      /* Hopefully the loop is longer than an increment */
      while(ofs >= le)
	ofs -= ll;
      /* Precalc how many times to go through the loop, taking
	 the vibrato control ratio into account this time. */
      i = PRECALC_LOOP_COUNT(ofs, le, incr);
      if(i > count) i = count;
      if(i > cc)
	{
	  i = cc;
	  vibflag = 1;
	}
      else cc -= i;
      count -= i;
      for(j = 0; j < i; j++)
	{
	  RESAMPLATION;
	  ofs += incr;
	}
      if(vibflag)
	{
	  cc = vp->vibrato_control_ratio;
	  incr = update_vibrato(vp, 0);
	  vibflag = 0;
	}
    }

#else /* PRECALC_LOOPS */
  while (count--)
    {
      if (!cc--)
	{
	  cc=vp->vibrato_control_ratio;
	  incr=update_vibrato(vp, 0);
	}
      RESAMPLATION;
      ofs += incr;
      if (ofs >= le)
	ofs -= ll; /* Hopefully the loop is longer than an increment. */
    }
#endif /* PRECALC_LOOPS */

  vp->vibrato_control_counter = cc;
  vp->sample_increment = incr;
  vp->sample_offset = ofs; /* Update offset */
  return resample_buffer + resample_buffer_offset;
}

static sample_t *rs_vib_bidir(Voice *vp, int32 count)
{
  INTERPVARS
#if SAMPLE_LENGTH_BITS == 32
  int32
#else
  splen_t
#endif
    ofs = vp->sample_offset,
    le = vp->sample->loop_end,
    ls = vp->sample->loop_start;
  sample_t
    *dest = resample_buffer + resample_buffer_offset,
    *src = vp->sample->data;
  int cc=vp->vibrato_control_counter;
  int32 incr = vp->sample_increment;

#ifdef PRECALC_LOOPS
#if SAMPLE_LENGTH_BITS == 32
  int32
#else
  splen_t
#endif
    le2 = le << 1,
    ls2 = ls << 1;
  int32 i, j;
  int vibflag = 0;

  /* Play normally until inside the loop region */
  while (count && incr > 0 && ofs < ls)
    {
      i = PRECALC_LOOP_COUNT(ofs, ls, incr);
      if (i > count) i = count;
      if (i > cc)
	{
	  i = cc;
	  vibflag = 1;
	}
      else cc -= i;
      count -= i;
      for(j = 0; j < i; j++)
	{
	  RESAMPLATION;
	  ofs += incr;
	}
      if (vibflag)
	{
	  cc = vp->vibrato_control_ratio;
	  incr = update_vibrato(vp, 0);
	  vibflag = 0;
	}
    }

  /* Then do the bidirectional looping */

  while (count)
    {
      /* Precalc how many times we should go through the loop */
      i = PRECALC_LOOP_COUNT(ofs, incr > 0 ? le : ls, incr);
      if(i > count) i = count;
      if(i > cc)
	{
	  i = cc;
	  vibflag = 1;
	}
      else cc -= i;
      count -= i;
      while (i--)
	{
	  RESAMPLATION;
	  ofs += incr;
	}
      if (vibflag)
	{
	  cc = vp->vibrato_control_ratio;
	  incr = update_vibrato(vp, (incr < 0));
	  vibflag = 0;
	}
      if (ofs >= 0 && ofs >= le)
	{
	  /* fold the overshoot back in */
	  ofs = le2 - ofs;
	  incr *= -1;
	}
      else if (ofs <= 0 || ofs <= ls)
	{
	  ofs = ls2 - ofs;
	  incr *= -1;
	}
    }

#else /* PRECALC_LOOPS */
  /* Play normally until inside the loop region */

  if (ofs < ls)
    {
      while (count--)
	{
	  if (!cc--)
	    {
	      cc = vp->vibrato_control_ratio;
	      incr = update_vibrato(vp, 0);
	    }
	  RESAMPLATION;
	  ofs += incr;
	  if (ofs >= ls)
	    break;
	}
    }

  /* Then do the bidirectional looping */

  if (count > 0)
    while (count--)
      {
	if (!cc--)
	  {
	    cc=vp->vibrato_control_ratio;
	    incr=update_vibrato(vp, (incr < 0));
	  }
	RESAMPLATION;
	ofs += incr;
	if (ofs >= le)
	  {
	    /* fold the overshoot back in */
	    ofs = le - (ofs - le);
	    incr = -incr;
	  }
	else if (ofs <= ls)
	  {
	    ofs = ls + (ls - ofs);
	    incr = -incr;
	  }
      }
#endif /* PRECALC_LOOPS */

  /* Update changed values */
  vp->vibrato_control_counter = cc;
  vp->sample_increment = incr;
  vp->sample_offset = ofs;
  return resample_buffer + resample_buffer_offset;
}

/*********************** portamento versions ***************************/

static int rs_update_porta(int v)
{
    Voice *vp = &voice[v];
    int32 d;

    d = vp->porta_dpb;
    if(vp->porta_pb < 0)
    {
	if(d > -vp->porta_pb)
	    d = -vp->porta_pb;
    }
    else
    {
	if(d > vp->porta_pb)
	    d = -vp->porta_pb;
	else
	    d = -d;
    }

    vp->porta_pb += d;
    if(vp->porta_pb == 0)
    {
	vp->porta_control_ratio = 0;
	vp->porta_pb = 0;
    }
    recompute_freq(v);
    return vp->porta_control_ratio;
}

static sample_t *porta_resample_voice(int v, int32 *countptr, int mode)
{
    Voice *vp = &voice[v];
    int32 n = *countptr, i;
    sample_t *(* resampler)(int, int32 *, int);
    int cc = vp->porta_control_counter;
    int loop;

    if(vp->vibrato_control_ratio)
	resampler = vib_resample_voice;
    else
	resampler = normal_resample_voice;
    if(mode != 1)
	loop = 1;
    else
	loop = 0;

    vp->cache = NULL;
    resample_buffer_offset = 0;
    while(resample_buffer_offset < n)
    {
	if(cc == 0)
	{
	    if((cc = rs_update_porta(v)) == 0)
	    {
		i = n - resample_buffer_offset;
		resampler(v, &i, mode);
		resample_buffer_offset += i;
		break;
	    }
	}

	i = n - resample_buffer_offset;
	if(i > cc)
	    i = cc;
	resampler(v, &i, mode);
	resample_buffer_offset += i;

	if(!loop && (i == 0 || vp->status == VOICE_FREE))
	    break;
	cc -= i;
    }
    *countptr = resample_buffer_offset;
    resample_buffer_offset = 0;
    vp->porta_control_counter = cc;
    return resample_buffer;
}

/* interface function */
static sample_t *vib_resample_voice(int v, int32 *countptr, int mode)
{
    Voice *vp = &voice[v];

    vp->cache = NULL;
    if(mode == 0)
	return rs_vib_loop(vp, *countptr);
    if(mode == 1)
	return rs_vib_plain(v, countptr);
    return rs_vib_bidir(vp, *countptr);
}

/* interface function */
static sample_t *normal_resample_voice(int v, int32 *countptr, int mode)
{
    Voice *vp = &voice[v];
    if(mode == 0)
	return rs_loop(vp, *countptr);
    if(mode == 1)
	return rs_plain(v, countptr);
    return rs_bidir(vp, *countptr);
}

/* interface function */
sample_t *resample_voice(int v, int32 *countptr)
{
    Voice *vp = &voice[v];
    int mode;

    if(vp->sample->sample_rate == play_mode->rate &&
       vp->sample->root_freq == freq_table[vp->sample->note_to_use] &&
       vp->frequency == vp->orig_frequency)
    {
	int32 ofs;

	/* Pre-resampled data -- just update the offset and check if
	   we're out of data. */
	ofs = (int32)(vp->sample_offset >> FRACTION_BITS); /* Kind of silly to use
						   FRACTION_BITS here... */
	if(*countptr >= (vp->sample->data_length>>FRACTION_BITS) - ofs)
	{
	    /* Note finished. Free the voice. */
	    vp->timeout = 1;

	    /* Let the caller know how much data we had left */
	    *countptr = (int32)(vp->sample->data_length >> FRACTION_BITS) - ofs;
	}
	else
	    vp->sample_offset += *countptr << FRACTION_BITS;
	return vp->sample->data+ofs;
    }

    mode = vp->sample->modes;
    if((mode & MODES_LOOPING) &&
       ((mode & MODES_ENVELOPE) ||
	(vp->status & (VOICE_ON | VOICE_SUSTAINED))))
    {
	if(mode & MODES_PINGPONG)
	{
	    vp->cache = NULL;
	    mode = 2;	/* Bidir loop */
	}
	else
	    mode = 0;	/* loop */
    }
    else
	mode = 1;	/* no loop */

    if(vp->porta_control_ratio)
	return porta_resample_voice(v, countptr, mode);

    if(vp->vibrato_control_ratio)
	return vib_resample_voice(v, countptr, mode);

    return normal_resample_voice(v, countptr, mode);
}

void pre_resample(Sample * sp)
{
  double a, b, xdiff;
  splen_t ofs, newlen;
  sample_t *newdata, *dest, *src = (sample_t *)sp->data, *vptr;
  int32 v, v1, v2, v3, v4, v5, i, count, incr;

  ctl->cmsg(CMSG_INFO, VERB_DEBUG, " * pre-resampling for note %d (%s%d)",
	    sp->note_to_use,
	    note_name[sp->note_to_use % 12], (sp->note_to_use & 0x7F) / 12);

  a = b = ((double) (sp->root_freq) * play_mode->rate) /
      ((double) (sp->sample_rate) * freq_table[(int) (sp->note_to_use)]);
  if((int64)sp->data_length * a >= 0x7fffffffL)
  {
      /* Too large to compute */
      ctl->cmsg(CMSG_INFO, VERB_DEBUG, " *** Can't pre-resampling for note %d",
		sp->note_to_use);
      return;
  }
  newlen = (splen_t)(sp->data_length * a);
  count = (newlen >> FRACTION_BITS);
  ofs = incr = (sp->data_length - 1) / (count - 1);

  if((double)newlen + incr >= 0x7fffffffL)
  {
      /* Too large to compute */
      ctl->cmsg(CMSG_INFO, VERB_DEBUG, " *** Can't pre-resampling for note %d",
		sp->note_to_use);
      return;
  }

  dest = newdata = (sample_t *)safe_malloc((int32)(newlen >> (FRACTION_BITS - 1)) + 2);
  dest[newlen >> FRACTION_BITS] = 0;

  *dest++ = src[0];

  /* Since we're pre-processing and this doesn't have to be done in
     real-time, we go ahead and do the full sliding cubic interpolation. */
  for(i = 1; i < count; i++)
  {
/* use lesser order interpolation if we run out of samples */
#if defined(GAUSS_INTERPOLATION) || defined(NEWTON_INTERPOLATION)
    int32 left, right, temp_n;
    sample_t *sptr;
    int ii, jj;

    left = (ofs>>FRACTION_BITS);
    right = (sp->data_length>>FRACTION_BITS) - (ofs>>FRACTION_BITS) - 1;
    temp_n = (right<<1)-1;
    if (temp_n <= 0)
    	temp_n = 1;
    if (temp_n > (left<<1)+1)
    	temp_n = (left<<1)+1;

#ifdef GAUSS_INTERPOLATION
    if (temp_n < gauss_n)
#else
    if (temp_n < newt_n)
#endif
      {
    	xdiff = ofs & FRACTION_MASK;
    	xdiff /= (1L<<FRACTION_BITS);
    	xdiff += temp_n>>1;
	a = 0;
	sptr = src + (ofs>>FRACTION_BITS) - (temp_n>>1);
    	for (ii = temp_n; ii;)
    	{
    	    for (jj = 0; jj <= ii; jj++)
    	    	a += sptr[jj] * newt_coeffs[ii][jj];
    	    a *= xdiff - --ii;
    	}
	v = (sample_t) a + *sptr;
      }
#else
    /* I don't think it is worth the trouble of interpolating a few points
       with cubic instead of linear if the window can support a cubic, so if
       the pentic window can't be filled, just use linear instead.
     */
    if ((((ofs>>FRACTION_BITS)-2) < 0) ||
        (((ofs>>FRACTION_BITS)+3) > (sp->data_length>>FRACTION_BITS)))
            v = (sample_t)((int32)src[(ofs>>FRACTION_BITS)] + ((((int32)src[(ofs>>FRACTION_BITS)+1]-(int32)src[(ofs>>FRACTION_BITS)]) * (ofs & FRACTION_MASK)) >> FRACTION_BITS));
#endif
    else
      {
#ifdef GAUSS_INTERPOLATION
	sample_t *sptr;
	float *gptr, *gend;

	a = 0;
    	gptr = gauss_table[ofs&FRACTION_MASK];
    	gend = gptr + gauss_n;
	sptr = src + (ofs>>FRACTION_BITS) - (gauss_n>>1);
	do {
	    a += *(sptr++) * *(gptr++);
	} while (gptr <= gend);
	v = (int32) a;
#elif defined(NEWTON_INTERPOLATION)
	int n_new, n_old;
	int32 diff;

	if (newt_grow >= 0 && src == newt_old_src &&
	    (diff = (ofs>>FRACTION_BITS) - newt_old_trunc_x) > 0)
	{
	    n_new = newt_n + ((newt_grow + diff)<<1);
	    if (n_new <= newt_max)
	    {
	    	n_old = newt_n + (newt_grow<<1);
	    	newt_grow += diff;
	    	for (v1=(ofs>>FRACTION_BITS)+(n_new>>1)+1,v2=n_new;
		     v2 > n_old; --v1, --v2)
		{
		    newt_divd[0][v2] = src[v1];
	    	}
	    	for (v1 = 1; v1 <= n_new; v1++)
	    	    for (v2 = n_new; v2 > n_old; --v2)
		    	newt_divd[v1][v2] = (newt_divd[v1-1][v2] -
					     newt_divd[v1-1][v2-1]) *
					     newt_recip[v1];
	    }
	    else
	    	newt_grow = -1;
	}
	if (newt_grow < 0 || src != newt_old_src || diff < 0){
	    newt_grow = 0;
	    for (v1=(ofs>>FRACTION_BITS)-(newt_n>>1),v2=0;
		 v2 <= newt_n; v1++, v2++)
	    {
		newt_divd[0][v2] = src[v1];
	    }
	    for (v1 = 1; v1 <= newt_n; v1++)
	    	for (v2 = newt_n; v2 >= v1; --v2)
		    newt_divd[v1][v2] = (newt_divd[v1-1][v2] -
					 newt_divd[v1-1][v2-1]) *
					 newt_recip[v1];
	}
	n_new = newt_n + (newt_grow<<1);
	v2 = n_new;
	a = newt_divd[v2][v2];
	xdiff = (double)(ofs&FRACTION_MASK) / (1L<<FRACTION_BITS) +
	        	(newt_n>>1) + newt_grow;
	for (--v2; v2; --v2)
	{
	    a *= xdiff - v2;
	    a += newt_divd[v2][v2];
	}
	a = a*xdiff + **newt_divd;
	v = (int32) a;

	newt_old_src = src;
	newt_old_trunc_x = (ofs>>FRACTION_BITS);
#else
    	/* pentic B-spline 6-point interpolation */
    	vptr = src + (ofs>>FRACTION_BITS);
    	v = *(vptr - 2);
    	v1 = *(vptr - 1);
    	v2 = *vptr;
    	v3 = *(vptr + 1);
    	v4 = *(vptr + 2);
    	v5 = *(vptr + 3);

    	xdiff = TIM_FSCALENEG(ofs & FRACTION_MASK, FRACTION_BITS);
    	xdiff = (double) xdiff / (1L<<FRACTION_BITS);
    	v  = v2 + 0.04166666666*xdiff*((v3-v1)*16+(v-v4)*2+
             xdiff*((v3+v1)*16-v-v2*30-v4+
             xdiff*(v3*66-v2*70-v4*33+v1*39+v5*7-v*9+
             xdiff*(v2*126-v3*124+v4*61-v1*64-v5*12+v*13+
             xdiff*((v3-v2)*50+(v1-v4)*25+(v5-v)*5)))));
#endif
      }

    if(v < -32768)
	*dest++ = -32768;
    else if(v > 32767)
	*dest++ = 32767;
    else
	*dest++ = (int16)v;
    ofs += incr;
  }

  sp->data_length = newlen;
  sp->loop_start = (splen_t)(sp->loop_start * b);
  sp->loop_end = (splen_t)(sp->loop_end * b);
  free(sp->data);
  sp->data = (sample_t *) newdata;
  sp->root_freq = freq_table[(int) (sp->note_to_use)];
  sp->sample_rate = play_mode->rate;
  sp->low_freq = freq_table[0];
  sp->high_freq = freq_table[127];
}
