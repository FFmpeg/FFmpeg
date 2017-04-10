/** libavcodec DCE definitions
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

#include "libavcodec/avcodec.h"
#include "libavcodec/xvmc_internal.h"
#include "libavcodec/xvididct.h"
#include "libavcodec/wmv2dsp.h"
#include "libavcodec/vp9dsp.h"
#include "libavcodec/vp8dsp.h"
#include "libavcodec/vp56dsp.h"
#include "libavcodec/vp3dsp.h"
#include "libavcodec/vorbisdsp.h"
#include "libavcodec/videodsp.h"
#include "libavcodec/vdpau_compat.h"
#include "libavcodec/x86/vc1dsp.h"
#include "libavcodec/vc1dsp.h"
#include "libavcodec/synth_filter.h"
#include "libavcodec/svq1enc.h"
#include "libavcodec/x86/simple_idct.h"
#include "libavcodec/sbrdsp.h"
#include "libavcodec/rv34dsp.h"
#include "libavcodec/rdft.h"
#include "libavcodec/qpeldsp.h"
#include "libavcodec/aacpsdsp.h"
#include "libavcodec/pixblockdsp.h"
#include "libavcodec/mpegvideo.h"
#include "libavcodec/mpegvideoencdsp.h"
#include "libavcodec/mpegvideodsp.h"
#include "libavcodec/mpegaudiodsp.h"
#include "libavcodec/mlpdsp.h"
#include "libavcodec/me_cmp.h"
#include "libavcodec/lossless_videodsp.h"
#include "libavcodec/lossless_audiodsp.h"
#include "libavcodec/iirfilter.h"
#include "libavcodec/idctdsp.h"
#include "libavcodec/hpeldsp.h"
#include "libavcodec/x86/hevcdsp.h"
#include "libavcodec/hevcpred.h"
#include "libavcodec/hevcdsp.h"
#include "libavcodec/h264qpel.h"
#include "libavcodec/h264dsp.h"
#include "libavcodec/h264chroma.h"
#include "libavcodec/h264pred.h"
#include "libavcodec/h263dsp.h"
#include "libavcodec/g722dsp.h"
#include "libavcodec/fmtconvert.h"
#include "libavcodec/flacdsp.h"
#include "libavcodec/fft.h"
#include "libavcodec/fdctdsp.h"
#include "libavcodec/x86/fdct.h"
#include "libavcodec/celp_math.h"
#include "libavcodec/celp_filters.h"
#include "libavcodec/blockdsp.h"
#include "libavcodec/audiodsp.h"
#include "libavcodec/acelp_vectors.h"
#include "libavcodec/acelp_filters.h"
#include "libavcodec/ac3dsp.h"
#include "libavcodec/aacsbr.h"
#include "libavcodec/aac.h"
#include "libavcodec/aacenc.h"
#include "libavcodec/msmpeg4.h"

#if !(CONFIG_MSMPEG4_DECODER)
int ff_msmpeg4_decode_picture_header(MpegEncContext *s) {return 0;}
#endif
#if !(CONFIG_MSMPEG4_ENCODER)
int ff_msmpeg4_encode_init(MpegEncContext *s) {return 0;}
#endif
void ff_aac_coder_init_mips(AACEncContext *c) {return;}
void ff_aacdec_init_mips(AACContext *c) {return;}
void ff_aacsbr_func_ptr_init_mips(AACSBRContext *c) {return;}
void ff_ac3dsp_init_arm(AC3DSPContext *c, int bit_exact) {return;}
void ff_ac3dsp_init_mips(AC3DSPContext *c, int bit_exact) {return;}
void ff_acelp_filter_init_mips(ACELPFContext *c) {return;}
void ff_acelp_vectors_init_mips(ACELPVContext *c) {return;}
#if !(ARCH_X86_32)
void ff_add_bytes_mmx(uint8_t *dst, uint8_t *src, ptrdiff_t w) {return;}
#endif
#if !(ARCH_X86_32)
void ff_add_hfyu_left_pred_bgr32_mmx(uint8_t *dst, const uint8_t *src,
                                     intptr_t w, uint8_t *left) {return;}
#endif
#if !(ARCH_X86_32)
void ff_add_int16_mmx(uint16_t *dst, const uint16_t *src, unsigned mask, int w) {return;}
#endif
#if !(ARCH_X86_32)
void ff_add_median_pred_mmxext(uint8_t *dst, const uint8_t *top,
                               const uint8_t *diff, ptrdiff_t w,
                               int *left, int *left_top) {return;}
#endif
void ff_audiodsp_init_arm(AudioDSPContext *c) {return;}
void ff_audiodsp_init_ppc(AudioDSPContext *c) {return;}
void ff_blockdsp_init_alpha(BlockDSPContext *c) {return;}
void ff_blockdsp_init_arm(BlockDSPContext *c) {return;}
void ff_blockdsp_init_mips(BlockDSPContext *c) {return;}
void ff_blockdsp_init_ppc(BlockDSPContext *c) {return;}
void ff_celp_filter_init_mips(CELPFContext *c) {return;}
void ff_celp_math_init_mips(CELPMContext *c) {return;}
#if !(ARCH_X86_32)
void ff_diff_bytes_mmx(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                       intptr_t w) {return;}
#endif
#if !(ARCH_X86_32)
void ff_diff_int16_mmx (uint16_t *dst, const uint16_t *src1, const uint16_t *src2,
                        unsigned mask, int w) {return;}
#endif
#if !(HAVE_MMX_INLINE)
void ff_fdct_mmx(int16_t *block) {return;}
#endif
#if !(HAVE_MMXEXT_INLINE)
void ff_fdct_mmxext(int16_t *block) {return;}
#endif
#if !(HAVE_SSE2_INLINE)
void ff_fdct_sse2(int16_t *block) {return;}
#endif
void ff_fdctdsp_init_ppc(FDCTDSPContext *c, AVCodecContext *avctx,
                         unsigned high_bit_depth) {return;}
void ff_fft_fixed_init_arm(FFTContext *s) {return;}
void ff_fft_init_aarch64(FFTContext *s) {return;}
void ff_fft_init_arm(FFTContext *s) {return;}
void ff_fft_init_mips(FFTContext *s) {return;}
void ff_fft_init_ppc(FFTContext *s) {return;}
#if !(ARCH_X86_64)
void ff_flac_decorrelate_indep8_16_avx(uint8_t **out, int32_t **in, int channels, int len, int shift) {return;}
#endif
#if !(ARCH_X86_64)
void ff_flac_decorrelate_indep8_16_sse2(uint8_t **out, int32_t **in, int channels, int len, int shift) {return;}
#endif
#if !(ARCH_X86_64)
void ff_flac_decorrelate_indep8_32_avx(uint8_t **out, int32_t **in, int channels, int len, int shift) {return;}
#endif
#if !(ARCH_X86_64)
void ff_flac_decorrelate_indep8_32_sse2(uint8_t **out, int32_t **in, int channels, int len, int shift) {return;}
#endif
void ff_flacdsp_init_arm(FLACDSPContext *c, enum AVSampleFormat fmt, int channels, int bps) {return;}
void ff_fmt_convert_init_aarch64(FmtConvertContext *c, AVCodecContext *avctx) {return;}
void ff_fmt_convert_init_arm(FmtConvertContext *c, AVCodecContext *avctx) {return;}
void ff_fmt_convert_init_mips(FmtConvertContext *c) {return;}
void ff_fmt_convert_init_ppc(FmtConvertContext *c, AVCodecContext *avctx) {return;}
void ff_g722dsp_init_arm(G722DSPContext *c) {return;}
void ff_h263dsp_init_mips(H263DSPContext *ctx) {return;}
void ff_h264_pred_init_aarch64(H264PredContext *h, int codec_id,
                               const int bit_depth,
                               const int chroma_format_idc) {return;}
void ff_h264_pred_init_arm(H264PredContext *h, int codec_id,
                           const int bit_depth, const int chroma_format_idc) {return;}
void ff_h264_pred_init_mips(H264PredContext *h, int codec_id,
                            const int bit_depth, const int chroma_format_idc) {return;}
void ff_h264chroma_init_aarch64(H264ChromaContext *c, int bit_depth) {return;}
void ff_h264chroma_init_arm(H264ChromaContext *c, int bit_depth) {return;}
void ff_h264chroma_init_mips(H264ChromaContext *c, int bit_depth) {return;}
void ff_h264chroma_init_ppc(H264ChromaContext *c, int bit_depth) {return;}
void ff_h264dsp_init_aarch64(H264DSPContext *c, const int bit_depth,
                             const int chroma_format_idc) {return;}
void ff_h264dsp_init_arm(H264DSPContext *c, const int bit_depth,
                         const int chroma_format_idc) {return;}
void ff_h264dsp_init_mips(H264DSPContext *c, const int bit_depth,
                          const int chroma_format_idc) {return;}
void ff_h264dsp_init_ppc(H264DSPContext *c, const int bit_depth,
                         const int chroma_format_idc) {return;}
void ff_h264qpel_init_aarch64(H264QpelContext *c, int bit_depth) {return;}
void ff_h264qpel_init_arm(H264QpelContext *c, int bit_depth) {return;}
void ff_h264qpel_init_mips(H264QpelContext *c, int bit_depth) {return;}
void ff_h264qpel_init_ppc(H264QpelContext *c, int bit_depth) {return;}
void ff_hevc_dsp_init_mips(HEVCDSPContext *c, const int bit_depth) {return;}
#if !(ARCH_X86_64)
void ff_hevc_h_loop_filter_luma_10_avx(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_h_loop_filter_luma_10_sse2(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_h_loop_filter_luma_10_ssse3(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_h_loop_filter_luma_12_avx(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_h_loop_filter_luma_12_sse2(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_h_loop_filter_luma_12_ssse3(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_h_loop_filter_luma_8_avx(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_h_loop_filter_luma_8_sse2(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_h_loop_filter_luma_8_ssse3(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_idct_16x16_10_avx(int16_t *coeffs, int col_limit) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_idct_16x16_10_sse2(int16_t *coeffs, int col_limit) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_idct_16x16_8_avx(int16_t *coeffs, int col_limit) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_idct_16x16_8_sse2(int16_t *coeffs, int col_limit) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_idct_32x32_10_avx(int16_t *coeffs, int col_limit) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_idct_32x32_10_sse2(int16_t *coeffs, int col_limit) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_idct_32x32_8_avx(int16_t *coeffs, int col_limit) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_idct_32x32_8_sse2(int16_t *coeffs, int col_limit) {return;}
#endif
void ff_hevc_pred_init_mips(HEVCPredContext *hpc, int bit_depth) {return;}
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h16_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h24_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h32_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h32_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h48_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h48_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h64_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h64_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_h8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv16_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv24_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv32_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv32_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv48_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv48_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv64_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv64_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_hv8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v16_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v24_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v32_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v32_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v48_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v48_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v64_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v64_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_epel_v8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels16_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels24_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels32_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels32_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels48_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels48_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels64_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels64_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_pel_pixels8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h16_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h24_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h32_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h32_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h48_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h48_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h64_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h64_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_h8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv16_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv24_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv32_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv48_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv64_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_hv8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v16_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v24_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v32_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v32_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v48_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v48_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v64_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v64_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_qpel_v8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_h8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_hv8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_epel_v8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_pel_pixels8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_h8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_hv8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_bi_w_qpel_v8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h12_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h12_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h12_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h16_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h16_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h16_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h16_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h24_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h24_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h24_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h24_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h32_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h32_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h32_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h32_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h32_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h48_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h48_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h48_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h48_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h48_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h4_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h4_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h4_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h64_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h64_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h64_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h64_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h64_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h6_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h6_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h6_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h8_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h8_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_h8_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv12_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv12_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv12_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv16_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv16_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv16_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv16_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv24_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv24_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv24_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv24_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv32_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv32_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv32_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv32_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv32_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv48_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv48_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv48_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv48_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv48_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv4_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv4_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv4_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv64_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv64_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv64_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv64_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv64_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv6_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv6_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv6_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv8_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv8_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_hv8_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v12_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v12_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v12_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v16_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v16_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v16_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v16_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v24_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v24_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v24_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v24_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v32_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v32_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v32_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v32_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v32_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v48_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v48_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v48_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v48_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v48_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v4_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v4_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v4_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v64_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v64_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v64_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v64_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v64_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v6_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v6_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v6_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v8_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v8_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_epel_v8_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels12_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels12_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels12_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels16_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels16_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels16_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels16_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels24_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels24_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels24_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels24_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels32_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels32_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels32_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels32_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels32_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels48_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels48_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels48_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels48_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels48_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels4_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels4_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels4_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels64_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels64_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels64_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels64_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels64_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels6_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels6_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels6_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels8_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels8_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_pel_pixels8_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h12_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h12_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h12_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h16_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h16_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h16_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h16_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h24_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h24_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h24_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h24_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h32_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h32_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h32_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h32_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h32_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h48_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h48_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h48_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h48_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h48_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h4_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h4_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h4_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h64_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h64_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h64_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h64_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h64_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h8_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h8_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_h8_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv12_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv12_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv12_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv16_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv16_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv16_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv16_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv24_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv24_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv24_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv24_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv32_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv32_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv32_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv32_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv48_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv48_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv48_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv48_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv4_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv4_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv4_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv64_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv64_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv64_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv64_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv8_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv8_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_hv8_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v12_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v12_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v12_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v16_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v16_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v16_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v16_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v24_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v24_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v24_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v24_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v32_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v32_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v32_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v32_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v32_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v48_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v48_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v48_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v48_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v48_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v4_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v4_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v4_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v64_10_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v64_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v64_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v64_8_avx2(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v64_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v8_10_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v8_12_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_qpel_v8_8_sse4(int16_t *dst, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h16_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h24_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h32_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h32_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h48_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h48_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h64_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h64_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_h8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv16_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv24_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv32_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv32_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv48_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv48_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv64_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv64_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_hv8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v16_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v24_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v32_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v32_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v48_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v48_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v64_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v64_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_epel_v8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels128_8_avx2(uint8_t *dst, ptrdiff_t dststride,uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels32_8_avx2(uint8_t *dst, ptrdiff_t dststride,uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels48_8_avx2(uint8_t *dst, ptrdiff_t dststride,uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels64_8_avx2(uint8_t *dst, ptrdiff_t dststride,uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_pel_pixels96_8_avx2(uint8_t *dst, ptrdiff_t dststride,uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my,int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h16_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h24_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h32_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h32_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h48_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h48_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h64_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h64_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_h8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv16_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv24_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv32_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv48_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv64_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_hv8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v16_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v24_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v32_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v32_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v48_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v48_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v64_10_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v64_8_avx2(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_qpel_v8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_h8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_hv8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_epel_v8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels6_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels6_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels6_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_pel_pixels8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_h8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_hv8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v12_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v12_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v12_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v16_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v16_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v16_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v24_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v24_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v24_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v32_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v32_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v32_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v48_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v48_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v48_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v4_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v4_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v4_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v64_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v64_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v64_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v8_10_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v8_12_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_put_hevc_uni_w_qpel_v8_8_sse4(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *_src, ptrdiff_t _srcstride, int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_v_loop_filter_luma_10_avx(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_v_loop_filter_luma_10_sse2(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_v_loop_filter_luma_10_ssse3(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_v_loop_filter_luma_12_avx(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_v_loop_filter_luma_12_sse2(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_v_loop_filter_luma_12_ssse3(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_v_loop_filter_luma_8_avx(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_v_loop_filter_luma_8_sse2(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
#if !(ARCH_X86_64)
void ff_hevc_v_loop_filter_luma_8_ssse3(uint8_t *pix, ptrdiff_t stride, int beta, int *tc, uint8_t *no_p, uint8_t *no_q) {return;}
#endif
void ff_hevcdsp_init_arm(HEVCDSPContext *c, const int bit_depth) {return;}
void ff_hpeldsp_init_aarch64(HpelDSPContext *c, int flags) {return;}
void ff_hpeldsp_init_alpha(HpelDSPContext *c, int flags) {return;}
void ff_hpeldsp_init_arm(HpelDSPContext *c, int flags) {return;}
void ff_hpeldsp_init_mips(HpelDSPContext *c, int flags) {return;}
void ff_hpeldsp_init_ppc(HpelDSPContext *c, int flags) {return;}
void ff_idctdsp_init_aarch64(IDCTDSPContext *c, AVCodecContext *avctx,
                             unsigned high_bit_depth) {return;}
void ff_idctdsp_init_alpha(IDCTDSPContext *c, AVCodecContext *avctx,
                           unsigned high_bit_depth) {return;}
void ff_idctdsp_init_arm(IDCTDSPContext *c, AVCodecContext *avctx,
                         unsigned high_bit_depth) {return;}
void ff_idctdsp_init_mips(IDCTDSPContext *c, AVCodecContext *avctx,
                          unsigned high_bit_depth) {return;}
void ff_idctdsp_init_ppc(IDCTDSPContext *c, AVCodecContext *avctx,
                         unsigned high_bit_depth) {return;}
void ff_iir_filter_init_mips(FFIIRFilterContext *f) {return;}
#if !(ARCH_X86_32)
void ff_lfe_fir0_float_sse(float *pcm_samples, int32_t *lfe_samples, const float *filter_coeff, ptrdiff_t npcmblocks) {return;}
#endif
void ff_llauddsp_init_arm(LLAudDSPContext *c) {return;}
void ff_llauddsp_init_ppc(LLAudDSPContext *c) {return;}
void ff_llviddsp_init_ppc(LLVidDSPContext *llviddsp) {return;}
void ff_me_cmp_init_alpha(MECmpContext *c, AVCodecContext *avctx) {return;}
void ff_me_cmp_init_arm(MECmpContext *c, AVCodecContext *avctx) {return;}
void ff_me_cmp_init_mips(MECmpContext *c, AVCodecContext *avctx) {return;}
void ff_me_cmp_init_ppc(MECmpContext *c, AVCodecContext *avctx) {return;}
#if !(ARCH_X86_64)
void ff_mlp_rematrix_channel_avx2_bmi2(int32_t *samples, const int32_t *coeffs, const uint8_t *bypassed_lsbs, const int8_t *noise_buffer, int index, unsigned int dest_ch, uint16_t blockpos, unsigned int maxchan, int matrix_noise_shift, int access_unit_size_pow2, int32_t mask) {return;}
#endif
#if !(ARCH_X86_64)
void ff_mlp_rematrix_channel_sse4(int32_t *samples, const int32_t *coeffs, const uint8_t *bypassed_lsbs, const int8_t *noise_buffer, int index, unsigned int dest_ch, uint16_t blockpos, unsigned int maxchan, int matrix_noise_shift, int access_unit_size_pow2, int32_t mask) {return;}
#endif
void ff_mlpdsp_init_arm(MLPDSPContext *c) {return;}
void ff_mpadsp_init_aarch64(MPADSPContext *s) {return;}
void ff_mpadsp_init_arm(MPADSPContext *s) {return;}
void ff_mpadsp_init_mipsdsp(MPADSPContext *s) {return;}
void ff_mpadsp_init_mipsfpu(MPADSPContext *s) {return;}
void ff_mpadsp_init_ppc(MPADSPContext *s) {return;}
void ff_mpegvideodsp_init_ppc(MpegVideoDSPContext *c) {return;}
void ff_mpegvideoencdsp_init_arm(MpegvideoEncDSPContext *c,
                                 AVCodecContext *avctx) {return;}
void ff_mpegvideoencdsp_init_mips(MpegvideoEncDSPContext *c,
                                  AVCodecContext *avctx) {return;}
void ff_mpegvideoencdsp_init_ppc(MpegvideoEncDSPContext *c,
                                 AVCodecContext *avctx) {return;}
void ff_mpv_common_init_arm(MpegEncContext *s) {return;}
void ff_mpv_common_init_axp(MpegEncContext *s) {return;}
void ff_mpv_common_init_mips(MpegEncContext *s) {return;}
void ff_mpv_common_init_neon(MpegEncContext *s) {return;}
void ff_mpv_common_init_ppc(MpegEncContext *s) {return;}
#if !(CONFIG_MSMPEG4_ENCODER)
void ff_msmpeg4_encode_ext_header(MpegEncContext *s) {return;}
#endif
#if !(CONFIG_MSMPEG4_ENCODER)
void ff_msmpeg4_encode_mb(MpegEncContext *s, int16_t block[6][64],
                          int motion_x, int motion_y) {return;}
#endif
#if !(CONFIG_MSMPEG4_ENCODER)
void ff_msmpeg4_encode_picture_header(MpegEncContext *s, int picture_number) {return;}
#endif
void ff_pixblockdsp_init_alpha(PixblockDSPContext *c, AVCodecContext *avctx,
                               unsigned high_bit_depth) {return;}
void ff_pixblockdsp_init_arm(PixblockDSPContext *c, AVCodecContext *avctx,
                             unsigned high_bit_depth) {return;}
void ff_pixblockdsp_init_mips(PixblockDSPContext *c, AVCodecContext *avctx,
                              unsigned high_bit_depth) {return;}
void ff_pixblockdsp_init_ppc(PixblockDSPContext *c, AVCodecContext *avctx,
                             unsigned high_bit_depth) {return;}
void ff_psdsp_init_arm(PSDSPContext *s) {return;}
void ff_psdsp_init_mips(PSDSPContext *s) {return;}
void ff_qpeldsp_init_mips(QpelDSPContext *c) {return;}
void ff_rdft_init_arm(RDFTContext *s) {return;}
#if !(ARCH_X86_32)
void ff_rv34_idct_dc_add_mmx(uint8_t *dst, ptrdiff_t stride, int dc) {return;}
#endif
void ff_rv34dsp_init_arm(RV34DSPContext *c) {return;}
void ff_rv40dsp_init_aarch64(RV34DSPContext *c) {return;}
void ff_rv40dsp_init_arm(RV34DSPContext *c) {return;}
void ff_sbrdsp_init_arm(SBRDSPContext *s) {return;}
void ff_sbrdsp_init_mips(SBRDSPContext *s) {return;}
#if !(ARCH_X86_64)
void ff_simple_idct10_avx(int16_t *block) {return;}
#endif
#if !(ARCH_X86_64)
void ff_simple_idct10_put_avx(uint8_t *dest, ptrdiff_t line_size, int16_t *block) {return;}
#endif
#if !(ARCH_X86_64)
void ff_simple_idct10_put_sse2(uint8_t *dest, ptrdiff_t line_size, int16_t *block) {return;}
#endif
#if !(ARCH_X86_64)
void ff_simple_idct10_sse2(int16_t *block) {return;}
#endif
#if !(ARCH_X86_64)
void ff_simple_idct12_avx(int16_t *block) {return;}
#endif
#if !(ARCH_X86_64)
void ff_simple_idct12_put_avx(uint8_t *dest, ptrdiff_t line_size, int16_t *block) {return;}
#endif
#if !(ARCH_X86_64)
void ff_simple_idct12_put_sse2(uint8_t *dest, ptrdiff_t line_size, int16_t *block) {return;}
#endif
#if !(ARCH_X86_64)
void ff_simple_idct12_sse2(int16_t *block) {return;}
#endif
#if !(HAVE_MMX_INLINE)
void ff_simple_idct_add_mmx(uint8_t *dest, ptrdiff_t line_size, int16_t *block) {return;}
#endif
#if !(HAVE_SSE2_INLINE)
void ff_simple_idct_add_sse2(uint8_t *dest, ptrdiff_t line_size, int16_t *block) {return;}
#endif
#if !(HAVE_MMX_INLINE)
void ff_simple_idct_mmx(int16_t *block) {return;}
#endif
#if !(HAVE_MMX_INLINE)
void ff_simple_idct_put_mmx(uint8_t *dest, ptrdiff_t line_size, int16_t *block) {return;}
#endif
#if !(HAVE_SSE2_INLINE)
void ff_simple_idct_put_sse2(uint8_t *dest, ptrdiff_t line_size, int16_t *block) {return;}
#endif
void ff_svq1enc_init_ppc(SVQ1EncContext *c) {return;}
void ff_synth_filter_init_aarch64(SynthFilterContext *c) {return;}
void ff_synth_filter_init_arm(SynthFilterContext *c) {return;}
void ff_vc1dsp_init_aarch64(VC1DSPContext* dsp) {return;}
void ff_vc1dsp_init_arm(VC1DSPContext* dsp) {return;}
void ff_vc1dsp_init_mips(VC1DSPContext* dsp) {return;}
#if !(HAVE_6REGS && HAVE_MMX_INLINE)
void ff_vc1dsp_init_mmx(VC1DSPContext *dsp) {return;}
#endif
#if !(HAVE_6REGS && HAVE_MMXEXT_INLINE)
void ff_vc1dsp_init_mmxext(VC1DSPContext *dsp) {return;}
#endif
void ff_vc1dsp_init_ppc(VC1DSPContext *c) {return;}
void ff_vdpau_add_data_chunk(uint8_t *data, const uint8_t *buf,
                             int buf_size) {return;}
void ff_vdpau_h264_picture_complete(H264Context *h) {return;}
void ff_vdpau_h264_picture_start(H264Context *h) {return;}
void ff_vdpau_h264_set_reference_frames(H264Context *h) {return;}
void ff_vdpau_mpeg4_decode_picture(Mpeg4DecContext *s, const uint8_t *buf,
                                   int buf_size) {return;}
void ff_vdpau_mpeg_picture_complete(MpegEncContext *s, const uint8_t *buf,
                                    int buf_size, int slice_count) {return;}
void ff_vdpau_vc1_decode_picture(MpegEncContext *s, const uint8_t *buf,
                                 int buf_size) {return;}
void ff_videodsp_init_aarch64(VideoDSPContext *ctx, int bpc) {return;}
void ff_videodsp_init_arm(VideoDSPContext *ctx, int bpc) {return;}
void ff_videodsp_init_ppc(VideoDSPContext *ctx, int bpc) {return;}
void ff_vorbisdsp_init_aarch64(VorbisDSPContext *dsp) {return;}
void ff_vorbisdsp_init_arm(VorbisDSPContext *dsp) {return;}
void ff_vorbisdsp_init_ppc(VorbisDSPContext *dsp) {return;}
void ff_vp3dsp_init_arm(VP3DSPContext *c, int flags) {return;}
void ff_vp3dsp_init_ppc(VP3DSPContext *c, int flags) {return;}
void ff_vp6dsp_init_arm(VP56DSPContext *s) {return;}
void ff_vp78dsp_init_arm(VP8DSPContext *c) {return;}
void ff_vp78dsp_init_ppc(VP8DSPContext *c) {return;}
void ff_vp8dsp_init_arm(VP8DSPContext *c) {return;}
void ff_vp8dsp_init_mips(VP8DSPContext *c) {return;}
#if !(ARCH_X86_64)
void ff_vp9_iadst_iadst_16x16_add_avx2(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob) {return;}
#endif
#if !(ARCH_X86_64)
void ff_vp9_iadst_idct_16x16_add_avx2(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob) {return;}
#endif
#if !(ARCH_X86_64)
void ff_vp9_idct_iadst_16x16_add_avx2(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob) {return;}
#endif
#if !(ARCH_X86_64)
void ff_vp9_idct_idct_16x16_add_avx2(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob) {return;}
#endif
#if !(ARCH_X86_64)
void ff_vp9_idct_idct_32x32_add_avx2(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob) {return;}
#endif
void ff_vp9dsp_init_aarch64(VP9DSPContext *dsp, int bpp) {return;}
void ff_vp9dsp_init_arm(VP9DSPContext *dsp, int bpp) {return;}
void ff_vp9dsp_init_mips(VP9DSPContext *dsp, int bpp) {return;}
void ff_wmv2dsp_init_mips(WMV2DSPContext *c) {return;}
void ff_xvid_idct_init_mips(IDCTDSPContext *c, AVCodecContext *avctx,
                            unsigned high_bit_depth) {return;}
void ff_xvmc_init_block(MpegEncContext *s) {return;}
void ff_xvmc_pack_pblocks(MpegEncContext *s, int cbp) {return;}
AVCodec ff_aac_at_decoder = {0};
AVCodec ff_aac_at_encoder = {0};
AVCodec ff_ac3_at_decoder = {0};
AVCodec ff_adpcm_ima_qt_at_decoder = {0};
AVCodec ff_alac_at_decoder = {0};
AVCodec ff_alac_at_encoder = {0};
AVCodec ff_amr_nb_at_decoder = {0};
AVCodec ff_eac3_at_decoder = {0};
AVCodec ff_gsm_ms_at_decoder = {0};
AVCodec ff_h264_crystalhd_decoder = {0};
AVCodec ff_h264_mediacodec_decoder = {0};
AVCodec ff_h264_mmal_decoder = {0};
AVCodec ff_h264_omx_encoder = {0};
AVCodec ff_h264_vaapi_encoder = {0};
AVCodec ff_h264_vda_decoder = {0};
AVCodec ff_h264_vdpau_decoder = {0};
AVCodec ff_h264_videotoolbox_encoder = {0};
AVCodec ff_hap_encoder = {0};
AVCodec ff_hevc_mediacodec_decoder = {0};
AVCodec ff_hevc_vaapi_encoder = {0};
AVCodec ff_ilbc_at_decoder = {0};
AVCodec ff_ilbc_at_encoder = {0};
AVCodec ff_libcelt_decoder = {0};
AVCodec ff_libfdk_aac_decoder = {0};
AVCodec ff_libfdk_aac_encoder = {0};
AVCodec ff_libgsm_decoder = {0};
AVCodec ff_libgsm_encoder = {0};
AVCodec ff_libgsm_ms_decoder = {0};
AVCodec ff_libgsm_ms_encoder = {0};
AVCodec ff_libkvazaar_encoder = {0};
AVCodec ff_libopencore_amrnb_decoder = {0};
AVCodec ff_libopencore_amrnb_encoder = {0};
AVCodec ff_libopencore_amrwb_decoder = {0};
AVCodec ff_libopenh264_decoder = {0};
AVCodec ff_libopenh264_encoder = {0};
AVCodec ff_libopenjpeg_decoder = {0};
AVCodec ff_libopenjpeg_encoder = {0};
AVCodec ff_libopus_decoder = {0};
AVCodec ff_libschroedinger_decoder = {0};
AVCodec ff_libschroedinger_encoder = {0};
AVCodec ff_libshine_encoder = {0};
AVCodec ff_libtwolame_encoder = {0};
AVCodec ff_libvo_amrwbenc_encoder = {0};
AVCodec ff_libvpx_vp8_decoder = {0};
AVCodec ff_libvpx_vp9_decoder = {0};
AVCodec ff_libwavpack_encoder = {0};
AVCodec ff_libwebp_anim_encoder = {0};
AVCodec ff_libwebp_encoder = {0};
AVCodec ff_libx262_encoder = {0};
AVCodec ff_libxavs_encoder = {0};
AVCodec ff_libzvbi_teletext_decoder = {0};
AVCodec ff_mjpeg_vaapi_encoder = {0};
AVCodec ff_mp1_at_decoder = {0};
AVCodec ff_mp2_at_decoder = {0};
AVCodec ff_mp3_at_decoder = {0};
AVCodec ff_mpeg1_vdpau_decoder = {0};
AVCodec ff_mpeg2_crystalhd_decoder = {0};
AVCodec ff_mpeg2_mmal_decoder = {0};
AVCodec ff_mpeg2_vaapi_encoder = {0};
AVCodec ff_mpeg4_crystalhd_decoder = {0};
AVCodec ff_mpeg4_mediacodec_decoder = {0};
AVCodec ff_mpeg4_mmal_decoder = {0};
AVCodec ff_mpeg4_vdpau_decoder = {0};
AVCodec ff_mpeg_vdpau_decoder = {0};
AVCodec ff_mpeg_xvmc_decoder = {0};
AVCodec ff_msmpeg4_crystalhd_decoder = {0};
AVCodec ff_pcm_alaw_at_decoder = {0};
AVCodec ff_pcm_alaw_at_encoder = {0};
AVCodec ff_pcm_mulaw_at_decoder = {0};
AVCodec ff_pcm_mulaw_at_encoder = {0};
AVCodec ff_qdm2_at_decoder = {0};
AVCodec ff_qdmc_at_decoder = {0};
AVCodec ff_vc1_crystalhd_decoder = {0};
AVCodec ff_vc1_mmal_decoder = {0};
AVCodec ff_vc1_vdpau_decoder = {0};
AVCodec ff_vp8_mediacodec_decoder = {0};
AVCodec ff_vp8_vaapi_encoder = {0};
AVCodec ff_vp9_mediacodec_decoder = {0};
AVCodec ff_wmv3_crystalhd_decoder = {0};
AVCodec ff_wmv3_vdpau_decoder = {0};
AVHWAccel ff_h263_vaapi_hwaccel = {0};
AVHWAccel ff_h263_videotoolbox_hwaccel = {0};
AVHWAccel ff_h264_mediacodec_hwaccel = {0};
AVHWAccel ff_h264_mmal_hwaccel = {0};
AVHWAccel ff_h264_vaapi_hwaccel = {0};
AVHWAccel ff_h264_vda_hwaccel = {0};
AVHWAccel ff_h264_vda_old_hwaccel = {0};
AVHWAccel ff_h264_vdpau_hwaccel = {0};
AVHWAccel ff_h264_videotoolbox_hwaccel = {0};
AVHWAccel ff_hevc_mediacodec_hwaccel = {0};
AVHWAccel ff_hevc_vaapi_hwaccel = {0};
AVHWAccel ff_hevc_vdpau_hwaccel = {0};
AVHWAccel ff_mpeg1_vdpau_hwaccel = {0};
AVHWAccel ff_mpeg1_videotoolbox_hwaccel = {0};
AVHWAccel ff_mpeg1_xvmc_hwaccel = {0};
AVHWAccel ff_mpeg2_mmal_hwaccel = {0};
AVHWAccel ff_mpeg2_vaapi_hwaccel = {0};
AVHWAccel ff_mpeg2_vdpau_hwaccel = {0};
AVHWAccel ff_mpeg2_videotoolbox_hwaccel = {0};
AVHWAccel ff_mpeg2_xvmc_hwaccel = {0};
AVHWAccel ff_mpeg4_mediacodec_hwaccel = {0};
AVHWAccel ff_mpeg4_mmal_hwaccel = {0};
AVHWAccel ff_mpeg4_vaapi_hwaccel = {0};
AVHWAccel ff_mpeg4_vdpau_hwaccel = {0};
AVHWAccel ff_mpeg4_videotoolbox_hwaccel = {0};
AVHWAccel ff_vc1_mmal_hwaccel = {0};
AVHWAccel ff_vc1_vaapi_hwaccel = {0};
AVHWAccel ff_vc1_vdpau_hwaccel = {0};
AVHWAccel ff_vp8_mediacodec_hwaccel = {0};
#if !(CONFIG_VP9_D3D11VA_HWACCEL)
AVHWAccel ff_vp9_d3d11va_hwaccel = {0};
#endif
#if !(CONFIG_VP9_DXVA2_HWACCEL)
AVHWAccel ff_vp9_dxva2_hwaccel = {0};
#endif
AVHWAccel ff_vp9_mediacodec_hwaccel = {0};
AVHWAccel ff_vp9_vaapi_hwaccel = {0};
AVHWAccel ff_wmv3_vaapi_hwaccel = {0};
AVHWAccel ff_wmv3_vdpau_hwaccel = {0};
