/*
 * Inject per-frame JPEG APP segments from a length-prefixed sidecar.
 *
 * Copyright (c) 2026 Reinhard Nißl
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
 * Sidecar format (big-endian), one record per encoded JPEG packet:
 *
 *   u32be length
 *   u8    payload[length]
 *
 * Payload is zero or more complete JPEG APP segments (APP0..APP15),
 * each ``FF En`` + 16-bit length (length includes those two bytes).
 * A zero length leaves the packet unchanged.
 *
 * After SOI the payload is inserted. If it starts with APP0 and the
 * two bytes immediately after SOI are an APP0 marker, that segment is
 * dropped so the sidecar can replace it. An APP0 preceded by fill
 * bytes or another segment is retained. An APP1-only payload keeps
 * the encoder APP0: SOI, APP1, APP0, DQT, ...
 *
 * A sidecar read or validation error stops further record consumption
 * for this filter context. Flushing does not clear that error.
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/error.h"
#include "libavutil/file_open.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "libavcodec/bsf.h"
#include "libavcodec/bsf_internal.h"
#include "libavcodec/mjpeg.h"

#define JPEGAPP_MAX_RECORD (16 * 1024 * 1024)

typedef struct JPEGAppContext {
    const AVClass *class;
    char *filename;
    FILE *file;
    uint8_t *buf;
    int buf_size;
    int read_error;
    int64_t packet_index;
} JPEGAppContext;

static int jpeg_app_segment_len(const uint8_t *data, int size)
{
    int len;

    if (size < 4 || data[0] != 0xff || data[1] < APP0 || data[1] > APP15)
        return AVERROR_INVALIDDATA;
    len = AV_RB16(data + 2);
    if (len < 2 || len > size - 2)
        return AVERROR_INVALIDDATA;
    return 2 + len;
}

static int jpegapp_validate_payload(const uint8_t *data, int size)
{
    int pos = 0, n;

    while (pos < size) {
        n = jpeg_app_segment_len(data + pos, size - pos);
        if (n < 0)
            return n;
        pos += n;
    }
    return 0;
}

static int jpegapp_init(AVBSFContext *ctx)
{
    JPEGAppContext *s = ctx->priv_data;

    if (!s->filename || !s->filename[0]) {
        av_log(ctx, AV_LOG_ERROR, "Option filename is required\n");
        return AVERROR(EINVAL);
    }

    s->file = avpriv_fopen_utf8(s->filename, "rb");
    if (!s->file) {
        int err = errno ? AVERROR(errno) : AVERROR(EIO);
        av_log(ctx, AV_LOG_ERROR, "Could not open '%s': %s\n",
               s->filename, av_err2str(err));
        return err;
    }
    return 0;
}

static void jpegapp_close(AVBSFContext *ctx)
{
    JPEGAppContext *s = ctx->priv_data;

    if (s->file) {
        fclose(s->file);
        s->file = NULL;
    }
    av_freep(&s->buf);
    s->buf_size = 0;
}

static int jpegapp_read_record(AVBSFContext *ctx, int *out_size)
{
    JPEGAppContext *s = ctx->priv_data;
    uint8_t hdr[4];
    uint32_t n;
    size_t got;
    int ret;

    errno = 0;
    got = fread(hdr, 1, 4, s->file);
    ret = errno;
    if (got == 0 && feof(s->file) && !ferror(s->file)) {
        av_log(ctx, AV_LOG_ERROR,
               "jpegapp: no sidecar record for packet %"PRId64"\n",
               s->packet_index);
        return AVERROR_INVALIDDATA;
    }
    if (got != 4) {
        if (ferror(s->file)) {
            av_log(ctx, AV_LOG_ERROR, "jpegapp: sidecar read failed\n");
            return ret ? AVERROR(ret) : AVERROR(EIO);
        }
        av_log(ctx, AV_LOG_ERROR, "jpegapp: truncated length prefix\n");
        return AVERROR_INVALIDDATA;
    }
    n = AV_RB32(hdr);
    if (n > JPEGAPP_MAX_RECORD) {
        av_log(ctx, AV_LOG_ERROR, "jpegapp: record length %u too large\n", n);
        return AVERROR_INVALIDDATA;
    }
    if (n > (uint32_t)s->buf_size) {
        ret = av_reallocp(&s->buf, n);
        if (ret < 0) {
            s->buf_size = 0;
            return ret;
        }
        s->buf_size = n;
    }
    if (n) {
        errno = 0;
        got = fread(s->buf, 1, n, s->file);
        ret = errno;
        if (got != n) {
            if (ferror(s->file)) {
                av_log(ctx, AV_LOG_ERROR, "jpegapp: sidecar read failed\n");
                return ret ? AVERROR(ret) : AVERROR(EIO);
            }
            av_log(ctx, AV_LOG_ERROR, "jpegapp: truncated record payload\n");
            return AVERROR_INVALIDDATA;
        }
        ret = jpegapp_validate_payload(s->buf, n);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "jpegapp: payload is not a sequence of APP0..APP15 segments\n");
            return ret;
        }
    }
    *out_size = n;
    return 0;
}

static int jpegapp_filter(AVBSFContext *ctx, AVPacket *out)
{
    JPEGAppContext *s = ctx->priv_data;
    AVPacket *in;
    int rec_size = 0, src_off, dst_size, ret;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    ret = s->read_error;
    if (!ret)
        ret = jpegapp_read_record(ctx, &rec_size);
    s->packet_index++;
    if (ret < 0) {
        s->read_error = ret;
        goto fail;
    }

    if (in->size < 2 || AV_RB16(in->data) != 0xffd8) {
        av_log(ctx, AV_LOG_ERROR, "jpegapp: packet is not JPEG (missing SOI)\n");
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    src_off = 2;
    if (rec_size >= 2 && s->buf[0] == 0xff && s->buf[1] == APP0 &&
        in->size >= 4 && in->data[2] == 0xff && in->data[3] == APP0) {
        int skip = jpeg_app_segment_len(in->data + 2, in->size - 2);
        if (skip < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "jpegapp: existing APP0 is invalid or truncated\n");
            ret = skip;
            goto fail;
        }
        src_off = 2 + skip;
    }

    if (!rec_size) {
        ret = av_packet_ref(out, in);
        goto fail;
    }

    if (in->size - src_off > INT_MAX - 2 - rec_size) {
        ret = AVERROR(ERANGE);
        goto fail;
    }
    dst_size = 2 + rec_size + (in->size - src_off);
    ret = av_new_packet(out, dst_size);
    if (ret < 0)
        goto fail;
    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

    memcpy(out->data, in->data, 2);
    memcpy(out->data + 2, s->buf, rec_size);
    memcpy(out->data + 2 + rec_size, in->data + src_off, in->size - src_off);
    av_packet_free(&in);
    return 0;

fail:
    if (ret < 0)
        av_packet_unref(out);
    av_packet_free(&in);
    return ret;
}

#define OFFSET(x) offsetof(JPEGAppContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_BSF_PARAM)
static const AVOption jpegapp_options[] = {
    { "filename", "sidecar of u32be-length + APP-segment records", OFFSET(filename),
      AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
    { "file",     "sidecar of u32be-length + APP-segment records", OFFSET(filename),
      AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
    { NULL },
};

static const AVClass jpegapp_class = {
    .class_name = "jpegapp",
    .item_name  = av_default_item_name,
    .option     = jpegapp_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVCodecID jpegapp_codec_ids[] = {
    AV_CODEC_ID_MJPEG, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_jpegapp_bsf = {
    .p.name         = "jpegapp",
    .p.codec_ids    = jpegapp_codec_ids,
    .p.priv_class   = &jpegapp_class,
    .priv_data_size = sizeof(JPEGAppContext),
    .init           = jpegapp_init,
    .close          = jpegapp_close,
    .filter         = jpegapp_filter,
};
