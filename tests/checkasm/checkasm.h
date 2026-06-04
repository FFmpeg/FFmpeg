/*
 * Assembly testing and benchmarking tool
 * Copyright (c) 2015 Henrik Gramner
 * Copyright (c) 2008 Loren Merritt
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef TESTS_CHECKASM_CHECKASM_H
#define TESTS_CHECKASM_CHECKASM_H

#include <stdint.h>
#include <checkasm/test.h>
#include <checkasm/utils.h>

#include "config.h"
#include "libavutil/avstring.h"
#include "libavutil/cpu.h"
#include "libavutil/emms.h"
#include "libavutil/internal.h"
#include "libavutil/lfg.h"
#include "libavutil/timer.h"

void checkasm_check_aacencdsp(void);
void checkasm_check_aacpsdsp(void);
void checkasm_check_ac3dsp(void);
void checkasm_check_aes(void);
void checkasm_check_afir(void);
void checkasm_check_alacdsp(void);
void checkasm_check_apv_dsp(void);
void checkasm_check_audiodsp(void);
void checkasm_check_av_tx(void);
void checkasm_check_blackdetect(void);
void checkasm_check_blend(void);
void checkasm_check_blockdsp(void);
void checkasm_check_bswapdsp(void);
void checkasm_check_cavsdsp(void);
void checkasm_check_colordetect(void);
void checkasm_check_colorspace(void);
void checkasm_check_crc(void);
void checkasm_check_dcadsp(void);
void checkasm_check_diracdsp(void);
void checkasm_check_exrdsp(void);
void checkasm_check_fdctdsp(void);
void checkasm_check_fixed_dsp(void);
void checkasm_check_flacdsp(void);
void checkasm_check_float_dsp(void);
void checkasm_check_fmtconvert(void);
void checkasm_check_g722dsp(void);
void checkasm_check_h263dsp(void);
void checkasm_check_h264chroma(void);
void checkasm_check_h264dsp(void);
void checkasm_check_h264pred(void);
void checkasm_check_h264qpel(void);
void checkasm_check_hevc_add_res(void);
void checkasm_check_hevc_deblock(void);
void checkasm_check_hevc_dequant(void);
void checkasm_check_hevc_idct(void);
void checkasm_check_hevc_pel(void);
void checkasm_check_hevc_pred(void);
void checkasm_check_hevc_sao(void);
void checkasm_check_hpeldsp(void);
void checkasm_check_huffyuvdsp(void);
void checkasm_check_huffyuvencdsp(void);
void checkasm_check_idctdsp(void);
void checkasm_check_idet(void);
void checkasm_check_jpeg2000dsp(void);
void checkasm_check_llauddsp(void);
void checkasm_check_lls(void);
void checkasm_check_llviddsp(void);
void checkasm_check_llvidencdsp(void);
void checkasm_check_lpc(void);
void checkasm_check_motion(void);
void checkasm_check_mpeg4videodsp(void);
void checkasm_check_mpegvideo_unquantize(void);
void checkasm_check_mpegvideoencdsp(void);
void checkasm_check_nlmeans(void);
void checkasm_check_opusdsp(void);
void checkasm_check_pixblockdsp(void);
void checkasm_check_pixelutils(void);
void checkasm_check_png(void);
void checkasm_check_qpeldsp(void);
void checkasm_check_sbcdsp(void);
void checkasm_check_sbrdsp(void);
void checkasm_check_rv34dsp(void);
void checkasm_check_rv40dsp(void);
void checkasm_check_scene_sad(void);
void checkasm_check_snowdsp(void);
void checkasm_check_svq1enc(void);
void checkasm_check_synth_filter(void);
void checkasm_check_sw_gbrp(void);
void checkasm_check_sw_range_convert(void);
void checkasm_check_sw_rgb(void);
void checkasm_check_sw_scale(void);
void checkasm_check_sw_xyz2rgb(void);
void checkasm_check_sw_yuv2rgb(void);
void checkasm_check_sw_yuv2yuv(void);
void checkasm_check_sw_ops(void);
void checkasm_check_takdsp(void);
void checkasm_check_utvideodsp(void);
void checkasm_check_v210dec(void);
void checkasm_check_v210enc(void);
void checkasm_check_vc1dsp(void);
void checkasm_check_vf_bwdif(void);
void checkasm_check_vf_eq(void);
void checkasm_check_vf_fspp(void);
void checkasm_check_vf_gblur(void);
void checkasm_check_vf_hflip(void);
void checkasm_check_vf_pp7(void);
void checkasm_check_vf_threshold(void);
void checkasm_check_vf_sobel(void);
void checkasm_check_vp3dsp(void);
void checkasm_check_vp6dsp(void);
void checkasm_check_vp8dsp(void);
void checkasm_check_vp9dsp(void);
void checkasm_check_vp9_ipred(void);
void checkasm_check_vp9_itxfm(void);
void checkasm_check_vp9_loopfilter(void);
void checkasm_check_vp9_mc(void);
void checkasm_check_videodsp(void);
void checkasm_check_vorbisdsp(void);
void checkasm_check_vvc_alf(void);
void checkasm_check_vvc_mc(void);
void checkasm_check_vvc_sao(void);

#define rnd checkasm_rand
#define declare_func_float declare_func
#define bench(...) checkasm_bench(__VA_ARGS__)

#define randomize_stddev(buf, size, stddev) \
    checkasm_randomize_distf(buf, size, (CheckasmDist){ 0.0, stddev })
#define randomize_stddev_dbl(buf, size, stddev) \
    checkasm_randomize_dist(buf, size, (CheckasmDist){ 0.0, stddev })

#define PIXEL_RECT(name, w, h)                                   \
    BUF_RECT(uint16_t, name##_16, w, h);                         \
    av_unused ptrdiff_t name##_stride = name##_16_stride;        \
    av_unused int       name##_buf_h  = name##_16_buf_h;         \
    av_unused uint8_t*  name##_buf    = (uint8_t*)name##_16_buf; \
    uint8_t*            name          = (uint8_t*)name##_16

#define CLEAR_PIXEL_RECT(name) CLEAR_BUF_RECT(name##_16)

/* This assumes that there is a local variable named "bit_depth".
 * For tests that don't have that and only operate on a single
 * bitdepth, just call checkasm_check(uint8_t, ...) directly. */
#define checkasm_check_pixel2(buf1, stride1, buf2, stride2, ...) \
    ((bit_depth > 8) ?                                           \
     checkasm_check2(uint16_t, (const uint16_t*)buf1, stride1,   \
                               (const uint16_t*)buf2, stride2,   \
                               __VA_ARGS__) :                    \
     checkasm_check2(uint8_t,  (const uint8_t*) buf1, stride1,   \
                               (const uint8_t*) buf2, stride2,   \
                               __VA_ARGS__))
#define checkasm_check_pixel(...) \
    checkasm_check_pixel2(__VA_ARGS__, 0, 0, 0)
#define checkasm_check_pixel_padded(...) \
    checkasm_check_pixel2(__VA_ARGS__, 1, 1, 8)
#define checkasm_check_pixel_padded_align(...) \
    checkasm_check_pixel2(__VA_ARGS__, 8)

/* This assumes that there is a local variable named "bit_depth"
 * and that the type-specific buffers obey the name ## _BITDEPTH
 * convention.
 * For tests that don't have that and only operate on a single
 * bitdepth, just call checkasm_check(uint8_t, ...) directly. */
#define checkasm_check_dctcoef(buf1, stride1, buf2, stride2, ...) \
    ((bit_depth > 8) ?                                        \
     checkasm_check(int32_t, buf1 ## _32, stride1,            \
                             buf2 ## _32, stride2,            \
                             __VA_ARGS__) :                   \
     checkasm_check(int16_t, buf1 ## _16, stride1,            \
                             buf2 ## _16, stride2,            \
                             __VA_ARGS__))

typedef uint8_t pixel;

#endif /* TESTS_CHECKASM_CHECKASM_H */
