/*
 * Intel MediaSDK QSV based VP9 encoder
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


#include <stdint.h>
#include <sys/types.h>

#include <mfxvideo.h>

#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "qsv.h"
#include "qsvenc.h"

typedef struct QSVVP9EncContext {
    AVClass *class;
    QSVEncContext qsv;
} QSVVP9EncContext;

static av_cold int qsv_enc_init(AVCodecContext *avctx)
{
    QSVVP9EncContext *q = avctx->priv_data;
    q->qsv.low_power = 1;

    return ff_qsv_enc_init(avctx, &q->qsv);
}

static int qsv_enc_frame(AVCodecContext *avctx, AVPacket *pkt,
                         const AVFrame *frame, int *got_packet)
{
    QSVVP9EncContext *q = avctx->priv_data;

    return ff_qsv_encode(avctx, &q->qsv, pkt, frame, got_packet);
}

static av_cold int qsv_enc_close(AVCodecContext *avctx)
{
    QSVVP9EncContext *q = avctx->priv_data;

    return ff_qsv_enc_close(avctx, &q->qsv);
}

#define OFFSET(x) offsetof(QSVVP9EncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    QSV_COMMON_OPTS

    { "profile",   NULL, OFFSET(qsv.profile), AV_OPT_TYPE_INT,   { .i64 = MFX_PROFILE_UNKNOWN },   0,       INT_MAX,  VE,  .unit = "profile" },
    { "unknown",   NULL, 0,                   AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_UNKNOWN},   INT_MIN,  INT_MAX,  VE,  .unit = "profile" },
    { "profile0",  NULL, 0,                   AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_VP9_0   },  INT_MIN,  INT_MAX,  VE,  .unit = "profile" },
    { "profile1",  NULL, 0,                   AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_VP9_1   },  INT_MIN,  INT_MAX,  VE,  .unit = "profile" },
    { "profile2",  NULL, 0,                   AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_VP9_2   },  INT_MIN,  INT_MAX,  VE,  .unit = "profile" },
    { "profile3",  NULL, 0,                   AV_OPT_TYPE_CONST, { .i64 = MFX_PROFILE_VP9_3   },  INT_MIN,  INT_MAX,  VE,  .unit = "profile" },

#if QSV_HAVE_EXT_VP9_TILES
    /* The minimum tile width in luma pixels is 256, set maximum tile_cols to 32 for 8K video */
    { "tile_cols",  "Number of columns for tiled encoding",   OFFSET(qsv.tile_cols),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 32, VE },
    /* Set maximum tile_rows to 4 per VP9 spec */
    { "tile_rows",  "Number of rows for tiled encoding",      OFFSET(qsv.tile_rows),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 4, VE },
#else
    { "tile_cols",  "(not supported)",                        OFFSET(qsv.tile_cols),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 0, VE },
    { "tile_rows",  "(not supported)",                        OFFSET(qsv.tile_rows),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 0, VE },
#endif

    { NULL },
};

static const AVClass class = {
    .class_name = "vp9_qsv encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const FFCodecDefault qsv_enc_defaults[] = {
    { "b",         "0"     },
    { "refs",      "0"     },
    { "g",         "250"   },
    { "trellis",   "-1"    },
    { "flags",     "+cgop" },
    { NULL },
};

const FFCodec ff_vp9_qsv_encoder = {
    .p.name         = "vp9_qsv",
    CODEC_LONG_NAME("VP9 video (Intel Quick Sync Video acceleration)"),
    .priv_data_size = sizeof(QSVVP9EncContext),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VP9,
    .init           = qsv_enc_init,
    FF_CODEC_ENCODE_CB(qsv_enc_frame),
    .close          = qsv_enc_close,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HYBRID,
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_NV12,
                                                    AV_PIX_FMT_P010,
                                                    AV_PIX_FMT_VUYX,
                                                    AV_PIX_FMT_QSV,
                                                    AV_PIX_FMT_XV30,
                                                    AV_PIX_FMT_NONE },
    .p.priv_class   = &class,
    .defaults       = qsv_enc_defaults,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .p.wrapper_name = "qsv",
    .hw_configs     = ff_qsv_enc_hw_configs,
};
