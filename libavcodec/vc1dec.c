/*
 * VC-1 and WMV3 decoder
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
 * VC-1 and WMV3 decoder
 */

#include "avcodec.h"
#include "blockdsp.h"
#include "get_bits.h"
#include "hwaccel.h"
#include "internal.h"
#include "mpeg_er.h"
#include "mpegvideo.h"
#include "msmpeg4.h"
#include "msmpeg4data.h"
#include "profiles.h"
#include "vc1.h"
#include "vc1data.h"
#include "libavutil/avassert.h"


#if CONFIG_WMV3IMAGE_DECODER || CONFIG_VC1IMAGE_DECODER

typedef struct SpriteData {
    /**
     * Transform coefficients for both sprites in 16.16 fixed point format,
     * in the order they appear in the bitstream:
     *  x scale
     *  rotation 1 (unused)
     *  x offset
     *  rotation 2 (unused)
     *  y scale
     *  y offset
     *  alpha
     */
    int coefs[2][7];

    int effect_type, effect_flag;
    int effect_pcount1, effect_pcount2;   ///< amount of effect parameters stored in effect_params
    int effect_params1[15], effect_params2[10]; ///< effect parameters in 16.16 fixed point format
} SpriteData;

static inline int get_fp_val(GetBitContext* gb)
{
    return (get_bits_long(gb, 30) - (1 << 29)) << 1;
}

static void vc1_sprite_parse_transform(GetBitContext* gb, int c[7])
{
    c[1] = c[3] = 0;

    switch (get_bits(gb, 2)) {
    case 0:
        c[0] = 1 << 16;
        c[2] = get_fp_val(gb);
        c[4] = 1 << 16;
        break;
    case 1:
        c[0] = c[4] = get_fp_val(gb);
        c[2] = get_fp_val(gb);
        break;
    case 2:
        c[0] = get_fp_val(gb);
        c[2] = get_fp_val(gb);
        c[4] = get_fp_val(gb);
        break;
    case 3:
        c[0] = get_fp_val(gb);
        c[1] = get_fp_val(gb);
        c[2] = get_fp_val(gb);
        c[3] = get_fp_val(gb);
        c[4] = get_fp_val(gb);
        break;
    }
    c[5] = get_fp_val(gb);
    if (get_bits1(gb))
        c[6] = get_fp_val(gb);
    else
        c[6] = 1 << 16;
}

static int vc1_parse_sprites(VC1Context *v, GetBitContext* gb, SpriteData* sd)
{
    AVCodecContext *avctx = v->s.avctx;
    int sprite, i;

    for (sprite = 0; sprite <= v->two_sprites; sprite++) {
        vc1_sprite_parse_transform(gb, sd->coefs[sprite]);
        if (sd->coefs[sprite][1] || sd->coefs[sprite][3])
            avpriv_request_sample(avctx, "Non-zero rotation coefficients");
        av_log(avctx, AV_LOG_DEBUG, sprite ? "S2:" : "S1:");
        for (i = 0; i < 7; i++)
            av_log(avctx, AV_LOG_DEBUG, " %d.%.3d",
                   sd->coefs[sprite][i] / (1<<16),
                   (abs(sd->coefs[sprite][i]) & 0xFFFF) * 1000 / (1 << 16));
        av_log(avctx, AV_LOG_DEBUG, "\n");
    }

    skip_bits(gb, 2);
    if (sd->effect_type = get_bits_long(gb, 30)) {
        switch (sd->effect_pcount1 = get_bits(gb, 4)) {
        case 7:
            vc1_sprite_parse_transform(gb, sd->effect_params1);
            break;
        case 14:
            vc1_sprite_parse_transform(gb, sd->effect_params1);
            vc1_sprite_parse_transform(gb, sd->effect_params1 + 7);
            break;
        default:
            for (i = 0; i < sd->effect_pcount1; i++)
                sd->effect_params1[i] = get_fp_val(gb);
        }
        if (sd->effect_type != 13 || sd->effect_params1[0] != sd->coefs[0][6]) {
            // effect 13 is simple alpha blending and matches the opacity above
            av_log(avctx, AV_LOG_DEBUG, "Effect: %d; params: ", sd->effect_type);
            for (i = 0; i < sd->effect_pcount1; i++)
                av_log(avctx, AV_LOG_DEBUG, " %d.%.2d",
                       sd->effect_params1[i] / (1 << 16),
                       (abs(sd->effect_params1[i]) & 0xFFFF) * 1000 / (1 << 16));
            av_log(avctx, AV_LOG_DEBUG, "\n");
        }

        sd->effect_pcount2 = get_bits(gb, 16);
        if (sd->effect_pcount2 > 10) {
            av_log(avctx, AV_LOG_ERROR, "Too many effect parameters\n");
            return AVERROR_INVALIDDATA;
        } else if (sd->effect_pcount2) {
            i = -1;
            av_log(avctx, AV_LOG_DEBUG, "Effect params 2: ");
            while (++i < sd->effect_pcount2) {
                sd->effect_params2[i] = get_fp_val(gb);
                av_log(avctx, AV_LOG_DEBUG, " %d.%.2d",
                       sd->effect_params2[i] / (1 << 16),
                       (abs(sd->effect_params2[i]) & 0xFFFF) * 1000 / (1 << 16));
            }
            av_log(avctx, AV_LOG_DEBUG, "\n");
        }
    }
    if (sd->effect_flag = get_bits1(gb))
        av_log(avctx, AV_LOG_DEBUG, "Effect flag set\n");

    if (get_bits_count(gb) >= gb->size_in_bits +
       (avctx->codec_id == AV_CODEC_ID_WMV3IMAGE ? 64 : 0)) {
        av_log(avctx, AV_LOG_ERROR, "Buffer overrun\n");
        return AVERROR_INVALIDDATA;
    }
    if (get_bits_count(gb) < gb->size_in_bits - 8)
        av_log(avctx, AV_LOG_WARNING, "Buffer not fully read\n");

    return 0;
}

static void vc1_draw_sprites(VC1Context *v, SpriteData* sd)
{
    int i, plane, row, sprite;
    int sr_cache[2][2] = { { -1, -1 }, { -1, -1 } };
    uint8_t* src_h[2][2];
    int xoff[2], xadv[2], yoff[2], yadv[2], alpha;
    int ysub[2];
    MpegEncContext *s = &v->s;

    for (i = 0; i <= v->two_sprites; i++) {
        xoff[i] = av_clip(sd->coefs[i][2], 0, v->sprite_width-1 << 16);
        xadv[i] = sd->coefs[i][0];
        if (xadv[i] != 1<<16 || (v->sprite_width << 16) - (v->output_width << 16) - xoff[i])
            xadv[i] = av_clip(xadv[i], 0, ((v->sprite_width<<16) - xoff[i] - 1) / v->output_width);

        yoff[i] = av_clip(sd->coefs[i][5], 0, v->sprite_height-1 << 16);
        yadv[i] = av_clip(sd->coefs[i][4], 0, ((v->sprite_height << 16) - yoff[i]) / v->output_height);
    }
    alpha = av_clip_uint16(sd->coefs[1][6]);

    for (plane = 0; plane < (CONFIG_GRAY && s->avctx->flags & AV_CODEC_FLAG_GRAY ? 1 : 3); plane++) {
        int width = v->output_width>>!!plane;

        for (row = 0; row < v->output_height>>!!plane; row++) {
            uint8_t *dst = v->sprite_output_frame->data[plane] +
                           v->sprite_output_frame->linesize[plane] * row;

            for (sprite = 0; sprite <= v->two_sprites; sprite++) {
                uint8_t *iplane = s->current_picture.f->data[plane];
                int      iline  = s->current_picture.f->linesize[plane];
                int      ycoord = yoff[sprite] + yadv[sprite] * row;
                int      yline  = ycoord >> 16;
                int      next_line;
                ysub[sprite] = ycoord & 0xFFFF;
                if (sprite) {
                    iplane = s->last_picture.f->data[plane];
                    iline  = s->last_picture.f->linesize[plane];
                }
                next_line = FFMIN(yline + 1, (v->sprite_height >> !!plane) - 1) * iline;
                if (!(xoff[sprite] & 0xFFFF) && xadv[sprite] == 1 << 16) {
                        src_h[sprite][0] = iplane + (xoff[sprite] >> 16) +  yline      * iline;
                    if (ysub[sprite])
                        src_h[sprite][1] = iplane + (xoff[sprite] >> 16) + next_line;
                } else {
                    if (sr_cache[sprite][0] != yline) {
                        if (sr_cache[sprite][1] == yline) {
                            FFSWAP(uint8_t*, v->sr_rows[sprite][0], v->sr_rows[sprite][1]);
                            FFSWAP(int,        sr_cache[sprite][0],   sr_cache[sprite][1]);
                        } else {
                            v->vc1dsp.sprite_h(v->sr_rows[sprite][0], iplane + yline * iline, xoff[sprite], xadv[sprite], width);
                            sr_cache[sprite][0] = yline;
                        }
                    }
                    if (ysub[sprite] && sr_cache[sprite][1] != yline + 1) {
                        v->vc1dsp.sprite_h(v->sr_rows[sprite][1],
                                           iplane + next_line, xoff[sprite],
                                           xadv[sprite], width);
                        sr_cache[sprite][1] = yline + 1;
                    }
                    src_h[sprite][0] = v->sr_rows[sprite][0];
                    src_h[sprite][1] = v->sr_rows[sprite][1];
                }
            }

            if (!v->two_sprites) {
                if (ysub[0]) {
                    v->vc1dsp.sprite_v_single(dst, src_h[0][0], src_h[0][1], ysub[0], width);
                } else {
                    memcpy(dst, src_h[0][0], width);
                }
            } else {
                if (ysub[0] && ysub[1]) {
                    v->vc1dsp.sprite_v_double_twoscale(dst, src_h[0][0], src_h[0][1], ysub[0],
                                                       src_h[1][0], src_h[1][1], ysub[1], alpha, width);
                } else if (ysub[0]) {
                    v->vc1dsp.sprite_v_double_onescale(dst, src_h[0][0], src_h[0][1], ysub[0],
                                                       src_h[1][0], alpha, width);
                } else if (ysub[1]) {
                    v->vc1dsp.sprite_v_double_onescale(dst, src_h[1][0], src_h[1][1], ysub[1],
                                                       src_h[0][0], (1<<16)-1-alpha, width);
                } else {
                    v->vc1dsp.sprite_v_double_noscale(dst, src_h[0][0], src_h[1][0], alpha, width);
                }
            }
        }

        if (!plane) {
            for (i = 0; i <= v->two_sprites; i++) {
                xoff[i] >>= 1;
                yoff[i] >>= 1;
            }
        }

    }
}


static int vc1_decode_sprites(VC1Context *v, GetBitContext* gb)
{
    int ret;
    MpegEncContext *s     = &v->s;
    AVCodecContext *avctx = s->avctx;
    SpriteData sd;

    memset(&sd, 0, sizeof(sd));

    ret = vc1_parse_sprites(v, gb, &sd);
    if (ret < 0)
        return ret;

    if (!s->current_picture.f || !s->current_picture.f->data[0]) {
        av_log(avctx, AV_LOG_ERROR, "Got no sprites\n");
        return AVERROR_UNKNOWN;
    }

    if (v->two_sprites && (!s->last_picture_ptr || !s->last_picture.f->data[0])) {
        av_log(avctx, AV_LOG_WARNING, "Need two sprites, only got one\n");
        v->two_sprites = 0;
    }

    av_frame_unref(v->sprite_output_frame);
    if ((ret = ff_get_buffer(avctx, v->sprite_output_frame, 0)) < 0)
        return ret;

    vc1_draw_sprites(v, &sd);

    return 0;
}

static void vc1_sprite_flush(AVCodecContext *avctx)
{
    VC1Context *v     = avctx->priv_data;
    MpegEncContext *s = &v->s;
    AVFrame *f = s->current_picture.f;
    int plane, i;

    /* Windows Media Image codecs have a convergence interval of two keyframes.
       Since we can't enforce it, clear to black the missing sprite. This is
       wrong but it looks better than doing nothing. */

    if (f && f->data[0])
        for (plane = 0; plane < (CONFIG_GRAY && s->avctx->flags & AV_CODEC_FLAG_GRAY ? 1 : 3); plane++)
            for (i = 0; i < v->sprite_height>>!!plane; i++)
                memset(f->data[plane] + i * f->linesize[plane],
                       plane ? 128 : 0, f->linesize[plane]);
}

#endif

av_cold int ff_vc1_decode_init_alloc_tables(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    int i, ret = AVERROR(ENOMEM);
    int mb_height = FFALIGN(s->mb_height, 2);

    /* Allocate mb bitplanes */
    v->mv_type_mb_plane = av_malloc (s->mb_stride * mb_height);
    v->direct_mb_plane  = av_malloc (s->mb_stride * mb_height);
    v->forward_mb_plane = av_malloc (s->mb_stride * mb_height);
    v->fieldtx_plane    = av_mallocz(s->mb_stride * mb_height);
    v->acpred_plane     = av_malloc (s->mb_stride * mb_height);
    v->over_flags_plane = av_malloc (s->mb_stride * mb_height);
    if (!v->mv_type_mb_plane || !v->direct_mb_plane || !v->forward_mb_plane ||
        !v->fieldtx_plane || !v->acpred_plane || !v->over_flags_plane)
        goto error;

    v->n_allocated_blks = s->mb_width + 2;
    v->block            = av_malloc(sizeof(*v->block) * v->n_allocated_blks);
    v->cbp_base         = av_malloc(sizeof(v->cbp_base[0]) * 3 * s->mb_stride);
    if (!v->block || !v->cbp_base)
        goto error;
    v->cbp              = v->cbp_base + 2 * s->mb_stride;
    v->ttblk_base       = av_malloc(sizeof(v->ttblk_base[0]) * 3 * s->mb_stride);
    if (!v->ttblk_base)
        goto error;
    v->ttblk            = v->ttblk_base + 2 * s->mb_stride;
    v->is_intra_base    = av_mallocz(sizeof(v->is_intra_base[0]) * 3 * s->mb_stride);
    if (!v->is_intra_base)
        goto error;
    v->is_intra         = v->is_intra_base + 2 * s->mb_stride;
    v->luma_mv_base     = av_mallocz(sizeof(v->luma_mv_base[0]) * 3 * s->mb_stride);
    if (!v->luma_mv_base)
        goto error;
    v->luma_mv          = v->luma_mv_base + 2 * s->mb_stride;

    /* allocate block type info in that way so it could be used with s->block_index[] */
    v->mb_type_base = av_malloc(s->b8_stride * (mb_height * 2 + 1) + s->mb_stride * (mb_height + 1) * 2);
    if (!v->mb_type_base)
        goto error;
    v->mb_type[0]   = v->mb_type_base + s->b8_stride + 1;
    v->mb_type[1]   = v->mb_type_base + s->b8_stride * (mb_height * 2 + 1) + s->mb_stride + 1;
    v->mb_type[2]   = v->mb_type[1] + s->mb_stride * (mb_height + 1);

    /* allocate memory to store block level MV info */
    v->blk_mv_type_base = av_mallocz(     s->b8_stride * (mb_height * 2 + 1) + s->mb_stride * (mb_height + 1) * 2);
    if (!v->blk_mv_type_base)
        goto error;
    v->blk_mv_type      = v->blk_mv_type_base + s->b8_stride + 1;
    v->mv_f_base        = av_mallocz(2 * (s->b8_stride * (mb_height * 2 + 1) + s->mb_stride * (mb_height + 1) * 2));
    if (!v->mv_f_base)
        goto error;
    v->mv_f[0]          = v->mv_f_base + s->b8_stride + 1;
    v->mv_f[1]          = v->mv_f[0] + (s->b8_stride * (mb_height * 2 + 1) + s->mb_stride * (mb_height + 1) * 2);
    v->mv_f_next_base   = av_mallocz(2 * (s->b8_stride * (mb_height * 2 + 1) + s->mb_stride * (mb_height + 1) * 2));
    if (!v->mv_f_next_base)
        goto error;
    v->mv_f_next[0]     = v->mv_f_next_base + s->b8_stride + 1;
    v->mv_f_next[1]     = v->mv_f_next[0] + (s->b8_stride * (mb_height * 2 + 1) + s->mb_stride * (mb_height + 1) * 2);

    if (s->avctx->codec_id == AV_CODEC_ID_WMV3IMAGE || s->avctx->codec_id == AV_CODEC_ID_VC1IMAGE) {
        for (i = 0; i < 4; i++)
            if (!(v->sr_rows[i >> 1][i & 1] = av_malloc(v->output_width)))
                return AVERROR(ENOMEM);
    }

    ret = ff_intrax8_common_init(s->avctx, &v->x8, &s->idsp,
                                 s->block, s->block_last_index,
                                 s->mb_width, s->mb_height);
    if (ret < 0)
        goto error;

    return 0;

error:
    ff_vc1_decode_end(s->avctx);
    return ret;
}

av_cold void ff_vc1_init_transposed_scantables(VC1Context *v)
{
    int i;
    for (i = 0; i < 64; i++) {
#define transpose(x) (((x) >> 3) | (((x) & 7) << 3))
        v->zz_8x8[0][i] = transpose(ff_wmv1_scantable[0][i]);
        v->zz_8x8[1][i] = transpose(ff_wmv1_scantable[1][i]);
        v->zz_8x8[2][i] = transpose(ff_wmv1_scantable[2][i]);
        v->zz_8x8[3][i] = transpose(ff_wmv1_scantable[3][i]);
        v->zzi_8x8[i]   = transpose(ff_vc1_adv_interlaced_8x8_zz[i]);
    }
    v->left_blk_sh = 0;
    v->top_blk_sh  = 3;
}

/** Initialize a VC1/WMV3 decoder
 * @todo TODO: Handle VC-1 IDUs (Transport level?)
 * @todo TODO: Decipher remaining bits in extra_data
 */
static av_cold int vc1_decode_init(AVCodecContext *avctx)
{
    VC1Context *v = avctx->priv_data;
    MpegEncContext *s = &v->s;
    GetBitContext gb;
    int ret;

    /* save the container output size for WMImage */
    v->output_width  = avctx->width;
    v->output_height = avctx->height;

    if (!avctx->extradata_size || !avctx->extradata)
        return AVERROR_INVALIDDATA;
    v->s.avctx = avctx;

    if ((ret = ff_vc1_init_common(v)) < 0)
        return ret;

    if (avctx->codec_id == AV_CODEC_ID_WMV3 || avctx->codec_id == AV_CODEC_ID_WMV3IMAGE) {
        int count = 0;

        // looks like WMV3 has a sequence header stored in the extradata
        // advanced sequence header may be before the first frame
        // the last byte of the extradata is a version number, 1 for the
        // samples we can decode

        init_get_bits(&gb, avctx->extradata, avctx->extradata_size*8);

        if ((ret = ff_vc1_decode_sequence_header(avctx, v, &gb)) < 0)
          return ret;

        if (avctx->codec_id == AV_CODEC_ID_WMV3IMAGE && !v->res_sprite) {
            avpriv_request_sample(avctx, "Non sprite WMV3IMAGE");
            return AVERROR_PATCHWELCOME;
        }

        count = avctx->extradata_size*8 - get_bits_count(&gb);
        if (count > 0) {
            av_log(avctx, AV_LOG_INFO, "Extra data: %i bits left, value: %X\n",
                   count, get_bits_long(&gb, FFMIN(count, 32)));
        } else if (count < 0) {
            av_log(avctx, AV_LOG_INFO, "Read %i bits in overflow\n", -count);
        }
    } else { // VC1/WVC1/WVP2
        const uint8_t *start = avctx->extradata;
        uint8_t *end = avctx->extradata + avctx->extradata_size;
        const uint8_t *next;
        int size, buf2_size;
        uint8_t *buf2 = NULL;
        int seq_initialized = 0, ep_initialized = 0;

        if (avctx->extradata_size < 16) {
            av_log(avctx, AV_LOG_ERROR, "Extradata size too small: %i\n", avctx->extradata_size);
            return AVERROR_INVALIDDATA;
        }

        buf2  = av_mallocz(avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!buf2)
            return AVERROR(ENOMEM);

        start = find_next_marker(start, end); // in WVC1 extradata first byte is its size, but can be 0 in mkv
        next  = start;
        for (; next < end; start = next) {
            next = find_next_marker(start + 4, end);
            size = next - start - 4;
            if (size <= 0)
                continue;
            buf2_size = vc1_unescape_buffer(start + 4, size, buf2);
            init_get_bits(&gb, buf2, buf2_size * 8);
            switch (AV_RB32(start)) {
            case VC1_CODE_SEQHDR:
                if ((ret = ff_vc1_decode_sequence_header(avctx, v, &gb)) < 0) {
                    av_free(buf2);
                    return ret;
                }
                seq_initialized = 1;
                break;
            case VC1_CODE_ENTRYPOINT:
                if ((ret = ff_vc1_decode_entry_point(avctx, v, &gb)) < 0) {
                    av_free(buf2);
                    return ret;
                }
                ep_initialized = 1;
                break;
            }
        }
        av_free(buf2);
        if (!seq_initialized || !ep_initialized) {
            av_log(avctx, AV_LOG_ERROR, "Incomplete extradata\n");
            return AVERROR_INVALIDDATA;
        }
        v->res_sprite = (avctx->codec_id == AV_CODEC_ID_VC1IMAGE);
    }

    avctx->profile = v->profile;
    if (v->profile == PROFILE_ADVANCED)
        avctx->level = v->level;

    if (!CONFIG_GRAY || !(avctx->flags & AV_CODEC_FLAG_GRAY))
        avctx->pix_fmt = ff_get_format(avctx, avctx->codec->pix_fmts);
    else {
        avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        if (avctx->color_range == AVCOL_RANGE_UNSPECIFIED)
            avctx->color_range = AVCOL_RANGE_MPEG;
    }

    // ensure static VLC tables are initialized
    if ((ret = ff_msmpeg4_decode_init(avctx)) < 0)
        return ret;
    if ((ret = ff_vc1_decode_init_alloc_tables(v)) < 0)
        return ret;
    // Hack to ensure the above functions will be called
    // again once we know all necessary settings.
    // That this is necessary might indicate a bug.
    ff_vc1_decode_end(avctx);

    ff_blockdsp_init(&s->bdsp, avctx);
    ff_h264chroma_init(&v->h264chroma, 8);
    ff_qpeldsp_init(&s->qdsp);

    // Must happen after calling ff_vc1_decode_end
    // to avoid de-allocating the sprite_output_frame
    v->sprite_output_frame = av_frame_alloc();
    if (!v->sprite_output_frame)
        return AVERROR(ENOMEM);

    avctx->has_b_frames = !!avctx->max_b_frames;

    if (v->color_prim == 1 || v->color_prim == 5 || v->color_prim == 6)
        avctx->color_primaries = v->color_prim;
    if (v->transfer_char == 1 || v->transfer_char == 7)
        avctx->color_trc = v->transfer_char;
    if (v->matrix_coef == 1 || v->matrix_coef == 6 || v->matrix_coef == 7)
        avctx->colorspace = v->matrix_coef;

    s->mb_width  = (avctx->coded_width  + 15) >> 4;
    s->mb_height = (avctx->coded_height + 15) >> 4;

    if (v->profile == PROFILE_ADVANCED || v->res_fasttx) {
        ff_vc1_init_transposed_scantables(v);
    } else {
        memcpy(v->zz_8x8, ff_wmv1_scantable, 4*64);
        v->left_blk_sh = 3;
        v->top_blk_sh  = 0;
    }

    if (avctx->codec_id == AV_CODEC_ID_WMV3IMAGE || avctx->codec_id == AV_CODEC_ID_VC1IMAGE) {
        v->sprite_width  = avctx->coded_width;
        v->sprite_height = avctx->coded_height;

        avctx->coded_width  = avctx->width  = v->output_width;
        avctx->coded_height = avctx->height = v->output_height;

        // prevent 16.16 overflows
        if (v->sprite_width  > 1 << 14 ||
            v->sprite_height > 1 << 14 ||
            v->output_width  > 1 << 14 ||
            v->output_height > 1 << 14) {
            ret = AVERROR_INVALIDDATA;
            goto error;
        }

        if ((v->sprite_width&1) || (v->sprite_height&1)) {
            avpriv_request_sample(avctx, "odd sprites support");
            ret = AVERROR_PATCHWELCOME;
            goto error;
        }
    }
    return 0;
error:
    av_frame_free(&v->sprite_output_frame);
    return ret;
}

/** Close a VC1/WMV3 decoder
 * @warning Initial try at using MpegEncContext stuff
 */
av_cold int ff_vc1_decode_end(AVCodecContext *avctx)
{
    VC1Context *v = avctx->priv_data;
    int i;

    av_frame_free(&v->sprite_output_frame);

    for (i = 0; i < 4; i++)
        av_freep(&v->sr_rows[i >> 1][i & 1]);
    av_freep(&v->hrd_rate);
    av_freep(&v->hrd_buffer);
    ff_mpv_common_end(&v->s);
    av_freep(&v->mv_type_mb_plane);
    av_freep(&v->direct_mb_plane);
    av_freep(&v->forward_mb_plane);
    av_freep(&v->fieldtx_plane);
    av_freep(&v->acpred_plane);
    av_freep(&v->over_flags_plane);
    av_freep(&v->mb_type_base);
    av_freep(&v->blk_mv_type_base);
    av_freep(&v->mv_f_base);
    av_freep(&v->mv_f_next_base);
    av_freep(&v->block);
    av_freep(&v->cbp_base);
    av_freep(&v->ttblk_base);
    av_freep(&v->is_intra_base); // FIXME use v->mb_type[]
    av_freep(&v->luma_mv_base);
    ff_intrax8_common_end(&v->x8);
    return 0;
}


/** Decode a VC1/WMV3 frame
 * @todo TODO: Handle VC-1 IDUs (Transport level?)
 */
static int vc1_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size, n_slices = 0, i, ret;
    VC1Context *v = avctx->priv_data;
    MpegEncContext *s = &v->s;
    AVFrame *pict = data;
    uint8_t *buf2 = NULL;
    const uint8_t *buf_start = buf, *buf_start_second_field = NULL;
    int mb_height, n_slices1=-1;
    struct {
        uint8_t *buf;
        GetBitContext gb;
        int mby_start;
        const uint8_t *rawbuf;
        int raw_size;
    } *slices = NULL, *tmp;

    v->second_field = 0;

    if(s->avctx->flags & AV_CODEC_FLAG_LOW_DELAY)
        s->low_delay = 1;

    /* no supplementary picture */
    if (buf_size == 0 || (buf_size == 4 && AV_RB32(buf) == VC1_CODE_ENDOFSEQ)) {
        /* special case for last picture */
        if (s->low_delay == 0 && s->next_picture_ptr) {
            if ((ret = av_frame_ref(pict, s->next_picture_ptr->f)) < 0)
                return ret;
            s->next_picture_ptr = NULL;

            *got_frame = 1;
        }

        return buf_size;
    }

    //for advanced profile we may need to parse and unescape data
    if (avctx->codec_id == AV_CODEC_ID_VC1 || avctx->codec_id == AV_CODEC_ID_VC1IMAGE) {
        int buf_size2 = 0;
        buf2 = av_mallocz(buf_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!buf2)
            return AVERROR(ENOMEM);

        if (IS_MARKER(AV_RB32(buf))) { /* frame starts with marker and needs to be parsed */
            const uint8_t *start, *end, *next;
            int size;

            next = buf;
            for (start = buf, end = buf + buf_size; next < end; start = next) {
                next = find_next_marker(start + 4, end);
                size = next - start - 4;
                if (size <= 0) continue;
                switch (AV_RB32(start)) {
                case VC1_CODE_FRAME:
                    if (avctx->hwaccel)
                        buf_start = start;
                    buf_size2 = vc1_unescape_buffer(start + 4, size, buf2);
                    break;
                case VC1_CODE_FIELD: {
                    int buf_size3;
                    if (avctx->hwaccel)
                        buf_start_second_field = start;
                    tmp = av_realloc_array(slices, sizeof(*slices), n_slices+1);
                    if (!tmp) {
                        ret = AVERROR(ENOMEM);
                        goto err;
                    }
                    slices = tmp;
                    slices[n_slices].buf = av_mallocz(buf_size + AV_INPUT_BUFFER_PADDING_SIZE);
                    if (!slices[n_slices].buf) {
                        ret = AVERROR(ENOMEM);
                        goto err;
                    }
                    buf_size3 = vc1_unescape_buffer(start + 4, size,
                                                    slices[n_slices].buf);
                    init_get_bits(&slices[n_slices].gb, slices[n_slices].buf,
                                  buf_size3 << 3);
                    slices[n_slices].mby_start = avctx->coded_height + 31 >> 5;
                    slices[n_slices].rawbuf = start;
                    slices[n_slices].raw_size = size + 4;
                    n_slices1 = n_slices - 1; // index of the last slice of the first field
                    n_slices++;
                    break;
                }
                case VC1_CODE_ENTRYPOINT: /* it should be before frame data */
                    buf_size2 = vc1_unescape_buffer(start + 4, size, buf2);
                    init_get_bits(&s->gb, buf2, buf_size2 * 8);
                    ff_vc1_decode_entry_point(avctx, v, &s->gb);
                    break;
                case VC1_CODE_SLICE: {
                    int buf_size3;
                    tmp = av_realloc_array(slices, sizeof(*slices), n_slices+1);
                    if (!tmp) {
                        ret = AVERROR(ENOMEM);
                        goto err;
                    }
                    slices = tmp;
                    slices[n_slices].buf = av_mallocz(buf_size + AV_INPUT_BUFFER_PADDING_SIZE);
                    if (!slices[n_slices].buf) {
                        ret = AVERROR(ENOMEM);
                        goto err;
                    }
                    buf_size3 = vc1_unescape_buffer(start + 4, size,
                                                    slices[n_slices].buf);
                    init_get_bits(&slices[n_slices].gb, slices[n_slices].buf,
                                  buf_size3 << 3);
                    slices[n_slices].mby_start = get_bits(&slices[n_slices].gb, 9);
                    slices[n_slices].rawbuf = start;
                    slices[n_slices].raw_size = size + 4;
                    n_slices++;
                    break;
                }
                }
            }
        } else if (v->interlace && ((buf[0] & 0xC0) == 0xC0)) { /* WVC1 interlaced stores both fields divided by marker */
            const uint8_t *divider;
            int buf_size3;

            divider = find_next_marker(buf, buf + buf_size);
            if ((divider == (buf + buf_size)) || AV_RB32(divider) != VC1_CODE_FIELD) {
                av_log(avctx, AV_LOG_ERROR, "Error in WVC1 interlaced frame\n");
                ret = AVERROR_INVALIDDATA;
                goto err;
            } else { // found field marker, unescape second field
                if (avctx->hwaccel)
                    buf_start_second_field = divider;
                tmp = av_realloc_array(slices, sizeof(*slices), n_slices+1);
                if (!tmp) {
                    ret = AVERROR(ENOMEM);
                    goto err;
                }
                slices = tmp;
                slices[n_slices].buf = av_mallocz(buf_size + AV_INPUT_BUFFER_PADDING_SIZE);
                if (!slices[n_slices].buf) {
                    ret = AVERROR(ENOMEM);
                    goto err;
                }
                buf_size3 = vc1_unescape_buffer(divider + 4, buf + buf_size - divider - 4, slices[n_slices].buf);
                init_get_bits(&slices[n_slices].gb, slices[n_slices].buf,
                              buf_size3 << 3);
                slices[n_slices].mby_start = s->mb_height + 1 >> 1;
                slices[n_slices].rawbuf = divider;
                slices[n_slices].raw_size = buf + buf_size - divider;
                n_slices1 = n_slices - 1;
                n_slices++;
            }
            buf_size2 = vc1_unescape_buffer(buf, divider - buf, buf2);
        } else {
            buf_size2 = vc1_unescape_buffer(buf, buf_size, buf2);
        }
        init_get_bits(&s->gb, buf2, buf_size2*8);
    } else
        init_get_bits(&s->gb, buf, buf_size*8);

    if (v->res_sprite) {
        v->new_sprite  = !get_bits1(&s->gb);
        v->two_sprites =  get_bits1(&s->gb);
        /* res_sprite means a Windows Media Image stream, AV_CODEC_ID_*IMAGE means
           we're using the sprite compositor. These are intentionally kept separate
           so you can get the raw sprites by using the wmv3 decoder for WMVP or
           the vc1 one for WVP2 */
        if (avctx->codec_id == AV_CODEC_ID_WMV3IMAGE || avctx->codec_id == AV_CODEC_ID_VC1IMAGE) {
            if (v->new_sprite) {
                // switch AVCodecContext parameters to those of the sprites
                avctx->width  = avctx->coded_width  = v->sprite_width;
                avctx->height = avctx->coded_height = v->sprite_height;
            } else {
                goto image;
            }
        }
    }

    if (s->context_initialized &&
        (s->width  != avctx->coded_width ||
         s->height != avctx->coded_height)) {
        ff_vc1_decode_end(avctx);
    }

    if (!s->context_initialized) {
        if ((ret = ff_msmpeg4_decode_init(avctx)) < 0)
            goto err;
        if ((ret = ff_vc1_decode_init_alloc_tables(v)) < 0) {
            ff_mpv_common_end(s);
            goto err;
        }

        s->low_delay = !avctx->has_b_frames || v->res_sprite;

        if (v->profile == PROFILE_ADVANCED) {
            if(avctx->coded_width<=1 || avctx->coded_height<=1) {
                ret = AVERROR_INVALIDDATA;
                goto err;
            }
            s->h_edge_pos = avctx->coded_width;
            s->v_edge_pos = avctx->coded_height;
        }
    }

    // do parse frame header
    v->pic_header_flag = 0;
    v->first_pic_header_flag = 1;
    if (v->profile < PROFILE_ADVANCED) {
        if ((ret = ff_vc1_parse_frame_header(v, &s->gb)) < 0) {
            goto err;
        }
    } else {
        if ((ret = ff_vc1_parse_frame_header_adv(v, &s->gb)) < 0) {
            goto err;
        }
    }
    v->first_pic_header_flag = 0;

    if (avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(v->s.avctx, AV_LOG_DEBUG, "pict_type: %c\n", av_get_picture_type_char(s->pict_type));

    if ((avctx->codec_id == AV_CODEC_ID_WMV3IMAGE || avctx->codec_id == AV_CODEC_ID_VC1IMAGE)
        && s->pict_type != AV_PICTURE_TYPE_I) {
        av_log(v->s.avctx, AV_LOG_ERROR, "Sprite decoder: expected I-frame\n");
        ret = AVERROR_INVALIDDATA;
        goto err;
    }

    if ((s->mb_height >> v->field_mode) == 0) {
        av_log(v->s.avctx, AV_LOG_ERROR, "image too short\n");
        ret = AVERROR_INVALIDDATA;
        goto err;
    }

    // for skipping the frame
    s->current_picture.f->pict_type = s->pict_type;
    s->current_picture.f->key_frame = s->pict_type == AV_PICTURE_TYPE_I;

    /* skip B-frames if we don't have reference frames */
    if (!s->last_picture_ptr && (s->pict_type == AV_PICTURE_TYPE_B || s->droppable)) {
        av_log(v->s.avctx, AV_LOG_DEBUG, "Skipping B frame without reference frames\n");
        goto end;
    }
    if ((avctx->skip_frame >= AVDISCARD_NONREF && s->pict_type == AV_PICTURE_TYPE_B) ||
        (avctx->skip_frame >= AVDISCARD_NONKEY && s->pict_type != AV_PICTURE_TYPE_I) ||
         avctx->skip_frame >= AVDISCARD_ALL) {
        goto end;
    }

    if (s->next_p_frame_damaged) {
        if (s->pict_type == AV_PICTURE_TYPE_B)
            goto end;
        else
            s->next_p_frame_damaged = 0;
    }

    if ((ret = ff_mpv_frame_start(s, avctx)) < 0) {
        goto err;
    }

    v->s.current_picture_ptr->field_picture = v->field_mode;
    v->s.current_picture_ptr->f->interlaced_frame = (v->fcm != PROGRESSIVE);
    v->s.current_picture_ptr->f->top_field_first  = v->tff;

    // process pulldown flags
    s->current_picture_ptr->f->repeat_pict = 0;
    // Pulldown flags are only valid when 'broadcast' has been set.
    // So ticks_per_frame will be 2
    if (v->rff) {
        // repeat field
        s->current_picture_ptr->f->repeat_pict = 1;
    } else if (v->rptfrm) {
        // repeat frames
        s->current_picture_ptr->f->repeat_pict = v->rptfrm * 2;
    }

    s->me.qpel_put = s->qdsp.put_qpel_pixels_tab;
    s->me.qpel_avg = s->qdsp.avg_qpel_pixels_tab;

    if (avctx->hwaccel) {
        s->mb_y = 0;
        if (v->field_mode && buf_start_second_field) {
            // decode first field
            s->picture_structure = PICT_BOTTOM_FIELD - v->tff;
            if ((ret = avctx->hwaccel->start_frame(avctx, buf_start, buf_start_second_field - buf_start)) < 0)
                goto err;

            if (n_slices1 == -1) {
                // no slices, decode the field as-is
                if ((ret = avctx->hwaccel->decode_slice(avctx, buf_start, buf_start_second_field - buf_start)) < 0)
                    goto err;
            } else {
                if ((ret = avctx->hwaccel->decode_slice(avctx, buf_start, slices[0].rawbuf - buf_start)) < 0)
                    goto err;

                for (i = 0 ; i < n_slices1 + 1; i++) {
                    s->gb = slices[i].gb;
                    s->mb_y = slices[i].mby_start;

                    v->pic_header_flag = get_bits1(&s->gb);
                    if (v->pic_header_flag) {
                        if (ff_vc1_parse_frame_header_adv(v, &s->gb) < 0) {
                            av_log(v->s.avctx, AV_LOG_ERROR, "Slice header damaged\n");
                            ret = AVERROR_INVALIDDATA;
                            if (avctx->err_recognition & AV_EF_EXPLODE)
                                goto err;
                            continue;
                        }
                    }

                    if ((ret = avctx->hwaccel->decode_slice(avctx, slices[i].rawbuf, slices[i].raw_size)) < 0)
                        goto err;
                }
            }

            if ((ret = avctx->hwaccel->end_frame(avctx)) < 0)
                goto err;

            // decode second field
            s->gb = slices[n_slices1 + 1].gb;
            s->mb_y = slices[n_slices1 + 1].mby_start;
            s->picture_structure = PICT_TOP_FIELD + v->tff;
            v->second_field = 1;
            v->pic_header_flag = 0;
            if (ff_vc1_parse_frame_header_adv(v, &s->gb) < 0) {
                av_log(avctx, AV_LOG_ERROR, "parsing header for second field failed");
                ret = AVERROR_INVALIDDATA;
                goto err;
            }
            v->s.current_picture_ptr->f->pict_type = v->s.pict_type;

            if ((ret = avctx->hwaccel->start_frame(avctx, buf_start_second_field, (buf + buf_size) - buf_start_second_field)) < 0)
                goto err;

            if (n_slices - n_slices1 == 2) {
                // no slices, decode the field as-is
                if ((ret = avctx->hwaccel->decode_slice(avctx, buf_start_second_field, (buf + buf_size) - buf_start_second_field)) < 0)
                    goto err;
            } else {
                if ((ret = avctx->hwaccel->decode_slice(avctx, buf_start_second_field, slices[n_slices1 + 2].rawbuf - buf_start_second_field)) < 0)
                    goto err;

                for (i = n_slices1 + 2; i < n_slices; i++) {
                    s->gb = slices[i].gb;
                    s->mb_y = slices[i].mby_start;

                    v->pic_header_flag = get_bits1(&s->gb);
                    if (v->pic_header_flag) {
                        if (ff_vc1_parse_frame_header_adv(v, &s->gb) < 0) {
                            av_log(v->s.avctx, AV_LOG_ERROR, "Slice header damaged\n");
                            ret = AVERROR_INVALIDDATA;
                            if (avctx->err_recognition & AV_EF_EXPLODE)
                                goto err;
                            continue;
                        }
                    }

                    if ((ret = avctx->hwaccel->decode_slice(avctx, slices[i].rawbuf, slices[i].raw_size)) < 0)
                        goto err;
                }
            }

            if ((ret = avctx->hwaccel->end_frame(avctx)) < 0)
                goto err;
        } else {
            s->picture_structure = PICT_FRAME;
            if ((ret = avctx->hwaccel->start_frame(avctx, buf_start, (buf + buf_size) - buf_start)) < 0)
                goto err;

            if (n_slices == 0) {
                // no slices, decode the frame as-is
                if ((ret = avctx->hwaccel->decode_slice(avctx, buf_start, (buf + buf_size) - buf_start)) < 0)
                    goto err;
            } else {
                // decode the frame part as the first slice
                if ((ret = avctx->hwaccel->decode_slice(avctx, buf_start, slices[0].rawbuf - buf_start)) < 0)
                    goto err;

                // and process the slices as additional slices afterwards
                for (i = 0 ; i < n_slices; i++) {
                    s->gb = slices[i].gb;
                    s->mb_y = slices[i].mby_start;

                    v->pic_header_flag = get_bits1(&s->gb);
                    if (v->pic_header_flag) {
                        if (ff_vc1_parse_frame_header_adv(v, &s->gb) < 0) {
                            av_log(v->s.avctx, AV_LOG_ERROR, "Slice header damaged\n");
                            ret = AVERROR_INVALIDDATA;
                            if (avctx->err_recognition & AV_EF_EXPLODE)
                                goto err;
                            continue;
                        }
                    }

                    if ((ret = avctx->hwaccel->decode_slice(avctx, slices[i].rawbuf, slices[i].raw_size)) < 0)
                        goto err;
                }
            }
            if ((ret = avctx->hwaccel->end_frame(avctx)) < 0)
                goto err;
        }
    } else {
        int header_ret = 0;

        ff_mpeg_er_frame_start(s);

        v->bits = buf_size * 8;
        v->end_mb_x = s->mb_width;
        if (v->field_mode) {
            s->current_picture.f->linesize[0] <<= 1;
            s->current_picture.f->linesize[1] <<= 1;
            s->current_picture.f->linesize[2] <<= 1;
            s->linesize                      <<= 1;
            s->uvlinesize                    <<= 1;
        }
        mb_height = s->mb_height >> v->field_mode;

        av_assert0 (mb_height > 0);

        for (i = 0; i <= n_slices; i++) {
            if (i > 0 &&  slices[i - 1].mby_start >= mb_height) {
                if (v->field_mode <= 0) {
                    av_log(v->s.avctx, AV_LOG_ERROR, "Slice %d starts beyond "
                           "picture boundary (%d >= %d)\n", i,
                           slices[i - 1].mby_start, mb_height);
                    continue;
                }
                v->second_field = 1;
                av_assert0((s->mb_height & 1) == 0);
                v->blocks_off   = s->b8_stride * (s->mb_height&~1);
                v->mb_off       = s->mb_stride * s->mb_height >> 1;
            } else {
                v->second_field = 0;
                v->blocks_off   = 0;
                v->mb_off       = 0;
            }
            if (i) {
                v->pic_header_flag = 0;
                if (v->field_mode && i == n_slices1 + 2) {
                    if ((header_ret = ff_vc1_parse_frame_header_adv(v, &s->gb)) < 0) {
                        av_log(v->s.avctx, AV_LOG_ERROR, "Field header damaged\n");
                        ret = AVERROR_INVALIDDATA;
                        if (avctx->err_recognition & AV_EF_EXPLODE)
                            goto err;
                        continue;
                    }
                } else if (get_bits1(&s->gb)) {
                    v->pic_header_flag = 1;
                    if ((header_ret = ff_vc1_parse_frame_header_adv(v, &s->gb)) < 0) {
                        av_log(v->s.avctx, AV_LOG_ERROR, "Slice header damaged\n");
                        ret = AVERROR_INVALIDDATA;
                        if (avctx->err_recognition & AV_EF_EXPLODE)
                            goto err;
                        continue;
                    }
                }
            }
            if (header_ret < 0)
                continue;
            s->start_mb_y = (i == 0) ? 0 : FFMAX(0, slices[i-1].mby_start % mb_height);
            if (!v->field_mode || v->second_field)
                s->end_mb_y = (i == n_slices     ) ? mb_height : FFMIN(mb_height, slices[i].mby_start % mb_height);
            else {
                if (i >= n_slices) {
                    av_log(v->s.avctx, AV_LOG_ERROR, "first field slice count too large\n");
                    continue;
                }
                s->end_mb_y = (i == n_slices1 + 1) ? mb_height : FFMIN(mb_height, slices[i].mby_start % mb_height);
            }
            if (s->end_mb_y <= s->start_mb_y) {
                av_log(v->s.avctx, AV_LOG_ERROR, "end mb y %d %d invalid\n", s->end_mb_y, s->start_mb_y);
                continue;
            }
            if (((s->pict_type == AV_PICTURE_TYPE_P && !v->p_frame_skipped) ||
                 (s->pict_type == AV_PICTURE_TYPE_B && !v->bi_type)) &&
                !v->cbpcy_vlc) {
                av_log(v->s.avctx, AV_LOG_ERROR, "missing cbpcy_vlc\n");
                continue;
            }
            ff_vc1_decode_blocks(v);
            if (i != n_slices)
                s->gb = slices[i].gb;
        }
        if (v->field_mode) {
            v->second_field = 0;
            s->current_picture.f->linesize[0] >>= 1;
            s->current_picture.f->linesize[1] >>= 1;
            s->current_picture.f->linesize[2] >>= 1;
            s->linesize                      >>= 1;
            s->uvlinesize                    >>= 1;
            if (v->s.pict_type != AV_PICTURE_TYPE_BI && v->s.pict_type != AV_PICTURE_TYPE_B) {
                FFSWAP(uint8_t *, v->mv_f_next[0], v->mv_f[0]);
                FFSWAP(uint8_t *, v->mv_f_next[1], v->mv_f[1]);
            }
        }
        ff_dlog(s->avctx, "Consumed %i/%i bits\n",
                get_bits_count(&s->gb), s->gb.size_in_bits);
//  if (get_bits_count(&s->gb) > buf_size * 8)
//      return -1;
        if(s->er.error_occurred && s->pict_type == AV_PICTURE_TYPE_B) {
            ret = AVERROR_INVALIDDATA;
            goto err;
        }
        if (!v->field_mode)
            ff_er_frame_end(&s->er);
    }

    ff_mpv_frame_end(s);

    if (avctx->codec_id == AV_CODEC_ID_WMV3IMAGE || avctx->codec_id == AV_CODEC_ID_VC1IMAGE) {
image:
        avctx->width  = avctx->coded_width  = v->output_width;
        avctx->height = avctx->coded_height = v->output_height;
        if (avctx->skip_frame >= AVDISCARD_NONREF)
            goto end;
#if CONFIG_WMV3IMAGE_DECODER || CONFIG_VC1IMAGE_DECODER
        if ((ret = vc1_decode_sprites(v, &s->gb)) < 0)
            goto err;
#endif
        if ((ret = av_frame_ref(pict, v->sprite_output_frame)) < 0)
            goto err;
        *got_frame = 1;
    } else {
        if (s->pict_type == AV_PICTURE_TYPE_B || s->low_delay) {
            if ((ret = av_frame_ref(pict, s->current_picture_ptr->f)) < 0)
                goto err;
            ff_print_debug_info(s, s->current_picture_ptr, pict);
            *got_frame = 1;
        } else if (s->last_picture_ptr) {
            if ((ret = av_frame_ref(pict, s->last_picture_ptr->f)) < 0)
                goto err;
            ff_print_debug_info(s, s->last_picture_ptr, pict);
            *got_frame = 1;
        }
    }

end:
    av_free(buf2);
    for (i = 0; i < n_slices; i++)
        av_free(slices[i].buf);
    av_free(slices);
    return buf_size;

err:
    av_free(buf2);
    for (i = 0; i < n_slices; i++)
        av_free(slices[i].buf);
    av_free(slices);
    return ret;
}


static const enum AVPixelFormat vc1_hwaccel_pixfmt_list_420[] = {
#if CONFIG_VC1_DXVA2_HWACCEL
    AV_PIX_FMT_DXVA2_VLD,
#endif
#if CONFIG_VC1_D3D11VA_HWACCEL
    AV_PIX_FMT_D3D11VA_VLD,
    AV_PIX_FMT_D3D11,
#endif
#if CONFIG_VC1_NVDEC_HWACCEL
    AV_PIX_FMT_CUDA,
#endif
#if CONFIG_VC1_VAAPI_HWACCEL
    AV_PIX_FMT_VAAPI,
#endif
#if CONFIG_VC1_VDPAU_HWACCEL
    AV_PIX_FMT_VDPAU,
#endif
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NONE
};

AVCodec ff_vc1_decoder = {
    .name           = "vc1",
    .long_name      = NULL_IF_CONFIG_SMALL("SMPTE VC-1"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VC1,
    .priv_data_size = sizeof(VC1Context),
    .init           = vc1_decode_init,
    .close          = ff_vc1_decode_end,
    .decode         = vc1_decode_frame,
    .flush          = ff_mpeg_flush,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY,
    .pix_fmts       = vc1_hwaccel_pixfmt_list_420,
    .hw_configs     = (const AVCodecHWConfigInternal*[]) {
#if CONFIG_VC1_DXVA2_HWACCEL
                        HWACCEL_DXVA2(vc1),
#endif
#if CONFIG_VC1_D3D11VA_HWACCEL
                        HWACCEL_D3D11VA(vc1),
#endif
#if CONFIG_VC1_D3D11VA2_HWACCEL
                        HWACCEL_D3D11VA2(vc1),
#endif
#if CONFIG_VC1_NVDEC_HWACCEL
                        HWACCEL_NVDEC(vc1),
#endif
#if CONFIG_VC1_VAAPI_HWACCEL
                        HWACCEL_VAAPI(vc1),
#endif
#if CONFIG_VC1_VDPAU_HWACCEL
                        HWACCEL_VDPAU(vc1),
#endif
                        NULL
                    },
    .profiles       = NULL_IF_CONFIG_SMALL(ff_vc1_profiles)
};

#if CONFIG_WMV3_DECODER
AVCodec ff_wmv3_decoder = {
    .name           = "wmv3",
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Media Video 9"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_WMV3,
    .priv_data_size = sizeof(VC1Context),
    .init           = vc1_decode_init,
    .close          = ff_vc1_decode_end,
    .decode         = vc1_decode_frame,
    .flush          = ff_mpeg_flush,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY,
    .pix_fmts       = vc1_hwaccel_pixfmt_list_420,
    .hw_configs     = (const AVCodecHWConfigInternal*[]) {
#if CONFIG_WMV3_DXVA2_HWACCEL
                        HWACCEL_DXVA2(wmv3),
#endif
#if CONFIG_WMV3_D3D11VA_HWACCEL
                        HWACCEL_D3D11VA(wmv3),
#endif
#if CONFIG_WMV3_D3D11VA2_HWACCEL
                        HWACCEL_D3D11VA2(wmv3),
#endif
#if CONFIG_WMV3_NVDEC_HWACCEL
                        HWACCEL_NVDEC(wmv3),
#endif
#if CONFIG_WMV3_VAAPI_HWACCEL
                        HWACCEL_VAAPI(wmv3),
#endif
#if CONFIG_WMV3_VDPAU_HWACCEL
                        HWACCEL_VDPAU(wmv3),
#endif
                        NULL
                    },
    .profiles       = NULL_IF_CONFIG_SMALL(ff_vc1_profiles)
};
#endif

#if CONFIG_WMV3IMAGE_DECODER
AVCodec ff_wmv3image_decoder = {
    .name           = "wmv3image",
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Media Video 9 Image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_WMV3IMAGE,
    .priv_data_size = sizeof(VC1Context),
    .init           = vc1_decode_init,
    .close          = ff_vc1_decode_end,
    .decode         = vc1_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .flush          = vc1_sprite_flush,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    },
};
#endif

#if CONFIG_VC1IMAGE_DECODER
AVCodec ff_vc1image_decoder = {
    .name           = "vc1image",
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Media Video 9 Image v2"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VC1IMAGE,
    .priv_data_size = sizeof(VC1Context),
    .init           = vc1_decode_init,
    .close          = ff_vc1_decode_end,
    .decode         = vc1_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .flush          = vc1_sprite_flush,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    },
};
#endif
