/*
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * Options definition for AVCodecContext.
 */

#include "avcodec.h"
#include "opt.h"
#include <float.h>              /* FLT_MIN, FLT_MAX */

static const char* context_to_name(void* ptr) {
    AVCodecContext *avc= ptr;

    if(avc && avc->codec && avc->codec->name)
        return avc->codec->name;
    else
        return "NULL";
}

#define OFFSET(x) offsetof(AVCodecContext,x)
#define DEFAULT 0 //should be NAN but it does not work as it is not a constant in glibc as required by ANSI/ISO C
//these names are too long to be readable
#define V AV_OPT_FLAG_VIDEO_PARAM
#define A AV_OPT_FLAG_AUDIO_PARAM
#define S AV_OPT_FLAG_SUBTITLE_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
#define D AV_OPT_FLAG_DECODING_PARAM

#define AV_CODEC_DEFAULT_BITRATE 200*1000

static const AVOption options[]={
{"b", "set bitrate (in bits/s)", OFFSET(bit_rate), FF_OPT_TYPE_INT, AV_CODEC_DEFAULT_BITRATE, INT_MIN, INT_MAX, V|E},
{"ab", "set bitrate (in bits/s)", OFFSET(bit_rate), FF_OPT_TYPE_INT, 64*1000, INT_MIN, INT_MAX, A|E},
{"bt", "set video bitrate tolerance (in bits/s)", OFFSET(bit_rate_tolerance), FF_OPT_TYPE_INT, AV_CODEC_DEFAULT_BITRATE*20, 1, INT_MAX, V|E},
{"flags", NULL, OFFSET(flags), FF_OPT_TYPE_FLAGS, DEFAULT, 0, UINT_MAX, V|A|E|D, "flags"},
{"mv4", "use four motion vector by macroblock (mpeg4)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_4MV, INT_MIN, INT_MAX, V|E, "flags"},
{"obmc", "use overlapped block motion compensation (h263+)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_OBMC, INT_MIN, INT_MAX, V|E, "flags"},
{"qpel", "use 1/4 pel motion compensation", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_QPEL, INT_MIN, INT_MAX, V|E, "flags"},
{"loop", "use loop filter", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_LOOP_FILTER, INT_MIN, INT_MAX, V|E, "flags"},
{"qscale", "use fixed qscale", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_QSCALE, INT_MIN, INT_MAX, 0, "flags"},
{"gmc", "use gmc", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_GMC, INT_MIN, INT_MAX, V|E, "flags"},
{"mv0", "always try a mb with mv=<0,0>", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_MV0, INT_MIN, INT_MAX, V|E, "flags"},
{"part", "use data partitioning", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_PART, INT_MIN, INT_MAX, V|E, "flags"},
{"input_preserved", NULL, 0, FF_OPT_TYPE_CONST, CODEC_FLAG_INPUT_PRESERVED, INT_MIN, INT_MAX, 0, "flags"},
{"pass1", "use internal 2pass ratecontrol in first  pass mode", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_PASS1, INT_MIN, INT_MAX, 0, "flags"},
{"pass2", "use internal 2pass ratecontrol in second pass mode", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_PASS2, INT_MIN, INT_MAX, 0, "flags"},
{"extern_huff", "use external huffman table (for mjpeg)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_EXTERN_HUFF, INT_MIN, INT_MAX, 0, "flags"},
{"gray", "only decode/encode grayscale", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_GRAY, INT_MIN, INT_MAX, V|E|D, "flags"},
{"emu_edge", "don't draw edges", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_EMU_EDGE, INT_MIN, INT_MAX, 0, "flags"},
{"psnr", "error[?] variables will be set during encoding", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_PSNR, INT_MIN, INT_MAX, V|E, "flags"},
{"truncated", NULL, 0, FF_OPT_TYPE_CONST, CODEC_FLAG_TRUNCATED, INT_MIN, INT_MAX, 0, "flags"},
{"naq", "normalize adaptive quantization", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_NORMALIZE_AQP, INT_MIN, INT_MAX, V|E, "flags"},
{"ildct", "use interlaced dct", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_INTERLACED_DCT, INT_MIN, INT_MAX, V|E, "flags"},
{"low_delay", "force low delay", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_LOW_DELAY, INT_MIN, INT_MAX, V|D|E, "flags"},
{"alt", "enable alternate scantable (mpeg2/mpeg4)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_ALT_SCAN, INT_MIN, INT_MAX, V|E, "flags"},
{"global_header", "place global headers in extradata instead of every keyframe", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_GLOBAL_HEADER, INT_MIN, INT_MAX, V|A|E, "flags"},
{"bitexact", "use only bitexact stuff (except (i)dct)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_BITEXACT, INT_MIN, INT_MAX, A|V|S|D|E, "flags"},
{"aic", "h263 advanced intra coding / mpeg4 ac prediction", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_AC_PRED, INT_MIN, INT_MAX, V|E, "flags"},
{"umv", "use unlimited motion vectors", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_H263P_UMV, INT_MIN, INT_MAX, V|E, "flags"},
{"cbp", "use rate distortion optimization for cbp", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_CBP_RD, INT_MIN, INT_MAX, V|E, "flags"},
{"qprd", "use rate distortion optimization for qp selection", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_QP_RD, INT_MIN, INT_MAX, V|E, "flags"},
{"aiv", "h263 alternative inter vlc", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_H263P_AIV, INT_MIN, INT_MAX, V|E, "flags"},
{"slice", NULL, 0, FF_OPT_TYPE_CONST, CODEC_FLAG_H263P_SLICE_STRUCT, INT_MIN, INT_MAX, V|E, "flags"},
{"ilme", "interlaced motion estimation", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_INTERLACED_ME, INT_MIN, INT_MAX, V|E, "flags"},
{"scan_offset", "will reserve space for svcd scan offset user data", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_SVCD_SCAN_OFFSET, INT_MIN, INT_MAX, V|E, "flags"},
{"cgop", "closed gop", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_CLOSED_GOP, INT_MIN, INT_MAX, V|E, "flags"},
{"fast", "allow non spec compliant speedup tricks", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_FAST, INT_MIN, INT_MAX, V|E, "flags2"},
{"sgop", "strictly enforce gop size", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_STRICT_GOP, INT_MIN, INT_MAX, V|E, "flags2"},
{"noout", "skip bitstream encoding", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_NO_OUTPUT, INT_MIN, INT_MAX, V|E, "flags2"},
{"local_header", "place global headers at every keyframe instead of in extradata", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_LOCAL_HEADER, INT_MIN, INT_MAX, V|E, "flags2"},
{"sub_id", NULL, OFFSET(sub_id), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"me_method", "set motion estimation method", OFFSET(me_method), FF_OPT_TYPE_INT, ME_EPZS, INT_MIN, INT_MAX, V|E, "me_method"},
{"zero", "zero motion estimation (fastest)", 0, FF_OPT_TYPE_CONST, ME_ZERO, INT_MIN, INT_MAX, V|E, "me_method" },
{"full", "full motion estimation (slowest)", 0, FF_OPT_TYPE_CONST, ME_FULL, INT_MIN, INT_MAX, V|E, "me_method" },
{"epzs", "EPZS motion estimation (default)", 0, FF_OPT_TYPE_CONST, ME_EPZS, INT_MIN, INT_MAX, V|E, "me_method" },
{"esa", "esa motion estimation (alias for full)", 0, FF_OPT_TYPE_CONST, ME_FULL, INT_MIN, INT_MAX, V|E, "me_method" },
{"tesa", "tesa motion estimation", 0, FF_OPT_TYPE_CONST, ME_TESA, INT_MIN, INT_MAX, V|E, "me_method" },
{"dia", "dia motion estimation (alias for epzs)", 0, FF_OPT_TYPE_CONST, ME_EPZS, INT_MIN, INT_MAX, V|E, "me_method" },
{"log", "log motion estimation", 0, FF_OPT_TYPE_CONST, ME_LOG, INT_MIN, INT_MAX, V|E, "me_method" },
{"phods", "phods motion estimation", 0, FF_OPT_TYPE_CONST, ME_PHODS, INT_MIN, INT_MAX, V|E, "me_method" },
{"x1", "X1 motion estimation", 0, FF_OPT_TYPE_CONST, ME_X1, INT_MIN, INT_MAX, V|E, "me_method" },
{"hex", "hex motion estimation", 0, FF_OPT_TYPE_CONST, ME_HEX, INT_MIN, INT_MAX, V|E, "me_method" },
{"umh", "umh motion estimation", 0, FF_OPT_TYPE_CONST, ME_UMH, INT_MIN, INT_MAX, V|E, "me_method" },
{"iter", "iter motion estimation", 0, FF_OPT_TYPE_CONST, ME_ITER, INT_MIN, INT_MAX, V|E, "me_method" },
{"extradata_size", NULL, OFFSET(extradata_size), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"time_base", NULL, OFFSET(time_base), FF_OPT_TYPE_RATIONAL, DEFAULT, INT_MIN, INT_MAX},
{"g", "set the group of picture size", OFFSET(gop_size), FF_OPT_TYPE_INT, 12, INT_MIN, INT_MAX, V|E},
{"rate_emu", "frame rate emulation", OFFSET(rate_emu), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"ar", "set audio sampling rate (in Hz)", OFFSET(sample_rate), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"ac", "set number of audio channels", OFFSET(channels), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"cutoff", "set cutoff bandwidth", OFFSET(cutoff), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, A|E},
{"frame_size", NULL, OFFSET(frame_size), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, A|E},
{"frame_number", NULL, OFFSET(frame_number), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
#if LIBAVCODEC_VERSION_MAJOR < 53
{"real_pict_num", NULL, OFFSET(real_pict_num), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
#endif
{"delay", NULL, OFFSET(delay), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"qcomp", "video quantizer scale compression (VBR)", OFFSET(qcompress), FF_OPT_TYPE_FLOAT, 0.5, -FLT_MAX, FLT_MAX, V|E},
{"qblur", "video quantizer scale blur (VBR)", OFFSET(qblur), FF_OPT_TYPE_FLOAT, 0.5, 0, FLT_MAX, V|E},
{"qmin", "min video quantizer scale (VBR)", OFFSET(qmin), FF_OPT_TYPE_INT, 2, 1, 51, V|E},
{"qmax", "max video quantizer scale (VBR)", OFFSET(qmax), FF_OPT_TYPE_INT, 31, 1, 51, V|E},
{"qdiff", "max difference between the quantizer scale (VBR)", OFFSET(max_qdiff), FF_OPT_TYPE_INT, 3, INT_MIN, INT_MAX, V|E},
{"bf", "use 'frames' B frames", OFFSET(max_b_frames), FF_OPT_TYPE_INT, DEFAULT, 0, FF_MAX_B_FRAMES, V|E},
{"b_qfactor", "qp factor between p and b frames", OFFSET(b_quant_factor), FF_OPT_TYPE_FLOAT, 1.25, -FLT_MAX, FLT_MAX, V|E},
{"rc_strategy", "ratecontrol method", OFFSET(rc_strategy), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"b_strategy", "strategy to choose between I/P/B-frames", OFFSET(b_frame_strategy), FF_OPT_TYPE_INT, 0, INT_MIN, INT_MAX, V|E},
{"wpredp", "weighted prediction analysis method", OFFSET(weighted_p_pred), FF_OPT_TYPE_INT, 0, INT_MIN, INT_MAX, V|E},
{"hurry_up", NULL, OFFSET(hurry_up), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|D},
{"ps", "rtp payload size in bytes", OFFSET(rtp_payload_size), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"mv_bits", NULL, OFFSET(mv_bits), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"header_bits", NULL, OFFSET(header_bits), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"i_tex_bits", NULL, OFFSET(i_tex_bits), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"p_tex_bits", NULL, OFFSET(p_tex_bits), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"i_count", NULL, OFFSET(i_count), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"p_count", NULL, OFFSET(p_count), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"skip_count", NULL, OFFSET(skip_count), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"misc_bits", NULL, OFFSET(misc_bits), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"frame_bits", NULL, OFFSET(frame_bits), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"codec_tag", NULL, OFFSET(codec_tag), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"bug", "workaround not auto detected encoder bugs", OFFSET(workaround_bugs), FF_OPT_TYPE_FLAGS, FF_BUG_AUTODETECT, INT_MIN, INT_MAX, V|D, "bug"},
{"autodetect", NULL, 0, FF_OPT_TYPE_CONST, FF_BUG_AUTODETECT, INT_MIN, INT_MAX, V|D, "bug"},
{"old_msmpeg4", "some old lavc generated msmpeg4v3 files (no autodetection)", 0, FF_OPT_TYPE_CONST, FF_BUG_OLD_MSMPEG4, INT_MIN, INT_MAX, V|D, "bug"},
{"xvid_ilace", "Xvid interlacing bug (autodetected if fourcc==XVIX)", 0, FF_OPT_TYPE_CONST, FF_BUG_XVID_ILACE, INT_MIN, INT_MAX, V|D, "bug"},
{"ump4", "(autodetected if fourcc==UMP4)", 0, FF_OPT_TYPE_CONST, FF_BUG_UMP4, INT_MIN, INT_MAX, V|D, "bug"},
{"no_padding", "padding bug (autodetected)", 0, FF_OPT_TYPE_CONST, FF_BUG_NO_PADDING, INT_MIN, INT_MAX, V|D, "bug"},
{"amv", NULL, 0, FF_OPT_TYPE_CONST, FF_BUG_AMV, INT_MIN, INT_MAX, V|D, "bug"},
{"ac_vlc", "illegal vlc bug (autodetected per fourcc)", 0, FF_OPT_TYPE_CONST, FF_BUG_AC_VLC, INT_MIN, INT_MAX, V|D, "bug"},
{"qpel_chroma", NULL, 0, FF_OPT_TYPE_CONST, FF_BUG_QPEL_CHROMA, INT_MIN, INT_MAX, V|D, "bug"},
{"std_qpel", "old standard qpel (autodetected per fourcc/version)", 0, FF_OPT_TYPE_CONST, FF_BUG_STD_QPEL, INT_MIN, INT_MAX, V|D, "bug"},
{"qpel_chroma2", NULL, 0, FF_OPT_TYPE_CONST, FF_BUG_QPEL_CHROMA2, INT_MIN, INT_MAX, V|D, "bug"},
{"direct_blocksize", "direct-qpel-blocksize bug (autodetected per fourcc/version)", 0, FF_OPT_TYPE_CONST, FF_BUG_DIRECT_BLOCKSIZE, INT_MIN, INT_MAX, V|D, "bug"},
{"edge", "edge padding bug (autodetected per fourcc/version)", 0, FF_OPT_TYPE_CONST, FF_BUG_EDGE, INT_MIN, INT_MAX, V|D, "bug"},
{"hpel_chroma", NULL, 0, FF_OPT_TYPE_CONST, FF_BUG_HPEL_CHROMA, INT_MIN, INT_MAX, V|D, "bug"},
{"dc_clip", NULL, 0, FF_OPT_TYPE_CONST, FF_BUG_DC_CLIP, INT_MIN, INT_MAX, V|D, "bug"},
{"ms", "workaround various bugs in microsofts broken decoders", 0, FF_OPT_TYPE_CONST, FF_BUG_MS, INT_MIN, INT_MAX, V|D, "bug"},
{"trunc", "trancated frames", 0, FF_OPT_TYPE_CONST,FF_BUG_TRUNCATED, INT_MIN, INT_MAX, V|D, "bug"},
{"lelim", "single coefficient elimination threshold for luminance (negative values also consider dc coefficient)", OFFSET(luma_elim_threshold), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"celim", "single coefficient elimination threshold for chrominance (negative values also consider dc coefficient)", OFFSET(chroma_elim_threshold), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"strict", "how strictly to follow the standards", OFFSET(strict_std_compliance), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, A|V|D|E, "strict"},
{"very", "strictly conform to a older more strict version of the spec or reference software", 0, FF_OPT_TYPE_CONST, FF_COMPLIANCE_VERY_STRICT, INT_MIN, INT_MAX, V|D|E, "strict"},
{"strict", "strictly conform to all the things in the spec no matter what consequences", 0, FF_OPT_TYPE_CONST, FF_COMPLIANCE_STRICT, INT_MIN, INT_MAX, V|D|E, "strict"},
{"normal", NULL, 0, FF_OPT_TYPE_CONST, FF_COMPLIANCE_NORMAL, INT_MIN, INT_MAX, V|D|E, "strict"},
{"inofficial", "allow unofficial extensions", 0, FF_OPT_TYPE_CONST, FF_COMPLIANCE_INOFFICIAL, INT_MIN, INT_MAX, V|D|E, "strict"},
{"experimental", "allow non standardized experimental things", 0, FF_OPT_TYPE_CONST, FF_COMPLIANCE_EXPERIMENTAL, INT_MIN, INT_MAX, V|D|E, "strict"},
{"b_qoffset", "qp offset between P and B frames", OFFSET(b_quant_offset), FF_OPT_TYPE_FLOAT, 1.25, -FLT_MAX, FLT_MAX, V|E},
{"er", "set error detection aggressivity", OFFSET(error_recognition), FF_OPT_TYPE_INT, FF_ER_CAREFUL, INT_MIN, INT_MAX, A|V|D, "er"},
{"careful", NULL, 0, FF_OPT_TYPE_CONST, FF_ER_CAREFUL, INT_MIN, INT_MAX, V|D, "er"},
{"compliant", NULL, 0, FF_OPT_TYPE_CONST, FF_ER_COMPLIANT, INT_MIN, INT_MAX, V|D, "er"},
{"aggressive", NULL, 0, FF_OPT_TYPE_CONST, FF_ER_AGGRESSIVE, INT_MIN, INT_MAX, V|D, "er"},
{"very_aggressive", NULL, 0, FF_OPT_TYPE_CONST, FF_ER_VERY_AGGRESSIVE, INT_MIN, INT_MAX, V|D, "er"},
{"has_b_frames", NULL, OFFSET(has_b_frames), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"block_align", NULL, OFFSET(block_align), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"parse_only", NULL, OFFSET(parse_only), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"mpeg_quant", "use MPEG quantizers instead of H.263", OFFSET(mpeg_quant), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"stats_out", NULL, OFFSET(stats_out), FF_OPT_TYPE_STRING, DEFAULT, CHAR_MIN, CHAR_MAX},
{"stats_in", NULL, OFFSET(stats_in), FF_OPT_TYPE_STRING, DEFAULT, CHAR_MIN, CHAR_MAX},
{"qsquish", "how to keep quantizer between qmin and qmax (0 = clip, 1 = use differentiable function)", OFFSET(rc_qsquish), FF_OPT_TYPE_FLOAT, DEFAULT, 0, 99, V|E},
{"rc_qmod_amp", "experimental quantizer modulation", OFFSET(rc_qmod_amp), FF_OPT_TYPE_FLOAT, DEFAULT, -FLT_MAX, FLT_MAX, V|E},
{"rc_qmod_freq", "experimental quantizer modulation", OFFSET(rc_qmod_freq), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"rc_override_count", NULL, OFFSET(rc_override_count), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"rc_eq", "set rate control equation", OFFSET(rc_eq), FF_OPT_TYPE_STRING, DEFAULT, CHAR_MIN, CHAR_MAX, V|E},
{"maxrate", "set max video bitrate tolerance (in bits/s)", OFFSET(rc_max_rate), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"minrate", "set min video bitrate tolerance (in bits/s)", OFFSET(rc_min_rate), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"bufsize", "set ratecontrol buffer size (in bits)", OFFSET(rc_buffer_size), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, A|V|E},
{"rc_buf_aggressivity", "currently useless", OFFSET(rc_buffer_aggressivity), FF_OPT_TYPE_FLOAT, 1.0, -FLT_MAX, FLT_MAX, V|E},
{"i_qfactor", "qp factor between P and I frames", OFFSET(i_quant_factor), FF_OPT_TYPE_FLOAT, -0.8, -FLT_MAX, FLT_MAX, V|E},
{"i_qoffset", "qp offset between P and I frames", OFFSET(i_quant_offset), FF_OPT_TYPE_FLOAT, 0.0, -FLT_MAX, FLT_MAX, V|E},
{"rc_init_cplx", "initial complexity for 1-pass encoding", OFFSET(rc_initial_cplx), FF_OPT_TYPE_FLOAT, DEFAULT, -FLT_MAX, FLT_MAX, V|E},
{"dct", "DCT algorithm", OFFSET(dct_algo), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, V|E, "dct"},
{"auto", "autoselect a good one (default)", 0, FF_OPT_TYPE_CONST, FF_DCT_AUTO, INT_MIN, INT_MAX, V|E, "dct"},
{"fastint", "fast integer", 0, FF_OPT_TYPE_CONST, FF_DCT_FASTINT, INT_MIN, INT_MAX, V|E, "dct"},
{"int", "accurate integer", 0, FF_OPT_TYPE_CONST, FF_DCT_INT, INT_MIN, INT_MAX, V|E, "dct"},
{"mmx", NULL, 0, FF_OPT_TYPE_CONST, FF_DCT_MMX, INT_MIN, INT_MAX, V|E, "dct"},
{"mlib", NULL, 0, FF_OPT_TYPE_CONST, FF_DCT_MLIB, INT_MIN, INT_MAX, V|E, "dct"},
{"altivec", NULL, 0, FF_OPT_TYPE_CONST, FF_DCT_ALTIVEC, INT_MIN, INT_MAX, V|E, "dct"},
{"faan", "floating point AAN DCT", 0, FF_OPT_TYPE_CONST, FF_DCT_FAAN, INT_MIN, INT_MAX, V|E, "dct"},
{"lumi_mask", "compresses bright areas stronger than medium ones", OFFSET(lumi_masking), FF_OPT_TYPE_FLOAT, 0, -FLT_MAX, FLT_MAX, V|E},
{"tcplx_mask", "temporal complexity masking", OFFSET(temporal_cplx_masking), FF_OPT_TYPE_FLOAT, 0, -FLT_MAX, FLT_MAX, V|E},
{"scplx_mask", "spatial complexity masking", OFFSET(spatial_cplx_masking), FF_OPT_TYPE_FLOAT, 0, -FLT_MAX, FLT_MAX, V|E},
{"p_mask", "inter masking", OFFSET(p_masking), FF_OPT_TYPE_FLOAT, 0, -FLT_MAX, FLT_MAX, V|E},
{"dark_mask", "compresses dark areas stronger than medium ones", OFFSET(dark_masking), FF_OPT_TYPE_FLOAT, 0, -FLT_MAX, FLT_MAX, V|E},
{"idct", "select IDCT implementation", OFFSET(idct_algo), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, V|E|D, "idct"},
{"auto", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_AUTO, INT_MIN, INT_MAX, V|E|D, "idct"},
{"int", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_INT, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simple", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_SIMPLE, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simplemmx", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_SIMPLEMMX, INT_MIN, INT_MAX, V|E|D, "idct"},
{"libmpeg2mmx", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_LIBMPEG2MMX, INT_MIN, INT_MAX, V|E|D, "idct"},
{"ps2", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_PS2, INT_MIN, INT_MAX, V|E|D, "idct"},
{"mlib", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_MLIB, INT_MIN, INT_MAX, V|E|D, "idct"},
{"arm", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_ARM, INT_MIN, INT_MAX, V|E|D, "idct"},
{"altivec", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_ALTIVEC, INT_MIN, INT_MAX, V|E|D, "idct"},
{"sh4", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_SH4, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simplearm", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_SIMPLEARM, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simplearmv5te", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_SIMPLEARMV5TE, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simplearmv6", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_SIMPLEARMV6, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simpleneon", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_SIMPLENEON, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simplealpha", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_SIMPLEALPHA, INT_MIN, INT_MAX, V|E|D, "idct"},
{"h264", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_H264, INT_MIN, INT_MAX, V|E|D, "idct"},
{"vp3", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_VP3, INT_MIN, INT_MAX, V|E|D, "idct"},
{"ipp", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_IPP, INT_MIN, INT_MAX, V|E|D, "idct"},
{"xvidmmx", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_XVIDMMX, INT_MIN, INT_MAX, V|E|D, "idct"},
{"faani", "floating point AAN IDCT", 0, FF_OPT_TYPE_CONST, FF_IDCT_FAAN, INT_MIN, INT_MAX, V|D|E, "idct"},
{"slice_count", NULL, OFFSET(slice_count), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"ec", "set error concealment strategy", OFFSET(error_concealment), FF_OPT_TYPE_FLAGS, 3, INT_MIN, INT_MAX, V|D, "ec"},
{"guess_mvs", "iterative motion vector (MV) search (slow)", 0, FF_OPT_TYPE_CONST, FF_EC_GUESS_MVS, INT_MIN, INT_MAX, V|D, "ec"},
{"deblock", "use strong deblock filter for damaged MBs", 0, FF_OPT_TYPE_CONST, FF_EC_DEBLOCK, INT_MIN, INT_MAX, V|D, "ec"},
{"bits_per_coded_sample", NULL, OFFSET(bits_per_coded_sample), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"pred", "prediction method", OFFSET(prediction_method), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E, "pred"},
{"left", NULL, 0, FF_OPT_TYPE_CONST, FF_PRED_LEFT, INT_MIN, INT_MAX, V|E, "pred"},
{"plane", NULL, 0, FF_OPT_TYPE_CONST, FF_PRED_PLANE, INT_MIN, INT_MAX, V|E, "pred"},
{"median", NULL, 0, FF_OPT_TYPE_CONST, FF_PRED_MEDIAN, INT_MIN, INT_MAX, V|E, "pred"},
{"aspect", "sample aspect ratio", OFFSET(sample_aspect_ratio), FF_OPT_TYPE_RATIONAL, DEFAULT, 0, 10, V|E},
{"debug", "print specific debug info", OFFSET(debug), FF_OPT_TYPE_FLAGS, DEFAULT, 0, INT_MAX, V|A|S|E|D, "debug"},
{"pict", "picture info", 0, FF_OPT_TYPE_CONST, FF_DEBUG_PICT_INFO, INT_MIN, INT_MAX, V|D, "debug"},
{"rc", "rate control", 0, FF_OPT_TYPE_CONST, FF_DEBUG_RC, INT_MIN, INT_MAX, V|E, "debug"},
{"bitstream", NULL, 0, FF_OPT_TYPE_CONST, FF_DEBUG_BITSTREAM, INT_MIN, INT_MAX, V|D, "debug"},
{"mb_type", "macroblock (MB) type", 0, FF_OPT_TYPE_CONST, FF_DEBUG_MB_TYPE, INT_MIN, INT_MAX, V|D, "debug"},
{"qp", "per-block quantization parameter (QP)", 0, FF_OPT_TYPE_CONST, FF_DEBUG_QP, INT_MIN, INT_MAX, V|D, "debug"},
{"mv", "motion vector", 0, FF_OPT_TYPE_CONST, FF_DEBUG_MV, INT_MIN, INT_MAX, V|D, "debug"},
{"dct_coeff", NULL, 0, FF_OPT_TYPE_CONST, FF_DEBUG_DCT_COEFF, INT_MIN, INT_MAX, V|D, "debug"},
{"skip", NULL, 0, FF_OPT_TYPE_CONST, FF_DEBUG_SKIP, INT_MIN, INT_MAX, V|D, "debug"},
{"startcode", NULL, 0, FF_OPT_TYPE_CONST, FF_DEBUG_STARTCODE, INT_MIN, INT_MAX, V|D, "debug"},
{"pts", NULL, 0, FF_OPT_TYPE_CONST, FF_DEBUG_PTS, INT_MIN, INT_MAX, V|D, "debug"},
{"er", "error recognition", 0, FF_OPT_TYPE_CONST, FF_DEBUG_ER, INT_MIN, INT_MAX, V|D, "debug"},
{"mmco", "memory management control operations (H.264)", 0, FF_OPT_TYPE_CONST, FF_DEBUG_MMCO, INT_MIN, INT_MAX, V|D, "debug"},
{"bugs", NULL, 0, FF_OPT_TYPE_CONST, FF_DEBUG_BUGS, INT_MIN, INT_MAX, V|D, "debug"},
{"vis_qp", "visualize quantization parameter (QP), lower QP are tinted greener", 0, FF_OPT_TYPE_CONST, FF_DEBUG_VIS_QP, INT_MIN, INT_MAX, V|D, "debug"},
{"vis_mb_type", "visualize block types", 0, FF_OPT_TYPE_CONST, FF_DEBUG_VIS_MB_TYPE, INT_MIN, INT_MAX, V|D, "debug"},
{"buffers", "picture buffer allocations", 0, FF_OPT_TYPE_CONST, FF_DEBUG_BUFFERS, INT_MIN, INT_MAX, V|D, "debug"},
{"vismv", "visualize motion vectors (MVs)", OFFSET(debug_mv), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, V|D, "debug_mv"},
{"pf", "forward predicted MVs of P-frames", 0, FF_OPT_TYPE_CONST, FF_DEBUG_VIS_MV_P_FOR, INT_MIN, INT_MAX, V|D, "debug_mv"},
{"bf", "forward predicted MVs of B-frames", 0, FF_OPT_TYPE_CONST, FF_DEBUG_VIS_MV_B_FOR, INT_MIN, INT_MAX, V|D, "debug_mv"},
{"bb", "backward predicted MVs of B-frames", 0, FF_OPT_TYPE_CONST, FF_DEBUG_VIS_MV_B_BACK, INT_MIN, INT_MAX, V|D, "debug_mv"},
{"mb_qmin", "obsolete, use qmin", OFFSET(mb_qmin), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"mb_qmax", "obsolete, use qmax", OFFSET(mb_qmax), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"cmp", "full pel me compare function", OFFSET(me_cmp), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"subcmp", "sub pel me compare function", OFFSET(me_sub_cmp), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"mbcmp", "macroblock compare function", OFFSET(mb_cmp), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"ildctcmp", "interlaced dct compare function", OFFSET(ildct_cmp), FF_OPT_TYPE_INT, FF_CMP_VSAD, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"dia_size", "diamond type & size for motion estimation", OFFSET(dia_size), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"last_pred", "amount of motion predictors from the previous frame", OFFSET(last_predictor_count), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"preme", "pre motion estimation", OFFSET(pre_me), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"precmp", "pre motion estimation compare function", OFFSET(me_pre_cmp), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"sad", "sum of absolute differences, fast (default)", 0, FF_OPT_TYPE_CONST, FF_CMP_SAD, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"sse", "sum of squared errors", 0, FF_OPT_TYPE_CONST, FF_CMP_SSE, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"satd", "sum of absolute Hadamard transformed differences", 0, FF_OPT_TYPE_CONST, FF_CMP_SATD, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"dct", "sum of absolute DCT transformed differences", 0, FF_OPT_TYPE_CONST, FF_CMP_DCT, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"psnr", "sum of squared quantization errors (avoid, low quality)", 0, FF_OPT_TYPE_CONST, FF_CMP_PSNR, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"bit", "number of bits needed for the block", 0, FF_OPT_TYPE_CONST, FF_CMP_BIT, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"rd", "rate distortion optimal, slow", 0, FF_OPT_TYPE_CONST, FF_CMP_RD, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"zero", "0", 0, FF_OPT_TYPE_CONST, FF_CMP_ZERO, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"vsad", "sum of absolute vertical differences", 0, FF_OPT_TYPE_CONST, FF_CMP_VSAD, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"vsse","sum of squared vertical differences", 0, FF_OPT_TYPE_CONST, FF_CMP_VSSE, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"nsse", "noise preserving sum of squared differences", 0, FF_OPT_TYPE_CONST, FF_CMP_NSSE, INT_MIN, INT_MAX, V|E, "cmp_func"},
#if CONFIG_SNOW_ENCODER
{"w53", "5/3 wavelet, only used in snow", 0, FF_OPT_TYPE_CONST, FF_CMP_W53, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"w97", "9/7 wavelet, only used in snow", 0, FF_OPT_TYPE_CONST, FF_CMP_W97, INT_MIN, INT_MAX, V|E, "cmp_func"},
#endif
{"dctmax", NULL, 0, FF_OPT_TYPE_CONST, FF_CMP_DCTMAX, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"chroma", NULL, 0, FF_OPT_TYPE_CONST, FF_CMP_CHROMA, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"pre_dia_size", "diamond type & size for motion estimation pre-pass", OFFSET(pre_dia_size), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"subq", "sub pel motion estimation quality", OFFSET(me_subpel_quality), FF_OPT_TYPE_INT, 8, INT_MIN, INT_MAX, V|E},
{"dtg_active_format", NULL, OFFSET(dtg_active_format), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"me_range", "limit motion vectors range (1023 for DivX player)", OFFSET(me_range), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"ibias", "intra quant bias", OFFSET(intra_quant_bias), FF_OPT_TYPE_INT, FF_DEFAULT_QUANT_BIAS, INT_MIN, INT_MAX, V|E},
{"pbias", "inter quant bias", OFFSET(inter_quant_bias), FF_OPT_TYPE_INT, FF_DEFAULT_QUANT_BIAS, INT_MIN, INT_MAX, V|E},
{"color_table_id", NULL, OFFSET(color_table_id), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"internal_buffer_count", NULL, OFFSET(internal_buffer_count), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"global_quality", NULL, OFFSET(global_quality), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"coder", NULL, OFFSET(coder_type), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E, "coder"},
{"vlc", "variable length coder / huffman coder", 0, FF_OPT_TYPE_CONST, FF_CODER_TYPE_VLC, INT_MIN, INT_MAX, V|E, "coder"},
{"ac", "arithmetic coder", 0, FF_OPT_TYPE_CONST, FF_CODER_TYPE_AC, INT_MIN, INT_MAX, V|E, "coder"},
{"raw", "raw (no encoding)", 0, FF_OPT_TYPE_CONST, FF_CODER_TYPE_RAW, INT_MIN, INT_MAX, V|E, "coder"},
{"rle", "run-length coder", 0, FF_OPT_TYPE_CONST, FF_CODER_TYPE_RLE, INT_MIN, INT_MAX, V|E, "coder"},
{"deflate", "deflate-based coder", 0, FF_OPT_TYPE_CONST, FF_CODER_TYPE_DEFLATE, INT_MIN, INT_MAX, V|E, "coder"},
{"context", "context model", OFFSET(context_model), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"slice_flags", NULL, OFFSET(slice_flags), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"xvmc_acceleration", NULL, OFFSET(xvmc_acceleration), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"mbd", "macroblock decision algorithm (high quality mode)", OFFSET(mb_decision), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E, "mbd"},
{"simple", "use mbcmp (default)", 0, FF_OPT_TYPE_CONST, FF_MB_DECISION_SIMPLE, INT_MIN, INT_MAX, V|E, "mbd"},
{"bits", "use fewest bits", 0, FF_OPT_TYPE_CONST, FF_MB_DECISION_BITS, INT_MIN, INT_MAX, V|E, "mbd"},
{"rd", "use best rate distortion", 0, FF_OPT_TYPE_CONST, FF_MB_DECISION_RD, INT_MIN, INT_MAX, V|E, "mbd"},
{"stream_codec_tag", NULL, OFFSET(stream_codec_tag), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"sc_threshold", "scene change threshold", OFFSET(scenechange_threshold), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"lmin", "min lagrange factor (VBR)", OFFSET(lmin), FF_OPT_TYPE_INT,  2*FF_QP2LAMBDA, 0, INT_MAX, V|E},
{"lmax", "max lagrange factor (VBR)", OFFSET(lmax), FF_OPT_TYPE_INT, 31*FF_QP2LAMBDA, 0, INT_MAX, V|E},
{"nr", "noise reduction", OFFSET(noise_reduction), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"rc_init_occupancy", "number of bits which should be loaded into the rc buffer before decoding starts", OFFSET(rc_initial_buffer_occupancy), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"inter_threshold", NULL, OFFSET(inter_threshold), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"flags2", NULL, OFFSET(flags2), FF_OPT_TYPE_FLAGS, CODEC_FLAG2_FASTPSKIP|CODEC_FLAG2_BIT_RESERVOIR|CODEC_FLAG2_PSY|CODEC_FLAG2_MBTREE, 0, UINT_MAX, V|A|E|D, "flags2"},
{"error", NULL, OFFSET(error_rate), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"antialias", "MP3 antialias algorithm", OFFSET(antialias_algo), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|D, "aa"},
{"auto", NULL, 0, FF_OPT_TYPE_CONST, FF_AA_AUTO, INT_MIN, INT_MAX, V|D, "aa"},
{"fastint", NULL, 0, FF_OPT_TYPE_CONST, FF_AA_FASTINT, INT_MIN, INT_MAX, V|D, "aa"},
{"int", NULL, 0, FF_OPT_TYPE_CONST, FF_AA_INT, INT_MIN, INT_MAX, V|D, "aa"},
{"float", NULL, 0, FF_OPT_TYPE_CONST, FF_AA_FLOAT, INT_MIN, INT_MAX, V|D, "aa"},
{"qns", "quantizer noise shaping", OFFSET(quantizer_noise_shaping), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"threads", NULL, OFFSET(thread_count), FF_OPT_TYPE_INT, 1, INT_MIN, INT_MAX, V|E|D},
{"me_threshold", "motion estimaton threshold", OFFSET(me_threshold), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"mb_threshold", "macroblock threshold", OFFSET(mb_threshold), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"dc", "intra_dc_precision", OFFSET(intra_dc_precision), FF_OPT_TYPE_INT, 0, INT_MIN, INT_MAX, V|E},
{"nssew", "nsse weight", OFFSET(nsse_weight), FF_OPT_TYPE_INT, 8, INT_MIN, INT_MAX, V|E},
{"skip_top", "number of macroblock rows at the top which are skipped", OFFSET(skip_top), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|D},
{"skip_bottom", "number of macroblock rows at the bottom which are skipped", OFFSET(skip_bottom), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|D},
{"profile", NULL, OFFSET(profile), FF_OPT_TYPE_INT, FF_PROFILE_UNKNOWN, INT_MIN, INT_MAX, V|A|E, "profile"},
{"unknown", NULL, 0, FF_OPT_TYPE_CONST, FF_PROFILE_UNKNOWN, INT_MIN, INT_MAX, V|A|E, "profile"},
{"aac_main", NULL, 0, FF_OPT_TYPE_CONST, FF_PROFILE_AAC_MAIN, INT_MIN, INT_MAX, A|E, "profile"},
{"aac_low", NULL, 0, FF_OPT_TYPE_CONST, FF_PROFILE_AAC_LOW, INT_MIN, INT_MAX, A|E, "profile"},
{"aac_ssr", NULL, 0, FF_OPT_TYPE_CONST, FF_PROFILE_AAC_SSR, INT_MIN, INT_MAX, A|E, "profile"},
{"aac_ltp", NULL, 0, FF_OPT_TYPE_CONST, FF_PROFILE_AAC_LTP, INT_MIN, INT_MAX, A|E, "profile"},
{"level", NULL, OFFSET(level), FF_OPT_TYPE_INT, FF_LEVEL_UNKNOWN, INT_MIN, INT_MAX, V|A|E, "level"},
{"unknown", NULL, 0, FF_OPT_TYPE_CONST, FF_LEVEL_UNKNOWN, INT_MIN, INT_MAX, V|A|E, "level"},
{"lowres", "decode at 1= 1/2, 2=1/4, 3=1/8 resolutions", OFFSET(lowres), FF_OPT_TYPE_INT, 0, 0, INT_MAX, V|D},
{"skip_threshold", "frame skip threshold", OFFSET(frame_skip_threshold), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"skip_factor", "frame skip factor", OFFSET(frame_skip_factor), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"skip_exp", "frame skip exponent", OFFSET(frame_skip_exp), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"skipcmp", "frame skip compare function", OFFSET(frame_skip_cmp), FF_OPT_TYPE_INT, FF_CMP_DCTMAX, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"border_mask", "increases the quantizer for macroblocks close to borders", OFFSET(border_masking), FF_OPT_TYPE_FLOAT, DEFAULT, -FLT_MAX, FLT_MAX, V|E},
{"mblmin", "min macroblock lagrange factor (VBR)", OFFSET(mb_lmin), FF_OPT_TYPE_INT, FF_QP2LAMBDA * 2, 1, FF_LAMBDA_MAX, V|E},
{"mblmax", "max macroblock lagrange factor (VBR)", OFFSET(mb_lmax), FF_OPT_TYPE_INT, FF_QP2LAMBDA * 31, 1, FF_LAMBDA_MAX, V|E},
{"mepc", "motion estimation bitrate penalty compensation (1.0 = 256)", OFFSET(me_penalty_compensation), FF_OPT_TYPE_INT, 256, INT_MIN, INT_MAX, V|E},
{"skip_loop_filter", NULL, OFFSET(skip_loop_filter), FF_OPT_TYPE_INT, AVDISCARD_DEFAULT, INT_MIN, INT_MAX, V|D, "avdiscard"},
{"skip_idct"       , NULL, OFFSET(skip_idct)       , FF_OPT_TYPE_INT, AVDISCARD_DEFAULT, INT_MIN, INT_MAX, V|D, "avdiscard"},
{"skip_frame"      , NULL, OFFSET(skip_frame)      , FF_OPT_TYPE_INT, AVDISCARD_DEFAULT, INT_MIN, INT_MAX, V|D, "avdiscard"},
{"none"            , NULL, 0, FF_OPT_TYPE_CONST, AVDISCARD_NONE   , INT_MIN, INT_MAX, V|D, "avdiscard"},
{"default"         , NULL, 0, FF_OPT_TYPE_CONST, AVDISCARD_DEFAULT, INT_MIN, INT_MAX, V|D, "avdiscard"},
{"noref"           , NULL, 0, FF_OPT_TYPE_CONST, AVDISCARD_NONREF , INT_MIN, INT_MAX, V|D, "avdiscard"},
{"bidir"           , NULL, 0, FF_OPT_TYPE_CONST, AVDISCARD_BIDIR  , INT_MIN, INT_MAX, V|D, "avdiscard"},
{"nokey"           , NULL, 0, FF_OPT_TYPE_CONST, AVDISCARD_NONKEY , INT_MIN, INT_MAX, V|D, "avdiscard"},
{"all"             , NULL, 0, FF_OPT_TYPE_CONST, AVDISCARD_ALL    , INT_MIN, INT_MAX, V|D, "avdiscard"},
{"bidir_refine", "refine the two motion vectors used in bidirectional macroblocks", OFFSET(bidir_refine), FF_OPT_TYPE_INT, 1, 0, 4, V|E},
{"brd_scale", "downscales frames for dynamic B-frame decision", OFFSET(brd_scale), FF_OPT_TYPE_INT, DEFAULT, 0, 10, V|E},
{"crf", "enables constant quality mode, and selects the quality (x264)", OFFSET(crf), FF_OPT_TYPE_FLOAT, DEFAULT, 0, 51, V|E},
{"cqp", "constant quantization parameter rate control method", OFFSET(cqp), FF_OPT_TYPE_INT, -1, INT_MIN, INT_MAX, V|E},
{"keyint_min", "minimum interval between IDR-frames (x264)", OFFSET(keyint_min), FF_OPT_TYPE_INT, 25, INT_MIN, INT_MAX, V|E},
{"refs", "reference frames to consider for motion compensation (Snow)", OFFSET(refs), FF_OPT_TYPE_INT, 1, INT_MIN, INT_MAX, V|E},
{"chromaoffset", "chroma qp offset from luma", OFFSET(chromaoffset), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"bframebias", "influences how often B-frames are used", OFFSET(bframebias), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"trellis", "rate-distortion optimal quantization", OFFSET(trellis), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|A|E},
{"directpred", "direct mv prediction mode - 0 (none), 1 (spatial), 2 (temporal), 3 (auto)", OFFSET(directpred), FF_OPT_TYPE_INT, 2, INT_MIN, INT_MAX, V|E},
{"bpyramid", "allows B-frames to be used as references for predicting", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_BPYRAMID, INT_MIN, INT_MAX, V|E, "flags2"},
{"wpred", "weighted biprediction for b-frames (H.264)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_WPRED, INT_MIN, INT_MAX, V|E, "flags2"},
{"mixed_refs", "one reference per partition, as opposed to one reference per macroblock", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_MIXED_REFS, INT_MIN, INT_MAX, V|E, "flags2"},
{"dct8x8", "high profile 8x8 transform (H.264)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_8X8DCT, INT_MIN, INT_MAX, V|E, "flags2"},
{"fastpskip", "fast pskip (H.264)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_FASTPSKIP, INT_MIN, INT_MAX, V|E, "flags2"},
{"aud", "access unit delimiters (H.264)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_AUD, INT_MIN, INT_MAX, V|E, "flags2"},
{"skiprd", "RD optimal MB level residual skipping", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_SKIP_RD, INT_MIN, INT_MAX, V|E, "flags2"},
{"complexityblur", "reduce fluctuations in qp (before curve compression)", OFFSET(complexityblur), FF_OPT_TYPE_FLOAT, 20.0, FLT_MIN, FLT_MAX, V|E},
{"deblockalpha", "in-loop deblocking filter alphac0 parameter", OFFSET(deblockalpha), FF_OPT_TYPE_INT, DEFAULT, -6, 6, V|E},
{"deblockbeta", "in-loop deblocking filter beta parameter", OFFSET(deblockbeta), FF_OPT_TYPE_INT, DEFAULT, -6, 6, V|E},
{"partitions", "macroblock subpartition sizes to consider", OFFSET(partitions), FF_OPT_TYPE_FLAGS, DEFAULT, INT_MIN, INT_MAX, V|E, "partitions"},
{"parti4x4", NULL, 0, FF_OPT_TYPE_CONST, X264_PART_I4X4, INT_MIN, INT_MAX, V|E, "partitions"},
{"parti8x8", NULL, 0, FF_OPT_TYPE_CONST, X264_PART_I8X8, INT_MIN, INT_MAX, V|E, "partitions"},
{"partp4x4", NULL, 0, FF_OPT_TYPE_CONST, X264_PART_P4X4, INT_MIN, INT_MAX, V|E, "partitions"},
{"partp8x8", NULL, 0, FF_OPT_TYPE_CONST, X264_PART_P8X8, INT_MIN, INT_MAX, V|E, "partitions"},
{"partb8x8", NULL, 0, FF_OPT_TYPE_CONST, X264_PART_B8X8, INT_MIN, INT_MAX, V|E, "partitions"},
{"sc_factor", "multiplied by qscale for each frame and added to scene_change_score", OFFSET(scenechange_factor), FF_OPT_TYPE_INT, 6, 0, INT_MAX, V|E},
{"mv0_threshold", NULL, OFFSET(mv0_threshold), FF_OPT_TYPE_INT, 256, 0, INT_MAX, V|E},
{"ivlc", "intra vlc table", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_INTRA_VLC, INT_MIN, INT_MAX, V|E, "flags2"},
{"b_sensitivity", "adjusts sensitivity of b_frame_strategy 1", OFFSET(b_sensitivity), FF_OPT_TYPE_INT, 40, 1, INT_MAX, V|E},
{"compression_level", NULL, OFFSET(compression_level), FF_OPT_TYPE_INT, FF_COMPRESSION_DEFAULT, INT_MIN, INT_MAX, V|A|E},
{"use_lpc", "sets whether to use LPC mode (FLAC)", OFFSET(use_lpc), FF_OPT_TYPE_INT, -1, INT_MIN, INT_MAX, A|E},
{"lpc_coeff_precision", "LPC coefficient precision (FLAC)", OFFSET(lpc_coeff_precision), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, A|E},
{"min_prediction_order", NULL, OFFSET(min_prediction_order), FF_OPT_TYPE_INT, -1, INT_MIN, INT_MAX, A|E},
{"max_prediction_order", NULL, OFFSET(max_prediction_order), FF_OPT_TYPE_INT, -1, INT_MIN, INT_MAX, A|E},
{"prediction_order_method", "search method for selecting prediction order", OFFSET(prediction_order_method), FF_OPT_TYPE_INT, -1, INT_MIN, INT_MAX, A|E},
{"min_partition_order", NULL, OFFSET(min_partition_order), FF_OPT_TYPE_INT, -1, INT_MIN, INT_MAX, A|E},
{"max_partition_order", NULL, OFFSET(max_partition_order), FF_OPT_TYPE_INT, -1, INT_MIN, INT_MAX, A|E},
{"timecode_frame_start", "GOP timecode frame start number, in non drop frame format", OFFSET(timecode_frame_start), FF_OPT_TYPE_INT64, 0, 0, INT64_MAX, V|E},
{"drop_frame_timecode", NULL, 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_DROP_FRAME_TIMECODE, INT_MIN, INT_MAX, V|E, "flags2"},
{"non_linear_q", "use non linear quantizer", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_NON_LINEAR_QUANT, INT_MIN, INT_MAX, V|E, "flags2"},
{"request_channels", "set desired number of audio channels", OFFSET(request_channels), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, A|D},
{"drc_scale", "percentage of dynamic range compression to apply", OFFSET(drc_scale), FF_OPT_TYPE_FLOAT, 1.0, 0.0, 1.0, A|D},
{"reservoir", "use bit reservoir", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_BIT_RESERVOIR, INT_MIN, INT_MAX, A|E, "flags2"},
{"mbtree", "use macroblock tree ratecontrol (x264 only)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_MBTREE, INT_MIN, INT_MAX, V|E, "flags2"},
{"bits_per_raw_sample", NULL, OFFSET(bits_per_raw_sample), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"channel_layout", NULL, OFFSET(channel_layout), FF_OPT_TYPE_INT64, DEFAULT, 0, INT64_MAX, A|E|D, "channel_layout"},
{"request_channel_layout", NULL, OFFSET(request_channel_layout), FF_OPT_TYPE_INT64, DEFAULT, 0, INT64_MAX, A|D, "request_channel_layout"},
{"rc_max_vbv_use", NULL, OFFSET(rc_max_available_vbv_use), FF_OPT_TYPE_FLOAT, 1.0/3, 0.0, FLT_MAX, V|E},
{"rc_min_vbv_use", NULL, OFFSET(rc_min_vbv_overflow_use),  FF_OPT_TYPE_FLOAT, 3,     0.0, FLT_MAX, V|E},
{"ticks_per_frame", NULL, OFFSET(ticks_per_frame), FF_OPT_TYPE_INT, 1, 1, INT_MAX, A|V|E|D},
{"color_primaries", NULL, OFFSET(color_primaries), FF_OPT_TYPE_INT, AVCOL_PRI_UNSPECIFIED, 1, AVCOL_PRI_NB-1, V|E|D},
{"color_trc", NULL, OFFSET(color_trc), FF_OPT_TYPE_INT, AVCOL_TRC_UNSPECIFIED, 1, AVCOL_TRC_NB-1, V|E|D},
{"colorspace", NULL, OFFSET(colorspace), FF_OPT_TYPE_INT, AVCOL_SPC_UNSPECIFIED, 1, AVCOL_SPC_NB-1, V|E|D},
{"color_range", NULL, OFFSET(color_range), FF_OPT_TYPE_INT, AVCOL_RANGE_UNSPECIFIED, 0, AVCOL_RANGE_NB-1, V|E|D},
{"chroma_sample_location", NULL, OFFSET(chroma_sample_location), FF_OPT_TYPE_INT, AVCHROMA_LOC_UNSPECIFIED, 0, AVCHROMA_LOC_NB-1, V|E|D},
{"psy", "use psycho visual optimization", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_PSY, INT_MIN, INT_MAX, V|E, "flags2"},
{"psy_rd", "specify psycho visual strength", OFFSET(psy_rd), FF_OPT_TYPE_FLOAT, 1.0, 0, FLT_MAX, V|E},
{"psy_trellis", "specify psycho visual trellis", OFFSET(psy_trellis), FF_OPT_TYPE_FLOAT, 0, 0, FLT_MAX, V|E},
{"aq_mode", "specify aq method", OFFSET(aq_mode), FF_OPT_TYPE_INT, 1, 0, INT_MAX, V|E},
{"aq_strength", "specify aq strength", OFFSET(aq_strength), FF_OPT_TYPE_FLOAT, 1.0, 0, FLT_MAX, V|E},
{"rc_lookahead", "specify number of frames to look ahead for frametype", OFFSET(rc_lookahead), FF_OPT_TYPE_INT, 40, 0, INT_MAX, V|E},
{"ssim", "ssim will be calculated during encoding", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_SSIM, INT_MIN, INT_MAX, V|E, "flags2"},
{NULL},
};

#undef A
#undef V
#undef S
#undef E
#undef D
#undef DEFAULT

static const AVClass av_codec_context_class = { "AVCodecContext", context_to_name, options, LIBAVUTIL_VERSION_INT };

void avcodec_get_context_defaults2(AVCodecContext *s, enum AVMediaType codec_type){
    int flags=0;
    memset(s, 0, sizeof(AVCodecContext));

    s->av_class= &av_codec_context_class;

    s->codec_type = codec_type;
    if(codec_type == AVMEDIA_TYPE_AUDIO)
        flags= AV_OPT_FLAG_AUDIO_PARAM;
    else if(codec_type == AVMEDIA_TYPE_VIDEO)
        flags= AV_OPT_FLAG_VIDEO_PARAM;
    else if(codec_type == AVMEDIA_TYPE_SUBTITLE)
        flags= AV_OPT_FLAG_SUBTITLE_PARAM;
    av_opt_set_defaults2(s, flags, flags);

    s->time_base= (AVRational){0,1};
    s->get_buffer= avcodec_default_get_buffer;
    s->release_buffer= avcodec_default_release_buffer;
    s->get_format= avcodec_default_get_format;
    s->execute= avcodec_default_execute;
    s->execute2= avcodec_default_execute2;
    s->sample_aspect_ratio= (AVRational){0,1};
    s->pix_fmt= PIX_FMT_NONE;
    s->sample_fmt= SAMPLE_FMT_NONE;

    s->palctrl = NULL;
    s->reget_buffer= avcodec_default_reget_buffer;
    s->reordered_opaque= AV_NOPTS_VALUE;
}

AVCodecContext *avcodec_alloc_context2(enum AVMediaType codec_type){
    AVCodecContext *avctx= av_malloc(sizeof(AVCodecContext));

    if(avctx==NULL) return NULL;

    avcodec_get_context_defaults2(avctx, codec_type);

    return avctx;
}

void avcodec_get_context_defaults(AVCodecContext *s){
    avcodec_get_context_defaults2(s, AVMEDIA_TYPE_UNKNOWN);
}

AVCodecContext *avcodec_alloc_context(void){
    return avcodec_alloc_context2(AVMEDIA_TYPE_UNKNOWN);
}

int avcodec_copy_context(AVCodecContext *dest, const AVCodecContext *src)
{
    if (dest->codec) { // check that the dest context is uninitialized
        av_log(dest, AV_LOG_ERROR,
               "Tried to copy AVCodecContext %p into already-initialized %p\n",
               src, dest);
        return AVERROR(EINVAL);
    }
    memcpy(dest, src, sizeof(*dest));

    /* set values specific to opened codecs back to their default state */
    dest->priv_data       = NULL;
    dest->codec           = NULL;
    dest->palctrl         = NULL;
    dest->slice_offset    = NULL;
    dest->internal_buffer = NULL;
    dest->hwaccel         = NULL;
    dest->thread_opaque   = NULL;

    /* reallocate values that should be allocated separately */
    dest->rc_eq           = NULL;
    dest->extradata       = NULL;
    dest->intra_matrix    = NULL;
    dest->inter_matrix    = NULL;
    dest->rc_override     = NULL;
    if (src->rc_eq) {
        dest->rc_eq = av_strdup(src->rc_eq);
        if (!dest->rc_eq)
            return AVERROR(ENOMEM);
    }

#define alloc_and_copy_or_fail(obj, size, pad) \
    if (src->obj && size > 0) { \
        dest->obj = av_malloc(size + pad); \
        if (!dest->obj) \
            goto fail; \
        memcpy(dest->obj, src->obj, size); \
        if (pad) \
            memset(((uint8_t *) dest->obj) + size, 0, pad); \
    }
    alloc_and_copy_or_fail(extradata,    src->extradata_size,
                           FF_INPUT_BUFFER_PADDING_SIZE);
    alloc_and_copy_or_fail(intra_matrix, 64 * sizeof(int16_t), 0);
    alloc_and_copy_or_fail(inter_matrix, 64 * sizeof(int16_t), 0);
    alloc_and_copy_or_fail(rc_override,  src->rc_override_count * sizeof(*src->rc_override), 0);
#undef alloc_and_copy_or_fail

    return 0;

fail:
    av_freep(&dest->rc_override);
    av_freep(&dest->intra_matrix);
    av_freep(&dest->inter_matrix);
    av_freep(&dest->extradata);
    av_freep(&dest->rc_eq);
    return AVERROR(ENOMEM);
}
