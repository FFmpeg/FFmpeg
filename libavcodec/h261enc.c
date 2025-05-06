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
#include "put_bits.h"

#define H261_MAX_RUN   26
#define H261_MAX_LEVEL 15
#define H261_ESC_LEN   (6 + 6 + 8)
#define MV_TAB_OFFSET  32

static struct VLCLUT {
    uint8_t len;
    uint16_t code;
} vlc_lut[H261_MAX_RUN + 1][32 /* 0..2 * H261_MAX_LEN are used */];

// Not const despite never being initialized because doing so would
// put it into .rodata instead of .bss and bloat the binary.
// mv_penalty exists so that the motion estimation code can avoid branches.
static uint8_t mv_penalty[MAX_FCODE + 1][MAX_DMV * 2 + 1];
static uint8_t uni_h261_rl_len     [64 * 128];
static uint8_t uni_h261_rl_len_last[64 * 128];
static uint8_t h261_mv_codes[64][2];

typedef struct H261EncContext {
    MPVMainEncContext s;

    H261Context common;

    int gob_number;
    enum {
        H261_QCIF = 0,
        H261_CIF  = 1,
    } format;
} H261EncContext;

static int h261_encode_picture_header(MPVMainEncContext *const m)
{
    H261EncContext *const h = (H261EncContext *)m;
    MPVEncContext *const s = &h->s.s;
    int temp_ref;

    put_bits_assume_flushed(&s->pb);

    put_bits(&s->pb, 20, 0x10); /* PSC */

    temp_ref = s->c.picture_number * 30000LL * s->c.avctx->time_base.num /
               (1001LL * s->c.avctx->time_base.den);   // FIXME maybe this should use a timestamp
    put_sbits(&s->pb, 5, temp_ref); /* TemporalReference */

    put_bits(&s->pb, 1, 0); /* split screen off */
    put_bits(&s->pb, 1, 0); /* camera  off */
    put_bits(&s->pb, 1, s->c.pict_type == AV_PICTURE_TYPE_I); /* freeze picture release on/off */

    put_bits(&s->pb, 1, h->format); /* 0 == QCIF, 1 == CIF */

    put_bits(&s->pb, 1, 1); /* still image mode */
    put_bits(&s->pb, 1, 1); /* reserved */

    put_bits(&s->pb, 1, 0); /* no PEI */
    h->gob_number = h->format - 1;
    s->c.mb_skip_run = 0;

    return 0;
}

/**
 * Encode a group of blocks header.
 */
static void h261_encode_gob_header(MPVEncContext *const s, int mb_line)
{
    H261EncContext *const h = (H261EncContext *)s;
    if (h->format == H261_QCIF) {
        h->gob_number += 2; // QCIF
    } else {
        h->gob_number++;    // CIF
    }
    put_bits(&s->pb, 16, 1);            /* GBSC */
    put_bits(&s->pb, 4, h->gob_number); /* GN */
    put_bits(&s->pb, 5, s->c.qscale);     /* GQUANT */
    put_bits(&s->pb, 1, 0);             /* no GEI */
    s->c.mb_skip_run = 0;
    s->c.last_mv[0][0][0] = 0;
    s->c.last_mv[0][0][1] = 0;
}

void ff_h261_reorder_mb_index(MPVEncContext *const s)
{
    const H261EncContext *const h = (H261EncContext*)s;
    int index = s->c.mb_x + s->c.mb_y * s->c.mb_width;

    if (index % 11 == 0) {
        if (index % 33 == 0)
            h261_encode_gob_header(s, 0);
        s->c.last_mv[0][0][0] = 0;
        s->c.last_mv[0][0][1] = 0;
    }

    /* for CIF the GOB's are fragmented in the middle of a scanline
     * that's why we need to adjust the x and y index of the macroblocks */
    if (h->format == H261_CIF) {
        s->c.mb_x  = index % 11;
        index   /= 11;
        s->c.mb_y  = index % 3;
        index   /= 3;
        s->c.mb_x += 11 * (index % 2);
        index   /= 2;
        s->c.mb_y += 3 * index;

        ff_init_block_index(&s->c);
        ff_update_block_index(&s->c, 8, 0, 1);
    }
}

static void h261_encode_motion(PutBitContext *pb, int val)
{
    put_bits(pb, h261_mv_codes[MV_TAB_OFFSET + val][1],
                 h261_mv_codes[MV_TAB_OFFSET + val][0]);
}

static inline int get_cbp(const int block_last_index[6])
{
    int i, cbp;
    cbp = 0;
    for (i = 0; i < 6; i++)
        if (block_last_index[i] >= 0)
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
    MPVEncContext *const s = &h->s.s;
    int level, run, i, j, last_index, last_non_zero;

    if (s->c.mb_intra) {
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
               (s->c.block_last_index[n] > -1)) {
        // special case
        put_bits(&s->pb, 2, block[0] > 0 ? 2 : 3);
        i = 1;
    } else {
        i = 0;
    }

    /* AC coefs */
    last_index    = s->c.block_last_index[n];
    last_non_zero = i - 1;
    for (; i <= last_index; i++) {
        j     = s->c.intra_scantable.permutated[i];
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

static void h261_encode_mb(MPVEncContext *const s, int16_t block[6][64],
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

    if (!s->c.mb_intra) {
        /* compute cbp */
        cbp = get_cbp(s->c.block_last_index);

        /* mvd indicates if this block is motion compensated */
        mvd = motion_x | motion_y;

        if ((cbp | mvd) == 0) {
            /* skip macroblock */
            s->c.mb_skip_run++;
            s->c.last_mv[0][0][0] = 0;
            s->c.last_mv[0][0][1] = 0;
            s->c.qscale -= s->dquant;
            return;
        }
    }

    /* MB is not skipped, encode MBA */
    put_bits(&s->pb,
             ff_h261_mba_bits[s->c.mb_skip_run],
             ff_h261_mba_code[s->c.mb_skip_run]);
    s->c.mb_skip_run = 0;

    /* calculate MTYPE */
    if (!s->c.mb_intra) {
        com->mtype++;

        if (mvd || s->c.loop_filter)
            com->mtype += 3;
        if (s->c.loop_filter)
            com->mtype += 3;
        if (cbp)
            com->mtype++;
        av_assert1(com->mtype > 1);
    }

    if (s->dquant && cbp) {
        com->mtype++;
    } else
        s->c.qscale -= s->dquant;

    put_bits(&s->pb,
             ff_h261_mtype_bits[com->mtype],
             ff_h261_mtype_code[com->mtype]);

    com->mtype = ff_h261_mtype_map[com->mtype];

    if (IS_QUANT(com->mtype)) {
        ff_set_qscale(&s->c, s->c.qscale + s->dquant);
        put_bits(&s->pb, 5, s->c.qscale);
    }

    if (IS_16X16(com->mtype)) {
        mv_diff_x       = (motion_x >> 1) - s->c.last_mv[0][0][0];
        mv_diff_y       = (motion_y >> 1) - s->c.last_mv[0][0][1];
        s->c.last_mv[0][0][0] = (motion_x >> 1);
        s->c.last_mv[0][0][1] = (motion_y >> 1);
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
        s->c.last_mv[0][0][0] = 0;
        s->c.last_mv[0][0][1] = 0;
    }
}

static av_cold void h261_encode_init_static(void)
{
    uint8_t (*const mv_codes)[2] = h261_mv_codes + MV_TAB_OFFSET;
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

    for (ptrdiff_t i = 1;; i++) {
        // sign-one MV codes; diff -16..-1, 16..31
        mv_codes[32 - i][0] = mv_codes[-i][0] = (ff_h261_mv_tab[i][0] << 1) | 1 /* sign */;
        mv_codes[32 - i][1] = mv_codes[-i][1] = ff_h261_mv_tab[i][1] + 1;
        if (i == 16)
            break;
        // sign-zero MV codes: diff -31..-17, 1..15
        mv_codes[i][0] = mv_codes[i - 32][0] = ff_h261_mv_tab[i][0] << 1;
        mv_codes[i][1] = mv_codes[i - 32][1] = ff_h261_mv_tab[i][1] + 1;
    }
    // MV code for difference zero; has no sign
    mv_codes[0][0] = 1;
    mv_codes[0][1] = 1;
}

static av_cold int h261_encode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    H261EncContext *const h = avctx->priv_data;
    MPVEncContext *const s = &h->s.s;

    if (avctx->width == 176 && avctx->height == 144) {
        h->format = H261_QCIF;
    } else if (avctx->width == 352 && avctx->height == 288) {
        h->format = H261_CIF;
    } else {
        av_log(avctx, AV_LOG_ERROR,
                "The specified picture size of %dx%d is not valid for the "
                "H.261 codec.\nValid sizes are 176x144, 352x288\n",
               avctx->width, avctx->height);
        return AVERROR(EINVAL);
    }
    s->c.private_ctx = &h->common;
    h->s.encode_picture_header = h261_encode_picture_header;
    s->encode_mb               = h261_encode_mb;

    s->min_qcoeff       = -127;
    s->max_qcoeff       = 127;
    s->ac_esc_length    = H261_ESC_LEN;

    s->me.mv_penalty = mv_penalty;

    s->intra_ac_vlc_length      = s->inter_ac_vlc_length      = uni_h261_rl_len;
    s->intra_ac_vlc_last_length = s->inter_ac_vlc_last_length = uni_h261_rl_len_last;
    ff_thread_once(&init_static_once, h261_encode_init_static);

    return ff_mpv_encode_init(avctx);
}

const FFCodec ff_h261_encoder = {
    .p.name         = "h261",
    CODEC_LONG_NAME("H.261"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_H261,
    .p.priv_class   = &ff_mpv_enc_class,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size = sizeof(H261EncContext),
    .init           = h261_encode_init,
    FF_CODEC_ENCODE_CB(ff_mpv_encode_picture),
    .close          = ff_mpv_encode_end,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    CODEC_PIXFMTS(AV_PIX_FMT_YUV420P),
    .color_ranges   = AVCOL_RANGE_MPEG,
};
