/*
 * VC-1 and WMV3 decoder common code
 * Copyright (c) 2011 Mashiat Sarker Shakkhar
 * Copyright (c) 2006-2007 Konstantin Shishkov
 * Partly based on vc9.c (c) 2005 Anonymous, Alex Beregszaszi, Michael Niedermayer
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
 * VC-1 and WMV3 decoder common code
 */

#include "libavutil/attributes.h"
#include "internal.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "vc1.h"
#include "vc1data.h"
#include "wmv2data.h"
#include "unary.h"
#include "simple_idct.h"

/***********************************************************************/
/**
 * @name VC-1 Bitplane decoding
 * @see 8.7, p56
 * @{
 */

/** Decode rows by checking if they are skipped
 * @param plane Buffer to store decoded bits
 * @param[in] width Width of this buffer
 * @param[in] height Height of this buffer
 * @param[in] stride of this buffer
 */
static void decode_rowskip(uint8_t* plane, int width, int height, int stride,
                           GetBitContext *gb)
{
    int x, y;

    for (y = 0; y < height; y++) {
        if (!get_bits1(gb)) //rowskip
            memset(plane, 0, width);
        else
            for (x = 0; x < width; x++)
                plane[x] = get_bits1(gb);
        plane += stride;
    }
}

/** Decode columns by checking if they are skipped
 * @param plane Buffer to store decoded bits
 * @param[in] width Width of this buffer
 * @param[in] height Height of this buffer
 * @param[in] stride of this buffer
 * @todo FIXME: Optimize
 */
static void decode_colskip(uint8_t* plane, int width, int height, int stride,
                           GetBitContext *gb)
{
    int x, y;

    for (x = 0; x < width; x++) {
        if (!get_bits1(gb)) //colskip
            for (y = 0; y < height; y++)
                plane[y*stride] = 0;
        else
            for (y = 0; y < height; y++)
                plane[y*stride] = get_bits1(gb);
        plane ++;
    }
}

/** Decode a bitplane's bits
 * @param data bitplane where to store the decode bits
 * @param[out] raw_flag pointer to the flag indicating that this bitplane is not coded explicitly
 * @param v VC-1 context for bit reading and logging
 * @return Status
 * @todo FIXME: Optimize
 */
static int bitplane_decoding(uint8_t* data, int *raw_flag, VC1Context *v)
{
    GetBitContext *gb = &v->s.gb;

    int imode, x, y, code, offset;
    uint8_t invert, *planep = data;
    int width, height, stride;

    width  = v->s.mb_width;
    height = v->s.mb_height >> v->field_mode;
    stride = v->s.mb_stride;
    invert = get_bits1(gb);
    imode = get_vlc2(gb, ff_vc1_imode_vlc.table, VC1_IMODE_VLC_BITS, 1);

    *raw_flag = 0;
    switch (imode) {
    case IMODE_RAW:
        //Data is actually read in the MB layer (same for all tests == "raw")
        *raw_flag = 1; //invert ignored
        return invert;
    case IMODE_DIFF2:
    case IMODE_NORM2:
        if ((height * width) & 1) {
            *planep++ = get_bits1(gb);
            y = offset = 1;
            if (offset == width) {
                offset = 0;
                planep += stride - width;
            }
        }
        else
            y = offset = 0;
        // decode bitplane as one long line
        for (; y < height * width; y += 2) {
            code = get_vlc2(gb, ff_vc1_norm2_vlc.table, VC1_NORM2_VLC_BITS, 1);
            *planep++ = code & 1;
            offset++;
            if (offset == width) {
                offset  = 0;
                planep += stride - width;
            }
            *planep++ = code >> 1;
            offset++;
            if (offset == width) {
                offset  = 0;
                planep += stride - width;
            }
        }
        break;
    case IMODE_DIFF6:
    case IMODE_NORM6:
        if (!(height % 3) && (width % 3)) { // use 2x3 decoding
            for (y = 0; y < height; y += 3) {
                for (x = width & 1; x < width; x += 2) {
                    code = get_vlc2(gb, ff_vc1_norm6_vlc.table, VC1_NORM6_VLC_BITS, 2);
                    if (code < 0) {
                        av_log(v->s.avctx, AV_LOG_DEBUG, "invalid NORM-6 VLC\n");
                        return -1;
                    }
                    planep[x + 0]              = (code >> 0) & 1;
                    planep[x + 1]              = (code >> 1) & 1;
                    planep[x + 0 + stride]     = (code >> 2) & 1;
                    planep[x + 1 + stride]     = (code >> 3) & 1;
                    planep[x + 0 + stride * 2] = (code >> 4) & 1;
                    planep[x + 1 + stride * 2] = (code >> 5) & 1;
                }
                planep += stride * 3;
            }
            if (width & 1)
                decode_colskip(data, 1, height, stride, &v->s.gb);
        } else { // 3x2
            planep += (height & 1) * stride;
            for (y = height & 1; y < height; y += 2) {
                for (x = width % 3; x < width; x += 3) {
                    code = get_vlc2(gb, ff_vc1_norm6_vlc.table, VC1_NORM6_VLC_BITS, 2);
                    if (code < 0) {
                        av_log(v->s.avctx, AV_LOG_DEBUG, "invalid NORM-6 VLC\n");
                        return -1;
                    }
                    planep[x + 0]          = (code >> 0) & 1;
                    planep[x + 1]          = (code >> 1) & 1;
                    planep[x + 2]          = (code >> 2) & 1;
                    planep[x + 0 + stride] = (code >> 3) & 1;
                    planep[x + 1 + stride] = (code >> 4) & 1;
                    planep[x + 2 + stride] = (code >> 5) & 1;
                }
                planep += stride * 2;
            }
            x = width % 3;
            if (x)
                decode_colskip(data,             x, height, stride, &v->s.gb);
            if (height & 1)
                decode_rowskip(data + x, width - x,      1, stride, &v->s.gb);
        }
        break;
    case IMODE_ROWSKIP:
        decode_rowskip(data, width, height, stride, &v->s.gb);
        break;
    case IMODE_COLSKIP:
        decode_colskip(data, width, height, stride, &v->s.gb);
        break;
    default:
        break;
    }

    /* Applying diff operator */
    if (imode == IMODE_DIFF2 || imode == IMODE_DIFF6) {
        planep = data;
        planep[0] ^= invert;
        for (x = 1; x < width; x++)
            planep[x] ^= planep[x-1];
        for (y = 1; y < height; y++) {
            planep += stride;
            planep[0] ^= planep[-stride];
            for (x = 1; x < width; x++) {
                if (planep[x-1] != planep[x-stride]) planep[x] ^= invert;
                else                                 planep[x] ^= planep[x-1];
            }
        }
    } else if (invert) {
        planep = data;
        for (x = 0; x < stride * height; x++)
            planep[x] = !planep[x]; //FIXME stride
    }
    return (imode << 1) + invert;
}

/** @} */ //Bitplane group

/***********************************************************************/
/** VOP Dquant decoding
 * @param v VC-1 Context
 */
static int vop_dquant_decoding(VC1Context *v)
{
    GetBitContext *gb = &v->s.gb;
    int pqdiff;

    //variable size
    if (v->dquant != 2) {
        v->dquantfrm = get_bits1(gb);
        if (!v->dquantfrm)
            return 0;

        v->dqprofile = get_bits(gb, 2);
        switch (v->dqprofile) {
        case DQPROFILE_SINGLE_EDGE:
        case DQPROFILE_DOUBLE_EDGES:
            v->dqsbedge = get_bits(gb, 2);
            break;
        case DQPROFILE_ALL_MBS:
            v->dqbilevel = get_bits1(gb);
            if (!v->dqbilevel) {
                v->halfpq = 0;
                return 0;
            }
        default:
            break; //Forbidden ?
        }
    }

    pqdiff = get_bits(gb, 3);
    if (pqdiff == 7)
        v->altpq = get_bits(gb, 5);
    else
        v->altpq = v->pq + pqdiff + 1;

    return 0;
}

static int decode_sequence_header_adv(VC1Context *v, GetBitContext *gb);

/**
 * Decode Simple/Main Profiles sequence header
 * @see Figure 7-8, p16-17
 * @param avctx Codec context
 * @param gb GetBit context initialized from Codec context extra_data
 * @return Status
 */
int ff_vc1_decode_sequence_header(AVCodecContext *avctx, VC1Context *v, GetBitContext *gb)
{
    av_log(avctx, AV_LOG_DEBUG, "Header: %0X\n", show_bits_long(gb, 32));
    v->profile = get_bits(gb, 2);
    if (v->profile == PROFILE_COMPLEX) {
        av_log(avctx, AV_LOG_WARNING, "WMV3 Complex Profile is not fully supported\n");
    }

    if (v->profile == PROFILE_ADVANCED) {
        v->zz_8x4 = ff_vc1_adv_progressive_8x4_zz;
        v->zz_4x8 = ff_vc1_adv_progressive_4x8_zz;
        return decode_sequence_header_adv(v, gb);
    } else {
        v->chromaformat = 1;
        v->zz_8x4 = ff_wmv2_scantableA;
        v->zz_4x8 = ff_wmv2_scantableB;
        v->res_y411   = get_bits1(gb);
        v->res_sprite = get_bits1(gb);
        if (v->res_y411) {
            av_log(avctx, AV_LOG_ERROR,
                   "Old interlaced mode is not supported\n");
            return -1;
        }
    }

    // (fps-2)/4 (->30)
    v->frmrtq_postproc = get_bits(gb, 3); //common
    // (bitrate-32kbps)/64kbps
    v->bitrtq_postproc = get_bits(gb, 5); //common
    v->s.loop_filter   = get_bits1(gb); //common
    if (v->s.loop_filter == 1 && v->profile == PROFILE_SIMPLE) {
        av_log(avctx, AV_LOG_ERROR,
               "LOOPFILTER shall not be enabled in Simple Profile\n");
    }
    if (v->s.avctx->skip_loop_filter >= AVDISCARD_ALL)
        v->s.loop_filter = 0;

    v->res_x8          = get_bits1(gb); //reserved
    v->multires        = get_bits1(gb);
    v->res_fasttx      = get_bits1(gb);
    if (!v->res_fasttx) {
        v->vc1dsp.vc1_inv_trans_8x8    = ff_simple_idct_8;
        v->vc1dsp.vc1_inv_trans_8x4    = ff_simple_idct84_add;
        v->vc1dsp.vc1_inv_trans_4x8    = ff_simple_idct48_add;
        v->vc1dsp.vc1_inv_trans_4x4    = ff_simple_idct44_add;
        v->vc1dsp.vc1_inv_trans_8x8_dc = ff_simple_idct_add_8;
        v->vc1dsp.vc1_inv_trans_8x4_dc = ff_simple_idct84_add;
        v->vc1dsp.vc1_inv_trans_4x8_dc = ff_simple_idct48_add;
        v->vc1dsp.vc1_inv_trans_4x4_dc = ff_simple_idct44_add;
    }

    v->fastuvmc        = get_bits1(gb); //common
    if (!v->profile && !v->fastuvmc) {
        av_log(avctx, AV_LOG_ERROR,
               "FASTUVMC unavailable in Simple Profile\n");
        return -1;
    }
    v->extended_mv     = get_bits1(gb); //common
    if (!v->profile && v->extended_mv) {
        av_log(avctx, AV_LOG_ERROR,
               "Extended MVs unavailable in Simple Profile\n");
        return -1;
    }
    v->dquant          = get_bits(gb, 2); //common
    v->vstransform     = get_bits1(gb); //common

    v->res_transtab    = get_bits1(gb);
    if (v->res_transtab) {
        av_log(avctx, AV_LOG_ERROR,
               "1 for reserved RES_TRANSTAB is forbidden\n");
        return -1;
    }

    v->overlap         = get_bits1(gb); //common

    v->resync_marker   = get_bits1(gb);
    v->rangered        = get_bits1(gb);
    if (v->rangered && v->profile == PROFILE_SIMPLE) {
        av_log(avctx, AV_LOG_INFO,
               "RANGERED should be set to 0 in Simple Profile\n");
    }

    v->s.max_b_frames = avctx->max_b_frames = get_bits(gb, 3); //common
    v->quantizer_mode = get_bits(gb, 2); //common

    v->finterpflag = get_bits1(gb); //common

    if (v->res_sprite) {
        int w = get_bits(gb, 11);
        int h = get_bits(gb, 11);
        int ret = ff_set_dimensions(v->s.avctx, w, h);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set dimensions %d %d\n", w, h);
            return ret;
        }
        skip_bits(gb, 5); //frame rate
        v->res_x8 = get_bits1(gb);
        if (get_bits1(gb)) { // something to do with DC VLC selection
            av_log(avctx, AV_LOG_ERROR, "Unsupported sprite feature\n");
            return -1;
        }
        skip_bits(gb, 3); //slice code
        v->res_rtm_flag = 0;
    } else {
        v->res_rtm_flag = get_bits1(gb); //reserved
    }
    if (!v->res_rtm_flag) {
        av_log(avctx, AV_LOG_ERROR,
               "Old WMV3 version detected, some frames may be decoded incorrectly\n");
        //return -1;
    }
    //TODO: figure out what they mean (always 0x402F)
    if (!v->res_fasttx)
        skip_bits(gb, 16);
    av_log(avctx, AV_LOG_DEBUG,
           "Profile %i:\nfrmrtq_postproc=%i, bitrtq_postproc=%i\n"
           "LoopFilter=%i, MultiRes=%i, FastUVMC=%i, Extended MV=%i\n"
           "Rangered=%i, VSTransform=%i, Overlap=%i, SyncMarker=%i\n"
           "DQuant=%i, Quantizer mode=%i, Max B-frames=%i\n",
           v->profile, v->frmrtq_postproc, v->bitrtq_postproc,
           v->s.loop_filter, v->multires, v->fastuvmc, v->extended_mv,
           v->rangered, v->vstransform, v->overlap, v->resync_marker,
           v->dquant, v->quantizer_mode, avctx->max_b_frames);
    return 0;
}

static int decode_sequence_header_adv(VC1Context *v, GetBitContext *gb)
{
    v->res_rtm_flag = 1;
    v->level = get_bits(gb, 3);
    if (v->level >= 5) {
        av_log(v->s.avctx, AV_LOG_ERROR, "Reserved LEVEL %i\n",v->level);
    }
    v->chromaformat = get_bits(gb, 2);
    if (v->chromaformat != 1) {
        av_log(v->s.avctx, AV_LOG_ERROR,
               "Only 4:2:0 chroma format supported\n");
        return -1;
    }

    // (fps-2)/4 (->30)
    v->frmrtq_postproc       = get_bits(gb, 3); //common
    // (bitrate-32kbps)/64kbps
    v->bitrtq_postproc       = get_bits(gb, 5); //common
    v->postprocflag          = get_bits1(gb);   //common

    v->max_coded_width       = (get_bits(gb, 12) + 1) << 1;
    v->max_coded_height      = (get_bits(gb, 12) + 1) << 1;
    v->broadcast             = get_bits1(gb);
    v->interlace             = get_bits1(gb);
    v->tfcntrflag            = get_bits1(gb);
    v->finterpflag           = get_bits1(gb);
    skip_bits1(gb); // reserved

    av_log(v->s.avctx, AV_LOG_DEBUG,
           "Advanced Profile level %i:\nfrmrtq_postproc=%i, bitrtq_postproc=%i\n"
           "LoopFilter=%i, ChromaFormat=%i, Pulldown=%i, Interlace: %i\n"
           "TFCTRflag=%i, FINTERPflag=%i\n",
           v->level, v->frmrtq_postproc, v->bitrtq_postproc,
           v->s.loop_filter, v->chromaformat, v->broadcast, v->interlace,
           v->tfcntrflag, v->finterpflag);

    v->psf = get_bits1(gb);
    if (v->psf) { //PsF, 6.1.13
        av_log(v->s.avctx, AV_LOG_ERROR, "Progressive Segmented Frame mode: not supported (yet)\n");
        return -1;
    }
    v->s.max_b_frames = v->s.avctx->max_b_frames = 7;
    if (get_bits1(gb)) { //Display Info - decoding is not affected by it
        int w, h, ar = 0;
        av_log(v->s.avctx, AV_LOG_DEBUG, "Display extended info:\n");
        w = get_bits(gb, 14) + 1;
        h = get_bits(gb, 14) + 1;
        av_log(v->s.avctx, AV_LOG_DEBUG, "Display dimensions: %ix%i\n", w, h);
        if (get_bits1(gb))
            ar = get_bits(gb, 4);
        if (ar && ar < 14) {
            v->s.avctx->sample_aspect_ratio = ff_vc1_pixel_aspect[ar];
        } else if (ar == 15) {
            w = get_bits(gb, 8) + 1;
            h = get_bits(gb, 8) + 1;
            v->s.avctx->sample_aspect_ratio = (AVRational){w, h};
        } else {
            av_reduce(&v->s.avctx->sample_aspect_ratio.num,
                      &v->s.avctx->sample_aspect_ratio.den,
                      v->s.avctx->height * w,
                      v->s.avctx->width * h,
                      1 << 30);
        }
        ff_set_sar(v->s.avctx, v->s.avctx->sample_aspect_ratio);
        av_log(v->s.avctx, AV_LOG_DEBUG, "Aspect: %i:%i\n",
               v->s.avctx->sample_aspect_ratio.num,
               v->s.avctx->sample_aspect_ratio.den);

        if (get_bits1(gb)) { //framerate stuff
            if (get_bits1(gb)) {
                v->s.avctx->framerate.den = 32;
                v->s.avctx->framerate.num = get_bits(gb, 16) + 1;
            } else {
                int nr, dr;
                nr = get_bits(gb, 8);
                dr = get_bits(gb, 4);
                if (nr > 0 && nr < 8 && dr > 0 && dr < 3) {
                    v->s.avctx->framerate.den = ff_vc1_fps_dr[dr - 1];
                    v->s.avctx->framerate.num = ff_vc1_fps_nr[nr - 1] * 1000;
                }
            }
            if (v->broadcast) { // Pulldown may be present
                v->s.avctx->ticks_per_frame = 2;
            }
        }

        if (get_bits1(gb)) {
            v->color_prim    = get_bits(gb, 8);
            v->transfer_char = get_bits(gb, 8);
            v->matrix_coef   = get_bits(gb, 8);
        }
    }

    v->hrd_param_flag = get_bits1(gb);
    if (v->hrd_param_flag) {
        int i;
        v->hrd_num_leaky_buckets = get_bits(gb, 5);
        skip_bits(gb, 4); //bitrate exponent
        skip_bits(gb, 4); //buffer size exponent
        for (i = 0; i < v->hrd_num_leaky_buckets; i++) {
            skip_bits(gb, 16); //hrd_rate[n]
            skip_bits(gb, 16); //hrd_buffer[n]
        }
    }
    return 0;
}

int ff_vc1_decode_entry_point(AVCodecContext *avctx, VC1Context *v, GetBitContext *gb)
{
    int i;
    int w,h;
    int ret;

    av_log(avctx, AV_LOG_DEBUG, "Entry point: %08X\n", show_bits_long(gb, 32));
    v->broken_link    = get_bits1(gb);
    v->closed_entry   = get_bits1(gb);
    v->panscanflag    = get_bits1(gb);
    v->refdist_flag   = get_bits1(gb);
    v->s.loop_filter  = get_bits1(gb);
    if (v->s.avctx->skip_loop_filter >= AVDISCARD_ALL)
        v->s.loop_filter = 0;
    v->fastuvmc       = get_bits1(gb);
    v->extended_mv    = get_bits1(gb);
    v->dquant         = get_bits(gb, 2);
    v->vstransform    = get_bits1(gb);
    v->overlap        = get_bits1(gb);
    v->quantizer_mode = get_bits(gb, 2);

    if (v->hrd_param_flag) {
        for (i = 0; i < v->hrd_num_leaky_buckets; i++) {
            skip_bits(gb, 8); //hrd_full[n]
        }
    }

    if(get_bits1(gb)){
        w = (get_bits(gb, 12)+1)<<1;
        h = (get_bits(gb, 12)+1)<<1;
    } else {
        w = v->max_coded_width;
        h = v->max_coded_height;
    }
    if ((ret = ff_set_dimensions(avctx, w, h)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set dimensions %d %d\n", w, h);
        return ret;
    }

    if (v->extended_mv)
        v->extended_dmv = get_bits1(gb);
    if ((v->range_mapy_flag = get_bits1(gb))) {
        av_log(avctx, AV_LOG_ERROR, "Luma scaling is not supported, expect wrong picture\n");
        v->range_mapy = get_bits(gb, 3);
    }
    if ((v->range_mapuv_flag = get_bits1(gb))) {
        av_log(avctx, AV_LOG_ERROR, "Chroma scaling is not supported, expect wrong picture\n");
        v->range_mapuv = get_bits(gb, 3);
    }

    av_log(avctx, AV_LOG_DEBUG, "Entry point info:\n"
           "BrokenLink=%i, ClosedEntry=%i, PanscanFlag=%i\n"
           "RefDist=%i, Postproc=%i, FastUVMC=%i, ExtMV=%i\n"
           "DQuant=%i, VSTransform=%i, Overlap=%i, Qmode=%i\n",
           v->broken_link, v->closed_entry, v->panscanflag, v->refdist_flag, v->s.loop_filter,
           v->fastuvmc, v->extended_mv, v->dquant, v->vstransform, v->overlap, v->quantizer_mode);

    return 0;
}

/* fill lookup tables for intensity compensation */
#define INIT_LUT(lumscale, lumshift, luty, lutuv, chain) do {                 \
        int scale, shift, i;                                                  \
        if (!lumscale) {                                                      \
            scale = -64;                                                      \
            shift = (255 - lumshift * 2) * 64;                                \
            if (lumshift > 31)                                                \
                shift += 128 << 6;                                            \
        } else {                                                              \
            scale = lumscale + 32;                                            \
            if (lumshift > 31)                                                \
                shift = (lumshift - 64) * 64;                                 \
            else                                                              \
                shift = lumshift << 6;                                        \
        }                                                                     \
        for (i = 0; i < 256; i++) {                                           \
            int iy = chain ? luty[i]  : i;                                    \
            int iu = chain ? lutuv[i] : i;                                    \
            luty[i]  = av_clip_uint8((scale * iy + shift + 32) >> 6);         \
            lutuv[i] = av_clip_uint8((scale * (iu - 128) + 128*64 + 32) >> 6);\
        }                                                                     \
    } while(0)

static void rotate_luts(VC1Context *v)
{
#define ROTATE(DEF, L, N, C, A) do {                          \
        if (v->s.pict_type == AV_PICTURE_TYPE_BI || v->s.pict_type == AV_PICTURE_TYPE_B) { \
            C = A;                                            \
        } else {                                              \
            DEF;                                              \
            memcpy(&tmp, L   , sizeof(tmp));                  \
            memcpy(L   , N   , sizeof(tmp));                  \
            memcpy(N   , &tmp, sizeof(tmp));                  \
            C = N;                                            \
        }                                                     \
    } while(0)

    ROTATE(int tmp,             &v->last_use_ic, &v->next_use_ic, v->curr_use_ic, &v->aux_use_ic);
    ROTATE(uint8_t tmp[2][256], v->last_luty,   v->next_luty,   v->curr_luty,   v->aux_luty);
    ROTATE(uint8_t tmp[2][256], v->last_lutuv,  v->next_lutuv,  v->curr_lutuv,  v->aux_lutuv);

    INIT_LUT(32, 0, v->curr_luty[0], v->curr_lutuv[0], 0);
    INIT_LUT(32, 0, v->curr_luty[1], v->curr_lutuv[1], 0);
    *v->curr_use_ic = 0;
}

static int read_bfraction(VC1Context *v, GetBitContext* gb) {
    int bfraction_lut_index = get_vlc2(gb, ff_vc1_bfraction_vlc.table, VC1_BFRACTION_VLC_BITS, 1);

    if (bfraction_lut_index == 21 || bfraction_lut_index < 0) {
        av_log(v->s.avctx, AV_LOG_ERROR, "bfraction invalid\n");
        return AVERROR_INVALIDDATA;
    }
    v->bfraction_lut_index = bfraction_lut_index;
    v->bfraction           = ff_vc1_bfraction_lut[v->bfraction_lut_index];
    return 0;
}

int ff_vc1_parse_frame_header(VC1Context *v, GetBitContext* gb)
{
    int pqindex, lowquant, status;

    v->field_mode = 0;
    v->fcm = 0;
    if (v->finterpflag)
        v->interpfrm = get_bits1(gb);
    if (!v->s.avctx->codec)
        return -1;
    if (v->s.avctx->codec_id == AV_CODEC_ID_MSS2)
        v->respic   =
        v->rangered =
        v->multires = get_bits(gb, 2) == 1;
    else
        skip_bits(gb, 2); //framecnt unused
    v->rangeredfrm = 0;
    if (v->rangered)
        v->rangeredfrm = get_bits1(gb);
    if (get_bits1(gb)) {
        v->s.pict_type = AV_PICTURE_TYPE_P;
    } else {
        if (v->s.avctx->max_b_frames && !get_bits1(gb)) {
            v->s.pict_type = AV_PICTURE_TYPE_B;
        } else
            v->s.pict_type = AV_PICTURE_TYPE_I;
    }

    v->bi_type = 0;
    if (v->s.pict_type == AV_PICTURE_TYPE_B) {
        if (read_bfraction(v, gb) < 0)
            return AVERROR_INVALIDDATA;
        if (v->bfraction == 0) {
            v->s.pict_type = AV_PICTURE_TYPE_BI;
        }
    }
    if (v->s.pict_type == AV_PICTURE_TYPE_I || v->s.pict_type == AV_PICTURE_TYPE_BI)
        skip_bits(gb, 7); // skip buffer fullness

    if (v->parse_only)
        return 0;

    /* calculate RND */
    if (v->s.pict_type == AV_PICTURE_TYPE_I || v->s.pict_type == AV_PICTURE_TYPE_BI)
        v->rnd = 1;
    if (v->s.pict_type == AV_PICTURE_TYPE_P)
        v->rnd ^= 1;

    /* Quantizer stuff */
    pqindex = get_bits(gb, 5);
    if (!pqindex)
        return -1;
    if (v->quantizer_mode == QUANT_FRAME_IMPLICIT)
        v->pq = ff_vc1_pquant_table[0][pqindex];
    else
        v->pq = ff_vc1_pquant_table[1][pqindex];
    v->pqindex = pqindex;
    if (pqindex < 9)
        v->halfpq = get_bits1(gb);
    else
        v->halfpq = 0;
    switch (v->quantizer_mode) {
    case QUANT_FRAME_IMPLICIT:
        v->pquantizer = pqindex < 9;
        break;
    case QUANT_NON_UNIFORM:
        v->pquantizer = 0;
        break;
    case QUANT_FRAME_EXPLICIT:
        v->pquantizer = get_bits1(gb);
        break;
    default:
        v->pquantizer = 1;
        break;
    }
    v->dquantfrm = 0;
    if (v->extended_mv == 1)
        v->mvrange = get_unary(gb, 0, 3);
    v->k_x = v->mvrange + 9 + (v->mvrange >> 1); //k_x can be 9 10 12 13
    v->k_y = v->mvrange + 8; //k_y can be 8 9 10 11
    v->range_x = 1 << (v->k_x - 1);
    v->range_y = 1 << (v->k_y - 1);
    if (v->multires && v->s.pict_type != AV_PICTURE_TYPE_B)
        v->respic = get_bits(gb, 2);

    if (v->res_x8 && (v->s.pict_type == AV_PICTURE_TYPE_I || v->s.pict_type == AV_PICTURE_TYPE_BI)) {
        v->x8_type = get_bits1(gb);
    } else
        v->x8_type = 0;
    ff_dlog(v->s.avctx, "%c Frame: QP=[%i]%i (+%i/2) %i\n",
            (v->s.pict_type == AV_PICTURE_TYPE_P) ? 'P' : ((v->s.pict_type == AV_PICTURE_TYPE_I) ? 'I' : 'B'),
            pqindex, v->pq, v->halfpq, v->rangeredfrm);

    if (v->first_pic_header_flag)
        rotate_luts(v);

    switch (v->s.pict_type) {
    case AV_PICTURE_TYPE_P:
        v->tt_index = (v->pq > 4) + (v->pq > 12);

        lowquant = (v->pq > 12) ? 0 : 1;
        v->mv_mode = ff_vc1_mv_pmode_table[lowquant][get_unary(gb, 1, 4)];
        if (v->mv_mode == MV_PMODE_INTENSITY_COMP) {
            v->mv_mode2 = ff_vc1_mv_pmode_table2[lowquant][get_unary(gb, 1, 3)];
            v->lumscale = get_bits(gb, 6);
            v->lumshift = get_bits(gb, 6);
            v->last_use_ic = 1;
            /* fill lookup tables for intensity compensation */
            INIT_LUT(v->lumscale, v->lumshift, v->last_luty[0], v->last_lutuv[0], 1);
            INIT_LUT(v->lumscale, v->lumshift, v->last_luty[1], v->last_lutuv[1], 1);
        }
        v->qs_last = v->s.quarter_sample;
        if (v->mv_mode == MV_PMODE_INTENSITY_COMP) {
            v->s.quarter_sample = (v->mv_mode2 != MV_PMODE_1MV_HPEL &&
                                   v->mv_mode2 != MV_PMODE_1MV_HPEL_BILIN);
            v->s.mspel          = (v->mv_mode2 != MV_PMODE_1MV_HPEL_BILIN);
        } else {
            v->s.quarter_sample = (v->mv_mode != MV_PMODE_1MV_HPEL &&
                                   v->mv_mode != MV_PMODE_1MV_HPEL_BILIN);
            v->s.mspel          = (v->mv_mode != MV_PMODE_1MV_HPEL_BILIN);
        }

        if ((v->mv_mode  == MV_PMODE_INTENSITY_COMP &&
             v->mv_mode2 == MV_PMODE_MIXED_MV)      ||
            v->mv_mode   == MV_PMODE_MIXED_MV) {
            status = bitplane_decoding(v->mv_type_mb_plane, &v->mv_type_is_raw, v);
            if (status < 0)
                return -1;
            av_log(v->s.avctx, AV_LOG_DEBUG, "MB MV Type plane encoding: "
                   "Imode: %i, Invert: %i\n", status>>1, status&1);
        } else {
            v->mv_type_is_raw = 0;
            memset(v->mv_type_mb_plane, 0, v->s.mb_stride * v->s.mb_height);
        }
        status = bitplane_decoding(v->s.mbskip_table, &v->skip_is_raw, v);
        if (status < 0)
            return -1;
        av_log(v->s.avctx, AV_LOG_DEBUG, "MB Skip plane encoding: "
               "Imode: %i, Invert: %i\n", status>>1, status&1);

        /* Hopefully this is correct for P-frames */
        v->s.mv_table_index = get_bits(gb, 2); //but using ff_vc1_ tables
        v->cbpcy_vlc = &ff_vc1_cbpcy_p_vlc[get_bits(gb, 2)];

        if (v->dquant) {
            av_log(v->s.avctx, AV_LOG_DEBUG, "VOP DQuant info\n");
            vop_dquant_decoding(v);
        }

        if (v->vstransform) {
            v->ttmbf = get_bits1(gb);
            if (v->ttmbf) {
                v->ttfrm = ff_vc1_ttfrm_to_tt[get_bits(gb, 2)];
            } else
                v->ttfrm = 0; //FIXME Is that so ?
        } else {
            v->ttmbf = 1;
            v->ttfrm = TT_8X8;
        }
        break;
    case AV_PICTURE_TYPE_B:
        v->tt_index = (v->pq > 4) + (v->pq > 12);

        v->mv_mode          = get_bits1(gb) ? MV_PMODE_1MV : MV_PMODE_1MV_HPEL_BILIN;
        v->qs_last          = v->s.quarter_sample;
        v->s.quarter_sample = (v->mv_mode == MV_PMODE_1MV);
        v->s.mspel          = v->s.quarter_sample;

        status = bitplane_decoding(v->direct_mb_plane, &v->dmb_is_raw, v);
        if (status < 0)
            return -1;
        av_log(v->s.avctx, AV_LOG_DEBUG, "MB Direct Type plane encoding: "
               "Imode: %i, Invert: %i\n", status>>1, status&1);
        status = bitplane_decoding(v->s.mbskip_table, &v->skip_is_raw, v);
        if (status < 0)
            return -1;
        av_log(v->s.avctx, AV_LOG_DEBUG, "MB Skip plane encoding: "
               "Imode: %i, Invert: %i\n", status>>1, status&1);

        v->s.mv_table_index = get_bits(gb, 2);
        v->cbpcy_vlc        = &ff_vc1_cbpcy_p_vlc[get_bits(gb, 2)];

        if (v->dquant) {
            av_log(v->s.avctx, AV_LOG_DEBUG, "VOP DQuant info\n");
            vop_dquant_decoding(v);
        }

        if (v->vstransform) {
            v->ttmbf = get_bits1(gb);
            if (v->ttmbf) {
                v->ttfrm = ff_vc1_ttfrm_to_tt[get_bits(gb, 2)];
            } else
                v->ttfrm = 0;
        } else {
            v->ttmbf = 1;
            v->ttfrm = TT_8X8;
        }
        break;
    }

    if (!v->x8_type) {
        /* AC Syntax */
        v->c_ac_table_index = decode012(gb);
        if (v->s.pict_type == AV_PICTURE_TYPE_I || v->s.pict_type == AV_PICTURE_TYPE_BI) {
            v->y_ac_table_index = decode012(gb);
        }
        /* DC Syntax */
        v->s.dc_table_index = get_bits1(gb);
    }

    if (v->s.pict_type == AV_PICTURE_TYPE_BI) {
        v->s.pict_type = AV_PICTURE_TYPE_B;
        v->bi_type     = 1;
    }
    return 0;
}

int ff_vc1_parse_frame_header_adv(VC1Context *v, GetBitContext* gb)
{
    int pqindex, lowquant;
    int status;
    int mbmodetab, imvtab, icbptab, twomvbptab, fourmvbptab; /* useful only for debugging */
    int field_mode, fcm;

    v->numref          = 0;
    v->p_frame_skipped = 0;
    if (v->second_field) {
        if (v->fcm != ILACE_FIELD || v->field_mode!=1)
            return -1;
        if (v->fptype & 4)
            v->s.pict_type = (v->fptype & 1) ? AV_PICTURE_TYPE_BI : AV_PICTURE_TYPE_B;
        else
            v->s.pict_type = (v->fptype & 1) ? AV_PICTURE_TYPE_P : AV_PICTURE_TYPE_I;
        v->s.current_picture_ptr->f->pict_type = v->s.pict_type;
        if (!v->pic_header_flag)
            goto parse_common_info;
    }

    field_mode = 0;
    if (v->interlace) {
        fcm = decode012(gb);
        if (fcm) {
            if (fcm == ILACE_FIELD)
                field_mode = 1;
        }
    } else {
        fcm = PROGRESSIVE;
    }
    if (!v->first_pic_header_flag && v->field_mode != field_mode)
        return AVERROR_INVALIDDATA;
    v->field_mode = field_mode;
    v->fcm = fcm;

    av_assert0(    v->s.mb_height == v->s.height + 15 >> 4
                || v->s.mb_height == FFALIGN(v->s.height + 15 >> 4, 2));
    if (v->field_mode) {
        v->s.mb_height = FFALIGN(v->s.height + 15 >> 4, 2);
        v->fptype = get_bits(gb, 3);
        if (v->fptype & 4) // B-picture
            v->s.pict_type = (v->fptype & 2) ? AV_PICTURE_TYPE_BI : AV_PICTURE_TYPE_B;
        else
            v->s.pict_type = (v->fptype & 2) ? AV_PICTURE_TYPE_P : AV_PICTURE_TYPE_I;
    } else {
        v->s.mb_height = v->s.height + 15 >> 4;
        switch (get_unary(gb, 0, 4)) {
        case 0:
            v->s.pict_type = AV_PICTURE_TYPE_P;
            break;
        case 1:
            v->s.pict_type = AV_PICTURE_TYPE_B;
            break;
        case 2:
            v->s.pict_type = AV_PICTURE_TYPE_I;
            break;
        case 3:
            v->s.pict_type = AV_PICTURE_TYPE_BI;
            break;
        case 4:
            v->s.pict_type = AV_PICTURE_TYPE_P; // skipped pic
            v->p_frame_skipped = 1;
            break;
        }
    }
    if (v->tfcntrflag)
        skip_bits(gb, 8);
    if (v->broadcast) {
        if (!v->interlace || v->psf) {
            v->rptfrm = get_bits(gb, 2);
        } else {
            v->tff = get_bits1(gb);
            v->rff = get_bits1(gb);
        }
    } else {
        v->tff = 1;
    }
    if (v->panscanflag) {
        avpriv_report_missing_feature(v->s.avctx, "Pan-scan");
        //...
    }
    if (v->p_frame_skipped) {
        return 0;
    }
    v->rnd = get_bits1(gb);
    if (v->interlace)
        v->uvsamp = get_bits1(gb);
    if(!ff_vc1_bfraction_vlc.table)
        return 0; //parsing only, vlc tables havnt been allocated
    if (v->field_mode) {
        if (!v->refdist_flag)
            v->refdist = 0;
        else if ((v->s.pict_type != AV_PICTURE_TYPE_B) && (v->s.pict_type != AV_PICTURE_TYPE_BI)) {
            v->refdist = get_bits(gb, 2);
            if (v->refdist == 3)
                v->refdist += get_unary(gb, 0, 16);
        }
        if ((v->s.pict_type == AV_PICTURE_TYPE_B) || (v->s.pict_type == AV_PICTURE_TYPE_BI)) {
            if (read_bfraction(v, gb) < 0)
                return AVERROR_INVALIDDATA;
            v->frfd = (v->bfraction * v->refdist) >> 8;
            v->brfd = v->refdist - v->frfd - 1;
            if (v->brfd < 0)
                v->brfd = 0;
        }
        goto parse_common_info;
    }
    if (v->fcm == PROGRESSIVE) {
        if (v->finterpflag)
            v->interpfrm = get_bits1(gb);
        if (v->s.pict_type == AV_PICTURE_TYPE_B) {
            if (read_bfraction(v, gb) < 0)
                return AVERROR_INVALIDDATA;
            if (v->bfraction == 0) {
                v->s.pict_type = AV_PICTURE_TYPE_BI; /* XXX: should not happen here */
            }
        }
    }

    parse_common_info:
    if (v->field_mode)
        v->cur_field_type = !(v->tff ^ v->second_field);
    pqindex = get_bits(gb, 5);
    if (!pqindex)
        return -1;
    if (v->quantizer_mode == QUANT_FRAME_IMPLICIT)
        v->pq = ff_vc1_pquant_table[0][pqindex];
    else
        v->pq = ff_vc1_pquant_table[1][pqindex];
    v->pqindex = pqindex;
    if (pqindex < 9)
        v->halfpq = get_bits1(gb);
    else
        v->halfpq = 0;
    switch (v->quantizer_mode) {
    case QUANT_FRAME_IMPLICIT:
        v->pquantizer = pqindex < 9;
        break;
    case QUANT_NON_UNIFORM:
        v->pquantizer = 0;
        break;
    case QUANT_FRAME_EXPLICIT:
        v->pquantizer = get_bits1(gb);
        break;
    default:
        v->pquantizer = 1;
        break;
    }
    if (v->postprocflag)
        v->postproc = get_bits(gb, 2);

    if (v->parse_only)
        return 0;

    if (v->first_pic_header_flag)
        rotate_luts(v);

    switch (v->s.pict_type) {
    case AV_PICTURE_TYPE_I:
    case AV_PICTURE_TYPE_BI:
        if (v->fcm == ILACE_FRAME) { //interlace frame picture
            status = bitplane_decoding(v->fieldtx_plane, &v->fieldtx_is_raw, v);
            if (status < 0)
                return -1;
            av_log(v->s.avctx, AV_LOG_DEBUG, "FIELDTX plane encoding: "
                   "Imode: %i, Invert: %i\n", status>>1, status&1);
        }
        status = bitplane_decoding(v->acpred_plane, &v->acpred_is_raw, v);
        if (status < 0)
            return -1;
        av_log(v->s.avctx, AV_LOG_DEBUG, "ACPRED plane encoding: "
               "Imode: %i, Invert: %i\n", status>>1, status&1);
        v->condover = CONDOVER_NONE;
        if (v->overlap && v->pq <= 8) {
            v->condover = decode012(gb);
            if (v->condover == CONDOVER_SELECT) {
                status = bitplane_decoding(v->over_flags_plane, &v->overflg_is_raw, v);
                if (status < 0)
                    return -1;
                av_log(v->s.avctx, AV_LOG_DEBUG, "CONDOVER plane encoding: "
                       "Imode: %i, Invert: %i\n", status>>1, status&1);
            }
        }
        break;
    case AV_PICTURE_TYPE_P:
        if (v->field_mode) {
            v->numref = get_bits1(gb);
            if (!v->numref) {
                v->reffield          = get_bits1(gb);
                v->ref_field_type[0] = v->reffield ^ !v->cur_field_type;
            }
        }
        if (v->extended_mv)
            v->mvrange = get_unary(gb, 0, 3);
        else
            v->mvrange = 0;
        if (v->interlace) {
            if (v->extended_dmv)
                v->dmvrange = get_unary(gb, 0, 3);
            else
                v->dmvrange = 0;
            if (v->fcm == ILACE_FRAME) { // interlaced frame picture
                v->fourmvswitch = get_bits1(gb);
                v->intcomp      = get_bits1(gb);
                if (v->intcomp) {
                    v->lumscale = get_bits(gb, 6);
                    v->lumshift = get_bits(gb, 6);
                    INIT_LUT(v->lumscale, v->lumshift, v->last_luty[0], v->last_lutuv[0], 1);
                    INIT_LUT(v->lumscale, v->lumshift, v->last_luty[1], v->last_lutuv[1], 1);
                    v->last_use_ic = 1;
                }
                status = bitplane_decoding(v->s.mbskip_table, &v->skip_is_raw, v);
                av_log(v->s.avctx, AV_LOG_DEBUG, "SKIPMB plane encoding: "
                       "Imode: %i, Invert: %i\n", status>>1, status&1);
                mbmodetab = get_bits(gb, 2);
                if (v->fourmvswitch)
                    v->mbmode_vlc = &ff_vc1_intfr_4mv_mbmode_vlc[mbmodetab];
                else
                    v->mbmode_vlc = &ff_vc1_intfr_non4mv_mbmode_vlc[mbmodetab];
                imvtab         = get_bits(gb, 2);
                v->imv_vlc     = &ff_vc1_1ref_mvdata_vlc[imvtab];
                // interlaced p-picture cbpcy range is [1, 63]
                icbptab        = get_bits(gb, 3);
                v->cbpcy_vlc   = &ff_vc1_icbpcy_vlc[icbptab];
                twomvbptab     = get_bits(gb, 2);
                v->twomvbp_vlc = &ff_vc1_2mv_block_pattern_vlc[twomvbptab];
                if (v->fourmvswitch) {
                    fourmvbptab     = get_bits(gb, 2);
                    v->fourmvbp_vlc = &ff_vc1_4mv_block_pattern_vlc[fourmvbptab];
                }
            }
        }
        v->k_x = v->mvrange + 9 + (v->mvrange >> 1); //k_x can be 9 10 12 13
        v->k_y = v->mvrange + 8; //k_y can be 8 9 10 11
        v->range_x = 1 << (v->k_x - 1);
        v->range_y = 1 << (v->k_y - 1);

        v->tt_index = (v->pq > 4) + (v->pq > 12);
        if (v->fcm != ILACE_FRAME) {
            int mvmode;
            mvmode     = get_unary(gb, 1, 4);
            lowquant   = (v->pq > 12) ? 0 : 1;
            v->mv_mode = ff_vc1_mv_pmode_table[lowquant][mvmode];
            if (v->mv_mode == MV_PMODE_INTENSITY_COMP) {
                int mvmode2;
                mvmode2 = get_unary(gb, 1, 3);
                v->mv_mode2 = ff_vc1_mv_pmode_table2[lowquant][mvmode2];
                if (v->field_mode) {
                    v->intcompfield = decode210(gb) ^ 3;
                } else
                    v->intcompfield = 3;

                v->lumscale2 = v->lumscale = 32;
                v->lumshift2 = v->lumshift =  0;
                if (v->intcompfield & 1) {
                    v->lumscale = get_bits(gb, 6);
                    v->lumshift = get_bits(gb, 6);
                }
                if ((v->intcompfield & 2) && v->field_mode) {
                    v->lumscale2 = get_bits(gb, 6);
                    v->lumshift2 = get_bits(gb, 6);
                } else if(!v->field_mode) {
                    v->lumscale2 = v->lumscale;
                    v->lumshift2 = v->lumshift;
                }
                if (v->field_mode && v->second_field) {
                    if (v->cur_field_type) {
                        INIT_LUT(v->lumscale , v->lumshift , v->curr_luty[v->cur_field_type^1], v->curr_lutuv[v->cur_field_type^1], 0);
                        INIT_LUT(v->lumscale2, v->lumshift2, v->last_luty[v->cur_field_type  ], v->last_lutuv[v->cur_field_type  ], 1);
                    } else {
                        INIT_LUT(v->lumscale2, v->lumshift2, v->curr_luty[v->cur_field_type^1], v->curr_lutuv[v->cur_field_type^1], 0);
                        INIT_LUT(v->lumscale , v->lumshift , v->last_luty[v->cur_field_type  ], v->last_lutuv[v->cur_field_type  ], 1);
                    }
                    v->next_use_ic = *v->curr_use_ic = 1;
                } else {
                    INIT_LUT(v->lumscale , v->lumshift , v->last_luty[0], v->last_lutuv[0], 1);
                    INIT_LUT(v->lumscale2, v->lumshift2, v->last_luty[1], v->last_lutuv[1], 1);
                }
                v->last_use_ic = 1;
            }
            v->qs_last = v->s.quarter_sample;
            if (v->mv_mode == MV_PMODE_INTENSITY_COMP) {
                v->s.quarter_sample = (v->mv_mode2 != MV_PMODE_1MV_HPEL &&
                                       v->mv_mode2 != MV_PMODE_1MV_HPEL_BILIN);
                v->s.mspel          = (v->mv_mode2 != MV_PMODE_1MV_HPEL_BILIN);
            } else {
                v->s.quarter_sample = (v->mv_mode != MV_PMODE_1MV_HPEL &&
                                       v->mv_mode != MV_PMODE_1MV_HPEL_BILIN);
                v->s.mspel          = (v->mv_mode != MV_PMODE_1MV_HPEL_BILIN);
            }
        }
        if (v->fcm == PROGRESSIVE) { // progressive
            if ((v->mv_mode == MV_PMODE_INTENSITY_COMP &&
                 v->mv_mode2 == MV_PMODE_MIXED_MV)
                || v->mv_mode == MV_PMODE_MIXED_MV) {
                status = bitplane_decoding(v->mv_type_mb_plane, &v->mv_type_is_raw, v);
                if (status < 0)
                    return -1;
                av_log(v->s.avctx, AV_LOG_DEBUG, "MB MV Type plane encoding: "
                       "Imode: %i, Invert: %i\n", status>>1, status&1);
            } else {
                v->mv_type_is_raw = 0;
                memset(v->mv_type_mb_plane, 0, v->s.mb_stride * v->s.mb_height);
            }
            status = bitplane_decoding(v->s.mbskip_table, &v->skip_is_raw, v);
            if (status < 0)
                return -1;
            av_log(v->s.avctx, AV_LOG_DEBUG, "MB Skip plane encoding: "
                   "Imode: %i, Invert: %i\n", status>>1, status&1);

            /* Hopefully this is correct for P-frames */
            v->s.mv_table_index = get_bits(gb, 2); //but using ff_vc1_ tables
            v->cbpcy_vlc        = &ff_vc1_cbpcy_p_vlc[get_bits(gb, 2)];
        } else if (v->fcm == ILACE_FRAME) { // frame interlaced
            v->qs_last          = v->s.quarter_sample;
            v->s.quarter_sample = 1;
            v->s.mspel          = 1;
        } else {    // field interlaced
            mbmodetab = get_bits(gb, 3);
            imvtab = get_bits(gb, 2 + v->numref);
            if (!v->numref)
                v->imv_vlc = &ff_vc1_1ref_mvdata_vlc[imvtab];
            else
                v->imv_vlc = &ff_vc1_2ref_mvdata_vlc[imvtab];
            icbptab = get_bits(gb, 3);
            v->cbpcy_vlc = &ff_vc1_icbpcy_vlc[icbptab];
            if ((v->mv_mode == MV_PMODE_INTENSITY_COMP &&
                v->mv_mode2 == MV_PMODE_MIXED_MV) || v->mv_mode == MV_PMODE_MIXED_MV) {
                fourmvbptab     = get_bits(gb, 2);
                v->fourmvbp_vlc = &ff_vc1_4mv_block_pattern_vlc[fourmvbptab];
                v->mbmode_vlc = &ff_vc1_if_mmv_mbmode_vlc[mbmodetab];
            } else {
                v->mbmode_vlc = &ff_vc1_if_1mv_mbmode_vlc[mbmodetab];
            }
        }
        if (v->dquant) {
            av_log(v->s.avctx, AV_LOG_DEBUG, "VOP DQuant info\n");
            vop_dquant_decoding(v);
        }

        if (v->vstransform) {
            v->ttmbf = get_bits1(gb);
            if (v->ttmbf) {
                v->ttfrm = ff_vc1_ttfrm_to_tt[get_bits(gb, 2)];
            } else
                v->ttfrm = 0; //FIXME Is that so ?
        } else {
            v->ttmbf = 1;
            v->ttfrm = TT_8X8;
        }
        break;
    case AV_PICTURE_TYPE_B:
        if (v->fcm == ILACE_FRAME) {
            if (read_bfraction(v, gb) < 0)
                return AVERROR_INVALIDDATA;
            if (v->bfraction == 0) {
                return -1;
            }
        }
        if (v->extended_mv)
            v->mvrange = get_unary(gb, 0, 3);
        else
            v->mvrange = 0;
        v->k_x     = v->mvrange + 9 + (v->mvrange >> 1); //k_x can be 9 10 12 13
        v->k_y     = v->mvrange + 8; //k_y can be 8 9 10 11
        v->range_x = 1 << (v->k_x - 1);
        v->range_y = 1 << (v->k_y - 1);

        v->tt_index = (v->pq > 4) + (v->pq > 12);

        if (v->field_mode) {
            int mvmode;
            av_log(v->s.avctx, AV_LOG_DEBUG, "B Fields\n");
            if (v->extended_dmv)
                v->dmvrange = get_unary(gb, 0, 3);
            mvmode = get_unary(gb, 1, 3);
            lowquant = (v->pq > 12) ? 0 : 1;
            v->mv_mode          = ff_vc1_mv_pmode_table2[lowquant][mvmode];
            v->qs_last          = v->s.quarter_sample;
            v->s.quarter_sample = (v->mv_mode == MV_PMODE_1MV || v->mv_mode == MV_PMODE_MIXED_MV);
            v->s.mspel          = !(v->mv_mode == MV_PMODE_1MV_HPEL_BILIN || v->mv_mode == MV_PMODE_1MV_HPEL);
            status = bitplane_decoding(v->forward_mb_plane, &v->fmb_is_raw, v);
            if (status < 0)
                return -1;
            av_log(v->s.avctx, AV_LOG_DEBUG, "MB Forward Type plane encoding: "
                   "Imode: %i, Invert: %i\n", status>>1, status&1);
            mbmodetab = get_bits(gb, 3);
            if (v->mv_mode == MV_PMODE_MIXED_MV)
                v->mbmode_vlc = &ff_vc1_if_mmv_mbmode_vlc[mbmodetab];
            else
                v->mbmode_vlc = &ff_vc1_if_1mv_mbmode_vlc[mbmodetab];
            imvtab       = get_bits(gb, 3);
            v->imv_vlc   = &ff_vc1_2ref_mvdata_vlc[imvtab];
            icbptab      = get_bits(gb, 3);
            v->cbpcy_vlc = &ff_vc1_icbpcy_vlc[icbptab];
            if (v->mv_mode == MV_PMODE_MIXED_MV) {
                fourmvbptab     = get_bits(gb, 2);
                v->fourmvbp_vlc = &ff_vc1_4mv_block_pattern_vlc[fourmvbptab];
            }
            v->numref = 1; // interlaced field B pictures are always 2-ref
        } else if (v->fcm == ILACE_FRAME) {
            if (v->extended_dmv)
                v->dmvrange = get_unary(gb, 0, 3);
            if (get_bits1(gb)) /* intcomp - present but shall always be 0 */
                av_log(v->s.avctx, AV_LOG_WARNING, "Intensity compensation set for B picture\n");
            v->intcomp          = 0;
            v->mv_mode          = MV_PMODE_1MV;
            v->fourmvswitch     = 0;
            v->qs_last          = v->s.quarter_sample;
            v->s.quarter_sample = 1;
            v->s.mspel          = 1;
            status              = bitplane_decoding(v->direct_mb_plane, &v->dmb_is_raw, v);
            if (status < 0)
                return -1;
            av_log(v->s.avctx, AV_LOG_DEBUG, "MB Direct Type plane encoding: "
                   "Imode: %i, Invert: %i\n", status>>1, status&1);
            status = bitplane_decoding(v->s.mbskip_table, &v->skip_is_raw, v);
            if (status < 0)
                return -1;
            av_log(v->s.avctx, AV_LOG_DEBUG, "MB Skip plane encoding: "
                   "Imode: %i, Invert: %i\n", status>>1, status&1);
            mbmodetab       = get_bits(gb, 2);
            v->mbmode_vlc   = &ff_vc1_intfr_non4mv_mbmode_vlc[mbmodetab];
            imvtab          = get_bits(gb, 2);
            v->imv_vlc      = &ff_vc1_1ref_mvdata_vlc[imvtab];
            // interlaced p/b-picture cbpcy range is [1, 63]
            icbptab         = get_bits(gb, 3);
            v->cbpcy_vlc    = &ff_vc1_icbpcy_vlc[icbptab];
            twomvbptab      = get_bits(gb, 2);
            v->twomvbp_vlc  = &ff_vc1_2mv_block_pattern_vlc[twomvbptab];
            fourmvbptab     = get_bits(gb, 2);
            v->fourmvbp_vlc = &ff_vc1_4mv_block_pattern_vlc[fourmvbptab];
        } else {
            v->mv_mode          = get_bits1(gb) ? MV_PMODE_1MV : MV_PMODE_1MV_HPEL_BILIN;
            v->qs_last          = v->s.quarter_sample;
            v->s.quarter_sample = (v->mv_mode == MV_PMODE_1MV);
            v->s.mspel          = v->s.quarter_sample;
            status              = bitplane_decoding(v->direct_mb_plane, &v->dmb_is_raw, v);
            if (status < 0)
                return -1;
            av_log(v->s.avctx, AV_LOG_DEBUG, "MB Direct Type plane encoding: "
                   "Imode: %i, Invert: %i\n", status>>1, status&1);
            status = bitplane_decoding(v->s.mbskip_table, &v->skip_is_raw, v);
            if (status < 0)
                return -1;
            av_log(v->s.avctx, AV_LOG_DEBUG, "MB Skip plane encoding: "
                   "Imode: %i, Invert: %i\n", status>>1, status&1);
            v->s.mv_table_index = get_bits(gb, 2);
            v->cbpcy_vlc = &ff_vc1_cbpcy_p_vlc[get_bits(gb, 2)];
        }

        if (v->dquant) {
            av_log(v->s.avctx, AV_LOG_DEBUG, "VOP DQuant info\n");
            vop_dquant_decoding(v);
        }

        if (v->vstransform) {
            v->ttmbf = get_bits1(gb);
            if (v->ttmbf) {
                v->ttfrm = ff_vc1_ttfrm_to_tt[get_bits(gb, 2)];
            } else
                v->ttfrm = 0;
        } else {
            v->ttmbf = 1;
            v->ttfrm = TT_8X8;
        }
        break;
    }

    if (v->fcm != PROGRESSIVE && !v->s.quarter_sample) {
        v->range_x <<= 1;
        v->range_y <<= 1;
    }

    /* AC Syntax */
    v->c_ac_table_index = decode012(gb);
    if (v->s.pict_type == AV_PICTURE_TYPE_I || v->s.pict_type == AV_PICTURE_TYPE_BI) {
        v->y_ac_table_index = decode012(gb);
    }
    /* DC Syntax */
    v->s.dc_table_index = get_bits1(gb);
    if ((v->s.pict_type == AV_PICTURE_TYPE_I || v->s.pict_type == AV_PICTURE_TYPE_BI)
        && v->dquant) {
        av_log(v->s.avctx, AV_LOG_DEBUG, "VOP DQuant info\n");
        vop_dquant_decoding(v);
    }

    v->bi_type = (v->s.pict_type == AV_PICTURE_TYPE_BI);
    if (v->bi_type)
        v->s.pict_type = AV_PICTURE_TYPE_B;

    return 0;
}

static const uint32_t vc1_ac_tables[AC_MODES][186][2] = {
{
{ 0x0001,  2}, { 0x0005,  3}, { 0x000D,  4}, { 0x0012,  5}, { 0x000E,  6}, { 0x0015,  7},
{ 0x0013,  8}, { 0x003F,  8}, { 0x004B,  9}, { 0x011F,  9}, { 0x00B8, 10}, { 0x03E3, 10},
{ 0x0172, 11}, { 0x024D, 12}, { 0x03DA, 12}, { 0x02DD, 13}, { 0x1F55, 13}, { 0x05B9, 14},
{ 0x3EAE, 14}, { 0x0000,  4}, { 0x0010,  5}, { 0x0008,  7}, { 0x0020,  8}, { 0x0029,  9},
{ 0x01F4,  9}, { 0x0233, 10}, { 0x01E0, 11}, { 0x012A, 12}, { 0x03DD, 12}, { 0x050A, 13},
{ 0x1F29, 13}, { 0x0A42, 14}, { 0x1272, 15}, { 0x1737, 15}, { 0x0003,  5}, { 0x0011,  7},
{ 0x00C4,  8}, { 0x004B, 10}, { 0x00B4, 11}, { 0x07D4, 11}, { 0x0345, 12}, { 0x02D7, 13},
{ 0x07BF, 13}, { 0x0938, 14}, { 0x0BBB, 14}, { 0x095E, 15}, { 0x0013,  5}, { 0x0078,  7},
{ 0x0069,  9}, { 0x0232, 10}, { 0x0461, 11}, { 0x03EC, 12}, { 0x0520, 13}, { 0x1F2A, 13},
{ 0x3E50, 14}, { 0x3E51, 14}, { 0x1486, 15}, { 0x000C,  6}, { 0x0024,  9}, { 0x0094, 11},
{ 0x08C0, 12}, { 0x0F09, 14}, { 0x1EF0, 15}, { 0x003D,  6}, { 0x0053,  9}, { 0x01A0, 11},
{ 0x02D6, 13}, { 0x0F08, 14}, { 0x0013,  7}, { 0x007C,  9}, { 0x07C1, 11}, { 0x04AC, 14},
{ 0x001B,  7}, { 0x00A0, 10}, { 0x0344, 12}, { 0x0F79, 14}, { 0x0079,  7}, { 0x03E1, 10},
{ 0x02D4, 13}, { 0x2306, 14}, { 0x0021,  8}, { 0x023C, 10}, { 0x0FAE, 12}, { 0x23DE, 14},
{ 0x0035,  8}, { 0x0175, 11}, { 0x07B3, 13}, { 0x00C5,  8}, { 0x0174, 11}, { 0x0785, 13},
{ 0x0048,  9}, { 0x01A3, 11}, { 0x049E, 13}, { 0x002C,  9}, { 0x00FA, 10}, { 0x07D6, 11},
{ 0x0092, 10}, { 0x05CC, 13}, { 0x1EF1, 15}, { 0x00A3, 10}, { 0x03ED, 12}, { 0x093E, 14},
{ 0x01E2, 11}, { 0x1273, 15}, { 0x07C4, 11}, { 0x1487, 15}, { 0x0291, 12}, { 0x0293, 12},
{ 0x0F8A, 12}, { 0x0509, 13}, { 0x0508, 13}, { 0x078D, 13}, { 0x07BE, 13}, { 0x078C, 13},
{ 0x04AE, 14}, { 0x0BBA, 14}, { 0x2307, 14}, { 0x0B9A, 14}, { 0x1736, 15}, { 0x000E,  4},
{ 0x0045,  7}, { 0x01F3,  9}, { 0x047A, 11}, { 0x05DC, 13}, { 0x23DF, 14}, { 0x0019,  5},
{ 0x0028,  9}, { 0x0176, 11}, { 0x049D, 13}, { 0x23DD, 14}, { 0x0030,  6}, { 0x00A2, 10},
{ 0x02EF, 12}, { 0x05B8, 14}, { 0x003F,  6}, { 0x00A5, 10}, { 0x03DB, 12}, { 0x093F, 14},
{ 0x0044,  7}, { 0x07CB, 11}, { 0x095F, 15}, { 0x0063,  7}, { 0x03C3, 12}, { 0x0015,  8},
{ 0x08F6, 12}, { 0x0017,  8}, { 0x0498, 13}, { 0x002C,  8}, { 0x07B2, 13}, { 0x002F,  8},
{ 0x1F54, 13}, { 0x008D,  8}, { 0x07BD, 13}, { 0x008E,  8}, { 0x1182, 13}, { 0x00FB,  8},
{ 0x050B, 13}, { 0x002D,  8}, { 0x07C0, 11}, { 0x0079,  9}, { 0x1F5F, 13}, { 0x007A,  9},
{ 0x1F56, 13}, { 0x0231, 10}, { 0x03E4, 10}, { 0x01A1, 11}, { 0x0143, 11}, { 0x01F7, 11},
{ 0x016F, 12}, { 0x0292, 12}, { 0x02E7, 12}, { 0x016C, 12}, { 0x016D, 12}, { 0x03DC, 12},
{ 0x0F8B, 12}, { 0x0499, 13}, { 0x03D8, 12}, { 0x078E, 13}, { 0x02D5, 13}, { 0x1F5E, 13},
{ 0x1F2B, 13}, { 0x078F, 13}, { 0x04AD, 14}, { 0x3EAF, 14}, { 0x23DC, 14}, { 0x004A,  9}
},
{
{ 0x0000,  3}, { 0x0003,  4}, { 0x000B,  5}, { 0x0014,  6}, { 0x003F,  6}, { 0x005D,  7},
{ 0x00A2,  8}, { 0x00AC,  9}, { 0x016E,  9}, { 0x020A, 10}, { 0x02E2, 10}, { 0x0432, 11},
{ 0x05C9, 11}, { 0x0827, 12}, { 0x0B54, 12}, { 0x04E6, 13}, { 0x105F, 13}, { 0x172A, 13},
{ 0x20B2, 14}, { 0x2D4E, 14}, { 0x39F0, 14}, { 0x4175, 15}, { 0x5A9E, 15}, { 0x0004,  4},
{ 0x001E,  5}, { 0x0042,  7}, { 0x00B6,  8}, { 0x0173,  9}, { 0x0395, 10}, { 0x072E, 11},
{ 0x0B94, 12}, { 0x16A4, 13}, { 0x20B3, 14}, { 0x2E45, 14}, { 0x0005,  5}, { 0x0040,  7},
{ 0x0049,  9}, { 0x028F, 10}, { 0x05CB, 11}, { 0x048A, 13}, { 0x09DD, 14}, { 0x73E2, 15},
{ 0x0018,  5}, { 0x0025,  8}, { 0x008A, 10}, { 0x051B, 11}, { 0x0E5F, 12}, { 0x09C9, 14},
{ 0x139C, 15}, { 0x0029,  6}, { 0x004F,  9}, { 0x0412, 11}, { 0x048D, 13}, { 0x2E41, 14},
{ 0x0038,  6}, { 0x010E,  9}, { 0x05A8, 11}, { 0x105C, 13}, { 0x39F2, 14}, { 0x0058,  7},
{ 0x021F, 10}, { 0x0E7E, 12}, { 0x39FF, 14}, { 0x0023,  8}, { 0x02E3, 10}, { 0x04E5, 13},
{ 0x2E40, 14}, { 0x00A1,  8}, { 0x05BE, 11}, { 0x09C8, 14}, { 0x0083,  8}, { 0x013A, 11},
{ 0x1721, 13}, { 0x0044,  9}, { 0x0276, 12}, { 0x39F6, 14}, { 0x008B, 10}, { 0x04EF, 13},
{ 0x5A9B, 15}, { 0x0208, 10}, { 0x1CFE, 13}, { 0x0399, 10}, { 0x1CB4, 13}, { 0x039E, 10},
{ 0x39F3, 14}, { 0x05AB, 11}, { 0x73E3, 15}, { 0x0737, 11}, { 0x5A9F, 15}, { 0x082D, 12},
{ 0x0E69, 12}, { 0x0E68, 12}, { 0x0433, 11}, { 0x0B7B, 12}, { 0x2DF8, 14}, { 0x2E56, 14},
{ 0x2E57, 14}, { 0x39F7, 14}, { 0x51A5, 15}, { 0x0003,  3}, { 0x002A,  6}, { 0x00E4,  8},
{ 0x028E, 10}, { 0x0735, 11}, { 0x1058, 13}, { 0x1CFA, 13}, { 0x2DF9, 14}, { 0x4174, 15},
{ 0x0009,  4}, { 0x0054,  8}, { 0x0398, 10}, { 0x048B, 13}, { 0x139D, 15}, { 0x000D,  4},
{ 0x00AD,  9}, { 0x0826, 12}, { 0x2D4C, 14}, { 0x0011,  5}, { 0x016B,  9}, { 0x0B7F, 12},
{ 0x51A4, 15}, { 0x0019,  5}, { 0x021B, 10}, { 0x16FD, 13}, { 0x001D,  5}, { 0x0394, 10},
{ 0x28D3, 14}, { 0x002B,  6}, { 0x05BC, 11}, { 0x5A9A, 15}, { 0x002F,  6}, { 0x0247, 12},
{ 0x0010,  7}, { 0x0A35, 12}, { 0x003E,  6}, { 0x0B7A, 12}, { 0x0059,  7}, { 0x105E, 13},
{ 0x0026,  8}, { 0x09CF, 14}, { 0x0055,  8}, { 0x1CB5, 13}, { 0x0057,  8}, { 0x0E5B, 12},
{ 0x00A0,  8}, { 0x1468, 13}, { 0x0170,  9}, { 0x0090, 10}, { 0x01CE,  9}, { 0x021A, 10},
{ 0x0218, 10}, { 0x0168,  9}, { 0x021E, 10}, { 0x0244, 12}, { 0x0736, 11}, { 0x0138, 11},
{ 0x0519, 11}, { 0x0E5E, 12}, { 0x072C, 11}, { 0x0B55, 12}, { 0x09DC, 14}, { 0x20BB, 14},
{ 0x048C, 13}, { 0x1723, 13}, { 0x2E44, 14}, { 0x16A5, 13}, { 0x0518, 11}, { 0x39FE, 14},
{ 0x0169,  9}
},
{
{ 0x0001,  2}, { 0x0006,  3}, { 0x000F,  4}, { 0x0016,  5}, { 0x0020,  6}, { 0x0018,  7},
{ 0x0008,  8}, { 0x009A,  8}, { 0x0056,  9}, { 0x013E,  9}, { 0x00F0, 10}, { 0x03A5, 10},
{ 0x0077, 11}, { 0x01EF, 11}, { 0x009A, 12}, { 0x005D, 13}, { 0x0001,  4}, { 0x0011,  5},
{ 0x0002,  7}, { 0x000B,  8}, { 0x0012,  9}, { 0x01D6,  9}, { 0x027E, 10}, { 0x0191, 11},
{ 0x00EA, 12}, { 0x03DC, 12}, { 0x013B, 13}, { 0x0004,  5}, { 0x0014,  7}, { 0x009E,  8},
{ 0x0009, 10}, { 0x01AC, 11}, { 0x01E2, 11}, { 0x03CA, 12}, { 0x005F, 13}, { 0x0017,  5},
{ 0x004E,  7}, { 0x005E,  9}, { 0x00F3, 10}, { 0x01AD, 11}, { 0x00EC, 12}, { 0x05F0, 13},
{ 0x000E,  6}, { 0x00E1,  8}, { 0x03A4, 10}, { 0x009C, 12}, { 0x013D, 13}, { 0x003B,  6},
{ 0x001C,  9}, { 0x0014, 11}, { 0x09BE, 12}, { 0x0006,  7}, { 0x007A,  9}, { 0x0190, 11},
{ 0x0137, 13}, { 0x001B,  7}, { 0x0008, 10}, { 0x075C, 11}, { 0x0071,  7}, { 0x00D7, 10},
{ 0x09BF, 12}, { 0x0007,  8}, { 0x00AF, 10}, { 0x04CC, 11}, { 0x0034,  8}, { 0x0265, 10},
{ 0x009F, 12}, { 0x00E0,  8}, { 0x0016, 11}, { 0x0327, 12}, { 0x0015,  9}, { 0x017D, 11},
{ 0x0EBB, 12}, { 0x0014,  9}, { 0x00F6, 10}, { 0x01E4, 11}, { 0x00CB, 10}, { 0x099D, 12},
{ 0x00CA, 10}, { 0x02FC, 12}, { 0x017F, 11}, { 0x04CD, 11}, { 0x02FD, 12}, { 0x04FE, 11},
{ 0x013A, 13}, { 0x000A,  4}, { 0x0042,  7}, { 0x01D3,  9}, { 0x04DD, 11}, { 0x0012,  5},
{ 0x00E8,  8}, { 0x004C, 11}, { 0x0136, 13}, { 0x0039,  6}, { 0x0264, 10}, { 0x0EBA, 12},
{ 0x0000,  7}, { 0x00AE, 10}, { 0x099C, 12}, { 0x001F,  7}, { 0x04DE, 11}, { 0x0043,  7},
{ 0x04DC, 11}, { 0x0003,  8}, { 0x03CB, 12}, { 0x0006,  8}, { 0x099E, 12}, { 0x002A,  8},
{ 0x05F1, 13}, { 0x000F,  8}, { 0x09FE, 12}, { 0x0033,  8}, { 0x09FF, 12}, { 0x0098,  8},
{ 0x099F, 12}, { 0x00EA,  8}, { 0x013C, 13}, { 0x002E,  8}, { 0x0192, 11}, { 0x0136,  9},
{ 0x006A,  9}, { 0x0015, 11}, { 0x03AF, 10}, { 0x01E3, 11}, { 0x0074, 11}, { 0x00EB, 12},
{ 0x02F9, 12}, { 0x005C, 13}, { 0x00ED, 12}, { 0x03DD, 12}, { 0x0326, 12}, { 0x005E, 13},
{ 0x0016,  7}
},
{
{ 0x0004,  3}, { 0x0014,  5}, { 0x0017,  7}, { 0x007F,  8}, { 0x0154,  9}, { 0x01F2, 10},
{ 0x00BF, 11}, { 0x0065, 12}, { 0x0AAA, 12}, { 0x0630, 13}, { 0x1597, 13}, { 0x03B7, 14},
{ 0x2B22, 14}, { 0x0BE6, 15}, { 0x000B,  4}, { 0x0037,  7}, { 0x0062,  9}, { 0x0007, 11},
{ 0x0166, 12}, { 0x00CE, 13}, { 0x1590, 13}, { 0x05F6, 14}, { 0x0BE7, 15}, { 0x0007,  5},
{ 0x006D,  8}, { 0x0003, 11}, { 0x031F, 12}, { 0x05F2, 14}, { 0x0002,  6}, { 0x0061,  9},
{ 0x0055, 12}, { 0x01DF, 14}, { 0x001A,  6}, { 0x001E, 10}, { 0x0AC9, 12}, { 0x2B23, 14},
{ 0x001E,  6}, { 0x001F, 10}, { 0x0AC3, 12}, { 0x2B2B, 14}, { 0x0006,  7}, { 0x0004, 11},
{ 0x02F8, 13}, { 0x0019,  7}, { 0x0006, 11}, { 0x063D, 13}, { 0x0057,  7}, { 0x0182, 11},
{ 0x2AA2, 14}, { 0x0004,  8}, { 0x0180, 11}, { 0x059C, 14}, { 0x007D,  8}, { 0x0164, 12},
{ 0x076D, 15}, { 0x0002,  9}, { 0x018D, 11}, { 0x1581, 13}, { 0x00AD,  8}, { 0x0060, 12},
{ 0x0C67, 14}, { 0x001C,  9}, { 0x00EE, 13}, { 0x0003,  9}, { 0x02CF, 13}, { 0x00D9,  9},
{ 0x1580, 13}, { 0x0002, 11}, { 0x0183, 11}, { 0x0057, 12}, { 0x0061, 12}, { 0x0031, 11},
{ 0x0066, 12}, { 0x0631, 13}, { 0x0632, 13}, { 0x00AC, 13}, { 0x031D, 12}, { 0x0076, 12},
{ 0x003A, 11}, { 0x0165, 12}, { 0x0C66, 14}, { 0x0003,  2}, { 0x0054,  7}, { 0x02AB, 10},
{ 0x0016, 13}, { 0x05F7, 14}, { 0x0005,  4}, { 0x00F8,  9}, { 0x0AA9, 12}, { 0x005F, 15},
{ 0x0004,  4}, { 0x001C, 10}, { 0x1550, 13}, { 0x0004,  5}, { 0x0077, 11}, { 0x076C, 15},
{ 0x000E,  5}, { 0x000A, 12}, { 0x000C,  5}, { 0x0562, 11}, { 0x0004,  6}, { 0x031C, 12},
{ 0x0006,  6}, { 0x00C8, 13}, { 0x000D,  6}, { 0x01DA, 13}, { 0x0007,  6}, { 0x00C9, 13},
{ 0x0001,  7}, { 0x002E, 14}, { 0x0014,  7}, { 0x1596, 13}, { 0x000A,  7}, { 0x0AC2, 12},
{ 0x0016,  7}, { 0x015B, 14}, { 0x0015,  7}, { 0x015A, 14}, { 0x000F,  8}, { 0x005E, 15},
{ 0x007E,  8}, { 0x00AB,  8}, { 0x002D,  9}, { 0x00D8,  9}, { 0x000B,  9}, { 0x0014, 10},
{ 0x02B3, 10}, { 0x01F3, 10}, { 0x003A, 10}, { 0x0000, 10}, { 0x0058, 10}, { 0x002E,  9},
{ 0x005E, 10}, { 0x0563, 11}, { 0x00EC, 12}, { 0x0054, 12}, { 0x0AC1, 12}, { 0x1556, 13},
{ 0x02FA, 13}, { 0x0181, 11}, { 0x1557, 13}, { 0x059D, 14}, { 0x2AA3, 14}, { 0x2B2A, 14},
{ 0x01DE, 14}, { 0x063C, 13}, { 0x00CF, 13}, { 0x1594, 13}, { 0x000D,  9}
},
{
{ 0x0002,  2}, { 0x0006,  3}, { 0x000F,  4}, { 0x000D,  5}, { 0x000C,  5}, { 0x0015,  6},
{ 0x0013,  6}, { 0x0012,  6}, { 0x0017,  7}, { 0x001F,  8}, { 0x001E,  8}, { 0x001D,  8},
{ 0x0025,  9}, { 0x0024,  9}, { 0x0023,  9}, { 0x0021,  9}, { 0x0021, 10}, { 0x0020, 10},
{ 0x000F, 10}, { 0x000E, 10}, { 0x0007, 11}, { 0x0006, 11}, { 0x0020, 11}, { 0x0021, 11},
{ 0x0050, 12}, { 0x0051, 12}, { 0x0052, 12}, { 0x000E,  4}, { 0x0014,  6}, { 0x0016,  7},
{ 0x001C,  8}, { 0x0020,  9}, { 0x001F,  9}, { 0x000D, 10}, { 0x0022, 11}, { 0x0053, 12},
{ 0x0055, 12}, { 0x000B,  5}, { 0x0015,  7}, { 0x001E,  9}, { 0x000C, 10}, { 0x0056, 12},
{ 0x0011,  6}, { 0x001B,  8}, { 0x001D,  9}, { 0x000B, 10}, { 0x0010,  6}, { 0x0022,  9},
{ 0x000A, 10}, { 0x000D,  6}, { 0x001C,  9}, { 0x0008, 10}, { 0x0012,  7}, { 0x001B,  9},
{ 0x0054, 12}, { 0x0014,  7}, { 0x001A,  9}, { 0x0057, 12}, { 0x0019,  8}, { 0x0009, 10},
{ 0x0018,  8}, { 0x0023, 11}, { 0x0017,  8}, { 0x0019,  9}, { 0x0018,  9}, { 0x0007, 10},
{ 0x0058, 12}, { 0x0007,  4}, { 0x000C,  6}, { 0x0016,  8}, { 0x0017,  9}, { 0x0006, 10},
{ 0x0005, 11}, { 0x0004, 11}, { 0x0059, 12}, { 0x000F,  6}, { 0x0016,  9}, { 0x0005, 10},
{ 0x000E,  6}, { 0x0004, 10}, { 0x0011,  7}, { 0x0024, 11}, { 0x0010,  7}, { 0x0025, 11},
{ 0x0013,  7}, { 0x005A, 12}, { 0x0015,  8}, { 0x005B, 12}, { 0x0014,  8}, { 0x0013,  8},
{ 0x001A,  8}, { 0x0015,  9}, { 0x0014,  9}, { 0x0013,  9}, { 0x0012,  9}, { 0x0011,  9},
{ 0x0026, 11}, { 0x0027, 11}, { 0x005C, 12}, { 0x005D, 12}, { 0x005E, 12}, { 0x005F, 12},
{ 0x0003,  7}
},
{
{ 0x0002,  2}, { 0x000F,  4}, { 0x0015,  6}, { 0x0017,  7}, { 0x001F,  8}, { 0x0025,  9},
{ 0x0024,  9}, { 0x0021, 10}, { 0x0020, 10}, { 0x0007, 11}, { 0x0006, 11}, { 0x0020, 11},
{ 0x0006,  3}, { 0x0014,  6}, { 0x001E,  8}, { 0x000F, 10}, { 0x0021, 11}, { 0x0050, 12},
{ 0x000E,  4}, { 0x001D,  8}, { 0x000E, 10}, { 0x0051, 12}, { 0x000D,  5}, { 0x0023,  9},
{ 0x000D, 10}, { 0x000C,  5}, { 0x0022,  9}, { 0x0052, 12}, { 0x000B,  5}, { 0x000C, 10},
{ 0x0053, 12}, { 0x0013,  6}, { 0x000B, 10}, { 0x0054, 12}, { 0x0012,  6}, { 0x000A, 10},
{ 0x0011,  6}, { 0x0009, 10}, { 0x0010,  6}, { 0x0008, 10}, { 0x0016,  7}, { 0x0055, 12},
{ 0x0015,  7}, { 0x0014,  7}, { 0x001C,  8}, { 0x001B,  8}, { 0x0021,  9}, { 0x0020,  9},
{ 0x001F,  9}, { 0x001E,  9}, { 0x001D,  9}, { 0x001C,  9}, { 0x001B,  9}, { 0x001A,  9},
{ 0x0022, 11}, { 0x0023, 11}, { 0x0056, 12}, { 0x0057, 12}, { 0x0007,  4}, { 0x0019,  9},
{ 0x0005, 11}, { 0x000F,  6}, { 0x0004, 11}, { 0x000E,  6}, { 0x000D,  6}, { 0x000C,  6},
{ 0x0013,  7}, { 0x0012,  7}, { 0x0011,  7}, { 0x0010,  7}, { 0x001A,  8}, { 0x0019,  8},
{ 0x0018,  8}, { 0x0017,  8}, { 0x0016,  8}, { 0x0015,  8}, { 0x0014,  8}, { 0x0013,  8},
{ 0x0018,  9}, { 0x0017,  9}, { 0x0016,  9}, { 0x0015,  9}, { 0x0014,  9}, { 0x0013,  9},
{ 0x0012,  9}, { 0x0011,  9}, { 0x0007, 10}, { 0x0006, 10}, { 0x0005, 10}, { 0x0004, 10},
{ 0x0024, 11}, { 0x0025, 11}, { 0x0026, 11}, { 0x0027, 11}, { 0x0058, 12}, { 0x0059, 12},
{ 0x005A, 12}, { 0x005B, 12}, { 0x005C, 12}, { 0x005D, 12}, { 0x005E, 12}, { 0x005F, 12},
{ 0x0003,  7}
},
{
{ 0x0000,  2}, { 0x0003,  3}, { 0x000D,  4}, { 0x0005,  4}, { 0x001C,  5}, { 0x0016,  5},
{ 0x003F,  6}, { 0x003A,  6}, { 0x002E,  6}, { 0x0022,  6}, { 0x007B,  7}, { 0x0067,  7},
{ 0x005F,  7}, { 0x0047,  7}, { 0x0026,  7}, { 0x00EF,  8}, { 0x00CD,  8}, { 0x00C1,  8},
{ 0x00A9,  8}, { 0x004F,  8}, { 0x01F2,  9}, { 0x01DD,  9}, { 0x0199,  9}, { 0x0185,  9},
{ 0x015D,  9}, { 0x011B,  9}, { 0x03EF, 10}, { 0x03E1, 10}, { 0x03C8, 10}, { 0x0331, 10},
{ 0x0303, 10}, { 0x02F1, 10}, { 0x02A0, 10}, { 0x0233, 10}, { 0x0126, 10}, { 0x07C0, 11},
{ 0x076F, 11}, { 0x076C, 11}, { 0x0661, 11}, { 0x0604, 11}, { 0x0572, 11}, { 0x0551, 11},
{ 0x046A, 11}, { 0x0274, 11}, { 0x0F27, 12}, { 0x0F24, 12}, { 0x0EDB, 12}, { 0x0C8E, 12},
{ 0x0C0B, 12}, { 0x0C0A, 12}, { 0x0AE3, 12}, { 0x08D6, 12}, { 0x0490, 12}, { 0x0495, 12},
{ 0x1F19, 13}, { 0x1DB5, 13}, { 0x0009,  4}, { 0x0010,  5}, { 0x0029,  6}, { 0x0062,  7},
{ 0x00F3,  8}, { 0x00AD,  8}, { 0x01E5,  9}, { 0x0179,  9}, { 0x009C,  9}, { 0x03B1, 10},
{ 0x02AE, 10}, { 0x0127, 10}, { 0x076E, 11}, { 0x0570, 11}, { 0x0275, 11}, { 0x0F25, 12},
{ 0x0EC0, 12}, { 0x0AA0, 12}, { 0x08D7, 12}, { 0x1E4C, 13}, { 0x0008,  5}, { 0x0063,  7},
{ 0x00AF,  8}, { 0x017B,  9}, { 0x03B3, 10}, { 0x07DD, 11}, { 0x0640, 11}, { 0x0F8D, 12},
{ 0x0BC1, 12}, { 0x0491, 12}, { 0x0028,  6}, { 0x00C3,  8}, { 0x0151,  9}, { 0x02A1, 10},
{ 0x0573, 11}, { 0x0EC3, 12}, { 0x1F35, 13}, { 0x0065,  7}, { 0x01DA,  9}, { 0x02AF, 10},
{ 0x0277, 11}, { 0x08C9, 12}, { 0x1781, 13}, { 0x0025,  7}, { 0x0118,  9}, { 0x0646, 11},
{ 0x0AA6, 12}, { 0x1780, 13}, { 0x00C9,  8}, { 0x0321, 10}, { 0x0F9B, 12}, { 0x191E, 13},
{ 0x0048,  8}, { 0x07CC, 11}, { 0x0AA1, 12}, { 0x0180,  9}, { 0x0465, 11}, { 0x1905, 13},
{ 0x03E2, 10}, { 0x0EC1, 12}, { 0x3C9B, 14}, { 0x02F4, 10}, { 0x08C8, 12}, { 0x07C1, 11},
{ 0x0928, 13}, { 0x05E1, 11}, { 0x320D, 14}, { 0x0EC2, 12}, { 0x6418, 15}, { 0x1F34, 13},
{ 0x0078,  7}, { 0x0155,  9}, { 0x0552, 11}, { 0x191F, 13}, { 0x00FA,  8}, { 0x07DC, 11},
{ 0x1907, 13}, { 0x00AC,  8}, { 0x0249, 11}, { 0x13B1, 14}, { 0x01F6,  9}, { 0x0AE2, 12},
{ 0x01DC,  9}, { 0x04ED, 12}, { 0x0184,  9}, { 0x1904, 13}, { 0x0156,  9}, { 0x09D9, 13},
{ 0x03E7, 10}, { 0x0929, 13}, { 0x03B2, 10}, { 0x3B68, 14}, { 0x02F5, 10}, { 0x13B0, 14},
{ 0x0322, 10}, { 0x3B69, 14}, { 0x0234, 10}, { 0x7935, 15}, { 0x07C7, 11}, { 0xC833, 16},
{ 0x0660, 11}, { 0x7934, 15}, { 0x024B, 11}, { 0xC832, 16}, { 0x0AA7, 12}, { 0x1F18, 13},
{ 0x007A,  7}
},
{
{ 0x0002,  2}, { 0x0000,  3}, { 0x001E,  5}, { 0x0004,  5}, { 0x0012,  6}, { 0x0070,  7},
{ 0x001A,  7}, { 0x005F,  8}, { 0x0047,  8}, { 0x01D3,  9}, { 0x00B5,  9}, { 0x0057,  9},
{ 0x03B5, 10}, { 0x016D, 10}, { 0x0162, 10}, { 0x07CE, 11}, { 0x0719, 11}, { 0x0691, 11},
{ 0x02C6, 11}, { 0x0156, 11}, { 0x0F92, 12}, { 0x0D2E, 12}, { 0x0D20, 12}, { 0x059E, 12},
{ 0x0468, 12}, { 0x02A6, 12}, { 0x1DA2, 13}, { 0x1C60, 13}, { 0x1A43, 13}, { 0x0B1D, 13},
{ 0x08C0, 13}, { 0x055D, 13}, { 0x0003,  3}, { 0x000A,  5}, { 0x0077,  7}, { 0x00E5,  8},
{ 0x01D9,  9}, { 0x03E5, 10}, { 0x0166, 10}, { 0x0694, 11}, { 0x0152, 11}, { 0x059F, 12},
{ 0x1F3C, 13}, { 0x1A4B, 13}, { 0x055E, 13}, { 0x000C,  4}, { 0x007D,  7}, { 0x0044,  8},
{ 0x03E0, 10}, { 0x0769, 11}, { 0x0E31, 12}, { 0x1F26, 13}, { 0x055C, 13}, { 0x001B,  5},
{ 0x00E2,  8}, { 0x03A5, 10}, { 0x02C9, 11}, { 0x1F23, 13}, { 0x3B47, 14}, { 0x0007,  5},
{ 0x01D8,  9}, { 0x02D8, 11}, { 0x1F27, 13}, { 0x3494, 14}, { 0x0035,  6}, { 0x03E1, 10},
{ 0x059C, 12}, { 0x38C3, 14}, { 0x000C,  6}, { 0x0165, 10}, { 0x1D23, 13}, { 0x1638, 14},
{ 0x0068,  7}, { 0x0693, 11}, { 0x3A45, 14}, { 0x0020,  7}, { 0x0F90, 12}, { 0x7CF6, 15},
{ 0x00E8,  8}, { 0x058F, 12}, { 0x2CEF, 15}, { 0x0045,  8}, { 0x0B3A, 13}, { 0x01F1,  9},
{ 0x3B46, 14}, { 0x01A7,  9}, { 0x1676, 14}, { 0x0056,  9}, { 0x692A, 15}, { 0x038D, 10},
{ 0xE309, 16}, { 0x00AA, 10}, { 0x1C611, 17}, { 0x02DF, 11}, { 0xB3B9, 17}, { 0x02C8, 11},
{ 0x38C20, 18}, { 0x01B0, 11}, { 0x16390, 18}, { 0x0F9F, 12}, { 0x16771, 18}, { 0x0ED0, 12},
{ 0x71843, 19}, { 0x0D2A, 12}, { 0xF9E8C, 20}, { 0x0461, 12}, { 0xF9E8E, 20}, { 0x0B67, 13},
{ 0x055F, 13}, { 0x003F,  6}, { 0x006D,  9}, { 0x0E90, 12}, { 0x054E, 13}, { 0x0013,  6},
{ 0x0119, 10}, { 0x0B66, 13}, { 0x000B,  6}, { 0x0235, 11}, { 0x7CF5, 15}, { 0x0075,  7},
{ 0x0D24, 12}, { 0xF9E9, 16}, { 0x002E,  7}, { 0x1F22, 13}, { 0x0021,  7}, { 0x054F, 13},
{ 0x0014,  7}, { 0x3A44, 14}, { 0x00E4,  8}, { 0x7CF7, 15}, { 0x005E,  8}, { 0x7185, 15},
{ 0x0037,  8}, { 0x2C73, 15}, { 0x01DB,  9}, { 0x59DD, 16}, { 0x01C7,  9}, { 0x692B, 15},
{ 0x01A6,  9}, { 0x58E5, 16}, { 0x00B4,  9}, { 0x1F3D0, 17}, { 0x00B0,  9}, { 0xB1C9, 17},
{ 0x03E6, 10}, { 0x16770, 18}, { 0x016E, 10}, { 0x3E7A2, 18}, { 0x011B, 10}, { 0xF9E8D, 20},
{ 0x00D9, 10}, { 0xF9E8F, 20}, { 0x00A8, 10}, { 0x2C723, 19}, { 0x0749, 11}, { 0xE3084, 20},
{ 0x0696, 11}, { 0x58E45, 20}, { 0x02DE, 11}, { 0xB1C88, 21}, { 0x0231, 11}, { 0x1C610A, 21},
{ 0x01B1, 11}, { 0x71842D, 23}, { 0x0D2B, 12}, { 0x38C217, 22}, { 0x0D2F, 12}, { 0x163913, 22},
{ 0x05B2, 12}, { 0x163912, 22}, { 0x0469, 12}, { 0x71842C, 23}, { 0x1A42, 13}, { 0x08C1, 13},
{ 0x0073,  7}
}
};

static const uint16_t vlc_offs[] = {
        0,   520,   552,   616,  1128,  1160,  1224,  1740,  1772,  1836,  1900,  2436,
     2986,  3050,  3610,  4154,  4218,  4746,  5326,  5390,  5902,  6554,  7658,  8342,
     9304,  9988, 10630, 11234, 12174, 13006, 13560, 14232, 14786, 15432, 16350, 17522,
    20372, 21818, 22330, 22394, 23166, 23678, 23742, 24820, 25332, 25396, 26460, 26980,
    27048, 27592, 27600, 27608, 27616, 27624, 28224, 28258, 28290, 28802, 28834, 28866,
    29378, 29412, 29444, 29960, 29994, 30026, 30538, 30572, 30604, 31120, 31154, 31186,
    31714, 31746, 31778, 32306, 32340, 32372
};

/**
 * Init VC-1 specific tables and VC1Context members
 * @param v The VC1Context to initialize
 * @return Status
 */
av_cold int ff_vc1_init_common(VC1Context *v)
{
    static int done = 0;
    int i = 0;
    static VLC_TYPE vlc_table[32372][2];

    v->hrd_rate = v->hrd_buffer = NULL;

    /* VLC tables */
    if (!done) {
        INIT_VLC_STATIC(&ff_vc1_bfraction_vlc, VC1_BFRACTION_VLC_BITS, 23,
                        ff_vc1_bfraction_bits, 1, 1,
                        ff_vc1_bfraction_codes, 1, 1, 1 << VC1_BFRACTION_VLC_BITS);
        INIT_VLC_STATIC(&ff_vc1_norm2_vlc, VC1_NORM2_VLC_BITS, 4,
                        ff_vc1_norm2_bits, 1, 1,
                        ff_vc1_norm2_codes, 1, 1, 1 << VC1_NORM2_VLC_BITS);
        INIT_VLC_STATIC(&ff_vc1_norm6_vlc, VC1_NORM6_VLC_BITS, 64,
                        ff_vc1_norm6_bits, 1, 1,
                        ff_vc1_norm6_codes, 2, 2, 556);
        INIT_VLC_STATIC(&ff_vc1_imode_vlc, VC1_IMODE_VLC_BITS, 7,
                        ff_vc1_imode_bits, 1, 1,
                        ff_vc1_imode_codes, 1, 1, 1 << VC1_IMODE_VLC_BITS);
        for (i = 0; i < 3; i++) {
            ff_vc1_ttmb_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 0]];
            ff_vc1_ttmb_vlc[i].table_allocated = vlc_offs[i * 3 + 1] - vlc_offs[i * 3 + 0];
            init_vlc(&ff_vc1_ttmb_vlc[i], VC1_TTMB_VLC_BITS, 16,
                     ff_vc1_ttmb_bits[i], 1, 1,
                     ff_vc1_ttmb_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
            ff_vc1_ttblk_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 1]];
            ff_vc1_ttblk_vlc[i].table_allocated = vlc_offs[i * 3 + 2] - vlc_offs[i * 3 + 1];
            init_vlc(&ff_vc1_ttblk_vlc[i], VC1_TTBLK_VLC_BITS, 8,
                     ff_vc1_ttblk_bits[i], 1, 1,
                     ff_vc1_ttblk_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
            ff_vc1_subblkpat_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 2]];
            ff_vc1_subblkpat_vlc[i].table_allocated = vlc_offs[i * 3 + 3] - vlc_offs[i * 3 + 2];
            init_vlc(&ff_vc1_subblkpat_vlc[i], VC1_SUBBLKPAT_VLC_BITS, 15,
                     ff_vc1_subblkpat_bits[i], 1, 1,
                     ff_vc1_subblkpat_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
        }
        for (i = 0; i < 4; i++) {
            ff_vc1_4mv_block_pattern_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 9]];
            ff_vc1_4mv_block_pattern_vlc[i].table_allocated = vlc_offs[i * 3 + 10] - vlc_offs[i * 3 + 9];
            init_vlc(&ff_vc1_4mv_block_pattern_vlc[i], VC1_4MV_BLOCK_PATTERN_VLC_BITS, 16,
                     ff_vc1_4mv_block_pattern_bits[i], 1, 1,
                     ff_vc1_4mv_block_pattern_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
            ff_vc1_cbpcy_p_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 10]];
            ff_vc1_cbpcy_p_vlc[i].table_allocated = vlc_offs[i * 3 + 11] - vlc_offs[i * 3 + 10];
            init_vlc(&ff_vc1_cbpcy_p_vlc[i], VC1_CBPCY_P_VLC_BITS, 64,
                     ff_vc1_cbpcy_p_bits[i], 1, 1,
                     ff_vc1_cbpcy_p_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
            ff_vc1_mv_diff_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 11]];
            ff_vc1_mv_diff_vlc[i].table_allocated = vlc_offs[i * 3 + 12] - vlc_offs[i * 3 + 11];
            init_vlc(&ff_vc1_mv_diff_vlc[i], VC1_MV_DIFF_VLC_BITS, 73,
                     ff_vc1_mv_diff_bits[i], 1, 1,
                     ff_vc1_mv_diff_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
        }
        for (i = 0; i < 8; i++) {
            ff_vc1_ac_coeff_table[i].table           = &vlc_table[vlc_offs[i * 2 + 21]];
            ff_vc1_ac_coeff_table[i].table_allocated = vlc_offs[i * 2 + 22] - vlc_offs[i * 2 + 21];
            init_vlc(&ff_vc1_ac_coeff_table[i], AC_VLC_BITS, ff_vc1_ac_sizes[i],
                     &vc1_ac_tables[i][0][1], 8, 4,
                     &vc1_ac_tables[i][0][0], 8, 4, INIT_VLC_USE_NEW_STATIC);
            /* initialize interlaced MVDATA tables (2-Ref) */
            ff_vc1_2ref_mvdata_vlc[i].table           = &vlc_table[vlc_offs[i * 2 + 22]];
            ff_vc1_2ref_mvdata_vlc[i].table_allocated = vlc_offs[i * 2 + 23] - vlc_offs[i * 2 + 22];
            init_vlc(&ff_vc1_2ref_mvdata_vlc[i], VC1_2REF_MVDATA_VLC_BITS, 126,
                     ff_vc1_2ref_mvdata_bits[i], 1, 1,
                     ff_vc1_2ref_mvdata_codes[i], 4, 4, INIT_VLC_USE_NEW_STATIC);
        }
        for (i = 0; i < 4; i++) {
            /* initialize 4MV MBMODE VLC tables for interlaced frame P picture */
            ff_vc1_intfr_4mv_mbmode_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 37]];
            ff_vc1_intfr_4mv_mbmode_vlc[i].table_allocated = vlc_offs[i * 3 + 38] - vlc_offs[i * 3 + 37];
            init_vlc(&ff_vc1_intfr_4mv_mbmode_vlc[i], VC1_INTFR_4MV_MBMODE_VLC_BITS, 15,
                     ff_vc1_intfr_4mv_mbmode_bits[i], 1, 1,
                     ff_vc1_intfr_4mv_mbmode_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
            /* initialize NON-4MV MBMODE VLC tables for the same */
            ff_vc1_intfr_non4mv_mbmode_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 38]];
            ff_vc1_intfr_non4mv_mbmode_vlc[i].table_allocated = vlc_offs[i * 3 + 39] - vlc_offs[i * 3 + 38];
            init_vlc(&ff_vc1_intfr_non4mv_mbmode_vlc[i], VC1_INTFR_NON4MV_MBMODE_VLC_BITS, 9,
                     ff_vc1_intfr_non4mv_mbmode_bits[i], 1, 1,
                     ff_vc1_intfr_non4mv_mbmode_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
            /* initialize interlaced MVDATA tables (1-Ref) */
            ff_vc1_1ref_mvdata_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 39]];
            ff_vc1_1ref_mvdata_vlc[i].table_allocated = vlc_offs[i * 3 + 40] - vlc_offs[i * 3 + 39];
            init_vlc(&ff_vc1_1ref_mvdata_vlc[i], VC1_1REF_MVDATA_VLC_BITS, 72,
                     ff_vc1_1ref_mvdata_bits[i], 1, 1,
                     ff_vc1_1ref_mvdata_codes[i], 4, 4, INIT_VLC_USE_NEW_STATIC);
        }
        for (i = 0; i < 4; i++) {
            /* Initialize 2MV Block pattern VLC tables */
            ff_vc1_2mv_block_pattern_vlc[i].table           = &vlc_table[vlc_offs[i + 49]];
            ff_vc1_2mv_block_pattern_vlc[i].table_allocated = vlc_offs[i + 50] - vlc_offs[i + 49];
            init_vlc(&ff_vc1_2mv_block_pattern_vlc[i], VC1_2MV_BLOCK_PATTERN_VLC_BITS, 4,
                     ff_vc1_2mv_block_pattern_bits[i], 1, 1,
                     ff_vc1_2mv_block_pattern_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
        }
        for (i = 0; i < 8; i++) {
            /* Initialize interlaced CBPCY VLC tables (Table 124 - Table 131) */
            ff_vc1_icbpcy_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 53]];
            ff_vc1_icbpcy_vlc[i].table_allocated = vlc_offs[i * 3 + 54] - vlc_offs[i * 3 + 53];
            init_vlc(&ff_vc1_icbpcy_vlc[i], VC1_ICBPCY_VLC_BITS, 63,
                     ff_vc1_icbpcy_p_bits[i], 1, 1,
                     ff_vc1_icbpcy_p_codes[i], 2, 2, INIT_VLC_USE_NEW_STATIC);
            /* Initialize interlaced field picture MBMODE VLC tables */
            ff_vc1_if_mmv_mbmode_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 54]];
            ff_vc1_if_mmv_mbmode_vlc[i].table_allocated = vlc_offs[i * 3 + 55] - vlc_offs[i * 3 + 54];
            init_vlc(&ff_vc1_if_mmv_mbmode_vlc[i], VC1_IF_MMV_MBMODE_VLC_BITS, 8,
                     ff_vc1_if_mmv_mbmode_bits[i], 1, 1,
                     ff_vc1_if_mmv_mbmode_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
            ff_vc1_if_1mv_mbmode_vlc[i].table           = &vlc_table[vlc_offs[i * 3 + 55]];
            ff_vc1_if_1mv_mbmode_vlc[i].table_allocated = vlc_offs[i * 3 + 56] - vlc_offs[i * 3 + 55];
            init_vlc(&ff_vc1_if_1mv_mbmode_vlc[i], VC1_IF_1MV_MBMODE_VLC_BITS, 6,
                     ff_vc1_if_1mv_mbmode_bits[i], 1, 1,
                     ff_vc1_if_1mv_mbmode_codes[i], 1, 1, INIT_VLC_USE_NEW_STATIC);
        }
        done = 1;
    }

    /* Other defaults */
    v->pq      = -1;
    v->mvrange = 0; /* 7.1.1.18, p80 */

    ff_vc1dsp_init(&v->vc1dsp);

    return 0;
}
