/*
 * Chromaprint fingerprinting muxer
 * Copyright (c) 2015 Rodger Combs
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

#include "avformat.h"
#include "internal.h"
#include "libavutil/opt.h"
#include "libavcodec/internal.h"
#include <chromaprint.h>

#define CPR_VERSION_INT AV_VERSION_INT(CHROMAPRINT_VERSION_MAJOR, \
                                       CHROMAPRINT_VERSION_MINOR, \
                                       CHROMAPRINT_VERSION_PATCH)

typedef enum FingerprintFormat {
    FINGERPRINT_RAW,
    FINGERPRINT_COMPRESSED,
    FINGERPRINT_BASE64,
} FingerprintFormat;

typedef struct ChromaprintMuxContext {
    const AVClass *class;
    int silence_threshold;
    int algorithm;
    FingerprintFormat fp_format;
#if CPR_VERSION_INT >= AV_VERSION_INT(1, 4, 0)
    ChromaprintContext *ctx;
#else
    ChromaprintContext ctx;
#endif
} ChromaprintMuxContext;

static void cleanup(ChromaprintMuxContext *cpr)
{
    if (cpr->ctx) {
        ff_lock_avformat();
        chromaprint_free(cpr->ctx);
        ff_unlock_avformat();
    }
}

static int write_header(AVFormatContext *s)
{
    ChromaprintMuxContext *cpr = s->priv_data;
    AVStream *st;

    ff_lock_avformat();
    cpr->ctx = chromaprint_new(cpr->algorithm);
    ff_unlock_avformat();

    if (!cpr->ctx) {
        av_log(s, AV_LOG_ERROR, "Failed to create chromaprint context.\n");
        return AVERROR(ENOMEM);
    }

    if (cpr->silence_threshold != -1) {
#if CPR_VERSION_INT >= AV_VERSION_INT(0, 7, 0)
        if (!chromaprint_set_option(cpr->ctx, "silence_threshold", cpr->silence_threshold)) {
            av_log(s, AV_LOG_ERROR, "Failed to set silence threshold. Setting silence_threshold requires -algorithm 3 option.\n");
            goto fail;
        }
#else
        av_log(s, AV_LOG_ERROR, "Setting the silence threshold requires Chromaprint "
                                "version 0.7.0 or later.\n");
        goto fail;
#endif
    }

    if (s->nb_streams != 1) {
        av_log(s, AV_LOG_ERROR, "Only one stream is supported\n");
        goto fail;
    }

    st = s->streams[0];

    if (st->codecpar->channels > 2) {
        av_log(s, AV_LOG_ERROR, "Only up to 2 channels are supported\n");
        goto fail;
    }

    if (st->codecpar->sample_rate < 1000) {
        av_log(s, AV_LOG_ERROR, "Sampling rate must be at least 1000\n");
        goto fail;
    }

    if (!chromaprint_start(cpr->ctx, st->codecpar->sample_rate, st->codecpar->channels)) {
        av_log(s, AV_LOG_ERROR, "Failed to start chromaprint\n");
        goto fail;
    }

    return 0;
fail:
    cleanup(cpr);
    return AVERROR(EINVAL);
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ChromaprintMuxContext *cpr = s->priv_data;
    return chromaprint_feed(cpr->ctx, (const int16_t *)pkt->data, pkt->size / 2) ? 0 : AVERROR(EINVAL);
}

static int write_trailer(AVFormatContext *s)
{
    ChromaprintMuxContext *cpr = s->priv_data;
    AVIOContext *pb = s->pb;
    void *fp = NULL;
    char *enc_fp = NULL;
    int size, enc_size, ret = AVERROR(EINVAL);

    if (!chromaprint_finish(cpr->ctx)) {
        av_log(s, AV_LOG_ERROR, "Failed to generate fingerprint\n");
        goto fail;
    }

    if (!chromaprint_get_raw_fingerprint(cpr->ctx, (uint32_t **)&fp, &size)) {
        av_log(s, AV_LOG_ERROR, "Failed to retrieve fingerprint\n");
        goto fail;
    }

    switch (cpr->fp_format) {
    case FINGERPRINT_RAW:
        avio_write(pb, fp, size * 4); //fp points to array of uint32_t
        break;
    case FINGERPRINT_COMPRESSED:
    case FINGERPRINT_BASE64:
        if (!chromaprint_encode_fingerprint(fp, size, cpr->algorithm, &enc_fp, &enc_size,
                                            cpr->fp_format == FINGERPRINT_BASE64)) {
            av_log(s, AV_LOG_ERROR, "Failed to encode fingerprint\n");
            goto fail;
        }
        avio_write(pb, enc_fp, enc_size);
        break;
    }

    ret = 0;
fail:
    if (fp)
        chromaprint_dealloc(fp);
    if (enc_fp)
        chromaprint_dealloc(enc_fp);
    cleanup(cpr);
    return ret;
}

#define OFFSET(x) offsetof(ChromaprintMuxContext, x)
#define FLAGS AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "silence_threshold", "threshold for detecting silence", OFFSET(silence_threshold), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 32767, FLAGS },
    { "algorithm", "version of the fingerprint algorithm", OFFSET(algorithm), AV_OPT_TYPE_INT, { .i64 = CHROMAPRINT_ALGORITHM_DEFAULT }, CHROMAPRINT_ALGORITHM_TEST1, INT_MAX, FLAGS },
    { "fp_format", "fingerprint format to write", OFFSET(fp_format), AV_OPT_TYPE_INT, { .i64 = FINGERPRINT_BASE64 }, FINGERPRINT_RAW, FINGERPRINT_BASE64, FLAGS, "fp_format" },
    { "raw", "binary raw fingerprint", 0, AV_OPT_TYPE_CONST, {.i64 = FINGERPRINT_RAW }, INT_MIN, INT_MAX, FLAGS, "fp_format"},
    { "compressed", "binary compressed fingerprint", 0, AV_OPT_TYPE_CONST, {.i64 = FINGERPRINT_COMPRESSED }, INT_MIN, INT_MAX, FLAGS, "fp_format"},
    { "base64", "Base64 compressed fingerprint", 0, AV_OPT_TYPE_CONST, {.i64 = FINGERPRINT_BASE64 }, INT_MIN, INT_MAX, FLAGS, "fp_format"},
    { NULL },
};

static const AVClass chromaprint_class = {
    .class_name = "chromaprint muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_chromaprint_muxer = {
    .name              = "chromaprint",
    .long_name         = NULL_IF_CONFIG_SMALL("Chromaprint"),
    .priv_data_size    = sizeof(ChromaprintMuxContext),
    .audio_codec       = AV_NE(AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE),
    .write_header      = write_header,
    .write_packet      = write_packet,
    .write_trailer     = write_trailer,
    .flags             = AVFMT_NOTIMESTAMPS,
    .priv_class        = &chromaprint_class,
};
