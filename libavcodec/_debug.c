#include "libavutil/samplefmt.h"
#include "avcodec.h"

struct AACContext;
struct AACEncContext;
struct PSDSPContext;
struct AACSBRContext;
struct AC3DSPContext;
struct ACELPFContext;
struct ACELPVContext;
struct AudioDSPContext;
struct BlockDSPContext;
struct AVCodecContext;
struct CELPFContext;
struct CELPMContext;
struct FDCTDSPContext;
struct FFTContext;
struct FLACDSPContext;
struct FmtConvertContext;
struct G722DSPContext;
struct H263DSPContext;
struct H264ChromaContext;
struct H264DSPContext;
struct H264PredContext;
struct H264QpelContext;
struct HEVCDSPContext;
struct HEVCPredContext;
struct HpelDSPContext;
struct FFIIRFilterContext;
struct IMDCT15Context;
struct LLAudDSPContext;
struct MECmpContext;
struct MLPDSPContext;
struct MpegEncContext;
struct H264Context;
struct Mpeg4DecContext;
struct MpegVideoDSPContext;
struct MpegvideoEncDSPContext;
struct PixblockDSPContext;
struct QpelDSPContext;
struct RDFTContext;
struct RV34DSPContext;
struct SBRDSPContext;
struct SVQ1EncContext;
struct SynthFilterContext;
struct VC1DSPContext;
struct VideoDSPContext;
struct VorbisDSPContext;
struct VP3DSPContext;
struct VP56DSPContext;
struct VP8DSPContext;
struct VP9DSPContext;
struct IDCTDSPContext;
struct MPADSPContext;

#if !HAVE_INTRINSICS_NEON
void ff_mpv_common_init_neon(struct MpegEncContext *s){};
#endif

#if !ARCH_ALPHA
void ff_blockdsp_init_alpha(struct BlockDSPContext *c){};
void ff_fmt_convert_init_aarch64(struct FmtConvertContext *c, struct AVCodecContext *avctx){};
void ff_hpeldsp_init_alpha(struct HpelDSPContext *c, int flags){};
void ff_idctdsp_init_alpha(struct IDCTDSPContext *c, struct AVCodecContext *avctx,
                           unsigned high_bit_depth){};
void ff_me_cmp_init_alpha(struct MECmpContext *c, struct AVCodecContext *avctx){};
void ff_mpv_common_init_axp(struct MpegEncContext *s){};
void ff_pixblockdsp_init_alpha(struct PixblockDSPContext *c, struct AVCodecContext *avctx,
                               unsigned high_bit_depth){};
#endif

#if !ARCH_AARCH64
void ff_fft_init_aarch64(struct FFTContext *s){};
void ff_h264chroma_init_aarch64(struct H264ChromaContext *c, int bit_depth){};
void ff_h264dsp_init_aarch64(struct H264DSPContext *c, const int bit_depth,
                             const int chroma_format_idc){};
void ff_h264_pred_init_aarch64(struct H264PredContext *h, int codec_id,
                               const int bit_depth, const int chroma_format_idc){};
void ff_h264qpel_init_aarch64(struct H264QpelContext *c, int bit_depth){};
void ff_hpeldsp_init_aarch64(struct HpelDSPContext *c, int flags){};
void ff_imdct15_init_aarch64(struct IMDCT15Context *s){};
void ff_mpadsp_init_aarch64(struct MPADSPContext *s){};
void ff_rv40dsp_init_aarch64(struct RV34DSPContext *c){};
void ff_synth_filter_init_aarch64(struct SynthFilterContext *c){};
void ff_vc1dsp_init_aarch64(struct VC1DSPContext* dsp){};
void ff_videodsp_init_aarch64(struct VideoDSPContext *ctx, int bpc){};
void ff_vorbisdsp_init_aarch64(struct VorbisDSPContext *dsp){};
#endif

#if !ARCH_ARM
void ff_psdsp_init_arm(struct PSDSPContext *s){};
void ff_ac3dsp_init_arm(struct AC3DSPContext *c, int bit_exact){};
void ff_audiodsp_init_arm(struct AudioDSPContext *c){};
void ff_blockdsp_init_arm(struct BlockDSPContext *c){};
void ff_fft_init_arm(struct FFTContext *s){};
void ff_fft_fixed_init_arm(struct FFTContext *s){};
void ff_flacdsp_init_arm(struct FLACDSPContext *c, enum AVSampleFormat fmt, int channels, int bps){};
void ff_fmt_convert_init_arm(struct FmtConvertContext *c, struct AVCodecContext *avctx){};
void ff_g722dsp_init_arm(struct G722DSPContext *c){};
void ff_h264chroma_init_arm(struct H264ChromaContext *c, int bit_depth){};
void ff_h264dsp_init_arm(struct H264DSPContext *c, const int bit_depth,
                         const int chroma_format_idc){};
void ff_h264_pred_init_arm(struct H264PredContext *h, int codec_id,
                           const int bit_depth, const int chroma_format_idc){};
void ff_h264qpel_init_arm(struct H264QpelContext *c, int bit_depth){};
void ff_hevcdsp_init_arm(struct HEVCDSPContext *c, const int bit_depth){};
void ff_hpeldsp_init_arm(struct HpelDSPContext *c, int flags){};
void ff_idctdsp_init_arm(struct IDCTDSPContext *c, struct AVCodecContext *avctx,
                         unsigned high_bit_depth){};
void ff_llauddsp_init_arm(struct LLAudDSPContext *c){};
void ff_me_cmp_init_arm(struct MECmpContext *c, struct AVCodecContext *avctx){};
void ff_mlpdsp_init_arm(struct MLPDSPContext *c){};
void ff_mpadsp_init_arm(struct MPADSPContext *s){};
void ff_mpv_common_init_arm(struct MpegEncContext *s){};
void ff_mpegvideoencdsp_init_arm(struct MpegvideoEncDSPContext *c,
                                 struct AVCodecContext *avctx){};
void ff_pixblockdsp_init_arm(struct PixblockDSPContext *c, struct AVCodecContext *avctx,
                             unsigned high_bit_depth){};
void ff_rdft_init_arm(struct RDFTContext *s){};
void ff_rv34dsp_init_arm(struct RV34DSPContext *c){};
void ff_rv40dsp_init_arm(struct RV34DSPContext *c){};
void ff_sbrdsp_init_arm(struct SBRDSPContext *s){};
void ff_synth_filter_init_arm(struct SynthFilterContext *c){};
void ff_vc1dsp_init_arm(struct VC1DSPContext* dsp){};
void ff_videodsp_init_arm(struct VideoDSPContext *ctx, int bpc){};
void ff_vorbisdsp_init_arm(struct VorbisDSPContext *dsp){};
void ff_vp3dsp_init_arm(struct VP3DSPContext *c, int flags){};
void ff_vp6dsp_init_arm(struct VP56DSPContext *s, enum AVCodecID codec){};
void ff_vp78dsp_init_arm(struct VP8DSPContext *c){};
void ff_vp8dsp_init_arm(struct VP8DSPContext *c){};
#endif

#if !ARCH_PPC
void ff_audiodsp_init_ppc(struct AudioDSPContext *c){};
void ff_blockdsp_init_ppc(struct BlockDSPContext *c){};
void ff_fdctdsp_init_ppc(struct FDCTDSPContext *c, struct AVCodecContext *avctx, unsigned high_bit_depth){};
void ff_fft_init_ppc(struct FFTContext *s){};
void ff_fmt_convert_init_ppc(struct FmtConvertContext *c, struct AVCodecContext *avctx){};
void ff_h264chroma_init_ppc(struct H264ChromaContext *c, int bit_depth){};
void ff_h264dsp_init_ppc(struct H264DSPContext *c, const int bit_depth,
                         const int chroma_format_idc){};
void ff_h264qpel_init_ppc(struct H264QpelContext *c, int bit_depth){};
void ff_hpeldsp_init_ppc(struct HpelDSPContext *c, int flags){};
void ff_idctdsp_init_ppc(struct IDCTDSPContext *c, struct AVCodecContext *avctx,
                         unsigned high_bit_depth){};
void ff_llauddsp_init_ppc(struct LLAudDSPContext *c){};
void ff_me_cmp_init_ppc(struct MECmpContext *c, struct AVCodecContext *avctx){};
void ff_mpadsp_init_ppc(struct MPADSPContext *s){};
void ff_mpv_common_init_ppc(struct MpegEncContext *s){};
void ff_mpegvideodsp_init_ppc(struct MpegVideoDSPContext *c){};
void ff_mpegvideoencdsp_init_ppc(struct MpegvideoEncDSPContext *c,
                                 struct AVCodecContext *avctx){};
void ff_pixblockdsp_init_ppc(struct PixblockDSPContext *c, struct AVCodecContext *avctx,
                             unsigned high_bit_depth){};
void ff_svq1enc_init_ppc(struct SVQ1EncContext *c){};
void ff_vc1dsp_init_ppc(struct VC1DSPContext *c){};
void ff_videodsp_init_ppc(struct VideoDSPContext *ctx, int bpc){};
void ff_vorbisdsp_init_ppc(struct VorbisDSPContext *dsp){};
void ff_vp3dsp_init_ppc(struct VP3DSPContext *c, int flags){};
void ff_vp78dsp_init_ppc(struct VP8DSPContext *c){};
#endif

#if !ARCH_MIPS
void ff_aacdec_init_mips(struct AACContext *c){};
void ff_aac_coder_init_mips(struct AACEncContext *c) {};
void ff_psdsp_init_mips(struct PSDSPContext *s){};
void ff_aacsbr_func_ptr_init_mips(struct AACSBRContext *c){};
void ff_ac3dsp_init_mips(struct AC3DSPContext *c, int bit_exact){};
void ff_acelp_filter_init_mips(struct ACELPFContext *c){};
void ff_acelp_vectors_init_mips(struct ACELPVContext *c){};
void ff_blockdsp_init_mips(struct BlockDSPContext *c){};
void ff_celp_filter_init_mips(struct CELPFContext *c){};
void ff_celp_math_init_mips(struct CELPMContext *c){};
void ff_fft_init_mips(struct FFTContext *s){};
void ff_fmt_convert_init_mips(struct FmtConvertContext *c){};
void ff_h263dsp_init_mips(struct H263DSPContext *ctx){};
void ff_h264chroma_init_mips(struct H264ChromaContext *c, int bit_depth){};
void ff_h264dsp_init_mips(struct H264DSPContext *c, const int bit_depth,
                          const int chroma_format_idc){};
void ff_h264_pred_init_mips(struct H264PredContext *h, int codec_id,
                            const int bit_depth, const int chroma_format_idc){};
void ff_h264qpel_init_mips(struct H264QpelContext *c, int bit_depth){};
void ff_hevc_dsp_init_mips(struct HEVCDSPContext *c, const int bit_depth){};
void ff_hevc_pred_init_mips(struct HEVCPredContext *hpc, int bit_depth){};
void ff_hpeldsp_init_mips(struct HpelDSPContext *c, int flags){};
void ff_idctdsp_init_mips(struct IDCTDSPContext *c, struct AVCodecContext *avctx,
                          unsigned high_bit_depth){};
void ff_iir_filter_init_mips(struct FFIIRFilterContext *f){};
void ff_me_cmp_init_mips(struct MECmpContext *c, struct AVCodecContext *avctx){};
void ff_mpadsp_init_mipsfpu(struct MPADSPContext *s){};
void ff_mpadsp_init_mipsdsp(struct MPADSPContext *s){};
void ff_mpv_common_init_mips(struct MpegEncContext *s){};
void ff_mpegvideoencdsp_init_mips(struct MpegvideoEncDSPContext *c,
                                  struct AVCodecContext *avctx){};
void ff_pixblockdsp_init_mips(struct PixblockDSPContext *c, struct AVCodecContext *avctx,
                              unsigned high_bit_depth){};
void ff_qpeldsp_init_mips(struct QpelDSPContext *c){};
void ff_sbrdsp_init_mips(struct SBRDSPContext *s){};
void ff_vp8dsp_init_mips(struct VP8DSPContext *c){};
void ff_vp9dsp_init_mips(struct VP9DSPContext *dsp, int bpp){};
void ff_xvid_idct_init_mips(struct IDCTDSPContext *c, struct AVCodecContext *avctx,
                            unsigned high_bit_depth){};
#endif

#if !ARCH_X86
void ff_psdsp_init_x86(struct PSDSPContext *s){};
void ff_ac3dsp_init_x86(struct AC3DSPContext *c, int bit_exact){};
void ff_audiodsp_init_x86(struct AudioDSPContext *c){};
void ff_blockdsp_init_x86(struct BlockDSPContext *c, struct AVCodecContext *avctx){};
void ff_fdctdsp_init_x86(struct FDCTDSPContext *c, struct AVCodecContext *avctx, unsigned high_bit_depth){};
void ff_fft_init_x86(struct FFTContext *s){};
void ff_flacdsp_init_x86(struct FLACDSPContext *c, enum AVSampleFormat fmt, int channels, int bps){};
void ff_fmt_convert_init_x86(struct FmtConvertContext *c, struct AVCodecContext *avctx){};
void ff_g722dsp_init_x86(struct G722DSPContext *c){};
void ff_h263dsp_init_x86(struct H263DSPContext *ctx){};
void ff_h264chroma_init_x86(struct H264ChromaContext *c, int bit_depth){};
void ff_h264dsp_init_x86(struct H264DSPContext *c, const int bit_depth,
                         const int chroma_format_idc){};
void ff_h264_pred_init_x86(struct H264PredContext *h, int codec_id,
                           const int bit_depth, const int chroma_format_idc){};
void ff_h264qpel_init_x86(struct H264QpelContext *c, int bit_depth){};
void ff_hevc_dsp_init_x86(struct HEVCDSPContext *c, const int bit_depth){};
void ff_hpeldsp_init_x86(struct HpelDSPContext *c, int flags){};
void ff_idctdsp_init_x86(struct IDCTDSPContext *c, struct AVCodecContext *avctx,
                         unsigned high_bit_depth){};
void ff_llauddsp_init_x86(struct LLAudDSPContext *c){};
void ff_me_cmp_init_x86(struct MECmpContext *c, struct AVCodecContext *avctx){};
void ff_mlpdsp_init_x86(struct MLPDSPContext *c){};
void ff_mpadsp_init_x86(struct MPADSPContext *s){};
void ff_mpv_common_init_x86(struct MpegEncContext *s){};
void ff_mpegvideodsp_init_x86(struct MpegVideoDSPContext *c){};
void ff_mpegvideoencdsp_init_x86(struct MpegvideoEncDSPContext *c,
                                 struct AVCodecContext *avctx){};
void ff_pixblockdsp_init_x86(struct PixblockDSPContext *c, struct AVCodecContext *avctx,
                             unsigned high_bit_depth){};
void ff_qpeldsp_init_x86(struct QpelDSPContext *c){};
void ff_rv34dsp_init_x86(struct RV34DSPContext *c){};
void ff_rv40dsp_init_x86(struct RV34DSPContext *c){};
void ff_sbrdsp_init_x86(struct SBRDSPContext *s){};
void ff_svq1enc_init_x86(struct SVQ1EncContext *c){};
void ff_synth_filter_init_x86(struct SynthFilterContext *c){};
void ff_vc1dsp_init_x86(struct VC1DSPContext* dsp){};
void ff_videodsp_init_x86(struct VideoDSPContext *ctx, int bpc){};
void ff_vorbisdsp_init_x86(struct VorbisDSPContext *dsp){};
void ff_vp3dsp_init_x86(struct VP3DSPContext *c, int flags){};
void ff_vp6dsp_init_x86(struct VP56DSPContext* c, enum AVCodecID codec){};
void ff_vp78dsp_init_x86(struct VP8DSPContext *c){};
void ff_vp8dsp_init_x86(struct VP8DSPContext *c){};
void ff_vp9dsp_init_x86(struct VP9DSPContext *dsp, int bpp, int bitexact){};
#endif

#if !ARCH_X86_32
void ff_lfe_fir0_float_sse(float *pcm_samples, int32_t *lfe_samples,
                             const float *filter_coeff, ptrdiff_t npcmblocks){};
void ff_add_bytes_mmx(uint8_t *dst, uint8_t *src, intptr_t w){};
void ff_add_hfyu_median_pred_mmxext(uint8_t *dst, const uint8_t *top,
                                    const uint8_t *diff, intptr_t w,
                                    int *left, int *left_top){};
void ff_add_hfyu_left_pred_bgr32_mmx(uint8_t *dst, const uint8_t *src,
                                     intptr_t w, uint8_t *left){};
void ff_diff_bytes_mmx(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                       intptr_t w){};
#endif

#if !ARCH_X86_64
void ff_flac_decorrelate_indep8_16_sse2(uint8_t **out, int32_t **in, int channels, \
                                              int len, int shift) {};
void ff_flac_decorrelate_indep8_16_avx(uint8_t **out, int32_t **in, int channels, \
                                              int len, int shift) {};
void ff_flac_decorrelate_indep8_32_sse2(uint8_t **out, int32_t **in, int channels, \
                                              int len, int shift) {};
void ff_flac_decorrelate_indep8_32_avx(uint8_t **out, int32_t **in, int channels, \
                                              int len, int shift) {};
#endif

#if !HAVE_MMX_INLINE
void ff_fdct_mmx(int16_t *block){};
void ff_simple_idct_mmx(int16_t *block){};
void ff_simple_idct_add_mmx(uint8_t *dest, int line_size, int16_t *block){};
void ff_simple_idct_put_mmx(uint8_t *dest, int line_size, int16_t *block){};
void ff_vc1dsp_init_mmx(struct VC1DSPContext *dsp){}; 
#endif

#if !HAVE_MMXEXT_INLINE
void ff_fdct_mmxext(int16_t *block){};
void ff_vc1dsp_init_mmxext(struct VC1DSPContext *dsp){};
#endif

#if !HAVE_SSE2_INLINE
void ff_fdct_sse2(int16_t *block){};
#endif

#if !CONFIG_GPL 
void ff_flac_enc_lpc_16_sse4(int32_t * a, const int32_t * b, int c, int d, const int32_t * e,int f){};
#endif
							 
#if !CONFIG_MPEG_VDPAU_DECODER && !CONFIG_MPEG1_VDPAU_DECODER 
void ff_vdpau_mpeg_picture_complete(struct MpegEncContext *s, const uint8_t *buf,
                                    int buf_size, int slice_count){};
#endif

#if !CONFIG_H264_VDPAU_DECODER
void ff_vdpau_add_data_chunk(uint8_t *data, const uint8_t *buf,
                             int buf_size){};
void ff_vdpau_h264_picture_start(struct H264Context *h){};
void ff_vdpau_h264_set_reference_frames(struct H264Context *h){};
void ff_vdpau_h264_picture_complete(struct H264Context *h){};
#endif

#if !CONFIG_VC1_VDPAU_DECODER 
void ff_vdpau_vc1_decode_picture(struct MpegEncContext *s, const uint8_t *buf,
                                 int buf_size){};
#endif

#if !CONFIG_MPEG4_VDPAU_DECODER
void ff_vdpau_mpeg4_decode_picture(struct Mpeg4DecContext *s, const uint8_t *buf,
                                   int buf_size){};
#endif

#if !CONFIG_MPEG1_XVMC_HWACCEL && !CONFIG_MPEG2_XVMC_HWACCEL
void ff_xvmc_init_block(struct MpegEncContext *s){};
void ff_xvmc_pack_pblocks(struct MpegEncContext *s, int cbp){};
#endif

#if !CONFIG_H263_VAAPI_HWACCEL
AVHWAccel ff_h263_vaapi_hwaccel;
#endif 

#if !CONFIG_VIDEOTOOLBOX
AVHWAccel ff_h263_videotoolbox_hwaccel;
AVHWAccel ff_h264_videotoolbox_hwaccel;
AVHWAccel ff_mpeg1_videotoolbox_hwaccel;
AVHWAccel ff_mpeg2_videotoolbox_hwaccel;
AVHWAccel ff_mpeg4_videotoolbox_hwaccel;
#endif

#if !CONFIG_H264_MMAL_DECODER
AVHWAccel ff_h264_mmal_hwaccel;
AVHWAccel ff_mpeg2_mmal_hwaccel;
AVHWAccel ff_mpeg4_mmal_hwaccel;
AVHWAccel ff_vc1_mmal_hwaccel;
#endif

#if !CONFIG_H264_QSV_DECODER
AVHWAccel ff_h264_qsv_hwaccel;
#endif

#if !CONFIG_HEVC_QSV_DECODER
AVHWAccel ff_hevc_qsv_hwaccel;
#endif

#if !CONFIG_MPEG2_QSV_DECODER
AVHWAccel ff_mpeg2_qsv_hwaccel;
#endif

#if !CONFIG_VC1_QSV_DECODER
AVHWAccel ff_vc1_qsv_hwaccel;
#endif

#if !CONFIG_H264_VAAPI_HWACCEL
AVHWAccel ff_h264_vaapi_hwaccel;
#endif

#if !CONFIG_HEVC_VAAPI_HWACCEL
AVHWAccel ff_hevc_vaapi_hwaccel;
#endif

#if !CONFIG_MPEG2_VAAPI_HWACCEL
AVHWAccel ff_mpeg2_vaapi_hwaccel;
#endif

#if !CONFIG_MPEG4_VAAPI_HWACCEL
AVHWAccel ff_mpeg4_vaapi_hwaccel;
#endif

#if !CONFIG_VC1_VAAPI_HWACCEL
AVHWAccel ff_vc1_vaapi_hwaccel;
#endif

#if !CONFIG_VP9_VAAPI_HWACCEL
AVHWAccel ff_vp9_vaapi_hwaccel;
#endif

#if !CONFIG_WMV3_VAAPI_HWACCEL
AVHWAccel ff_wmv3_vaapi_hwaccel;
#endif

#if !CONFIG_H264_VDPAU_HWACCEL
AVHWAccel ff_h264_vdpau_hwaccel;
#endif

#if !CONFIG_HEVC_VDPAU_HWACCEL
AVHWAccel ff_hevc_vdpau_hwaccel;
#endif

#if !CONFIG_MPEG1_VDPAU_HWACCEL
AVHWAccel ff_mpeg1_vdpau_hwaccel;
#endif

#if !CONFIG_MPEG2_VDPAU_HWACCEL
AVHWAccel ff_mpeg2_vdpau_hwaccel;
#endif

#if !CONFIG_MPEG4_VDPAU_HWACCEL
AVHWAccel ff_mpeg4_vdpau_hwaccel;
#endif

#if !CONFIG_VC1_VDPAU_HWACCEL
AVHWAccel ff_vc1_vdpau_hwaccel;
#endif

#if !CONFIG_WMV3_VDPAU_HWACCEL
AVHWAccel ff_wmv3_vdpau_hwaccel;
#endif

#if !CONFIG_H264_VDA_HWACCEL
AVHWAccel ff_h264_vda_hwaccel;
AVHWAccel ff_h264_vda_old_hwaccel;
#endif

#if !CONFIG_MPEG1_XVMC_HWACCEL
AVHWAccel ff_mpeg1_xvmc_hwaccel;
#endif

#if !CONFIG_MPEG2_XVMC_HWACCEL
AVHWAccel ff_mpeg2_xvmc_hwaccel;
#endif

#if !CONFIG_PNG_ENCODER
AVCodec ff_png_encoder;
#endif

#if !CONFIG_APNG_ENCODER
AVCodec ff_apng_encoder;
#endif

#if !CONFIG_PNG_DECODER
AVCodec ff_png_decoder;
#endif

#if !CONFIG_APNG_DECODER
AVCodec ff_apng_decoder;
#endif

#if !CONFIG_DXA_DECODER
AVCodec ff_dxa_decoder;
#endif

#if !CONFIG_EXR_DECODER
AVCodec ff_exr_decoder;
#endif

#if !CONFIG_FLASHSV_ENCODER
AVCodec ff_flashsv_encoder;
#endif

#if !CONFIG_FLASHSV_DECODER
AVCodec ff_flashsv_decoder;
#endif

#if !CONFIG_FLASHSV2_ENCODER
AVCodec ff_flashsv2_encoder;
#endif

#if !CONFIG_FLASHSV2_DECODER
AVCodec ff_flashsv2_decoder;
#endif

#if !CONFIG_G2M_DECODER
AVCodec ff_g2m_decoder;
#endif

#if !CONFIG_H264_CRYSTALHD_DECODER
AVCodec ff_h264_crystalhd_decoder;
#endif

#if !CONFIG_H264_MMAL_DECODER
AVCodec ff_h264_mmal_decoder;
#endif

#if !CONFIG_H264_QSV_DECODER
AVCodec ff_h264_qsv_decoder;
#endif

#if !CONFIG_H264_VDA_DECODER
AVCodec ff_h264_vda_decoder;
#endif

#if !CONFIG_H264_VDPAU_DECODER
AVCodec ff_h264_vdpau_decoder;
#endif

#if !CONFIG_HAP_ENCODER
AVCodec ff_hap_encoder;
#endif

#if !CONFIG_HEVC_QSV_DECODER
AVCodec ff_hevc_qsv_decoder;
#endif

#if !CONFIG_MPEG_XVMC_DECODER
AVCodec ff_mpeg_xvmc_decoder;
#endif

#if !CONFIG_MPEG4_CRYSTALHD_DECODER
AVCodec ff_mpeg4_crystalhd_decoder;
#endif

#if !CONFIG_MPEG4_MMAL_DECODER
AVCodec ff_mpeg4_mmal_decoder;
#endif

#if !CONFIG_MPEG4_VDPAU_DECODER
AVCodec ff_mpeg4_vdpau_decoder;
#endif

#if !CONFIG_MPEG_VDPAU_DECODER
AVCodec ff_mpeg_vdpau_decoder;
#endif

#if !CONFIG_MPEG1_VDPAU_DECODER
AVCodec ff_mpeg1_vdpau_decoder;
#endif

#if !CONFIG_MPEG2_MMAL_DECODER
AVCodec ff_mpeg2_mmal_decoder;
#endif

#if !CONFIG_MPEG2_CRYSTALHD_DECODER
AVCodec ff_mpeg2_crystalhd_decoder;
#endif

#if !CONFIG_MPEG2_QSV_DECODER
AVCodec ff_mpeg2_qsv_decoder;
#endif

#if !CONFIG_MSMPEG4_CRYSTALHD_DECODER
AVCodec ff_msmpeg4_crystalhd_decoder;
#endif

#if !CONFIG_RSCC_DECODER
AVCodec ff_rscc_decoder;
#endif

#if !CONFIG_SCREENPRESSO_DECODER
AVCodec ff_screenpresso_decoder;
#endif

#if !CONFIG_TDSC_DECODER
AVCodec ff_tdsc_decoder;
#endif

#if !CONFIG_TSCC_DECODER
AVCodec ff_tscc_decoder;
#endif

#if !CONFIG_VC1_CRYSTALHD_DECODER
AVCodec ff_vc1_crystalhd_decoder;
#endif

#if !CONFIG_VC1_VDPAU_DECODER
AVCodec ff_vc1_vdpau_decoder;
#endif

#if !CONFIG_VC1_MMAL_DECODER
AVCodec ff_vc1_mmal_decoder;
#endif

#if !CONFIG_VC1_QSV_DECODER
AVCodec ff_vc1_qsv_decoder;
#endif

#if !CONFIG_WMV3_CRYSTALHD_DECODER
AVCodec ff_wmv3_crystalhd_decoder;
#endif

#if !CONFIG_WMV3_VDPAU_DECODER
AVCodec ff_wmv3_vdpau_decoder;
#endif

#if !CONFIG_ZEROCODEC_DECODER
AVCodec ff_zerocodec_decoder;
#endif

#if !CONFIG_ZLIB_ENCODER
AVCodec ff_zlib_encoder;
#endif

#if !CONFIG_ZLIB_DECODER
AVCodec ff_zlib_decoder;
#endif

#if !CONFIG_ZMBV_ENCODER
AVCodec ff_zmbv_encoder;
#endif

#if !CONFIG_ZMBV_DECODER
AVCodec ff_zmbv_decoder;
#endif

#if !CONFIG_ZLIB_ENCODER
AVCodec ff_zlib_encoder;
#endif

#if !CONFIG_LIBCELT_DECODER
AVCodec ff_libcelt_decoder;
#endif

#if !CONFIG_LIBDCADEC_DECODER
AVCodec ff_libdcadec_decoder;
#endif

#if !CONFIG_LIBFAAC_ENCODER
AVCodec ff_libfaac_encoder;
#endif

#if !CONFIG_LIBFDK_AAC_ENCODER
AVCodec ff_libfdk_aac_encoder;
#endif

#if !CONFIG_LIBFDK_AAC_DECODER
AVCodec ff_libfdk_aac_decoder;
#endif

#if !CONFIG_LIBGSM_ENCODER
AVCodec ff_libgsm_encoder;
#endif

#if !CONFIG_LIBGSM_DECODER
AVCodec ff_libgsm_decoder;
#endif

#if !CONFIG_LIBGSM_MS_ENCODER
AVCodec ff_libgsm_ms_encoder;
#endif

#if !CONFIG_LIBGSM_MS_DECODER
AVCodec ff_libgsm_ms_decoder;
#endif

#if !CONFIG_LIBILBC_ENCODER
AVCodec ff_libilbc_encoder;
#endif

#if !CONFIG_LIBILBC_DECODER
AVCodec ff_libilbc_decoder;
#endif

#if !CONFIG_LIBMP3LAME_ENCODER
AVCodec ff_libmp3lame_encoder;
#endif

#if !CONFIG_LIBOPENCORE_AMRNB_ENCODER
AVCodec ff_libopencore_amrnb_encoder;
#endif

#if !CONFIG_LIBOPENCORE_AMRNB_DECODER
AVCodec ff_libopencore_amrnb_decoder;
#endif

#if !CONFIG_LIBOPENCORE_AMRWB_DECODER
AVCodec ff_libopencore_amrwb_decoder;
#endif

#if !CONFIG_LIBOPENJPEG_ENCODER
AVCodec ff_libopenjpeg_encoder;
#endif

#if !CONFIG_LIBOPENJPEG_DECODER
AVCodec ff_libopenjpeg_decoder;
#endif

#if !CONFIG_LIBOPUS_ENCODER
AVCodec ff_libopus_encoder;
#endif

#if !CONFIG_LIBOPUS_DECODER
AVCodec ff_libopus_decoder;
#endif

#if !CONFIG_LIBSCHROEDINGER_ENCODER
AVCodec ff_libschroedinger_encoder;
#endif

#if !CONFIG_LIBSCHROEDINGER_DECODER
AVCodec ff_libschroedinger_decoder;
#endif

#if !CONFIG_LIBSHINE_ENCODER
AVCodec ff_libshine_encoder;
#endif

#if !CONFIG_LIBSHINE_DECODER
AVCodec ff_libshine_decoder;
#endif

#if !CONFIG_LIBSPEEX_ENCODER
AVCodec ff_libspeex_encoder;
#endif

#if !CONFIG_LIBSPEEX_DECODER
AVCodec ff_libspeex_decoder;
#endif

#if !CONFIG_LIBTHEORA_ENCODER
AVCodec ff_libtheora_encoder;
#endif

#if !CONFIG_LIBTWOLAME_ENCODER
AVCodec ff_libtwolame_encoder;
#endif

#if !CONFIG_LIBUTVIDEO_ENCODER
AVCodec ff_libutvideo_encoder;
#endif

#if !CONFIG_LIBUTVIDEO_DECODER
AVCodec ff_libutvideo_decoder;
#endif

#if !CONFIG_LIBVO_AMRWBENC_ENCODER
AVCodec ff_libvo_amrwbenc_encoder;
#endif

#if !CONFIG_LIBVORBIS_ENCODER
AVCodec ff_libvorbis_encoder;
#endif

#if !CONFIG_LIBVORBIS_DECODER
AVCodec ff_libvorbis_decoder;
#endif

#if !CONFIG_LIBVPX_VP8_ENCODER
AVCodec ff_libvpx_vp8_encoder;
#endif

#if !CONFIG_LIBVPX_VP8_DECODER
AVCodec ff_libvpx_vp8_decoder;
#endif

#if !CONFIG_LIBVPX_VP9_ENCODER
AVCodec ff_libvpx_vp9_encoder;
#endif

#if !CONFIG_LIBVPX_VP9_DECODER
AVCodec ff_libvpx_vp9_decoder;
#endif

#if !CONFIG_LIBWAVPACK_ENCODER
AVCodec ff_libwavpack_encoder;
#endif

#if !CONFIG_LIBWEBP_ANIM_ENCODER
AVCodec ff_libwebp_anim_encoder;
#endif

#if !CONFIG_LIBWEBP_ENCODER
AVCodec ff_libwebp_encoder;
#endif

#if !CONFIG_LIBX262_ENCODER
AVCodec ff_libx262_encoder;
#endif

#if !CONFIG_LIBX264_ENCODER
AVCodec ff_libx264_encoder;
AVCodec ff_libx264rgb_encoder;
#endif

#if !CONFIG_LIBX265_ENCODER
AVCodec ff_libx265_encoder;
#endif

#if !CONFIG_LIBXAVS_ENCODER
AVCodec ff_libxavs_encoder;
#endif

#if !CONFIG_LIBXVID_ENCODER
AVCodec ff_libxvid_encoder;
#endif

#if !CONFIG_LIBZVBI_TELETEXT_DECODER
AVCodec ff_libzvbi_teletext_decoder;
#endif

#if !CONFIG_LIBOPENH264_ENCODER
AVCodec ff_libopenh264_encoder;
#endif

#if !CONFIG_H264_QSV_ENCODER
AVCodec ff_h264_qsv_encoder;
#endif

#if !CONFIG_NVENC_ENCODER
AVCodec ff_nvenc_encoder;
#endif

#if !CONFIG_NVENC_H264_ENCODER
AVCodec ff_nvenc_h264_encoder;
#endif

#if !CONFIG_NVENC_HEVC_ENCODER
AVCodec ff_nvenc_hevc_encoder;
#endif

#if !CONFIG_HEVC_QSV_ENCODER
AVCodec ff_hevc_qsv_encoder;
#endif

#if !CONFIG_LIBKVAZAAR_ENCODER
AVCodec ff_libkvazaar_encoder;
#endif

#if !CONFIG_MPEG2_QSV_ENCODER
AVCodec ff_mpeg2_qsv_encoder;
#endif

#if !CONFIG_H264_MEDIACODEC_DECODER
AVCodec ff_h264_mediacodec_decoder;
#endif

#if !CONFIG_AAC_AT_DECODER
AVCodec ff_aac_at_decoder;
#endif

#if !CONFIG_AC3_AT_DECODER
AVCodec ff_ac3_at_decoder;
#endif

#if !CONFIG_ADPCM_IMA_QT_AT_DECODER
AVCodec ff_adpcm_ima_qt_at_decoder;
#endif

#if !CONFIG_ALAC_AT_DECODER
AVCodec ff_alac_at_decoder;
#endif

#if !CONFIG_AMR_NB_AT_DECODER
AVCodec ff_amr_nb_at_decoder;
#endif

#if !CONFIG_EAC3_AT_DECODER
AVCodec ff_eac3_at_decoder;
#endif

#if !CONFIG_GSM_MS_AT_DECODER
AVCodec ff_gsm_ms_at_decoder;
#endif

#if !CONFIG_ILBC_AT_DECODER
AVCodec ff_ilbc_at_decoder;
#endif

#if !CONFIG_MP1_AT_DECODER
AVCodec ff_mp1_at_decoder;
#endif

#if !CONFIG_MP2_AT_DECODER
AVCodec ff_mp2_at_decoder;
#endif

#if !CONFIG_MP3_AT_DECODER
AVCodec ff_mp3_at_decoder;
#endif

#if !CONFIG_PCM_MULAW_AT_DECODER
AVCodec ff_pcm_mulaw_at_decoder;
#endif

#if !CONFIG_PCM_ALAW_AT_DECODER
AVCodec ff_pcm_alaw_at_decoder;
#endif

#if !CONFIG_QDMC_AT_DECODER
AVCodec ff_qdmc_at_decoder;
#endif

#if !CONFIG_QDM2_AT_DECODER
AVCodec ff_qdm2_at_decoder;
#endif

#if !CONFIG_AAC_AT_ENCODER
AVCodec ff_aac_at_encoder;
#endif

#if !CONFIG_ALAC_AT_ENCODER
AVCodec ff_alac_at_encoder;
#endif

#if !CONFIG_ILBC_AT_ENCODER
AVCodec ff_ilbc_at_encoder;
#endif

#if !CONFIG_PCM_ALAW_AT_ENCODER
AVCodec ff_pcm_alaw_at_encoder;
#endif

#if !CONFIG_PCM_MULAW_AT_ENCODER
AVCodec ff_pcm_mulaw_at_encoder;
#endif

#if !CONFIG_H264_VAAPI_ENCODER
AVCodec ff_h264_vaapi_encoder;
#endif

#if !CONFIG_H264_VIDEOTOOLBOX_ENCODER
AVCodec ff_h264_videotoolbox_encoder;
#endif

#if !CONFIG_H264_OMX_ENCODER
AVCodec ff_h264_omx_encoder;
#endif

#if !CONFIG_HEVC_VAAPI_ENCODER
AVCodec ff_hevc_vaapi_encoder;
#endif

#if !CONFIG_MJPEG_VAAPI_ENCODER
AVCodec ff_mjpeg_vaapi_encoder;
#endif