/*
 * Copyright (c) 2022 Michael Niedermayer
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

#include "bsf.h"
#include "bsf_internal.h"
#include "libavutil/colorspace.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

typedef struct DVErrorMarkerContext {
    const AVClass *class;
    uint8_t color_rgba[4];
    int sta;
    uint8_t marked_block[76];
} DVErrorMarkerContext;

static void setdc(uint8_t *b, const uint8_t color_rgba[4], int cblocks, int y_step, int v_step, int u_step) {
    for (int i=0; i<4; i++) {
        b[0] = RGB_TO_Y_JPEG(color_rgba[0], color_rgba[1],color_rgba[2]) + 128;
        b[1] = 0x06;
        b += y_step;
    }
    for (int i=0; i<cblocks; i++) {
        b[0] = RGB_TO_V_JPEG(color_rgba[0], color_rgba[1],color_rgba[2]) - 128;
        b[1] = 0x16;
        b += v_step;
    }
    for (int i=0; i<cblocks; i++) {
        b[0] = RGB_TO_U_JPEG(color_rgba[0], color_rgba[1],color_rgba[2]) - 128;
        b[1] = 0x16;
        b += u_step;
    }
}

static int dv_error_marker_init(AVBSFContext *ctx)
{
    DVErrorMarkerContext *s = ctx->priv_data;

    memset(s->marked_block, -1, 76);
    setdc(s->marked_block, s->color_rgba, 1, 14, 10, 10);
    setdc(s->marked_block, s->color_rgba, 2, 10, 10,  8);

    return 0;
}

static int dv_error_marker_filter(AVBSFContext *ctx, AVPacket *pkt)
{
    DVErrorMarkerContext *s = ctx->priv_data;
    int ret = ff_bsf_get_packet_ref(ctx, pkt);
    uint8_t *p;
    int writable = 0;
    int stamask = s->sta;
    int match_count = 0;

    if (ret < 0)
        return ret;

    p = pkt->data;
    for(int i = 0; i < pkt->size - 79; i+=80) {
        // see page 44-46 or section 5.5 of http://web.archive.org/web/20060927044735/http://www.smpte.org/smpte_store/standards/pdf/s314m.pdf.
        if ((p[i] >> 4) == 9 && ((stamask >> (p[i+3] >> 4))&1)) {
            if (!writable) {
                ret = av_packet_make_writable(pkt);
                if (ret < 0) {
                    av_packet_unref(pkt);
                    return ret;
                }
                writable = 1;
                p = pkt->data;
            }
            memcpy(p+i+4, s->marked_block, 76);
            match_count ++;
        }
    }
    av_log(ctx, AV_LOG_DEBUG, "%8"PRId64": Replaced %5d blocks by color %X\n", pkt->pts, match_count, AV_RB32(s->color_rgba));

    return 0;
}

#define OFFSET(x) offsetof(DVErrorMarkerContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption options[] = {
    { "color"  , "set color", OFFSET(color_rgba), AV_OPT_TYPE_COLOR, {.str = "yellow"}, 0,      0, FLAGS },
    { "sta"    , "specify which error status value to match"
                            , OFFSET(sta       ), AV_OPT_TYPE_FLAGS, {.i64 =   0xFFFE}, 0, 0xFFFF, FLAGS, .unit = "sta" },
    { "ok"     , "No error, no concealment",                        0, AV_OPT_TYPE_CONST, {.i64 =   0x0001}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "Aa"     , "No error, concealment from previous frame type a",0, AV_OPT_TYPE_CONST, {.i64 =   0x0004}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "Ba"     , "No error, concealment from next frame type a",    0, AV_OPT_TYPE_CONST, {.i64 =   0x0010}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "Ca"     , "No error, unspecified concealment type a",        0, AV_OPT_TYPE_CONST, {.i64 =   0x0040}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "erri"   , "Error with inserted code, No concealment",        0, AV_OPT_TYPE_CONST, {.i64 =   0x0080}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "erru"   , "Error with unidentified pos, No concealment",     0, AV_OPT_TYPE_CONST, {.i64 =   0x8000}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "err"    , "Error, No concealment",                           0, AV_OPT_TYPE_CONST, {.i64 =   0x8080}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "Ab"     , "No error, concealment from previous frame type b",0, AV_OPT_TYPE_CONST, {.i64 =   0x0400}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "Bb"     , "No error, concealment from next frame type b",    0, AV_OPT_TYPE_CONST, {.i64 =   0x1000}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "Cb"     , "No error, unspecified concealment type b",        0, AV_OPT_TYPE_CONST, {.i64 =   0x4000}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "A"      , "No error, concealment from previous frame",       0, AV_OPT_TYPE_CONST, {.i64 =   0x0404}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "B"      , "No error, concealment from next frame",           0, AV_OPT_TYPE_CONST, {.i64 =   0x1010}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "C"      , "No error, unspecified concealment",               0, AV_OPT_TYPE_CONST, {.i64 =   0x4040}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "a"      , "No error, concealment type a",                    0, AV_OPT_TYPE_CONST, {.i64 =   0x0054}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "b"      , "No error, concealment type b",                    0, AV_OPT_TYPE_CONST, {.i64 =   0x5400}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "res"    , "Reserved",                                        0, AV_OPT_TYPE_CONST, {.i64 =   0x2B2A}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "notok"  , "Error or concealment",                            0, AV_OPT_TYPE_CONST, {.i64 =   0xD4D4}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { "notres" , "Not reserved",                                    0, AV_OPT_TYPE_CONST, {.i64 =   0xD4D5}, 0, 0xFFFF, FLAGS, .unit = "sta"},
    { NULL },
};

static const AVClass dv_error_marker_class = {
    .class_name = "dv_error_marker",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFBitStreamFilter ff_dv_error_marker_bsf = {
    .p.name         = "dv_error_marker",
    .p.codec_ids    = (const enum AVCodecID []){ AV_CODEC_ID_DVVIDEO, AV_CODEC_ID_NONE },
    .p.priv_class   = &dv_error_marker_class,
    .priv_data_size = sizeof(DVErrorMarkerContext),
    .init           = dv_error_marker_init,
    .filter         = dv_error_marker_filter,
};
