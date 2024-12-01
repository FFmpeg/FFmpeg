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

#include "config_components.h"

#include "avcodec.h"
#include "blockdsp.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "hwaccel_internal.h"
#include "hwconfig.h"
#include "mpeg_er.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "mpegvideodec.h"
#include "msmpeg4_vc1_data.h"
#include "profiles.h"
#include "simple_idct.h"
#include "vc1.h"
#include "vc1data.h"
#include "vc1_vlc_data.h"
#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"


static const enum AVPixelFormat vc1_hwaccel_pixfmt_list_420[] = {
#if CONFIG_VC1_DXVA2_HWACCEL
    AV_PIX_FMT_DXVA2_VLD,
#endif
#if CONFIG_VC1_D3D11VA_HWACCEL
    AV_PIX_FMT_D3D11VA_VLD,
    AV_PIX_FMT_D3D11,
#endif
#if CONFIG_VC1_D3D12VA_HWACCEL
    AV_PIX_FMT_D3D12,
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
    const uint8_t *src_h[2][2];
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
                const uint8_t *iplane = s->cur_pic.data[plane];
                int      iline  = s->cur_pic.linesize[plane];
                int      ycoord = yoff[sprite] + yadv[sprite] * row;
                int      yline  = ycoord >> 16;
                int      next_line;
                ysub[sprite] = ycoord & 0xFFFF;
                if (sprite) {
                    iplane = s->last_pic.data[plane];
                    iline  = s->last_pic.linesize[plane];
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

    if (!s->cur_pic.data[0]) {
        av_log(avctx, AV_LOG_ERROR, "Got no sprites\n");
        return AVERROR_UNKNOWN;
    }

    if (v->two_sprites && (!s->last_pic.ptr || !s->last_pic.data[0])) {
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
    MPVWorkPicture *f = &s->cur_pic;
    int plane, i;

    /* Windows Media Image codecs have a convergence interval of two keyframes.
       Since we can't enforce it, clear to black the missing sprite. This is
       wrong but it looks better than doing nothing. */

    if (f->data[0])
        for (plane = 0; plane < (CONFIG_GRAY && s->avctx->flags & AV_CODEC_FLAG_GRAY ? 1 : 3); plane++)
            for (i = 0; i < v->sprite_height>>!!plane; i++)
                memset(f->data[plane] + i * f->linesize[plane],
                       plane ? 128 : 0, f->linesize[plane]);
}

#endif

static av_cold int vc1_decode_init_alloc_tables(VC1Context *v)
{
    MpegEncContext *s = &v->s;
    int i, ret;
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
        return AVERROR(ENOMEM);

    v->n_allocated_blks = s->mb_width + 2;
    v->block            = av_malloc(sizeof(*v->block) * v->n_allocated_blks);
    v->cbp_base         = av_malloc(sizeof(v->cbp_base[0]) * 3 * s->mb_stride);
    if (!v->block || !v->cbp_base)
        return AVERROR(ENOMEM);
    v->cbp              = v->cbp_base + 2 * s->mb_stride;
    v->ttblk_base       = av_mallocz(sizeof(v->ttblk_base[0]) * 3 * s->mb_stride);
    if (!v->ttblk_base)
        return AVERROR(ENOMEM);
    v->ttblk            = v->ttblk_base + 2 * s->mb_stride;
    v->is_intra_base    = av_mallocz(sizeof(v->is_intra_base[0]) * 3 * s->mb_stride);
    if (!v->is_intra_base)
        return AVERROR(ENOMEM);
    v->is_intra         = v->is_intra_base + 2 * s->mb_stride;
    v->luma_mv_base     = av_mallocz(sizeof(v->luma_mv_base[0]) * 3 * s->mb_stride);
    if (!v->luma_mv_base)
        return AVERROR(ENOMEM);
    v->luma_mv          = v->luma_mv_base + 2 * s->mb_stride;

    /* allocate block type info in that way so it could be used with s->block_index[] */
    v->mb_type_base = av_mallocz(s->b8_stride * (mb_height * 2 + 1) + s->mb_stride * (mb_height + 1) * 2);
    if (!v->mb_type_base)
        return AVERROR(ENOMEM);
    v->mb_type[0]   = v->mb_type_base + s->b8_stride + 1;
    v->mb_type[1]   = v->mb_type_base + s->b8_stride * (mb_height * 2 + 1) + s->mb_stride + 1;
    v->mb_type[2]   = v->mb_type[1] + s->mb_stride * (mb_height + 1);

    /* allocate memory to store block level MV info */
    v->blk_mv_type_base = av_mallocz(     s->b8_stride * (mb_height * 2 + 1) + s->mb_stride * (mb_height + 1) * 2);
    if (!v->blk_mv_type_base)
        return AVERROR(ENOMEM);
    v->blk_mv_type      = v->blk_mv_type_base + s->b8_stride + 1;
    v->mv_f_base        = av_mallocz(2 * (s->b8_stride * (mb_height * 2 + 1) + s->mb_stride * (mb_height + 1) * 2));
    if (!v->mv_f_base)
        return AVERROR(ENOMEM);
    v->mv_f[0]          = v->mv_f_base + s->b8_stride + 1;
    v->mv_f[1]          = v->mv_f[0] + (s->b8_stride * (mb_height * 2 + 1) + s->mb_stride * (mb_height + 1) * 2);
    v->mv_f_next_base   = av_mallocz(2 * (s->b8_stride * (mb_height * 2 + 1) + s->mb_stride * (mb_height + 1) * 2));
    if (!v->mv_f_next_base)
        return AVERROR(ENOMEM);
    v->mv_f_next[0]     = v->mv_f_next_base + s->b8_stride + 1;
    v->mv_f_next[1]     = v->mv_f_next[0] + (s->b8_stride * (mb_height * 2 + 1) + s->mb_stride * (mb_height + 1) * 2);

    if (s->avctx->codec_id == AV_CODEC_ID_WMV3IMAGE || s->avctx->codec_id == AV_CODEC_ID_VC1IMAGE) {
        for (i = 0; i < 4; i++)
            if (!(v->sr_rows[i >> 1][i & 1] = av_malloc(v->output_width)))
                return AVERROR(ENOMEM);
    }

    ret = ff_intrax8_common_init(s->avctx, &v->x8,
                                 s->block, s->block_last_index,
                                 s->mb_width, s->mb_height);
    if (ret < 0)
        return ret;

    return 0;
}

static enum AVPixelFormat vc1_get_format(AVCodecContext *avctx)
{
    if (avctx->codec_id == AV_CODEC_ID_MSS2)
        return AV_PIX_FMT_YUV420P;

    if (CONFIG_GRAY && (avctx->flags & AV_CODEC_FLAG_GRAY)) {
        if (avctx->color_range == AVCOL_RANGE_UNSPECIFIED)
            avctx->color_range = AVCOL_RANGE_MPEG;
        return AV_PIX_FMT_GRAY8;
    }

    if (avctx->codec_id == AV_CODEC_ID_VC1IMAGE ||
        avctx->codec_id == AV_CODEC_ID_WMV3IMAGE)
        return AV_PIX_FMT_YUV420P;

    return ff_get_format(avctx, vc1_hwaccel_pixfmt_list_420);
}

static void vc1_decode_reset(AVCodecContext *avctx);

av_cold int ff_vc1_decode_init(AVCodecContext *avctx)
{
    VC1Context *const v = avctx->priv_data;
    MpegEncContext *const s = &v->s;
    int ret;

    ret = av_image_check_size(avctx->width, avctx->height, 0, avctx);
    if (ret < 0)
        return ret;

    ret = ff_mpv_decode_init(s, avctx);
    if (ret < 0)
        return ret;

    avctx->pix_fmt = vc1_get_format(avctx);

    ret = ff_mpv_common_init(s);
    if (ret < 0)
        return ret;

    s->y_dc_scale_table = ff_wmv3_dc_scale_table;
    s->c_dc_scale_table = ff_wmv3_dc_scale_table;

    ff_init_scantable(s->idsp.idct_permutation, &s->inter_scantable,
                      ff_wmv1_scantable[0]);
    ff_init_scantable(s->idsp.idct_permutation, &s->intra_scantable,
                      ff_wmv1_scantable[1]);

    ret = vc1_decode_init_alloc_tables(v);
    if (ret < 0) {
        vc1_decode_reset(avctx);
        return ret;
    }
    return 0;
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

static av_cold void vc1_init_static(void)
{
    static VLCElem vlc_table[32372];
    VLCInitState state = VLC_INIT_STATE(vlc_table);

    VLC_INIT_STATIC_TABLE(ff_vc1_norm2_vlc, VC1_NORM2_VLC_BITS, 4,
                          vc1_norm2_bits,  1, 1,
                          vc1_norm2_codes, 1, 1, 0);
    VLC_INIT_STATIC_TABLE(ff_vc1_norm6_vlc, VC1_NORM6_VLC_BITS, 64,
                          vc1_norm6_bits,  1, 1,
                          vc1_norm6_codes, 2, 2, 0);
    VLC_INIT_STATIC_TABLE(ff_vc1_imode_vlc, VC1_IMODE_VLC_BITS, 7,
                          vc1_imode_bits,  1, 1,
                          vc1_imode_codes, 1, 1, 0);
    for (int i = 0; i < 3; i++) {
        ff_vc1_ttmb_vlc[i] =
            ff_vlc_init_tables(&state, VC1_TTMB_VLC_BITS, 16,
                               vc1_ttmb_bits[i],  1, 1,
                               vc1_ttmb_codes[i], 2, 2, 0);
        ff_vc1_ttblk_vlc[i] =
            ff_vlc_init_tables(&state, VC1_TTBLK_VLC_BITS, 8,
                               vc1_ttblk_bits[i],  1, 1,
                               vc1_ttblk_codes[i], 1, 1, 0);
        ff_vc1_subblkpat_vlc[i] =
            ff_vlc_init_tables(&state, VC1_SUBBLKPAT_VLC_BITS, 15,
                               vc1_subblkpat_bits[i],  1, 1,
                               vc1_subblkpat_codes[i], 1, 1, 0);
    }
    for (int i = 0; i < 4; i++) {
        ff_vc1_4mv_block_pattern_vlc[i] =
            ff_vlc_init_tables(&state, VC1_4MV_BLOCK_PATTERN_VLC_BITS, 16,
                               vc1_4mv_block_pattern_bits[i],  1, 1,
                               vc1_4mv_block_pattern_codes[i], 1, 1, 0);
        ff_vc1_cbpcy_p_vlc[i] =
            ff_vlc_init_tables(&state, VC1_CBPCY_P_VLC_BITS, 64,
                               vc1_cbpcy_p_bits[i],  1, 1,
                               vc1_cbpcy_p_codes[i], 2, 2, 0);
        ff_vc1_mv_diff_vlc[i] =
            ff_vlc_init_tables(&state, VC1_MV_DIFF_VLC_BITS, 73,
                               vc1_mv_diff_bits[i],  1, 1,
                               vc1_mv_diff_codes[i], 2, 2, 0);
        /* initialize 4MV MBMODE VLC tables for interlaced frame P picture */
        ff_vc1_intfr_4mv_mbmode_vlc[i] =
            ff_vlc_init_tables(&state, VC1_INTFR_4MV_MBMODE_VLC_BITS, 15,
                               vc1_intfr_4mv_mbmode_bits[i],  1, 1,
                               vc1_intfr_4mv_mbmode_codes[i], 2, 2, 0);
        /* initialize NON-4MV MBMODE VLC tables for the same */
        ff_vc1_intfr_non4mv_mbmode_vlc[i] =
            ff_vlc_init_tables(&state, VC1_INTFR_NON4MV_MBMODE_VLC_BITS, 9,
                               vc1_intfr_non4mv_mbmode_bits[i],  1, 1,
                               vc1_intfr_non4mv_mbmode_codes[i], 1, 1, 0);
        /* initialize interlaced MVDATA tables (1-Ref) */
        ff_vc1_1ref_mvdata_vlc[i] =
            ff_vlc_init_tables(&state, VC1_1REF_MVDATA_VLC_BITS, 72,
                               vc1_1ref_mvdata_bits[i],  1, 1,
                               vc1_1ref_mvdata_codes[i], 4, 4, 0);
        /* Initialize 2MV Block pattern VLC tables */
        ff_vc1_2mv_block_pattern_vlc[i] =
            ff_vlc_init_tables(&state, VC1_2MV_BLOCK_PATTERN_VLC_BITS, 4,
                               vc1_2mv_block_pattern_bits[i],  1, 1,
                               vc1_2mv_block_pattern_codes[i], 1, 1, 0);
    }
    for (int i = 0; i < 8; i++) {
        ff_vc1_ac_coeff_table[i] =
            ff_vlc_init_tables(&state, AC_VLC_BITS, ff_vc1_ac_sizes[i],
                               &vc1_ac_tables[i][0][1], 8, 4,
                               &vc1_ac_tables[i][0][0], 8, 4, 0);
        /* initialize interlaced MVDATA tables (2-Ref) */
        ff_vc1_2ref_mvdata_vlc[i] =
            ff_vlc_init_tables(&state, VC1_2REF_MVDATA_VLC_BITS, 126,
                               vc1_2ref_mvdata_bits[i],  1, 1,
                               vc1_2ref_mvdata_codes[i], 4, 4, 0);
        /* Initialize interlaced CBPCY VLC tables (Table 124 - Table 131) */
        ff_vc1_icbpcy_vlc[i] =
            ff_vlc_init_tables(&state, VC1_ICBPCY_VLC_BITS, 63,
                               vc1_icbpcy_p_bits[i],  1, 1,
                               vc1_icbpcy_p_codes[i], 2, 2, 0);
        /* Initialize interlaced field picture MBMODE VLC tables */
        ff_vc1_if_mmv_mbmode_vlc[i] =
            ff_vlc_init_tables(&state, VC1_IF_MMV_MBMODE_VLC_BITS, 8,
                               vc1_if_mmv_mbmode_bits[i],  1, 1,
                               vc1_if_mmv_mbmode_codes[i], 1, 1, 0);
        ff_vc1_if_1mv_mbmode_vlc[i] =
            ff_vlc_init_tables(&state, VC1_IF_1MV_MBMODE_VLC_BITS, 6,
                               vc1_if_1mv_mbmode_bits[i],  1, 1,
                               vc1_if_1mv_mbmode_codes[i], 1, 1, 0);
    }
    ff_msmp4_vc1_vlcs_init_once();
}

/**
 * Init VC-1 specific tables and VC1Context members
 * @param v The VC1Context to initialize
 * @return Status
 */
av_cold void ff_vc1_init_common(VC1Context *v)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    MpegEncContext *const s = &v->s;

    /* defaults */
    v->pq      = -1;
    v->mvrange = 0; /* 7.1.1.18, p80 */

    s->avctx->chroma_sample_location = AVCHROMA_LOC_LEFT;
    s->out_format      = FMT_H263;

    s->h263_pred       = 1;
    s->msmpeg4_version = MSMP4_VC1;

    ff_vc1dsp_init(&v->vc1dsp);

    /* For error resilience */
    ff_qpeldsp_init(&s->qdsp);

    /* VLC tables */
    ff_thread_once(&init_static_once, vc1_init_static);
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

    ff_vc1_init_common(v);

    if (avctx->codec_id == AV_CODEC_ID_WMV3 || avctx->codec_id == AV_CODEC_ID_WMV3IMAGE) {
        int count = 0;

        // looks like WMV3 has a sequence header stored in the extradata
        // advanced sequence header may be before the first frame
        // the last byte of the extradata is a version number, 1 for the
        // samples we can decode

        ret = init_get_bits8(&gb, avctx->extradata, avctx->extradata_size);
        if (ret < 0)
            return ret;

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
        const uint8_t *end = avctx->extradata + avctx->extradata_size;
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
            buf2_size = v->vc1dsp.vc1_unescape_buffer(start + 4, size, buf2);
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

    ff_blockdsp_init(&s->bdsp);
    ff_h264chroma_init(&v->h264chroma, 8);

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
        v->vc1dsp.vc1_inv_trans_8x8    = ff_simple_idct_int16_8bit;
        v->vc1dsp.vc1_inv_trans_8x4    = ff_simple_idct84_add;
        v->vc1dsp.vc1_inv_trans_4x8    = ff_simple_idct48_add;
        v->vc1dsp.vc1_inv_trans_4x4    = ff_simple_idct44_add;
        v->vc1dsp.vc1_inv_trans_8x8_dc = ff_simple_idct_add_int16_8bit;
        v->vc1dsp.vc1_inv_trans_8x4_dc = ff_simple_idct84_add;
        v->vc1dsp.vc1_inv_trans_4x8_dc = ff_simple_idct48_add;
        v->vc1dsp.vc1_inv_trans_4x4_dc = ff_simple_idct44_add;
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
            return AVERROR_INVALIDDATA;
        }

        if ((v->sprite_width&1) || (v->sprite_height&1)) {
            avpriv_request_sample(avctx, "odd sprites support");
            return AVERROR_PATCHWELCOME;
        }
    }
    return 0;
}

static av_cold void vc1_decode_reset(AVCodecContext *avctx)
{
    VC1Context *v = avctx->priv_data;
    int i;

    av_frame_free(&v->sprite_output_frame);

    for (i = 0; i < 4; i++)
        av_freep(&v->sr_rows[i >> 1][i & 1]);
    ff_mpv_common_end(&v->s);
    memset(v->s.block_index, 0, sizeof(v->s.block_index));
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
}

/**
 * Close a MSS2/VC1/WMV3 decoder
 */
av_cold int ff_vc1_decode_end(AVCodecContext *avctx)
{
    vc1_decode_reset(avctx);
    return ff_mpv_decode_close(avctx);
}

/** Decode a VC1/WMV3 frame
 * @todo TODO: Handle VC-1 IDUs (Transport level?)
 */
static int vc1_decode_frame(AVCodecContext *avctx, AVFrame *pict,
                            int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size, n_slices = 0, i, ret;
    VC1Context *v = avctx->priv_data;
    MpegEncContext *s = &v->s;
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
    unsigned slices_allocated = 0;

    v->second_field = 0;

    if(s->avctx->flags & AV_CODEC_FLAG_LOW_DELAY)
        s->low_delay = 1;

    /* no supplementary picture */
    if (buf_size == 0 || (buf_size == 4 && AV_RB32(buf) == VC1_CODE_ENDOFSEQ)) {
        /* special case for last picture */
        if (s->low_delay == 0 && s->next_pic.ptr) {
            if ((ret = av_frame_ref(pict, s->next_pic.ptr->f)) < 0)
                return ret;
            ff_mpv_unref_picture(&s->next_pic);

            *got_frame = 1;
        }

        return buf_size;
    }

    //for advanced profile we may need to parse and unescape data
    if (avctx->codec_id == AV_CODEC_ID_VC1 || avctx->codec_id == AV_CODEC_ID_VC1IMAGE) {
        int buf_size2 = 0;
        size_t next_allocated = 0;
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
                    buf_start = start;
                    buf_size2 = v->vc1dsp.vc1_unescape_buffer(start + 4, size, buf2);
                    break;
                case VC1_CODE_FIELD: {
                    int buf_size3;
                    buf_start_second_field = start;
                    av_size_mult(sizeof(*slices), n_slices+1, &next_allocated);
                    tmp = next_allocated ? av_fast_realloc(slices, &slices_allocated, next_allocated) : NULL;
                    if (!tmp) {
                        ret = AVERROR(ENOMEM);
                        goto err;
                    }
                    slices = tmp;
                    slices[n_slices].buf = av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
                    if (!slices[n_slices].buf) {
                        ret = AVERROR(ENOMEM);
                        goto err;
                    }
                    buf_size3 = v->vc1dsp.vc1_unescape_buffer(start + 4, size,
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
                    buf_size2 = v->vc1dsp.vc1_unescape_buffer(start + 4, size, buf2);
                    init_get_bits(&s->gb, buf2, buf_size2 * 8);
                    ff_vc1_decode_entry_point(avctx, v, &s->gb);
                    break;
                case VC1_CODE_SLICE: {
                    int buf_size3;
                    av_size_mult(sizeof(*slices), n_slices+1, &next_allocated);
                    tmp = next_allocated ? av_fast_realloc(slices, &slices_allocated, next_allocated) : NULL;
                    if (!tmp) {
                        ret = AVERROR(ENOMEM);
                        goto err;
                    }
                    slices = tmp;
                    slices[n_slices].buf = av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
                    if (!slices[n_slices].buf) {
                        ret = AVERROR(ENOMEM);
                        goto err;
                    }
                    buf_size3 = v->vc1dsp.vc1_unescape_buffer(start + 4, size,
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
                buf_start_second_field = divider;
                av_size_mult(sizeof(*slices), n_slices+1, &next_allocated);
                tmp = next_allocated ? av_fast_realloc(slices, &slices_allocated, next_allocated) : NULL;
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
                buf_size3 = v->vc1dsp.vc1_unescape_buffer(divider + 4, buf + buf_size - divider - 4, slices[n_slices].buf);
                init_get_bits(&slices[n_slices].gb, slices[n_slices].buf,
                              buf_size3 << 3);
                slices[n_slices].mby_start = s->mb_height + 1 >> 1;
                slices[n_slices].rawbuf = divider;
                slices[n_slices].raw_size = buf + buf_size - divider;
                n_slices1 = n_slices - 1;
                n_slices++;
            }
            buf_size2 = v->vc1dsp.vc1_unescape_buffer(buf, divider - buf, buf2);
        } else {
            buf_size2 = v->vc1dsp.vc1_unescape_buffer(buf, buf_size, buf2);
        }
        init_get_bits(&s->gb, buf2, buf_size2*8);
    } else{
        ret = init_get_bits8(&s->gb, buf, buf_size);
        if (ret < 0)
            return ret;
    }

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
        vc1_decode_reset(avctx);
    }

    if (!s->context_initialized) {
        ret = ff_vc1_decode_init(avctx);
        if (ret < 0)
            goto err;

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
    if ((avctx->codec_id == AV_CODEC_ID_WMV3IMAGE || avctx->codec_id == AV_CODEC_ID_VC1IMAGE)
        && v->field_mode) {
        av_log(v->s.avctx, AV_LOG_ERROR, "Sprite decoder: expected Frames not Fields\n");
        ret = AVERROR_INVALIDDATA;
        goto err;
    }
    if ((s->mb_height >> v->field_mode) == 0) {
        av_log(v->s.avctx, AV_LOG_ERROR, "image too short\n");
        ret = AVERROR_INVALIDDATA;
        goto err;
    }

    /* skip B-frames if we don't have reference frames */
    if (!s->last_pic.ptr && s->pict_type == AV_PICTURE_TYPE_B) {
        av_log(v->s.avctx, AV_LOG_DEBUG, "Skipping B frame without reference frames\n");
        goto end;
    }
    if ((avctx->skip_frame >= AVDISCARD_NONREF && s->pict_type == AV_PICTURE_TYPE_B) ||
        (avctx->skip_frame >= AVDISCARD_NONKEY && s->pict_type != AV_PICTURE_TYPE_I) ||
         avctx->skip_frame >= AVDISCARD_ALL) {
        goto end;
    }

    if ((ret = ff_mpv_frame_start(s, avctx)) < 0) {
        goto err;
    }

    v->s.cur_pic.ptr->field_picture = v->field_mode;
    v->s.cur_pic.ptr->f->flags |= AV_FRAME_FLAG_INTERLACED * (v->fcm != PROGRESSIVE);
    v->s.cur_pic.ptr->f->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST * !!v->tff;
    v->last_interlaced = v->s.last_pic.ptr ? v->s.last_pic.ptr->f->flags & AV_FRAME_FLAG_INTERLACED : 0;
    v->next_interlaced = v->s.next_pic.ptr ? v->s.next_pic.ptr->f->flags & AV_FRAME_FLAG_INTERLACED : 0;

    // process pulldown flags
    s->cur_pic.ptr->f->repeat_pict = 0;
    // Pulldown flags are only valid when 'broadcast' has been set.
    if (v->rff) {
        // repeat field
        s->cur_pic.ptr->f->repeat_pict = 1;
    } else if (v->rptfrm) {
        // repeat frames
        s->cur_pic.ptr->f->repeat_pict = v->rptfrm * 2;
    }

    if (avctx->hwaccel) {
        const FFHWAccel *hwaccel = ffhwaccel(avctx->hwaccel);
        s->mb_y = 0;
        if (v->field_mode && buf_start_second_field) {
            // decode first field
            s->picture_structure = PICT_BOTTOM_FIELD - v->tff;
            ret = hwaccel->start_frame(avctx, buf_start,
                                       buf_start_second_field - buf_start);
            if (ret < 0)
                goto err;

            if (n_slices1 == -1) {
                // no slices, decode the field as-is
                ret = hwaccel->decode_slice(avctx, buf_start,
                                            buf_start_second_field - buf_start);
                if (ret < 0)
                    goto err;
            } else {
                ret = hwaccel->decode_slice(avctx, buf_start,
                                            slices[0].rawbuf - buf_start);
                if (ret < 0)
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

                    ret = hwaccel->decode_slice(avctx, slices[i].rawbuf,
                                                       slices[i].raw_size);
                    if (ret < 0)
                        goto err;
                }
            }

            if ((ret = hwaccel->end_frame(avctx)) < 0)
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
            v->s.cur_pic.ptr->f->pict_type = v->s.pict_type;

            ret = hwaccel->start_frame(avctx, buf_start_second_field,
                                       (buf + buf_size) - buf_start_second_field);
            if (ret < 0)
                goto err;

            if (n_slices - n_slices1 == 2) {
                // no slices, decode the field as-is
                ret = hwaccel->decode_slice(avctx, buf_start_second_field,
                                            (buf + buf_size) - buf_start_second_field);
                if (ret < 0)
                    goto err;
            } else {
                ret = hwaccel->decode_slice(avctx, buf_start_second_field,
                                            slices[n_slices1 + 2].rawbuf - buf_start_second_field);
                if (ret < 0)
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

                    ret = hwaccel->decode_slice(avctx, slices[i].rawbuf,
                                                slices[i].raw_size);
                    if (ret < 0)
                        goto err;
                }
            }

            if ((ret = hwaccel->end_frame(avctx)) < 0)
                goto err;
        } else {
            s->picture_structure = PICT_FRAME;
            ret = hwaccel->start_frame(avctx, buf_start,
                                       (buf + buf_size) - buf_start);
            if (ret < 0)
                goto err;

            if (n_slices == 0) {
                // no slices, decode the frame as-is
                ret = hwaccel->decode_slice(avctx, buf_start,
                                            (buf + buf_size) - buf_start);
                if (ret < 0)
                    goto err;
            } else {
                // decode the frame part as the first slice
                ret = hwaccel->decode_slice(avctx, buf_start,
                                            slices[0].rawbuf - buf_start);
                if (ret < 0)
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

                    ret = hwaccel->decode_slice(avctx, slices[i].rawbuf,
                                                       slices[i].raw_size);
                    if (ret < 0)
                        goto err;
                }
            }
            if ((ret = hwaccel->end_frame(avctx)) < 0)
                goto err;
        }
    } else {
        int header_ret = 0;

        ff_mpeg_er_frame_start(s);

        v->end_mb_x = s->mb_width;
        if (v->field_mode) {
            s->cur_pic.linesize[0] <<= 1;
            s->cur_pic.linesize[1] <<= 1;
            s->cur_pic.linesize[2] <<= 1;
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
            if (i != n_slices) {
                s->gb = slices[i].gb;
            }
        }
        if (v->field_mode) {
            v->second_field = 0;
            s->cur_pic.linesize[0] >>= 1;
            s->cur_pic.linesize[1] >>= 1;
            s->cur_pic.linesize[2] >>= 1;
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
        if (   !v->field_mode
            && avctx->codec_id != AV_CODEC_ID_WMV3IMAGE
            && avctx->codec_id != AV_CODEC_ID_VC1IMAGE)
            ff_er_frame_end(&s->er, NULL);
    }

    ff_mpv_frame_end(s);

    if (avctx->codec_id == AV_CODEC_ID_WMV3IMAGE || avctx->codec_id == AV_CODEC_ID_VC1IMAGE) {
image:
        avctx->width  = avctx->coded_width  = v->output_width;
        avctx->height = avctx->coded_height = v->output_height;
        if (avctx->skip_frame >= AVDISCARD_NONREF)
            goto end;
        if (!v->sprite_output_frame &&
            !(v->sprite_output_frame = av_frame_alloc())) {
            ret = AVERROR(ENOMEM);
            goto err;
        }
#if CONFIG_WMV3IMAGE_DECODER || CONFIG_VC1IMAGE_DECODER
        if ((ret = vc1_decode_sprites(v, &s->gb)) < 0)
            goto err;
#endif
        if ((ret = av_frame_ref(pict, v->sprite_output_frame)) < 0)
            goto err;
        *got_frame = 1;
    } else {
        if (s->pict_type == AV_PICTURE_TYPE_B || s->low_delay) {
            if ((ret = av_frame_ref(pict, s->cur_pic.ptr->f)) < 0)
                goto err;
            if (!v->field_mode)
                ff_print_debug_info(s, s->cur_pic.ptr, pict);
            *got_frame = 1;
        } else if (s->last_pic.ptr) {
            if ((ret = av_frame_ref(pict, s->last_pic.ptr->f)) < 0)
                goto err;
            if (!v->field_mode)
                ff_print_debug_info(s, s->last_pic.ptr, pict);
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


const FFCodec ff_vc1_decoder = {
    .p.name         = "vc1",
    CODEC_LONG_NAME("SMPTE VC-1"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VC1,
    .priv_data_size = sizeof(VC1Context),
    .init           = vc1_decode_init,
    .close          = ff_vc1_decode_end,
    FF_CODEC_DECODE_CB(vc1_decode_frame),
    .flush          = ff_mpeg_flush,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY,
    .hw_configs     = (const AVCodecHWConfigInternal *const []) {
#if CONFIG_VC1_DXVA2_HWACCEL
                        HWACCEL_DXVA2(vc1),
#endif
#if CONFIG_VC1_D3D11VA_HWACCEL
                        HWACCEL_D3D11VA(vc1),
#endif
#if CONFIG_VC1_D3D11VA2_HWACCEL
                        HWACCEL_D3D11VA2(vc1),
#endif
#if CONFIG_VC1_D3D12VA_HWACCEL
                        HWACCEL_D3D12VA(vc1),
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
    .p.profiles     = NULL_IF_CONFIG_SMALL(ff_vc1_profiles)
};

#if CONFIG_WMV3_DECODER
const FFCodec ff_wmv3_decoder = {
    .p.name         = "wmv3",
    CODEC_LONG_NAME("Windows Media Video 9"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WMV3,
    .priv_data_size = sizeof(VC1Context),
    .init           = vc1_decode_init,
    .close          = ff_vc1_decode_end,
    FF_CODEC_DECODE_CB(vc1_decode_frame),
    .flush          = ff_mpeg_flush,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY,
    .hw_configs     = (const AVCodecHWConfigInternal *const []) {
#if CONFIG_WMV3_DXVA2_HWACCEL
                        HWACCEL_DXVA2(wmv3),
#endif
#if CONFIG_WMV3_D3D11VA_HWACCEL
                        HWACCEL_D3D11VA(wmv3),
#endif
#if CONFIG_WMV3_D3D11VA2_HWACCEL
                        HWACCEL_D3D11VA2(wmv3),
#endif
#if CONFIG_WMV3_D3D12VA_HWACCEL
                        HWACCEL_D3D12VA(wmv3),
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
    .p.profiles     = NULL_IF_CONFIG_SMALL(ff_vc1_profiles)
};
#endif

#if CONFIG_WMV3IMAGE_DECODER
const FFCodec ff_wmv3image_decoder = {
    .p.name         = "wmv3image",
    CODEC_LONG_NAME("Windows Media Video 9 Image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WMV3IMAGE,
    .priv_data_size = sizeof(VC1Context),
    .init           = vc1_decode_init,
    .close          = ff_vc1_decode_end,
    FF_CODEC_DECODE_CB(vc1_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .flush          = vc1_sprite_flush,
};
#endif

#if CONFIG_VC1IMAGE_DECODER
const FFCodec ff_vc1image_decoder = {
    .p.name         = "vc1image",
    CODEC_LONG_NAME("Windows Media Video 9 Image v2"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VC1IMAGE,
    .priv_data_size = sizeof(VC1Context),
    .init           = vc1_decode_init,
    .close          = ff_vc1_decode_end,
    FF_CODEC_DECODE_CB(vc1_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .flush          = vc1_sprite_flush,
};
#endif
