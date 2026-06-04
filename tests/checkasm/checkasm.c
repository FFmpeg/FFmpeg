/*
 * Assembly testing and benchmarking tool
 * Copyright (c) 2025 Niklas Haas
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
 *
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "config_components.h"

#include <checkasm/checkasm.h>

#include "checkasm.h"

/* List of tests to invoke */
static const CheckasmTest tests[] = {
    /* NOTE: When adding a new test to this list here, it also needs to be
     * added in tests/fate/checkasm.mak, otherwise it doesn't get executed
     * as part of "make fate" or "make fate-checkasm". */
#if CONFIG_AVCODEC
    #if CONFIG_AAC_DECODER
        { "aacpsdsp", checkasm_check_aacpsdsp },
        { "sbrdsp",   checkasm_check_sbrdsp },
    #endif
    #if CONFIG_AAC_ENCODER
        { "aacencdsp", checkasm_check_aacencdsp },
    #endif
    #if CONFIG_AC3DSP
        { "ac3dsp", checkasm_check_ac3dsp },
    #endif
    #if CONFIG_ALAC_DECODER
        { "alacdsp", checkasm_check_alacdsp },
    #endif
    #if CONFIG_APV_DECODER
        { "apv_dsp", checkasm_check_apv_dsp },
    #endif
    #if CONFIG_AUDIODSP
        { "audiodsp", checkasm_check_audiodsp },
    #endif
    #if CONFIG_BLOCKDSP
        { "blockdsp", checkasm_check_blockdsp },
    #endif
    #if CONFIG_BSWAPDSP
        { "bswapdsp", checkasm_check_bswapdsp },
    #endif
    #if CONFIG_CAVS_DECODER
        { "cavsdsp", checkasm_check_cavsdsp },
    #endif
    #if CONFIG_DCA_DECODER
        { "dcadsp", checkasm_check_dcadsp },
        { "synth_filter", checkasm_check_synth_filter },
    #endif
    #if CONFIG_DIRAC_DECODER
        { "diracdsp", checkasm_check_diracdsp },
    #endif
    #if CONFIG_EXR_DECODER
        { "exrdsp", checkasm_check_exrdsp },
    #endif
    #if CONFIG_FDCTDSP
        { "fdctdsp", checkasm_check_fdctdsp },
    #endif
    #if CONFIG_FLAC_DECODER
        { "flacdsp", checkasm_check_flacdsp },
    #endif
    #if CONFIG_FMTCONVERT
        { "fmtconvert", checkasm_check_fmtconvert },
    #endif
    #if CONFIG_G722DSP
        { "g722dsp", checkasm_check_g722dsp },
    #endif
    #if CONFIG_H263DSP
        { "h263dsp", checkasm_check_h263dsp },
    #endif
    #if CONFIG_H264CHROMA
        { "h264chroma", checkasm_check_h264chroma },
    #endif
    #if CONFIG_H264DSP
        { "h264dsp", checkasm_check_h264dsp },
    #endif
    #if CONFIG_H264PRED
        { "h264pred", checkasm_check_h264pred },
    #endif
    #if CONFIG_H264QPEL
        { "h264qpel", checkasm_check_h264qpel },
    #endif
    #if CONFIG_HEVC_DECODER
        { "hevc_add_res", checkasm_check_hevc_add_res },
        { "hevc_deblock", checkasm_check_hevc_deblock },
        { "hevc_dequant", checkasm_check_hevc_dequant },
        { "hevc_idct", checkasm_check_hevc_idct },
        { "hevc_pel", checkasm_check_hevc_pel },
        { "hevc_pred", checkasm_check_hevc_pred },
        { "hevc_sao", checkasm_check_hevc_sao },
    #endif
    #if CONFIG_HPELDSP
        { "hpeldsp", checkasm_check_hpeldsp },
    #endif
    #if CONFIG_HUFFYUVDSP
        { "huffyuvdsp", checkasm_check_huffyuvdsp },
    #endif
    #if CONFIG_HUFFYUVENCDSP
        { "huffyuvencdsp", checkasm_check_huffyuvencdsp },
    #endif
    #if CONFIG_IDCTDSP
        { "idctdsp", checkasm_check_idctdsp },
    #endif
    #if CONFIG_JPEG2000_DECODER
        { "jpeg2000dsp", checkasm_check_jpeg2000dsp },
    #endif
    #if CONFIG_LLAUDDSP
        { "llauddsp", checkasm_check_llauddsp },
    #endif
    #if CONFIG_HUFFYUVDSP
        { "llviddsp", checkasm_check_llviddsp },
    #endif
    #if CONFIG_LLVIDENCDSP
        { "llvidencdsp", checkasm_check_llvidencdsp },
    #endif
    #if CONFIG_LPC
        { "lpc", checkasm_check_lpc },
    #endif
    #if CONFIG_ME_CMP
        { "motion", checkasm_check_motion },
    #endif
    #if CONFIG_MPEG4_DECODER
        { "mpeg4videodsp", checkasm_check_mpeg4videodsp },
    #endif
    #if CONFIG_MPEGVIDEO
        { "mpegvideo_unquantize", checkasm_check_mpegvideo_unquantize },
    #endif
    #if CONFIG_MPEGVIDEOENCDSP
        { "mpegvideoencdsp", checkasm_check_mpegvideoencdsp },
    #endif
    #if CONFIG_OPUS_DECODER
        { "opusdsp", checkasm_check_opusdsp },
    #endif
    #if CONFIG_PIXBLOCKDSP
        { "pixblockdsp", checkasm_check_pixblockdsp },
    #endif
    #if CONFIG_PNG_DECODER
        { "png", checkasm_check_png },
    #endif
    #if CONFIG_QPELDSP
        { "qpeldsp", checkasm_check_qpeldsp },
    #endif
    #if CONFIG_RV34DSP
        { "rv34dsp", checkasm_check_rv34dsp },
    #endif
    #if CONFIG_RV40_DECODER
        { "rv40dsp", checkasm_check_rv40dsp },
    #endif
    #if CONFIG_SBC_ENCODER
        { "sbcdsp", checkasm_check_sbcdsp },
    #endif
    #if CONFIG_SNOW_DECODER
        { "snowdsp", checkasm_check_snowdsp },
    #endif
    #if CONFIG_SVQ1_ENCODER
        { "svq1enc", checkasm_check_svq1enc },
    #endif
    #if CONFIG_TAK_DECODER
        { "takdsp", checkasm_check_takdsp },
    #endif
    #if CONFIG_UTVIDEO_DECODER
        { "utvideodsp", checkasm_check_utvideodsp },
    #endif
    #if CONFIG_V210_DECODER
        { "v210dec", checkasm_check_v210dec },
    #endif
    #if CONFIG_V210_ENCODER
        { "v210enc", checkasm_check_v210enc },
    #endif
    #if CONFIG_VC1DSP
        { "vc1dsp", checkasm_check_vc1dsp },
    #endif
    #if CONFIG_VP3DSP
        { "vp3dsp", checkasm_check_vp3dsp },
    #endif
    #if CONFIG_VP6_DECODER
        { "vp6dsp", checkasm_check_vp6dsp },
    #endif
    #if CONFIG_VP8DSP
        { "vp8dsp", checkasm_check_vp8dsp },
    #endif
    #if CONFIG_VP9_DECODER
        { "vp9dsp", checkasm_check_vp9dsp }, // all of the below
        { "vp9_ipred", checkasm_check_vp9_ipred },
        { "vp9_itxfm", checkasm_check_vp9_itxfm },
        { "vp9_loopfilter", checkasm_check_vp9_loopfilter },
        { "vp9_mc", checkasm_check_vp9_mc },
    #endif
    #if CONFIG_VIDEODSP
        { "videodsp", checkasm_check_videodsp },
    #endif
    #if CONFIG_VORBIS_DECODER
        { "vorbisdsp", checkasm_check_vorbisdsp },
    #endif
    #if CONFIG_VVC_DECODER
        { "vvc_alf", checkasm_check_vvc_alf },
        { "vvc_mc",  checkasm_check_vvc_mc  },
        { "vvc_sao", checkasm_check_vvc_sao },
    #endif
#endif
#if CONFIG_AVFILTER
    #if CONFIG_SCENE_SAD
        { "scene_sad", checkasm_check_scene_sad },
    #endif
    #if CONFIG_AFIR_FILTER
        { "af_afir", checkasm_check_afir },
    #endif
    #if CONFIG_BLACKDETECT_FILTER
        { "vf_blackdetect", checkasm_check_blackdetect },
    #endif
    #if CONFIG_BLEND_FILTER
        { "vf_blend", checkasm_check_blend },
    #endif
    #if CONFIG_BWDIF_FILTER
        { "vf_bwdif", checkasm_check_vf_bwdif },
    #endif
    #if CONFIG_COLORDETECT_FILTER
        { "vf_colordetect", checkasm_check_colordetect },
    #endif
    #if CONFIG_COLORSPACE_FILTER
        { "vf_colorspace", checkasm_check_colorspace },
    #endif
    #if CONFIG_EQ_FILTER
        { "vf_eq", checkasm_check_vf_eq },
    #endif
    #if CONFIG_FSPP_FILTER
        { "vf_fspp", checkasm_check_vf_fspp },
    #endif
    #if CONFIG_GBLUR_FILTER
        { "vf_gblur", checkasm_check_vf_gblur },
    #endif
    #if CONFIG_HFLIP_FILTER
        { "vf_hflip", checkasm_check_vf_hflip },
    #endif
    #if CONFIG_IDET_FILTER
        { "vf_idet", checkasm_check_idet },
    #endif
    #if CONFIG_NLMEANS_FILTER
        { "vf_nlmeans", checkasm_check_nlmeans },
    #endif
    #if CONFIG_PP7_FILTER
        { "vf_pp7", checkasm_check_vf_pp7 },
    #endif
    #if CONFIG_THRESHOLD_FILTER
        { "vf_threshold", checkasm_check_vf_threshold },
    #endif
    #if CONFIG_SOBEL_FILTER
        { "vf_sobel", checkasm_check_vf_sobel },
    #endif
#endif
#if CONFIG_SWSCALE
    { "sw_gbrp", checkasm_check_sw_gbrp },
    { "sw_range_convert", checkasm_check_sw_range_convert },
    { "sw_rgb", checkasm_check_sw_rgb },
    { "sw_scale", checkasm_check_sw_scale },
    { "sw_xyz2rgb", checkasm_check_sw_xyz2rgb },
    { "sw_yuv2rgb", checkasm_check_sw_yuv2rgb },
    { "sw_yuv2yuv", checkasm_check_sw_yuv2yuv },
    { "sw_ops", checkasm_check_sw_ops },
#endif
#if CONFIG_AVUTIL
        { "aes",       checkasm_check_aes },
        { "crc",       checkasm_check_crc },
        { "fixed_dsp", checkasm_check_fixed_dsp },
        { "float_dsp", checkasm_check_float_dsp },
        { "lls",       checkasm_check_lls },
#if CONFIG_PIXELUTILS
        { "pixelutils",checkasm_check_pixelutils },
#endif
        { "av_tx",     checkasm_check_av_tx },
#endif
    { NULL }
    /* NOTE: When adding a new test to this list here, it also needs to be
     * added in tests/fate/checkasm.mak, otherwise it doesn't get executed
     * as part of "make fate" or "make fate-checkasm". */
};

/* List of cpu flags to check */
static const CheckasmCpuInfo cpuflags[] = {
#if   ARCH_AARCH64
    { "ARMV8",    "armv8",    AV_CPU_FLAG_ARMV8 },
    { "NEON",     "neon",     AV_CPU_FLAG_NEON },
    { "DOTPROD",  "dotprod",  AV_CPU_FLAG_DOTPROD },
    { "I8MM",     "i8mm",     AV_CPU_FLAG_I8MM },
    { "SVE",      "sve",      AV_CPU_FLAG_SVE },
    { "SVE2",     "sve2",     AV_CPU_FLAG_SVE2 },
    { "SME",      "sme",      AV_CPU_FLAG_SME },
    { "SME-I16I64", "sme_i16i64", AV_CPU_FLAG_SME_I16I64 },
    { "CRC",      "crc",      AV_CPU_FLAG_ARM_CRC },
    { "SME2",     "sme2",      AV_CPU_FLAG_SME2 },
    { "PMULL",    "pmull_eor3", AV_CPU_FLAG_PMULL|AV_CPU_FLAG_EOR3 },
#elif ARCH_ARM
    { "ARMV5TE",  "armv5te",  AV_CPU_FLAG_ARMV5TE },
    { "ARMV6",    "armv6",    AV_CPU_FLAG_ARMV6 },
    { "ARMV6T2",  "armv6t2",  AV_CPU_FLAG_ARMV6T2 },
    { "VFP",      "vfp",      AV_CPU_FLAG_VFP },
    { "VFP_VM",   "vfp_vm",   AV_CPU_FLAG_VFP_VM },
    { "VFPV3",    "vfp3",     AV_CPU_FLAG_VFPV3 },
    { "NEON",     "neon",     AV_CPU_FLAG_NEON },
#elif ARCH_PPC
    { "ALTIVEC",  "altivec",  AV_CPU_FLAG_ALTIVEC },
    { "VSX",      "vsx",      AV_CPU_FLAG_VSX },
    { "POWER8",   "power8",   AV_CPU_FLAG_POWER8 },
#elif ARCH_RISCV
    { "RVI",      "rvi",      AV_CPU_FLAG_RVI },
    { "misaligned", "misaligned", AV_CPU_FLAG_RV_MISALIGNED },
    { "RV_zbb",   "rvb_b",    AV_CPU_FLAG_RVB_BASIC },
    { "RVB",      "rvb",      AV_CPU_FLAG_RVB },
    { "RV_zve32x","rvv_i32",  AV_CPU_FLAG_RVV_I32 },
    { "RV_zve32f","rvv_f32",  AV_CPU_FLAG_RVV_F32 },
    { "RV_zve64x","rvv_i64",  AV_CPU_FLAG_RVV_I64 },
    { "RV_zve64d","rvv_f64",  AV_CPU_FLAG_RVV_F64 },
    { "RV_zvbb",  "rv_zvbb",  AV_CPU_FLAG_RV_ZVBB },
#elif ARCH_MIPS
    { "MMI",      "mmi",      AV_CPU_FLAG_MMI },
    { "MSA",      "msa",      AV_CPU_FLAG_MSA },
#elif ARCH_X86
    { "MMX",        "mmx",       AV_CPU_FLAG_MMX|AV_CPU_FLAG_CMOV },
    { "MMXEXT",     "mmxext",    AV_CPU_FLAG_MMXEXT },
    { "SSE",        "sse",       AV_CPU_FLAG_SSE },
    { "SSE2",       "sse2",      AV_CPU_FLAG_SSE2|AV_CPU_FLAG_SSE2SLOW },
    { "SSE3",       "sse3",      AV_CPU_FLAG_SSE3|AV_CPU_FLAG_SSE3SLOW },
    { "SSSE3",      "ssse3",     AV_CPU_FLAG_SSSE3|AV_CPU_FLAG_ATOM },
    { "SSE4.1",     "sse4",      AV_CPU_FLAG_SSE4 },
    { "SSE4.2",     "sse42",     AV_CPU_FLAG_SSE42 },
    { "AES-NI",     "aesni",     AV_CPU_FLAG_AESNI },
    { "CLMUL",      "clmul",     AV_CPU_FLAG_CLMUL },
    { "AVX",        "avx",       AV_CPU_FLAG_AVX },
    { "XOP",        "xop",       AV_CPU_FLAG_XOP },
    { "FMA3",       "fma3",      AV_CPU_FLAG_FMA3 },
    { "FMA4",       "fma4",      AV_CPU_FLAG_FMA4 },
    { "AVX2",       "avx2",      AV_CPU_FLAG_AVX2 },
    { "AVX-512",    "avx512",    AV_CPU_FLAG_AVX512 },
    { "AVX-512ICL", "avx512icl", AV_CPU_FLAG_AVX512ICL },
#elif ARCH_LOONGARCH
    { "LSX",      "lsx",      AV_CPU_FLAG_LSX },
    { "LASX",     "lasx",     AV_CPU_FLAG_LASX },
#elif ARCH_WASM
    { "SIMD128",    "simd128",  AV_CPU_FLAG_SIMD128 },
#endif
    { NULL }
};

static void set_cpu_flags(uint64_t flags)
{
    av_force_cpu_flags((int) flags);
}

int main(int argc, const char *argv[])
{
    CheckasmConfig cfg = {
        .cpu_flags      = cpuflags,
        .tests          = tests,
        .set_cpu_flags  = set_cpu_flags,
        .cpu            = av_get_cpu_flags(),
    };

    return checkasm_main(&cfg, argc, argv);
}
