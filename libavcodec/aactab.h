/*
 * AAC data declarations
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * AAC data declarations
 * @author Oded Shimon  ( ods15 ods15 dyndns org )
 * @author Maxim Gavrilov ( maxim.gavrilov gmail com )
 */

#ifndef AVCODEC_AACTAB_H
#define AVCODEC_AACTAB_H

#include "libavutil/mem.h"
#include "aac.h"

#include <stdint.h>

/* NOTE:
 * Tables in this file are shared by the AAC decoders and encoder
 */

extern float ff_aac_pow2sf_tab[428];
extern float ff_aac_pow34sf_tab[428];

void ff_aac_tableinit(void);

/* @name ltp_coef
 * Table of the LTP coefficients
 */
static const INTFLOAT ltp_coef[8] = {
    Q30(0.570829), Q30(0.696616), Q30(0.813004), Q30(0.911304),
    Q30(0.984900), Q30(1.067894), Q30(1.194601), Q30(1.369533),
};

/* @name tns_tmp2_map
 * Tables of the tmp2[] arrays of LPC coefficients used for TNS.
 * The suffix _M_N[] indicate the values of coef_compress and coef_res
 * respectively.
 * @{
 */
static const INTFLOAT tns_tmp2_map_1_3[4] = {
    Q31(0.00000000), Q31(-0.43388373),  Q31(0.64278758),  Q31(0.34202015),
};

static const INTFLOAT tns_tmp2_map_0_3[8] = {
    Q31(0.00000000), Q31(-0.43388373), Q31(-0.78183150), Q31(-0.97492790),
    Q31(0.98480773), Q31( 0.86602539), Q31( 0.64278758), Q31( 0.34202015),
};

static const INTFLOAT tns_tmp2_map_1_4[8] = {
    Q31(0.00000000), Q31(-0.20791170), Q31(-0.40673664), Q31(-0.58778524),
    Q31(0.67369562), Q31( 0.52643216), Q31( 0.36124167), Q31( 0.18374951),
};

static const INTFLOAT tns_tmp2_map_0_4[16] = {
    Q31( 0.00000000), Q31(-0.20791170), Q31(-0.40673664), Q31(-0.58778524),
    Q31(-0.74314481), Q31(-0.86602539), Q31(-0.95105654), Q31(-0.99452192),
    Q31( 0.99573416), Q31( 0.96182561), Q31( 0.89516330), Q31( 0.79801720),
    Q31( 0.67369562), Q31( 0.52643216), Q31( 0.36124167), Q31( 0.18374951),
};

static const INTFLOAT * const tns_tmp2_map[4] = {
    tns_tmp2_map_0_3,
    tns_tmp2_map_0_4,
    tns_tmp2_map_1_3,
    tns_tmp2_map_1_4
};
// @}

/* @name window coefficients
 * @{
 */
DECLARE_ALIGNED(32, extern float,  ff_aac_kbd_long_1024)[1024];
DECLARE_ALIGNED(32, extern float,  ff_aac_kbd_short_128)[128];
DECLARE_ALIGNED(32, extern const float, ff_aac_eld_window_512)[1920];
DECLARE_ALIGNED(32, extern const int,   ff_aac_eld_window_512_fixed)[1920];
DECLARE_ALIGNED(32, extern const float, ff_aac_eld_window_480)[1800];
DECLARE_ALIGNED(32, extern const int,   ff_aac_eld_window_480_fixed)[1800];
// @}

/* Initializes data shared between float decoder and encoder. */
void ff_aac_float_common_init(void);

/* @name number of scalefactor window bands for long and short transform windows respectively
 * @{
 */
extern const uint8_t ff_aac_num_swb_1024[];
extern const uint8_t ff_aac_num_swb_960 [];
extern const uint8_t ff_aac_num_swb_512 [];
extern const uint8_t ff_aac_num_swb_480 [];
extern const uint8_t ff_aac_num_swb_128 [];
extern const uint8_t ff_aac_num_swb_120 [];
// @}

extern const uint8_t ff_aac_pred_sfb_max [];

extern const uint32_t ff_aac_scalefactor_code[121];
extern const uint8_t  ff_aac_scalefactor_bits[121];

extern const uint16_t * const ff_aac_spectral_codes[11];
extern const uint8_t  * const ff_aac_spectral_bits [11];
extern const uint16_t  ff_aac_spectral_sizes[11];

extern const float *const ff_aac_codebook_vectors[];
extern const float *const ff_aac_codebook_vector_vals[];
extern const uint16_t *const ff_aac_codebook_vector_idx[];

extern const uint16_t * const ff_swb_offset_1024[13];
extern const uint16_t * const ff_swb_offset_960 [13];
extern const uint16_t * const ff_swb_offset_512 [13];
extern const uint16_t * const ff_swb_offset_480 [13];
extern const uint16_t * const ff_swb_offset_128 [13];
extern const uint16_t * const ff_swb_offset_120 [13];

extern const uint8_t ff_tns_max_bands_1024[13];
extern const uint8_t ff_tns_max_bands_512 [13];
extern const uint8_t ff_tns_max_bands_480 [13];
extern const uint8_t ff_tns_max_bands_128 [13];

#endif /* AVCODEC_AACTAB_H */
