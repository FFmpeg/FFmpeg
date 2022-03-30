/*
 * librav1e encoder
 *
 * Copyright (c) 2019 Derek Buitenhuis
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

#include <rav1e.h>

#include "libavutil/internal.h"
#include "libavutil/avassert.h"
#include "libavutil/base64.h"
#include "libavutil/common.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "bsf.h"
#include "codec_internal.h"
#include "encode.h"
#include "internal.h"

typedef struct librav1eContext {
    const AVClass *class;

    RaContext *ctx;
    AVFrame *frame;
    RaFrame *rframe;
    AVBSFContext *bsf;

    uint8_t *pass_data;
    size_t pass_pos;
    int pass_size;

    AVDictionary *rav1e_opts;
    int quantizer;
    int speed;
    int tiles;
    int tile_rows;
    int tile_cols;
} librav1eContext;

static inline RaPixelRange range_map(enum AVPixelFormat pix_fmt, enum AVColorRange range)
{
    switch (pix_fmt) {
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUVJ444P:
        return RA_PIXEL_RANGE_FULL;
    }

    switch (range) {
    case AVCOL_RANGE_JPEG:
        return RA_PIXEL_RANGE_FULL;
    case AVCOL_RANGE_MPEG:
    default:
        return RA_PIXEL_RANGE_LIMITED;
    }
}

static inline RaChromaSampling pix_fmt_map(enum AVPixelFormat pix_fmt)
{
    switch (pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUV420P10:
    case AV_PIX_FMT_YUV420P12:
        return RA_CHROMA_SAMPLING_CS420;
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUV422P10:
    case AV_PIX_FMT_YUV422P12:
        return RA_CHROMA_SAMPLING_CS422;
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_YUV444P10:
    case AV_PIX_FMT_YUV444P12:
        return RA_CHROMA_SAMPLING_CS444;
    default:
        av_assert0(0);
    }
}

static inline RaChromaSamplePosition chroma_loc_map(enum AVChromaLocation chroma_loc)
{
    switch (chroma_loc) {
    case AVCHROMA_LOC_LEFT:
        return RA_CHROMA_SAMPLE_POSITION_VERTICAL;
    case AVCHROMA_LOC_TOPLEFT:
        return RA_CHROMA_SAMPLE_POSITION_COLOCATED;
    default:
        return RA_CHROMA_SAMPLE_POSITION_UNKNOWN;
    }
}

static int get_stats(AVCodecContext *avctx, int eos)
{
    librav1eContext *ctx = avctx->priv_data;
    RaData* buf = rav1e_twopass_out(ctx->ctx);
    if (!buf)
        return 0;

    if (!eos) {
        uint8_t *tmp = av_fast_realloc(ctx->pass_data, &ctx->pass_size,
                                      ctx->pass_pos + buf->len);
        if (!tmp) {
            rav1e_data_unref(buf);
            return AVERROR(ENOMEM);
        }

        ctx->pass_data = tmp;
        memcpy(ctx->pass_data + ctx->pass_pos, buf->data, buf->len);
        ctx->pass_pos += buf->len;
    } else {
        size_t b64_size = AV_BASE64_SIZE(ctx->pass_pos);

        memcpy(ctx->pass_data, buf->data, buf->len);

        avctx->stats_out = av_malloc(b64_size);
        if (!avctx->stats_out) {
            rav1e_data_unref(buf);
            return AVERROR(ENOMEM);
        }

        av_base64_encode(avctx->stats_out, b64_size, ctx->pass_data, ctx->pass_pos);

        av_freep(&ctx->pass_data);
    }

    rav1e_data_unref(buf);

    return 0;
}

static int set_stats(AVCodecContext *avctx)
{
    librav1eContext *ctx = avctx->priv_data;
    int ret = 1;

    while (ret > 0 && ctx->pass_size - ctx->pass_pos > 0) {
        ret = rav1e_twopass_in(ctx->ctx, ctx->pass_data + ctx->pass_pos, ctx->pass_size);
        if (ret < 0)
            return AVERROR_EXTERNAL;
        ctx->pass_pos += ret;
    }

    return 0;
}

static av_cold int librav1e_encode_close(AVCodecContext *avctx)
{
    librav1eContext *ctx = avctx->priv_data;

    if (ctx->ctx) {
        rav1e_context_unref(ctx->ctx);
        ctx->ctx = NULL;
    }
    if (ctx->rframe) {
        rav1e_frame_unref(ctx->rframe);
        ctx->rframe = NULL;
    }

    av_frame_free(&ctx->frame);
    av_bsf_free(&ctx->bsf);
    av_freep(&ctx->pass_data);

    return 0;
}

static av_cold int librav1e_encode_init(AVCodecContext *avctx)
{
    librav1eContext *ctx = avctx->priv_data;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    RaConfig *cfg = NULL;
    int rret;
    int ret = 0;

    ctx->frame = av_frame_alloc();
    if (!ctx->frame)
        return AVERROR(ENOMEM);

    cfg = rav1e_config_default();
    if (!cfg) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate rav1e config.\n");
        return AVERROR_EXTERNAL;
    }

    /*
     * Rav1e currently uses the time base given to it only for ratecontrol... where
     * the inverse is taken and used as a framerate. So, do what we do in other wrappers
     * and use the framerate if we can.
     */
    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        rav1e_config_set_time_base(cfg, (RaRational) {
                                   avctx->framerate.den, avctx->framerate.num
                                   });
    } else {
        rav1e_config_set_time_base(cfg, (RaRational) {
                                   avctx->time_base.num * avctx->ticks_per_frame,
                                   avctx->time_base.den
                                   });
    }

    if ((avctx->flags & AV_CODEC_FLAG_PASS1 || avctx->flags & AV_CODEC_FLAG_PASS2) && !avctx->bit_rate) {
        av_log(avctx, AV_LOG_ERROR, "A bitrate must be set to use two pass mode.\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    if (avctx->flags & AV_CODEC_FLAG_PASS2) {
        if (!avctx->stats_in) {
            av_log(avctx, AV_LOG_ERROR, "No stats file provided for second pass.\n");
            ret = AVERROR(EINVAL);
            goto end;
        }

        ctx->pass_size = (strlen(avctx->stats_in) * 3) / 4;
        ctx->pass_data = av_malloc(ctx->pass_size);
        if (!ctx->pass_data) {
            av_log(avctx, AV_LOG_ERROR, "Could not allocate stats buffer.\n");
            ret = AVERROR(ENOMEM);
            goto end;
        }

        ctx->pass_size = av_base64_decode(ctx->pass_data, avctx->stats_in, ctx->pass_size);
        if (ctx->pass_size < 0) {
            av_log(avctx, AV_LOG_ERROR, "Invalid pass file.\n");
            ret = AVERROR(EINVAL);
            goto end;
        }
    }

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
         const AVBitStreamFilter *filter = av_bsf_get_by_name("extract_extradata");
         int bret;

         if (!filter) {
            av_log(avctx, AV_LOG_ERROR, "extract_extradata bitstream filter "
                   "not found. This is a bug, please report it.\n");
            ret = AVERROR_BUG;
            goto end;
         }

         bret = av_bsf_alloc(filter, &ctx->bsf);
         if (bret < 0) {
             ret = bret;
             goto end;
         }

         bret = avcodec_parameters_from_context(ctx->bsf->par_in, avctx);
         if (bret < 0) {
             ret = bret;
             goto end;
         }

         bret = av_bsf_init(ctx->bsf);
         if (bret < 0) {
             ret = bret;
             goto end;
         }
    }

    {
        AVDictionaryEntry *en = NULL;
        while ((en = av_dict_get(ctx->rav1e_opts, "", en, AV_DICT_IGNORE_SUFFIX))) {
            int parse_ret = rav1e_config_parse(cfg, en->key, en->value);
            if (parse_ret < 0)
                av_log(avctx, AV_LOG_WARNING, "Invalid value for %s: %s.\n", en->key, en->value);
        }
    }

    rret = rav1e_config_parse_int(cfg, "width", avctx->width);
    if (rret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid width passed to rav1e.\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    rret = rav1e_config_parse_int(cfg, "height", avctx->height);
    if (rret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid height passed to rav1e.\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    rret = rav1e_config_parse_int(cfg, "threads", avctx->thread_count);
    if (rret < 0)
        av_log(avctx, AV_LOG_WARNING, "Invalid number of threads, defaulting to auto.\n");

    if (ctx->speed >= 0) {
        rret = rav1e_config_parse_int(cfg, "speed", ctx->speed);
        if (rret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not set speed preset.\n");
            ret = AVERROR_EXTERNAL;
            goto end;
        }
    }

    /* rav1e handles precedence between 'tiles' and cols/rows for us. */
    if (ctx->tiles > 0) {
        rret = rav1e_config_parse_int(cfg, "tiles", ctx->tiles);
        if (rret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not set number of tiles to encode with.\n");
            ret = AVERROR_EXTERNAL;
            goto end;
        }
    }
    if (ctx->tile_rows > 0) {
        rret = rav1e_config_parse_int(cfg, "tile_rows", ctx->tile_rows);
        if (rret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not set number of tile rows to encode with.\n");
            ret = AVERROR_EXTERNAL;
            goto end;
        }
    }
    if (ctx->tile_cols > 0) {
        rret = rav1e_config_parse_int(cfg, "tile_cols", ctx->tile_cols);
        if (rret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not set number of tile cols to encode with.\n");
            ret = AVERROR_EXTERNAL;
            goto end;
        }
    }

    if (avctx->gop_size > 0) {
        rret = rav1e_config_parse_int(cfg, "key_frame_interval", avctx->gop_size);
        if (rret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not set max keyint.\n");
            ret = AVERROR_EXTERNAL;
            goto end;
        }
    }

    if (avctx->keyint_min > 0) {
        rret = rav1e_config_parse_int(cfg, "min_key_frame_interval", avctx->keyint_min);
        if (rret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not set min keyint.\n");
            ret = AVERROR_EXTERNAL;
            goto end;
        }
    }

    if (avctx->bit_rate && ctx->quantizer < 0) {
        int max_quantizer = avctx->qmax >= 0 ? avctx->qmax : 255;

        rret = rav1e_config_parse_int(cfg, "quantizer", max_quantizer);
        if (rret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not set max quantizer.\n");
            ret = AVERROR_EXTERNAL;
            goto end;
        }

        if (avctx->qmin >= 0) {
            rret = rav1e_config_parse_int(cfg, "min_quantizer", avctx->qmin);
            if (rret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Could not set min quantizer.\n");
                ret = AVERROR_EXTERNAL;
                goto end;
            }
        }

        rret = rav1e_config_parse_int(cfg, "bitrate", avctx->bit_rate);
        if (rret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not set bitrate.\n");
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
    } else if (ctx->quantizer >= 0) {
        if (avctx->bit_rate)
            av_log(avctx, AV_LOG_WARNING, "Both bitrate and quantizer specified. Using quantizer mode.");

        rret = rav1e_config_parse_int(cfg, "quantizer", ctx->quantizer);
        if (rret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not set quantizer.\n");
            ret = AVERROR_EXTERNAL;
            goto end;
        }
    }

    rret = rav1e_config_set_pixel_format(cfg, desc->comp[0].depth,
                                         pix_fmt_map(avctx->pix_fmt),
                                         chroma_loc_map(avctx->chroma_sample_location),
                                         range_map(avctx->pix_fmt, avctx->color_range));
    if (rret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set pixel format properties.\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    /* rav1e's colorspace enums match standard values. */
    rret = rav1e_config_set_color_description(cfg, (RaMatrixCoefficients) avctx->colorspace,
                                              (RaColorPrimaries) avctx->color_primaries,
                                              (RaTransferCharacteristics) avctx->color_trc);
    if (rret < 0) {
        av_log(avctx, AV_LOG_WARNING, "Failed to set color properties.\n");
        if (avctx->err_recognition & AV_EF_EXPLODE) {
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
    }

    ctx->ctx = rav1e_context_new(cfg);
    if (!ctx->ctx) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create rav1e encode context.\n");
        ret = AVERROR_EXTERNAL;
        goto end;
    }

    ret = 0;

end:

    rav1e_config_unref(cfg);

    return ret;
}

static int librav1e_receive_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    librav1eContext *ctx = avctx->priv_data;
    RaFrame *rframe = ctx->rframe;
    RaPacket *rpkt = NULL;
    int ret;

    if (!rframe) {
        AVFrame *frame = ctx->frame;

        ret = ff_encode_get_frame(avctx, frame);
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;

        if (frame->buf[0]) {
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);

            int64_t *pts = av_malloc(sizeof(int64_t));
            if (!pts) {
                av_log(avctx, AV_LOG_ERROR, "Could not allocate PTS buffer.\n");
                return AVERROR(ENOMEM);
            }
            *pts = frame->pts;

            rframe = rav1e_frame_new(ctx->ctx);
            if (!rframe) {
                av_log(avctx, AV_LOG_ERROR, "Could not allocate new rav1e frame.\n");
                av_frame_unref(frame);
                av_freep(&pts);
                return AVERROR(ENOMEM);
            }

            for (int i = 0; i < desc->nb_components; i++) {
                int shift = i ? desc->log2_chroma_h : 0;
                int bytes = desc->comp[0].depth == 8 ? 1 : 2;
                rav1e_frame_fill_plane(rframe, i, frame->data[i],
                                       (frame->height >> shift) * frame->linesize[i],
                                       frame->linesize[i], bytes);
            }
            av_frame_unref(frame);
            rav1e_frame_set_opaque(rframe, pts, av_free);
        }
    }

    ret = rav1e_send_frame(ctx->ctx, rframe);
    if (rframe)
        if (ret == RA_ENCODER_STATUS_ENOUGH_DATA) {
            ctx->rframe = rframe; /* Queue is full. Store the RaFrame to retry next call */
        } else {
            rav1e_frame_unref(rframe); /* No need to unref if flushing. */
            ctx->rframe = NULL;
        }

    switch (ret) {
    case RA_ENCODER_STATUS_SUCCESS:
    case RA_ENCODER_STATUS_ENOUGH_DATA:
        break;
    case RA_ENCODER_STATUS_FAILURE:
        av_log(avctx, AV_LOG_ERROR, "Could not send frame: %s\n", rav1e_status_to_str(ret));
        return AVERROR_EXTERNAL;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown return code %d from rav1e_send_frame: %s\n", ret, rav1e_status_to_str(ret));
        return AVERROR_UNKNOWN;
    }

retry:

    if (avctx->flags & AV_CODEC_FLAG_PASS1) {
        int sret = get_stats(avctx, 0);
        if (sret < 0)
            return sret;
    } else if (avctx->flags & AV_CODEC_FLAG_PASS2) {
        int sret = set_stats(avctx);
        if (sret < 0)
            return sret;
    }

    ret = rav1e_receive_packet(ctx->ctx, &rpkt);
    switch (ret) {
    case RA_ENCODER_STATUS_SUCCESS:
        break;
    case RA_ENCODER_STATUS_LIMIT_REACHED:
        if (avctx->flags & AV_CODEC_FLAG_PASS1) {
            int sret = get_stats(avctx, 1);
            if (sret < 0)
                return sret;
        }
        return AVERROR_EOF;
    case RA_ENCODER_STATUS_ENCODED:
        goto retry;
    case RA_ENCODER_STATUS_NEED_MORE_DATA:
        if (avctx->internal->draining) {
            av_log(avctx, AV_LOG_ERROR, "Unexpected error when receiving packet after EOF.\n");
            return AVERROR_EXTERNAL;
        }
        return AVERROR(EAGAIN);
    case RA_ENCODER_STATUS_FAILURE:
        av_log(avctx, AV_LOG_ERROR, "Could not encode frame: %s\n", rav1e_status_to_str(ret));
        return AVERROR_EXTERNAL;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown return code %d from rav1e_receive_packet: %s\n", ret, rav1e_status_to_str(ret));
        return AVERROR_UNKNOWN;
    }

    ret = ff_get_encode_buffer(avctx, pkt, rpkt->len, 0);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate packet.\n");
        rav1e_packet_unref(rpkt);
        return ret;
    }

    memcpy(pkt->data, rpkt->data, rpkt->len);

    if (rpkt->frame_type == RA_FRAME_TYPE_KEY)
        pkt->flags |= AV_PKT_FLAG_KEY;

    pkt->pts = pkt->dts = *((int64_t *) rpkt->opaque);
    av_free(rpkt->opaque);
    rav1e_packet_unref(rpkt);

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        int ret = av_bsf_send_packet(ctx->bsf, pkt);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "extradata extraction send failed.\n");
            av_packet_unref(pkt);
            return ret;
        }

        ret = av_bsf_receive_packet(ctx->bsf, pkt);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "extradata extraction receive failed.\n");
            av_packet_unref(pkt);
            return ret;
        }
    }

    return 0;
}

#define OFFSET(x) offsetof(librav1eContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { "qp", "use constant quantizer mode", OFFSET(quantizer), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 255, VE },
    { "speed", "what speed preset to use", OFFSET(speed), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 10, VE },
    { "tiles", "number of tiles encode with", OFFSET(tiles), AV_OPT_TYPE_INT, { .i64 = 0 }, -1, INT64_MAX, VE },
    { "tile-rows", "number of tiles rows to encode with", OFFSET(tile_rows), AV_OPT_TYPE_INT, { .i64 = 0 }, -1, INT64_MAX, VE },
    { "tile-columns", "number of tiles columns to encode with", OFFSET(tile_cols), AV_OPT_TYPE_INT, { .i64 = 0 }, -1, INT64_MAX, VE },
    { "rav1e-params", "set the rav1e configuration using a :-separated list of key=value parameters", OFFSET(rav1e_opts), AV_OPT_TYPE_DICT, { 0 }, 0, 0, VE },
    { NULL }
};

static const FFCodecDefault librav1e_defaults[] = {
    { "b",           "0" },
    { "g",           "0" },
    { "keyint_min",  "0" },
    { "qmax",       "-1" },
    { "qmin",       "-1" },
    { NULL }
};

const enum AVPixelFormat librav1e_pix_fmts[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV422P12,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_NONE
};

static const AVClass class = {
    .class_name = "librav1e",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_librav1e_encoder = {
    .p.name         = "librav1e",
    .p.long_name    = NULL_IF_CONFIG_SMALL("librav1e AV1"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AV1,
    .init           = librav1e_encode_init,
    FF_CODEC_RECEIVE_PACKET_CB(librav1e_receive_packet),
    .close          = librav1e_encode_close,
    .priv_data_size = sizeof(librav1eContext),
    .p.priv_class   = &class,
    .defaults       = librav1e_defaults,
    .p.pix_fmts     = librav1e_pix_fmts,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_OTHER_THREADS |
                      AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_AUTO_THREADS,
    .p.wrapper_name = "librav1e",
};
