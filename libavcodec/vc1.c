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

#include "avcodec.h"
#include "decode.h"
#include "mpegvideo.h"
#include "vc1.h"
#include "vc1data.h"
#include "wmv2data.h"
#include "unary.h"

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
    imode = get_vlc2(gb, ff_vc1_imode_vlc, VC1_IMODE_VLC_BITS, 1);

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
            code = get_vlc2(gb, ff_vc1_norm2_vlc, VC1_NORM2_VLC_BITS, 1);
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
                    code = get_vlc2(gb, ff_vc1_norm6_vlc, VC1_NORM6_VLC_BITS, 2);
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
                    code = get_vlc2(gb, ff_vc1_norm6_vlc, VC1_NORM6_VLC_BITS, 2);
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

#if FF_API_TICKS_PER_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    if (v->broadcast) { // Pulldown may be present
        v->s.avctx->ticks_per_frame = 2;
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

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
            if (v->s.avctx->width  > v->max_coded_width ||
                v->s.avctx->height > v->max_coded_height) {
                avpriv_request_sample(v->s.avctx, "Huge resolution");
            } else
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
    int bfraction_lut_index = get_bits(gb, 3);

    if (bfraction_lut_index == 7)
        bfraction_lut_index = 7 + get_bits(gb, 4);

    if (bfraction_lut_index == 21) {
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
    v->fcm = PROGRESSIVE;
    if (v->finterpflag)
        v->interpfrm = get_bits1(gb);
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

    if (get_bits_left(gb) < 5)
        return AVERROR_INVALIDDATA;
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

        if (get_bits_left(gb) < 4)
            return AVERROR_INVALIDDATA;

        /* Hopefully this is correct for P-frames */
        v->s.mv_table_index = get_bits(gb, 2); //but using ff_vc1_ tables
        v->cbptab = get_bits(gb, 2);
        v->cbpcy_vlc = ff_vc1_cbpcy_p_vlc[v->cbptab];

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
        v->cbptab           = get_bits(gb, 2);
        v->cbpcy_vlc        = ff_vc1_cbpcy_p_vlc[v->cbptab];

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
    if (v->field_mode) {
        if (!v->refdist_flag)
            v->refdist = 0;
        else if ((v->s.pict_type != AV_PICTURE_TYPE_B) && (v->s.pict_type != AV_PICTURE_TYPE_BI)) {
            v->refdist = get_bits(gb, 2);
            if (v->refdist == 3)
                v->refdist += get_unary(gb, 0, 14);
            if (v->refdist > 16)
                return AVERROR_INVALIDDATA;
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
    v->dquantfrm = 0;
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
        } else
            v->fieldtx_is_raw = 0;
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
                if (status < 0)
                    return -1;
                av_log(v->s.avctx, AV_LOG_DEBUG, "SKIPMB plane encoding: "
                       "Imode: %i, Invert: %i\n", status>>1, status&1);
                v->mbmodetab = get_bits(gb, 2);
                if (v->fourmvswitch)
                    v->mbmode_vlc = ff_vc1_intfr_4mv_mbmode_vlc[v->mbmodetab];
                else
                    v->mbmode_vlc = ff_vc1_intfr_non4mv_mbmode_vlc[v->mbmodetab];
                v->imvtab      = get_bits(gb, 2);
                v->imv_vlc     = ff_vc1_1ref_mvdata_vlc[v->imvtab];
                // interlaced p-picture cbpcy range is [1, 63]
                v->icbptab     = get_bits(gb, 3);
                v->cbpcy_vlc   = ff_vc1_icbpcy_vlc[v->icbptab];
                v->twomvbptab     = get_bits(gb, 2);
                v->twomvbp_vlc = ff_vc1_2mv_block_pattern_vlc[v->twomvbptab];
                if (v->fourmvswitch) {
                    v->fourmvbptab     = get_bits(gb, 2);
                    v->fourmvbp_vlc = ff_vc1_4mv_block_pattern_vlc[v->fourmvbptab];
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
            v->cbptab           = get_bits(gb, 2);
            v->cbpcy_vlc        = ff_vc1_cbpcy_p_vlc[v->cbptab];
        } else if (v->fcm == ILACE_FRAME) { // frame interlaced
            v->qs_last          = v->s.quarter_sample;
            v->s.quarter_sample = 1;
            v->s.mspel          = 1;
        } else {    // field interlaced
            v->mbmodetab = get_bits(gb, 3);
            v->imvtab = get_bits(gb, 2 + v->numref);
            if (!v->numref)
                v->imv_vlc = ff_vc1_1ref_mvdata_vlc[v->imvtab];
            else
                v->imv_vlc = ff_vc1_2ref_mvdata_vlc[v->imvtab];
            v->icbptab = get_bits(gb, 3);
            v->cbpcy_vlc = ff_vc1_icbpcy_vlc[v->icbptab];
            if ((v->mv_mode == MV_PMODE_INTENSITY_COMP &&
                v->mv_mode2 == MV_PMODE_MIXED_MV) || v->mv_mode == MV_PMODE_MIXED_MV) {
                v->fourmvbptab     = get_bits(gb, 2);
                v->fourmvbp_vlc = ff_vc1_4mv_block_pattern_vlc[v->fourmvbptab];
                v->mbmode_vlc   = ff_vc1_if_mmv_mbmode_vlc[v->mbmodetab];
            } else {
                v->mbmode_vlc   = ff_vc1_if_1mv_mbmode_vlc[v->mbmodetab];
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
            v->s.mspel          = (v->mv_mode != MV_PMODE_1MV_HPEL_BILIN);
            status = bitplane_decoding(v->forward_mb_plane, &v->fmb_is_raw, v);
            if (status < 0)
                return -1;
            av_log(v->s.avctx, AV_LOG_DEBUG, "MB Forward Type plane encoding: "
                   "Imode: %i, Invert: %i\n", status>>1, status&1);
            v->mbmodetab = get_bits(gb, 3);
            if (v->mv_mode == MV_PMODE_MIXED_MV)
                v->mbmode_vlc = ff_vc1_if_mmv_mbmode_vlc[v->mbmodetab];
            else
                v->mbmode_vlc = ff_vc1_if_1mv_mbmode_vlc[v->mbmodetab];
            v->imvtab     = get_bits(gb, 3);
            v->imv_vlc   = ff_vc1_2ref_mvdata_vlc[v->imvtab];
            v->icbptab   = get_bits(gb, 3);
            v->cbpcy_vlc = ff_vc1_icbpcy_vlc[v->icbptab];
            if (v->mv_mode == MV_PMODE_MIXED_MV) {
                v->fourmvbptab     = get_bits(gb, 2);
                v->fourmvbp_vlc = ff_vc1_4mv_block_pattern_vlc[v->fourmvbptab];
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
            v->mbmodetab       = get_bits(gb, 2);
            v->mbmode_vlc   = ff_vc1_intfr_non4mv_mbmode_vlc[v->mbmodetab];
            v->imvtab       = get_bits(gb, 2);
            v->imv_vlc      = ff_vc1_1ref_mvdata_vlc[v->imvtab];
            // interlaced p/b-picture cbpcy range is [1, 63]
            v->icbptab      = get_bits(gb, 3);
            v->cbpcy_vlc    = ff_vc1_icbpcy_vlc[v->icbptab];
            v->twomvbptab      = get_bits(gb, 2);
            v->twomvbp_vlc  = ff_vc1_2mv_block_pattern_vlc[v->twomvbptab];
            v->fourmvbptab     = get_bits(gb, 2);
            v->fourmvbp_vlc = ff_vc1_4mv_block_pattern_vlc[v->fourmvbptab];
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
            v->cbptab = get_bits(gb, 2);
            v->cbpcy_vlc = ff_vc1_cbpcy_p_vlc[v->cbptab];
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


    /* AC Syntax */
    v->c_ac_table_index = decode012(gb);
    if (v->s.pict_type == AV_PICTURE_TYPE_I || v->s.pict_type == AV_PICTURE_TYPE_BI) {
        v->y_ac_table_index = decode012(gb);
    }
    else if (v->fcm != PROGRESSIVE && !v->s.quarter_sample) {
        v->range_x <<= 1;
        v->range_y <<= 1;
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
