/** libavfilter DCE definitions
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "config.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/deshake_opencl.h"
#include "libavfilter/unsharp_opencl.h"

int ff_opencl_apply_unsharp(AVFilterContext *ctx, AVFrame *in, AVFrame *out) {return 0;}
int ff_opencl_deshake_init(AVFilterContext *ctx) {return 0;}
int ff_opencl_deshake_process_inout_buf(AVFilterContext *ctx, AVFrame *in, AVFrame *out) {return 0;}
int ff_opencl_transform(AVFilterContext *ctx,
                        int width, int height, int cw, int ch,
                        const float *matrix_y, const float *matrix_uv,
                        enum InterpolateMethod interpolate,
                        enum FillMethod fill, AVFrame *in, AVFrame *out) {return 0;}
int ff_opencl_unsharp_init(AVFilterContext *ctx) {return 0;}
int ff_opencl_unsharp_process_inout_buf(AVFilterContext *ctx, AVFrame *in, AVFrame *out) {return 0;}
#if !(ARCH_X86_64)
void ff_multiply3x3_sse2(int16_t *data[3], ptrdiff_t stride, int w, int h,
                         const int16_t coeff[3][3][8]) {return;}
#endif
void ff_opencl_deshake_uninit(AVFilterContext *ctx) {return;}
void ff_opencl_unsharp_uninit(AVFilterContext *ctx) {return;}
#if !(ARCH_X86_64)
void ff_rgb2yuv_420p10_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_stride[3], int16_t *rgb_in[3], ptrdiff_t rgb_stride, int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_rgb2yuv_420p12_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_stride[3], int16_t *rgb_in[3], ptrdiff_t rgb_stride, int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_rgb2yuv_420p8_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_stride[3], int16_t *rgb_in[3], ptrdiff_t rgb_stride, int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_rgb2yuv_422p10_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_stride[3], int16_t *rgb_in[3], ptrdiff_t rgb_stride, int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_rgb2yuv_422p12_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_stride[3], int16_t *rgb_in[3], ptrdiff_t rgb_stride, int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_rgb2yuv_422p8_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_stride[3], int16_t *rgb_in[3], ptrdiff_t rgb_stride, int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_rgb2yuv_444p10_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_stride[3], int16_t *rgb_in[3], ptrdiff_t rgb_stride, int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_rgb2yuv_444p12_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_stride[3], int16_t *rgb_in[3], ptrdiff_t rgb_stride, int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_rgb2yuv_444p8_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_stride[3], int16_t *rgb_in[3], ptrdiff_t rgb_stride, int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_ssim_4x4_line_ssse3(const uint8_t *buf, ptrdiff_t buf_stride,
                            const uint8_t *ref, ptrdiff_t ref_stride,
                            int (*sums)[4], int w) {return;}
#endif
#if !(ARCH_X86_64)
void ff_w3fdif_complex_high_sse2(int32_t *work_line,
                                 uint8_t *in_lines_cur[5],
                                 uint8_t *in_lines_adj[5],
                                 const int16_t *coef, int linesize) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2rgb_420p10_sse2(int16_t *rgb_out[3], ptrdiff_t rgb_stride, uint8_t *yuv_in[3], const ptrdiff_t yuv_stride[3], int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2rgb_420p12_sse2(int16_t *rgb_out[3], ptrdiff_t rgb_stride, uint8_t *yuv_in[3], const ptrdiff_t yuv_stride[3], int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2rgb_420p8_sse2(int16_t *rgb_out[3], ptrdiff_t rgb_stride, uint8_t *yuv_in[3], const ptrdiff_t yuv_stride[3], int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2rgb_422p10_sse2(int16_t *rgb_out[3], ptrdiff_t rgb_stride, uint8_t *yuv_in[3], const ptrdiff_t yuv_stride[3], int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2rgb_422p12_sse2(int16_t *rgb_out[3], ptrdiff_t rgb_stride, uint8_t *yuv_in[3], const ptrdiff_t yuv_stride[3], int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2rgb_422p8_sse2(int16_t *rgb_out[3], ptrdiff_t rgb_stride, uint8_t *yuv_in[3], const ptrdiff_t yuv_stride[3], int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2rgb_444p10_sse2(int16_t *rgb_out[3], ptrdiff_t rgb_stride, uint8_t *yuv_in[3], const ptrdiff_t yuv_stride[3], int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2rgb_444p12_sse2(int16_t *rgb_out[3], ptrdiff_t rgb_stride, uint8_t *yuv_in[3], const ptrdiff_t yuv_stride[3], int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2rgb_444p8_sse2(int16_t *rgb_out[3], ptrdiff_t rgb_stride, uint8_t *yuv_in[3], const ptrdiff_t yuv_stride[3], int w, int h, const int16_t coeff[3][3][8], const int16_t yuv_offset[8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_420p10to10_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_420p10to12_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_420p10to8_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_420p12to10_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_420p12to12_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_420p12to8_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_420p8to10_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_420p8to12_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_420p8to8_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_422p10to10_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_422p10to12_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_422p10to8_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_422p12to10_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_422p12to12_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_422p12to8_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_422p8to10_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_422p8to12_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_422p8to8_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_444p10to10_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_444p10to12_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_444p10to8_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_444p12to10_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_444p12to12_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_444p12to8_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_444p8to10_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_444p8to12_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
#if !(ARCH_X86_64)
void ff_yuv2yuv_444p8to8_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], const int16_t yuv_offset[2][8]) {return;}
#endif
AVFilter ff_af_asyncts = {0};
AVFilter ff_af_azmq = {0};
AVFilter ff_af_bs2b = {0};
AVFilter ff_af_ladspa = {0};
AVFilter ff_af_resample = {0};
AVFilter ff_af_rubberband = {0};
AVFilter ff_af_sofalizer = {0};
AVFilter ff_asrc_flite = {0};
AVFilter ff_vf_coreimage = {0};
AVFilter ff_vf_frei0r = {0};
AVFilter ff_vf_ocr = {0};
AVFilter ff_vf_ocv = {0};
AVFilter ff_vf_scale_npp = {0};
AVFilter ff_vf_scale_vaapi = {0};
AVFilter ff_vf_vidstabdetect = {0};
AVFilter ff_vf_vidstabtransform = {0};
AVFilter ff_vf_zmq = {0};
AVFilter ff_vf_zscale = {0};
AVFilter ff_vsrc_coreimagesrc = {0};
AVFilter ff_vsrc_frei0r_src = {0};
