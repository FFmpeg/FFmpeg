/*
 * H.261 encoder
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2004 Maarten Daniels
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
 * H.261 encoder.
 */

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/thread.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "h261.h"
#include "h261enc.h"
#include "mpegvideoenc.h"

#define H261_MAX_RUN   26
#define H261_MAX_LEVEL 15
#define H261_ESC_LEN   (6 + 6 + 8)

static struct VLCLUT {
    uint8_t len;
    uint16_t code;
} vlc_lut[H261_MAX_RUN + 1][32 /* 0..2 * H261_MAX_LEN are used */];

static uint8_t uni_h261_rl_len     [64 * 128];
static uint8_t uni_h261_rl_len_last[64 * 128];

typedef struct H261EncContext {
    MpegEncContext s;

    H261Context common;

    int gob_number;
    enum {
        H261_QCIF = 0,
        H261_CIF  = 1,
    } format;
} H261EncContext;

void ff_h261_encode_picture_header(MpegEncContext *s)
{
    H261EncContext *const h = (H261EncContext *)s;
    int temp_ref;

    align_put_bits(&s->pb);

    /* Update the pointer to last GOB */
    s->ptr_lastgob = put_bits_ptr(&s->pb);

    put_bits(&s->pb, 20, 0x10); /* PSC */

    temp_ref = s->picture_number * 30000LL * s->avctx->time_base.num /
               (1001LL * s->avctx->time_base.den);   // FIXME maybe this should use a timestamp
    put_sbits(&s->pb, 5, temp_ref); /* TemporalReference */

    put_bits(&s->pb, 1, 0); /* split screen off */
    put_bits(&s->pb, 1, 0); /* camera  off */
    put_bits(&s->pb, 1, s->pict_type == AV_PICTURE_TYPE_I); /* freeze picture release on/off */

    put_bits(&s->pb, 1, h->format); /* 0 == QCIF, 1 == CIF */

    put_bits(&s->pb, 1, 1); /* still image mode */
    put_bits(&s->pb, 1, 1); /* reserved */

    put_bits(&s->pb, 1, 0); /* no PEI */
    h->gob_number = h->format - 1;
    s->mb_skip_run = 0;
}

/**
 * Encode a group of blocks header.
 */
static void h261_encode_gob_header(MpegEncContext *s, int mb_line)
{
    H261EncContext *const h = (H261EncContext *)s;
    if (h->format == H261_QCIF) {
        h->gob_number += 2; // QCIF
    } else {
        h->gob_number++;    // CIF
    }
    put_bits(&s->pb, 16, 1);            /* GBSC */
    put_bits(&s->pb, 4, h->gob_number); /* GN */
    put_bits(&s->pb, 5, s->qscale);     /* GQUANT */
    put_bits(&s->pb, 1, 0);             /* no GEI */
    s->mb_skip_run = 0;
    s->last_mv[0][0][0] = 0;
    s->last_mv[0][0][1] = 0;
}

void ff_h261_reorder_mb_index(MpegEncContext *s)
{
    const H261EncContext *const h = (H261EncContext*)s;
    int index = s->mb_x + s->mb_y * s->mb_width;

    if (index % 11 == 0) {
        if (index % 33 == 0)
            h261_encode_gob_header(s, 0);
        s->last_mv[0][0][0] = 0;
        s->last_mv[0][0][1] = 0;
    }

    /* for CIF the GOB's are fragmented in the middle of a scanline
     * that's why we need to adjust the x and y index of the macroblocks */
    if (h->format == H261_CIF) {
        s->mb_x  = index % 11;
        index   /= 11;
        s->mb_y  = index % 3;
        index   /= 3;
        s->mb_x += 11 * (index % 2);
        index   /= 2;
        s->mb_y += 3 * index;

        ff_init_block_index(s);
        ff_update_block_index(s, 8, 0, 1);
    }
}

static void h261_encode_motion(PutBitContext *pb, int val)
{
    int sign, code;
    if (val == 0) {
        // Corresponds to ff_h261_mv_tab[0]
        put_bits(pb, 1, 1);
    } else {
        if (val > 15)
            val -= 32;
        if (val < -16)
            val += 32;
        sign = val < 0;
        code = sign ? -val : val;
        put_bits(pb, ff_h261_mv_tab[code][1], ff_h261_mv_tab[code][0]);
        put_bits(pb, 1, sign);
    }
}

static inline int get_cbp(MpegEncContext *s, int16_t block[6][64])
{
    int i, cbp;
    cbp = 0;
    for (i = 0; i < 6; i++)
        if (s->block_last_index[i] >= 0)
            cbp |= 1 << (5 - i);
    return cbp;
}

/**
 * Encode an 8x8 block.
 * @param block the 8x8 block
 * @param n block index (0-3 are luma, 4-5 are chroma)
 */
static void h261_encode_block(H261EncContext *h, int16_t *block, int n)
{
    MpegEncContext *const s = &h->s;
    int level, run, i, j, last_index, last_non_zero;

    if (s->mb_intra) {
        /* DC coef */
        level = block[0];
        /* 255 cannot be represented, so we clamp */
        if (level > 254) {
            level    = 254;
            block[0] = 254;
        }
        /* 0 cannot be represented also */
        else if (level < 1) {
            level    = 1;
            block[0] = 1;
        }
        if (level == 128)
            put_bits(&s->pb, 8, 0xff);
        else
            put_bits(&s->pb, 8, level);
        i = 1;
    } else if ((block[0] == 1 || block[0] == -1) &&
               (s->block_last_index[n] > -1)) {
        // special case
        put_bits(&s->pb, 2, block[0] > 0 ? 2 : 3);
        i = 1;
    } else {
        i = 0;
    }

    /* AC coefs */
    last_index    = s->block_last_index[n];
    last_non_zero = i - 1;
    for (; i <= last_index; i++) {
        j     = s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            run    = i - last_non_zero - 1;

            if (run <= H261_MAX_RUN &&
                (unsigned)(level + H261_MAX_LEVEL) <= 2 * H261_MAX_LEVEL &&
                vlc_lut[run][level + H261_MAX_LEVEL].len) {
                put_bits(&s->pb, vlc_lut[run][level + H261_MAX_LEVEL].len,
                         vlc_lut[run][level + H261_MAX_LEVEL].code);
            } else {
                /* Escape */
                put_bits(&s->pb, 6 + 6, (1 << 6) | run);
                av_assert1(level != 0);
                av_assert1(FFABS(level) <= 127);
                put_sbits(&s->pb, 8, level);
            }
            last_non_zero = i;
        }
    }
    if (last_index > -1)
        put_bits(&s->pb, 2, 0x2); // EOB
}

void ff_h261_encode_mb(MpegEncContext *s, int16_t block[6][64],
                       int motion_x, int motion_y)
{
    /* The following is only allowed because this encoder
     * does not use slice threading. */
    H261EncContext *const h = (H261EncContext *)s;
    H261Context *const com = &h->common;
    int mvd, mv_diff_x, mv_diff_y, i, cbp;
    cbp = 63; // avoid warning
    mvd = 0;

    com->mtype = 0;

    if (!s->mb_intra) {
        /* compute cbp */
        cbp = get_cbp(s, block);

        /* mvd indicates if this block is motion compensated */
        mvd = motion_x | motion_y;

        if ((cbp | mvd) == 0) {
            /* skip macroblock */
            s->mb_skip_run++;
            s->last_mv[0][0][0] = 0;
            s->last_mv[0][0][1] = 0;
            s->qscale -= s->dquant;
            return;
        }
    }

    /* MB is not skipped, encode MBA */
    put_bits(&s->pb,
             ff_h261_mba_bits[s->mb_skip_run],
             ff_h261_mba_code[s->mb_skip_run]);
    s->mb_skip_run = 0;

    /* calculate MTYPE */
    if (!s->mb_intra) {
        com->mtype++;

        if (mvd || s->loop_filter)
            com->mtype += 3;
        if (s->loop_filter)
            com->mtype += 3;
        if (cbp)
            com->mtype++;
        av_assert1(com->mtype > 1);
    }

    if (s->dquant && cbp) {
        com->mtype++;
    } else
        s->qscale -= s->dquant;

    put_bits(&s->pb,
             ff_h261_mtype_bits[com->mtype],
             ff_h261_mtype_code[com->mtype]);

    com->mtype = ff_h261_mtype_map[com->mtype];

    if (IS_QUANT(com->mtype)) {
        ff_set_qscale(s, s->qscale + s->dquant);
        put_bits(&s->pb, 5, s->qscale);
    }

    if (IS_16X16(com->mtype)) {
        mv_diff_x       = (motion_x >> 1) - s->last_mv[0][0][0];
        mv_diff_y       = (motion_y >> 1) - s->last_mv[0][0][1];
        s->last_mv[0][0][0] = (motion_x >> 1);
        s->last_mv[0][0][1] = (motion_y >> 1);
        h261_encode_motion(&s->pb, mv_diff_x);
        h261_encode_motion(&s->pb, mv_diff_y);
    }

    if (HAS_CBP(com->mtype)) {
        av_assert1(cbp > 0);
        put_bits(&s->pb,
                 ff_h261_cbp_tab[cbp - 1][1],
                 ff_h261_cbp_tab[cbp - 1][0]);
    }
    for (i = 0; i < 6; i++)
        /* encode each block */
        h261_encode_block(h, block[i], i);

    if (!IS_16X16(com->mtype)) {
        s->last_mv[0][0][0] = 0;
        s->last_mv[0][0][1] = 0;
    }
}

static av_cold void h261_encode_init_static(void)
{
    memset(uni_h261_rl_len,      H261_ESC_LEN, sizeof(uni_h261_rl_len));
    memset(uni_h261_rl_len_last, H261_ESC_LEN + 2 /* EOB */, sizeof(uni_h261_rl_len_last));

    // The following loop is over the ordinary elements, not EOB or escape.
    for (size_t i = 1; i < FF_ARRAY_ELEMS(ff_h261_tcoeff_vlc) - 1; i++) {
        unsigned run   = ff_h261_tcoeff_run[i];
        unsigned level = ff_h261_tcoeff_level[i];
        unsigned len   = ff_h261_tcoeff_vlc[i][1] + 1 /* sign */;
        unsigned code  = ff_h261_tcoeff_vlc[i][0];

        vlc_lut[run][H261_MAX_LEVEL + level] = (struct VLCLUT){ len, code << 1 };
        vlc_lut[run][H261_MAX_LEVEL - level] = (struct VLCLUT){ len, (code << 1) | 1 };

        uni_h261_rl_len     [UNI_AC_ENC_INDEX(run, 64 + level)] = len;
        uni_h261_rl_len     [UNI_AC_ENC_INDEX(run, 64 - level)] = len;
        uni_h261_rl_len_last[UNI_AC_ENC_INDEX(run, 64 + level)] = len + 2;
        uni_h261_rl_len_last[UNI_AC_ENC_INDEX(run, 64 - level)] = len + 2;
    }
}

av_cold int ff_h261_encode_init(MpegEncContext *s)
{
    H261EncContext *const h = (H261EncContext*)s;
    static AVOnce init_static_once = AV_ONCE_INIT;

    if (s->width == 176 && s->height == 144) {
        h->format = H261_QCIF;
    } else if (s->width == 352 && s->height == 288) {
        h->format = H261_CIF;
    } else {
        av_log(s->avctx, AV_LOG_ERROR,
                "The specified picture size of %dx%d is not valid for the "
                "H.261 codec.\nValid sizes are 176x144, 352x288\n",
                s->width, s->height);
        return AVERROR(EINVAL);
    }
    s->private_ctx = &h->common;

    s->min_qcoeff       = -127;
    s->max_qcoeff       = 127;
    s->ac_esc_length    = H261_ESC_LEN;

    s->intra_ac_vlc_length      = s->inter_ac_vlc_length      = uni_h261_rl_len;
    s->intra_ac_vlc_last_length = s->inter_ac_vlc_last_length = uni_h261_rl_len_last;
    ff_thread_once(&init_static_once, h261_encode_init_static);

    return 0;
}

const FFCodec ff_h261_encoder = {
    .p.name         = "h261",
    CODEC_LONG_NAME("H.261"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_H261,
    .p.priv_class   = &ff_mpv_enc_class,
    .priv_data_size = sizeof(H261EncContext),
    .init           = ff_mpv_encode_init,
    FF_CODEC_ENCODE_CB(ff_mpv_encode_picture),
    .close          = ff_mpv_encode_end,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .p.pix_fmts     = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P,
                                                     AV_PIX_FMT_NONE },
    .color_ranges   = AVCOL_RANGE_MPEG,
    .p.capabilities = AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
};
