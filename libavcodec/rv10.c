/*
 * RV10/RV20 decoder
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer
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
 * RV10/RV20 decoder
 */

#include <inttypes.h>

#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "error_resilience.h"
#include "h263.h"
#include "internal.h"
#include "mpeg_er.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "mpeg4video.h"
#include "mpegvideodata.h"

#define RV_GET_MAJOR_VER(x)  ((x) >> 28)
#define RV_GET_MINOR_VER(x) (((x) >> 20) & 0xFF)
#define RV_GET_MICRO_VER(x) (((x) >> 12) & 0xFF)

#define DC_VLC_BITS 14 // FIXME find a better solution

typedef struct RVDecContext {
    MpegEncContext m;
    int sub_id;
    int orig_width, orig_height;
} RVDecContext;

static const uint16_t rv_lum_code[256] = {
    0x3e7f, 0x0f00, 0x0f01, 0x0f02, 0x0f03, 0x0f04, 0x0f05, 0x0f06,
    0x0f07, 0x0f08, 0x0f09, 0x0f0a, 0x0f0b, 0x0f0c, 0x0f0d, 0x0f0e,
    0x0f0f, 0x0f10, 0x0f11, 0x0f12, 0x0f13, 0x0f14, 0x0f15, 0x0f16,
    0x0f17, 0x0f18, 0x0f19, 0x0f1a, 0x0f1b, 0x0f1c, 0x0f1d, 0x0f1e,
    0x0f1f, 0x0f20, 0x0f21, 0x0f22, 0x0f23, 0x0f24, 0x0f25, 0x0f26,
    0x0f27, 0x0f28, 0x0f29, 0x0f2a, 0x0f2b, 0x0f2c, 0x0f2d, 0x0f2e,
    0x0f2f, 0x0f30, 0x0f31, 0x0f32, 0x0f33, 0x0f34, 0x0f35, 0x0f36,
    0x0f37, 0x0f38, 0x0f39, 0x0f3a, 0x0f3b, 0x0f3c, 0x0f3d, 0x0f3e,
    0x0f3f, 0x0380, 0x0381, 0x0382, 0x0383, 0x0384, 0x0385, 0x0386,
    0x0387, 0x0388, 0x0389, 0x038a, 0x038b, 0x038c, 0x038d, 0x038e,
    0x038f, 0x0390, 0x0391, 0x0392, 0x0393, 0x0394, 0x0395, 0x0396,
    0x0397, 0x0398, 0x0399, 0x039a, 0x039b, 0x039c, 0x039d, 0x039e,
    0x039f, 0x00c0, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6,
    0x00c7, 0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce,
    0x00cf, 0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056,
    0x0057, 0x0020, 0x0021, 0x0022, 0x0023, 0x000c, 0x000d, 0x0004,
    0x0000, 0x0005, 0x000e, 0x000f, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
    0x00d0, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00d7,
    0x00d8, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x00dd, 0x00de, 0x00df,
    0x03a0, 0x03a1, 0x03a2, 0x03a3, 0x03a4, 0x03a5, 0x03a6, 0x03a7,
    0x03a8, 0x03a9, 0x03aa, 0x03ab, 0x03ac, 0x03ad, 0x03ae, 0x03af,
    0x03b0, 0x03b1, 0x03b2, 0x03b3, 0x03b4, 0x03b5, 0x03b6, 0x03b7,
    0x03b8, 0x03b9, 0x03ba, 0x03bb, 0x03bc, 0x03bd, 0x03be, 0x03bf,
    0x0f40, 0x0f41, 0x0f42, 0x0f43, 0x0f44, 0x0f45, 0x0f46, 0x0f47,
    0x0f48, 0x0f49, 0x0f4a, 0x0f4b, 0x0f4c, 0x0f4d, 0x0f4e, 0x0f4f,
    0x0f50, 0x0f51, 0x0f52, 0x0f53, 0x0f54, 0x0f55, 0x0f56, 0x0f57,
    0x0f58, 0x0f59, 0x0f5a, 0x0f5b, 0x0f5c, 0x0f5d, 0x0f5e, 0x0f5f,
    0x0f60, 0x0f61, 0x0f62, 0x0f63, 0x0f64, 0x0f65, 0x0f66, 0x0f67,
    0x0f68, 0x0f69, 0x0f6a, 0x0f6b, 0x0f6c, 0x0f6d, 0x0f6e, 0x0f6f,
    0x0f70, 0x0f71, 0x0f72, 0x0f73, 0x0f74, 0x0f75, 0x0f76, 0x0f77,
    0x0f78, 0x0f79, 0x0f7a, 0x0f7b, 0x0f7c, 0x0f7d, 0x0f7e, 0x0f7f,
};

static const uint8_t rv_lum_bits[256] = {
    14, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10,
    10,  8,  8,  8,  8,  8,  8,  8,
     8,  8,  8,  8,  8,  8,  8,  8,
     8,  7,  7,  7,  7,  7,  7,  7,
     7,  6,  6,  6,  6,  5,  5,  4,
     2,  4,  5,  5,  6,  6,  6,  6,
     7,  7,  7,  7,  7,  7,  7,  7,
     8,  8,  8,  8,  8,  8,  8,  8,
     8,  8,  8,  8,  8,  8,  8,  8,
    10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
};

static const uint16_t rv_chrom_code[256] = {
    0xfe7f, 0x3f00, 0x3f01, 0x3f02, 0x3f03, 0x3f04, 0x3f05, 0x3f06,
    0x3f07, 0x3f08, 0x3f09, 0x3f0a, 0x3f0b, 0x3f0c, 0x3f0d, 0x3f0e,
    0x3f0f, 0x3f10, 0x3f11, 0x3f12, 0x3f13, 0x3f14, 0x3f15, 0x3f16,
    0x3f17, 0x3f18, 0x3f19, 0x3f1a, 0x3f1b, 0x3f1c, 0x3f1d, 0x3f1e,
    0x3f1f, 0x3f20, 0x3f21, 0x3f22, 0x3f23, 0x3f24, 0x3f25, 0x3f26,
    0x3f27, 0x3f28, 0x3f29, 0x3f2a, 0x3f2b, 0x3f2c, 0x3f2d, 0x3f2e,
    0x3f2f, 0x3f30, 0x3f31, 0x3f32, 0x3f33, 0x3f34, 0x3f35, 0x3f36,
    0x3f37, 0x3f38, 0x3f39, 0x3f3a, 0x3f3b, 0x3f3c, 0x3f3d, 0x3f3e,
    0x3f3f, 0x0f80, 0x0f81, 0x0f82, 0x0f83, 0x0f84, 0x0f85, 0x0f86,
    0x0f87, 0x0f88, 0x0f89, 0x0f8a, 0x0f8b, 0x0f8c, 0x0f8d, 0x0f8e,
    0x0f8f, 0x0f90, 0x0f91, 0x0f92, 0x0f93, 0x0f94, 0x0f95, 0x0f96,
    0x0f97, 0x0f98, 0x0f99, 0x0f9a, 0x0f9b, 0x0f9c, 0x0f9d, 0x0f9e,
    0x0f9f, 0x03c0, 0x03c1, 0x03c2, 0x03c3, 0x03c4, 0x03c5, 0x03c6,
    0x03c7, 0x03c8, 0x03c9, 0x03ca, 0x03cb, 0x03cc, 0x03cd, 0x03ce,
    0x03cf, 0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6,
    0x00e7, 0x0030, 0x0031, 0x0032, 0x0033, 0x0008, 0x0009, 0x0002,
    0x0000, 0x0003, 0x000a, 0x000b, 0x0034, 0x0035, 0x0036, 0x0037,
    0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef,
    0x03d0, 0x03d1, 0x03d2, 0x03d3, 0x03d4, 0x03d5, 0x03d6, 0x03d7,
    0x03d8, 0x03d9, 0x03da, 0x03db, 0x03dc, 0x03dd, 0x03de, 0x03df,
    0x0fa0, 0x0fa1, 0x0fa2, 0x0fa3, 0x0fa4, 0x0fa5, 0x0fa6, 0x0fa7,
    0x0fa8, 0x0fa9, 0x0faa, 0x0fab, 0x0fac, 0x0fad, 0x0fae, 0x0faf,
    0x0fb0, 0x0fb1, 0x0fb2, 0x0fb3, 0x0fb4, 0x0fb5, 0x0fb6, 0x0fb7,
    0x0fb8, 0x0fb9, 0x0fba, 0x0fbb, 0x0fbc, 0x0fbd, 0x0fbe, 0x0fbf,
    0x3f40, 0x3f41, 0x3f42, 0x3f43, 0x3f44, 0x3f45, 0x3f46, 0x3f47,
    0x3f48, 0x3f49, 0x3f4a, 0x3f4b, 0x3f4c, 0x3f4d, 0x3f4e, 0x3f4f,
    0x3f50, 0x3f51, 0x3f52, 0x3f53, 0x3f54, 0x3f55, 0x3f56, 0x3f57,
    0x3f58, 0x3f59, 0x3f5a, 0x3f5b, 0x3f5c, 0x3f5d, 0x3f5e, 0x3f5f,
    0x3f60, 0x3f61, 0x3f62, 0x3f63, 0x3f64, 0x3f65, 0x3f66, 0x3f67,
    0x3f68, 0x3f69, 0x3f6a, 0x3f6b, 0x3f6c, 0x3f6d, 0x3f6e, 0x3f6f,
    0x3f70, 0x3f71, 0x3f72, 0x3f73, 0x3f74, 0x3f75, 0x3f76, 0x3f77,
    0x3f78, 0x3f79, 0x3f7a, 0x3f7b, 0x3f7c, 0x3f7d, 0x3f7e, 0x3f7f,
};

static const uint8_t rv_chrom_bits[256] = {
    16, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14,
    14, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10,
    10,  8,  8,  8,  8,  8,  8,  8,
     8,  6,  6,  6,  6,  4,  4,  3,
     2,  3,  4,  4,  6,  6,  6,  6,
     8,  8,  8,  8,  8,  8,  8,  8,
    10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12,
    14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14,
};

static VLC rv_dc_lum, rv_dc_chrom;

int ff_rv_decode_dc(MpegEncContext *s, int n)
{
    int code;

    if (n < 4) {
        code = get_vlc2(&s->gb, rv_dc_lum.table, DC_VLC_BITS, 2);
        if (code < 0) {
            /* XXX: I don't understand why they use LONGER codes than
             * necessary. The following code would be completely useless
             * if they had thought about it !!! */
            code = get_bits(&s->gb, 7);
            if (code == 0x7c) {
                code = (int8_t) (get_bits(&s->gb, 7) + 1);
            } else if (code == 0x7d) {
                code = -128 + get_bits(&s->gb, 7);
            } else if (code == 0x7e) {
                if (get_bits1(&s->gb) == 0)
                    code = (int8_t) (get_bits(&s->gb, 8) + 1);
                else
                    code = (int8_t) (get_bits(&s->gb, 8));
            } else if (code == 0x7f) {
                skip_bits(&s->gb, 11);
                code = 1;
            }
        } else {
            code -= 128;
        }
    } else {
        code = get_vlc2(&s->gb, rv_dc_chrom.table, DC_VLC_BITS, 2);
        /* same remark */
        if (code < 0) {
            code = get_bits(&s->gb, 9);
            if (code == 0x1fc) {
                code = (int8_t) (get_bits(&s->gb, 7) + 1);
            } else if (code == 0x1fd) {
                code = -128 + get_bits(&s->gb, 7);
            } else if (code == 0x1fe) {
                skip_bits(&s->gb, 9);
                code = 1;
            } else {
                av_log(s->avctx, AV_LOG_ERROR, "chroma dc error\n");
                return 0xffff;
            }
        } else {
            code -= 128;
        }
    }
    return -code;
}

/* read RV 1.0 compatible frame header */
static int rv10_decode_picture_header(MpegEncContext *s)
{
    int mb_count, pb_frame, marker, mb_xy;

    marker = get_bits1(&s->gb);

    if (get_bits1(&s->gb))
        s->pict_type = AV_PICTURE_TYPE_P;
    else
        s->pict_type = AV_PICTURE_TYPE_I;

    if (!marker)
        av_log(s->avctx, AV_LOG_ERROR, "marker missing\n");

    pb_frame = get_bits1(&s->gb);

    ff_dlog(s->avctx, "pict_type=%d pb_frame=%d\n", s->pict_type, pb_frame);

    if (pb_frame) {
        avpriv_request_sample(s->avctx, "pb frame");
        return AVERROR_PATCHWELCOME;
    }

    s->qscale = get_bits(&s->gb, 5);
    if (s->qscale == 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid qscale value: 0\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->pict_type == AV_PICTURE_TYPE_I) {
        if (s->rv10_version == 3) {
            /* specific MPEG like DC coding not used */
            s->last_dc[0] = get_bits(&s->gb, 8);
            s->last_dc[1] = get_bits(&s->gb, 8);
            s->last_dc[2] = get_bits(&s->gb, 8);
            ff_dlog(s->avctx, "DC:%d %d %d\n", s->last_dc[0],
                    s->last_dc[1], s->last_dc[2]);
        }
    }
    /* if multiple packets per frame are sent, the position at which
     * to display the macroblocks is coded here */

    mb_xy = s->mb_x + s->mb_y * s->mb_width;
    if (show_bits(&s->gb, 12) == 0 || (mb_xy && mb_xy < s->mb_num)) {
        s->mb_x  = get_bits(&s->gb, 6); /* mb_x */
        s->mb_y  = get_bits(&s->gb, 6); /* mb_y */
        mb_count = get_bits(&s->gb, 12);
    } else {
        s->mb_x  = 0;
        s->mb_y  = 0;
        mb_count = s->mb_width * s->mb_height;
    }
    skip_bits(&s->gb, 3);   /* ignored */
    s->f_code          = 1;
    s->unrestricted_mv = 1;

    return mb_count;
}

static int rv20_decode_picture_header(RVDecContext *rv)
{
    MpegEncContext *s = &rv->m;
    int seq, mb_pos, i, ret;
    int rpr_max;

    i = get_bits(&s->gb, 2);
    switch (i) {
    case 0:
        s->pict_type = AV_PICTURE_TYPE_I;
        break;
    case 1:
        s->pict_type = AV_PICTURE_TYPE_I;
        break;                                  // hmm ...
    case 2:
        s->pict_type = AV_PICTURE_TYPE_P;
        break;
    case 3:
        s->pict_type = AV_PICTURE_TYPE_B;
        break;
    default:
        av_log(s->avctx, AV_LOG_ERROR, "unknown frame type\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->low_delay && s->pict_type == AV_PICTURE_TYPE_B) {
        av_log(s->avctx, AV_LOG_ERROR, "low delay B\n");
        return -1;
    }
    if (!s->last_picture_ptr && s->pict_type == AV_PICTURE_TYPE_B) {
        av_log(s->avctx, AV_LOG_ERROR, "early B-frame\n");
        return AVERROR_INVALIDDATA;
    }

    if (get_bits1(&s->gb)) {
        av_log(s->avctx, AV_LOG_ERROR, "reserved bit set\n");
        return AVERROR_INVALIDDATA;
    }

    s->qscale = get_bits(&s->gb, 5);
    if (s->qscale == 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid qscale value: 0\n");
        return AVERROR_INVALIDDATA;
    }

    if (RV_GET_MINOR_VER(rv->sub_id) >= 2)
        s->loop_filter = get_bits1(&s->gb) && !s->avctx->lowres;

    if (RV_GET_MINOR_VER(rv->sub_id) <= 1)
        seq = get_bits(&s->gb, 8) << 7;
    else
        seq = get_bits(&s->gb, 13) << 2;

    rpr_max = s->avctx->extradata[1] & 7;
    if (rpr_max) {
        int f, new_w, new_h;
        int rpr_bits = av_log2(rpr_max) + 1;

        f = get_bits(&s->gb, rpr_bits);

        if (f) {
            if (s->avctx->extradata_size < 8 + 2 * f) {
                av_log(s->avctx, AV_LOG_ERROR, "Extradata too small.\n");
                return AVERROR_INVALIDDATA;
            }

            new_w = 4 * ((uint8_t *) s->avctx->extradata)[6 + 2 * f];
            new_h = 4 * ((uint8_t *) s->avctx->extradata)[7 + 2 * f];
        } else {
            new_w = rv->orig_width;
            new_h = rv->orig_height;
        }
        if (new_w != s->width || new_h != s->height) {
            AVRational old_aspect = s->avctx->sample_aspect_ratio;
            av_log(s->avctx, AV_LOG_DEBUG,
                   "attempting to change resolution to %dx%d\n", new_w, new_h);
            if (av_image_check_size(new_w, new_h, 0, s->avctx) < 0)
                return AVERROR_INVALIDDATA;
            ff_mpv_common_end(s);

            // attempt to keep aspect during typical resolution switches
            if (!old_aspect.num)
                old_aspect = (AVRational){1, 1};
            if (2 * new_w * s->height == new_h * s->width)
                s->avctx->sample_aspect_ratio = av_mul_q(old_aspect, (AVRational){2, 1});
            if (new_w * s->height == 2 * new_h * s->width)
                s->avctx->sample_aspect_ratio = av_mul_q(old_aspect, (AVRational){1, 2});

            ret = ff_set_dimensions(s->avctx, new_w, new_h);
            if (ret < 0)
                return ret;

            s->width  = new_w;
            s->height = new_h;
            if ((ret = ff_mpv_common_init(s)) < 0)
                return ret;
        }

        if (s->avctx->debug & FF_DEBUG_PICT_INFO) {
            av_log(s->avctx, AV_LOG_DEBUG, "F %d/%d/%d\n", f, rpr_bits, rpr_max);
        }
    }
    if (av_image_check_size(s->width, s->height, 0, s->avctx) < 0)
        return AVERROR_INVALIDDATA;

    mb_pos = ff_h263_decode_mba(s);

    seq |= s->time & ~0x7FFF;
    if (seq - s->time >  0x4000)
        seq -= 0x8000;
    if (seq - s->time < -0x4000)
        seq += 0x8000;

    if (seq != s->time) {
        if (s->pict_type != AV_PICTURE_TYPE_B) {
            s->time            = seq;
            s->pp_time         = s->time - s->last_non_b_time;
            s->last_non_b_time = s->time;
        } else {
            s->time    = seq;
            s->pb_time = s->pp_time - (s->last_non_b_time - s->time);
        }
    }
    if (s->pict_type == AV_PICTURE_TYPE_B) {
        if (s->pp_time <=s->pb_time || s->pp_time <= s->pp_time - s->pb_time || s->pp_time<=0) {
            av_log(s->avctx, AV_LOG_DEBUG,
                   "messed up order, possible from seeking? skipping current b frame\n");
#define ERROR_SKIP_FRAME -123
            return ERROR_SKIP_FRAME;
        }
        ff_mpeg4_init_direct_mv(s);
    }

    s->no_rounding = get_bits1(&s->gb);

    if (RV_GET_MINOR_VER(rv->sub_id) <= 1 && s->pict_type == AV_PICTURE_TYPE_B)
        // binary decoder reads 3+2 bits here but they don't seem to be used
        skip_bits(&s->gb, 5);

    s->f_code          = 1;
    s->unrestricted_mv = 1;
    s->h263_aic        = s->pict_type == AV_PICTURE_TYPE_I;
    s->modified_quant  = 1;
    if (!s->avctx->lowres)
        s->loop_filter = 1;

    if (s->avctx->debug & FF_DEBUG_PICT_INFO) {
        av_log(s->avctx, AV_LOG_INFO,
               "num:%5d x:%2d y:%2d type:%d qscale:%2d rnd:%d\n",
               seq, s->mb_x, s->mb_y, s->pict_type, s->qscale,
               s->no_rounding);
    }

    av_assert0(s->pict_type != AV_PICTURE_TYPE_B || !s->low_delay);

    return s->mb_width * s->mb_height - mb_pos;
}

static av_cold int rv10_decode_init(AVCodecContext *avctx)
{
    RVDecContext *rv = avctx->priv_data;
    MpegEncContext *s = &rv->m;
    static int done = 0;
    int major_ver, minor_ver, micro_ver, ret;

    if (avctx->extradata_size < 8) {
        av_log(avctx, AV_LOG_ERROR, "Extradata is too small.\n");
        return AVERROR_INVALIDDATA;
    }
    if ((ret = av_image_check_size(avctx->coded_width,
                                   avctx->coded_height, 0, avctx)) < 0)
        return ret;

    ff_mpv_decode_defaults(s);
    ff_mpv_decode_init(s, avctx);

    s->out_format  = FMT_H263;

    rv->orig_width  =
    s->width        = avctx->coded_width;
    rv->orig_height =
    s->height       = avctx->coded_height;

    s->h263_long_vectors = ((uint8_t *) avctx->extradata)[3] & 1;
    rv->sub_id           = AV_RB32((uint8_t *) avctx->extradata + 4);

    major_ver = RV_GET_MAJOR_VER(rv->sub_id);
    minor_ver = RV_GET_MINOR_VER(rv->sub_id);
    micro_ver = RV_GET_MICRO_VER(rv->sub_id);

    s->low_delay = 1;
    switch (major_ver) {
    case 1:
        s->rv10_version = micro_ver ? 3 : 1;
        s->obmc         = micro_ver == 2;
        break;
    case 2:
        if (minor_ver >= 2) {
            s->low_delay           = 0;
            s->avctx->has_b_frames = 1;
        }
        break;
    default:
        av_log(s->avctx, AV_LOG_ERROR, "unknown header %X\n", rv->sub_id);
        avpriv_request_sample(avctx, "RV1/2 version");
        return AVERROR_PATCHWELCOME;
    }

    if (avctx->debug & FF_DEBUG_PICT_INFO) {
        av_log(avctx, AV_LOG_DEBUG, "ver:%X ver0:%"PRIX32"\n", rv->sub_id,
               ((uint32_t *) avctx->extradata)[0]);
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    ff_mpv_idct_init(s);
    if ((ret = ff_mpv_common_init(s)) < 0)
        return ret;

    ff_h263dsp_init(&s->h263dsp);
    ff_h263_decode_init_vlc();

    /* init rv vlc */
    if (!done) {
        INIT_VLC_STATIC(&rv_dc_lum, DC_VLC_BITS, 256,
                        rv_lum_bits, 1, 1,
                        rv_lum_code, 2, 2, 16384);
        INIT_VLC_STATIC(&rv_dc_chrom, DC_VLC_BITS, 256,
                        rv_chrom_bits, 1, 1,
                        rv_chrom_code, 2, 2, 16388);
        done = 1;
    }

    return 0;
}

static av_cold int rv10_decode_end(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;

    ff_mpv_common_end(s);
    return 0;
}

static int rv10_decode_packet(AVCodecContext *avctx, const uint8_t *buf,
                              int buf_size, int buf_size2)
{
    RVDecContext *rv = avctx->priv_data;
    MpegEncContext *s = &rv->m;
    int mb_count, mb_pos, left, start_mb_x, active_bits_size, ret;

    active_bits_size = buf_size * 8;
    init_get_bits(&s->gb, buf, FFMAX(buf_size, buf_size2) * 8);
    if (s->codec_id == AV_CODEC_ID_RV10)
        mb_count = rv10_decode_picture_header(s);
    else
        mb_count = rv20_decode_picture_header(rv);
    if (mb_count < 0) {
        if (mb_count != ERROR_SKIP_FRAME)
            av_log(s->avctx, AV_LOG_ERROR, "HEADER ERROR\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->mb_x >= s->mb_width ||
        s->mb_y >= s->mb_height) {
        av_log(s->avctx, AV_LOG_ERROR, "POS ERROR %d %d\n", s->mb_x, s->mb_y);
        return AVERROR_INVALIDDATA;
    }
    mb_pos = s->mb_y * s->mb_width + s->mb_x;
    left   = s->mb_width * s->mb_height - mb_pos;
    if (mb_count > left) {
        av_log(s->avctx, AV_LOG_ERROR, "COUNT ERROR\n");
        return AVERROR_INVALIDDATA;
    }

    if ((s->mb_x == 0 && s->mb_y == 0) || !s->current_picture_ptr) {
        // FIXME write parser so we always have complete frames?
        if (s->current_picture_ptr) {
            ff_er_frame_end(&s->er);
            ff_mpv_frame_end(s);
            s->mb_x = s->mb_y = s->resync_mb_x = s->resync_mb_y = 0;
        }
        if ((ret = ff_mpv_frame_start(s, avctx)) < 0)
            return ret;
        ff_mpeg_er_frame_start(s);
    } else {
        if (s->current_picture_ptr->f->pict_type != s->pict_type) {
            av_log(s->avctx, AV_LOG_ERROR, "Slice type mismatch\n");
            return AVERROR_INVALIDDATA;
        }
    }


    ff_dlog(avctx, "qscale=%d\n", s->qscale);

    /* default quantization values */
    if (s->codec_id == AV_CODEC_ID_RV10) {
        if (s->mb_y == 0)
            s->first_slice_line = 1;
    } else {
        s->first_slice_line = 1;
        s->resync_mb_x      = s->mb_x;
    }
    start_mb_x     = s->mb_x;
    s->resync_mb_y = s->mb_y;
    if (s->h263_aic) {
        s->y_dc_scale_table =
        s->c_dc_scale_table = ff_aic_dc_scale_table;
    } else {
        s->y_dc_scale_table =
        s->c_dc_scale_table = ff_mpeg1_dc_scale_table;
    }

    if (s->modified_quant)
        s->chroma_qscale_table = ff_h263_chroma_qscale_table;

    ff_set_qscale(s, s->qscale);

    s->rv10_first_dc_coded[0] = 0;
    s->rv10_first_dc_coded[1] = 0;
    s->rv10_first_dc_coded[2] = 0;
    s->block_wrap[0] =
    s->block_wrap[1] =
    s->block_wrap[2] =
    s->block_wrap[3] = s->b8_stride;
    s->block_wrap[4] =
    s->block_wrap[5] = s->mb_stride;
    ff_init_block_index(s);

    /* decode each macroblock */
    for (s->mb_num_left = mb_count; s->mb_num_left > 0; s->mb_num_left--) {
        int ret;
        ff_update_block_index(s);
        ff_tlog(avctx, "**mb x=%d y=%d\n", s->mb_x, s->mb_y);

        s->mv_dir  = MV_DIR_FORWARD;
        s->mv_type = MV_TYPE_16X16;
        ret = ff_h263_decode_mb(s, s->block);

        // Repeat the slice end check from ff_h263_decode_mb with our active
        // bitstream size
        if (ret != SLICE_ERROR) {
            int v = show_bits(&s->gb, 16);

            if (get_bits_count(&s->gb) + 16 > active_bits_size)
                v >>= get_bits_count(&s->gb) + 16 - active_bits_size;

            if (!v)
                ret = SLICE_END;
        }
        if (ret != SLICE_ERROR && active_bits_size < get_bits_count(&s->gb) &&
            8 * buf_size2 >= get_bits_count(&s->gb)) {
            active_bits_size = buf_size2 * 8;
            av_log(avctx, AV_LOG_DEBUG, "update size from %d to %d\n",
                   8 * buf_size, active_bits_size);
            ret = SLICE_OK;
        }

        if (ret == SLICE_ERROR || active_bits_size < get_bits_count(&s->gb)) {
            av_log(s->avctx, AV_LOG_ERROR, "ERROR at MB %d %d\n", s->mb_x,
                   s->mb_y);
            return AVERROR_INVALIDDATA;
        }
        if (s->pict_type != AV_PICTURE_TYPE_B)
            ff_h263_update_motion_val(s);
        ff_mpv_decode_mb(s, s->block);
        if (s->loop_filter)
            ff_h263_loop_filter(s);

        if (++s->mb_x == s->mb_width) {
            s->mb_x = 0;
            s->mb_y++;
            ff_init_block_index(s);
        }
        if (s->mb_x == s->resync_mb_x)
            s->first_slice_line = 0;
        if (ret == SLICE_END)
            break;
    }

    ff_er_add_slice(&s->er, start_mb_x, s->resync_mb_y, s->mb_x - 1, s->mb_y,
                    ER_MB_END);

    return active_bits_size;
}

static int get_slice_offset(AVCodecContext *avctx, const uint8_t *buf, int n)
{
    if (avctx->slice_count)
        return avctx->slice_offset[n];
    else
        return AV_RL32(buf + n * 8);
}

static int rv10_decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                             AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    MpegEncContext *s = avctx->priv_data;
    AVFrame *pict = data;
    int i, ret;
    int slice_count;
    const uint8_t *slices_hdr = NULL;

    ff_dlog(avctx, "*****frame %d size=%d\n", avctx->frame_number, buf_size);

    /* no supplementary picture */
    if (buf_size == 0) {
        return 0;
    }

    if (!avctx->slice_count) {
        slice_count = (*buf++) + 1;
        buf_size--;

        if (!slice_count || buf_size <= 8 * slice_count) {
            av_log(avctx, AV_LOG_ERROR, "Invalid slice count: %d.\n",
                   slice_count);
            return AVERROR_INVALIDDATA;
        }

        slices_hdr = buf + 4;
        buf       += 8 * slice_count;
        buf_size  -= 8 * slice_count;
    } else
        slice_count = avctx->slice_count;

    for (i = 0; i < slice_count; i++) {
        unsigned offset = get_slice_offset(avctx, slices_hdr, i);
        int size, size2;

        if (offset >= buf_size)
            return AVERROR_INVALIDDATA;

        if (i + 1 == slice_count)
            size = buf_size - offset;
        else
            size = get_slice_offset(avctx, slices_hdr, i + 1) - offset;

        if (i + 2 >= slice_count)
            size2 = buf_size - offset;
        else
            size2 = get_slice_offset(avctx, slices_hdr, i + 2) - offset;

        if (size <= 0 || size2 <= 0 ||
            offset + FFMAX(size, size2) > buf_size)
            return AVERROR_INVALIDDATA;

        if ((ret = rv10_decode_packet(avctx, buf + offset, size, size2)) < 0)
            return ret;

        if (ret > 8 * size)
            i++;
    }

    if (s->current_picture_ptr && s->mb_y >= s->mb_height) {
        ff_er_frame_end(&s->er);
        ff_mpv_frame_end(s);

        if (s->pict_type == AV_PICTURE_TYPE_B || s->low_delay) {
            if ((ret = av_frame_ref(pict, s->current_picture_ptr->f)) < 0)
                return ret;
            ff_print_debug_info(s, s->current_picture_ptr, pict);
            ff_mpv_export_qp_table(s, pict, s->current_picture_ptr, FF_QSCALE_TYPE_MPEG1);
        } else if (s->last_picture_ptr) {
            if ((ret = av_frame_ref(pict, s->last_picture_ptr->f)) < 0)
                return ret;
            ff_print_debug_info(s, s->last_picture_ptr, pict);
            ff_mpv_export_qp_table(s, pict,s->last_picture_ptr, FF_QSCALE_TYPE_MPEG1);
        }

        if (s->last_picture_ptr || s->low_delay) {
            *got_frame = 1;
        }

        // so we can detect if frame_end was not called (find some nicer solution...)
        s->current_picture_ptr = NULL;
    }

    return avpkt->size;
}

AVCodec ff_rv10_decoder = {
    .name           = "rv10",
    .long_name      = NULL_IF_CONFIG_SMALL("RealVideo 1.0"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_RV10,
    .priv_data_size = sizeof(RVDecContext),
    .init           = rv10_decode_init,
    .close          = rv10_decode_end,
    .decode         = rv10_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .max_lowres     = 3,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    },
};

AVCodec ff_rv20_decoder = {
    .name           = "rv20",
    .long_name      = NULL_IF_CONFIG_SMALL("RealVideo 2.0"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_RV20,
    .priv_data_size = sizeof(RVDecContext),
    .init           = rv10_decode_init,
    .close          = rv10_decode_end,
    .decode         = rv10_decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_DELAY,
    .flush          = ff_mpeg_flush,
    .max_lowres     = 3,
    .pix_fmts       = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    },
};
