/*
 * Animated JPEG XL Demuxer
 * Copyright (c) 2023 Leo Izen (thebombzen)
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
 * Animated JPEG XL Demuxer
 * @see ISO/IEC 18181-1 and 18181-2
 */

#include <stdint.h>
#include <string.h>

#include "libavcodec/jpegxl.h"
#include "libavcodec/jpegxl_parse.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

#include "avformat.h"
#include "demux.h"
#include "internal.h"

typedef struct JXLAnimDemuxContext {
    AVBufferRef *initial;
} JXLAnimDemuxContext;

static int jpegxl_anim_probe(const AVProbeData *p)
{
    uint8_t buffer[4096 + AV_INPUT_BUFFER_PADDING_SIZE];
    int copied = 0, ret;
    FFJXLMetadata meta = { 0 };

    /* this is a raw codestream */
    if (AV_RL16(p->buf) == FF_JPEGXL_CODESTREAM_SIGNATURE_LE) {
        ret = ff_jpegxl_parse_codestream_header(p->buf, p->buf_size, &meta, 5);
        if (ret >= 0 && meta.animation_offset > 0)
            return AVPROBE_SCORE_MAX;

        return 0;
    }

    /* not a JPEG XL file at all */
    if (AV_RL64(p->buf) != FF_JPEGXL_CONTAINER_SIGNATURE_LE)
        return 0;

    if (ff_jpegxl_collect_codestream_header(p->buf, p->buf_size, buffer,
            sizeof(buffer) - AV_INPUT_BUFFER_PADDING_SIZE, &copied) <= 0
            || copied <= 0)
        return 0;

    ret = ff_jpegxl_parse_codestream_header(buffer, copied, &meta, 10);
    if (ret >= 0 && meta.animation_offset > 0)
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int jpegxl_anim_read_header(AVFormatContext *s)
{
    JXLAnimDemuxContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    uint8_t head[256 + AV_INPUT_BUFFER_PADDING_SIZE];
    const int sizeofhead = sizeof(head) - AV_INPUT_BUFFER_PADDING_SIZE;
    int headsize = 0, ret;
    FFJXLMetadata meta = { 0 };

    uint64_t sig16 = avio_rl16(pb);
    if (sig16 == FF_JPEGXL_CODESTREAM_SIGNATURE_LE) {
        AV_WL16(head, sig16);
        headsize = avio_read(s->pb, head + 2, sizeofhead - 2);
        if (headsize < 0)
            return headsize;
        headsize += 2;
        ctx->initial = av_buffer_alloc(headsize);
        if (!ctx->initial)
            return AVERROR(ENOMEM);
        memcpy(ctx->initial->data, head, headsize);
    } else {
        uint64_t sig64 = avio_rl64(pb);
        sig64 = (sig64 << 16) | sig16;
        if (sig64 != FF_JPEGXL_CONTAINER_SIGNATURE_LE)
            return AVERROR_INVALIDDATA;
        avio_skip(pb, 2); // first box always 12 bytes
        while (1) {
            int copied = 0;
            uint8_t buf[4096];
            int read = avio_read(pb, buf, sizeof(buf));
            if (read < 0)
                return read;
            if (!ctx->initial) {
                ctx->initial = av_buffer_alloc(read + 12);
                if (!ctx->initial)
                    return AVERROR(ENOMEM);
                AV_WL64(ctx->initial->data, FF_JPEGXL_CONTAINER_SIGNATURE_LE);
                AV_WL32(ctx->initial->data + 8, 0x0a870a0d);
            } else {
                /* this only should be happening zero or one times in practice */
                if (av_buffer_realloc(&ctx->initial, ctx->initial->size + read) < 0)
                    return AVERROR(ENOMEM);
            }
            ff_jpegxl_collect_codestream_header(buf, read, head + headsize, sizeofhead - headsize, &copied);
            memcpy(ctx->initial->data + (ctx->initial->size - read), buf, read);
            headsize += copied;
            if (headsize >= sizeofhead || read < sizeof(buf))
                break;
        }
    }

    /* offset in bits of the animation header */
    ret = ff_jpegxl_parse_codestream_header(head, headsize, &meta, 0);
    if (ret < 0 || meta.animation_offset <= 0)
        return AVERROR_INVALIDDATA;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_JPEGXL;
    avpriv_set_pts_info(st, 1, meta.timebase.num, meta.timebase.den);
    ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL;

    return 0;
}

/* the decoder requires the full input file as a single packet */
static int jpegxl_anim_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    JXLAnimDemuxContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret;
    int64_t size;
    size_t offset = 0;

    size = avio_size(pb);
    if (size < 0)
        return size;
    if (size > INT_MAX)
        return AVERROR(EDOM);
    if (size == 0)
        size = 4096;

    if (ctx->initial && size < ctx->initial->size)
        size = ctx->initial->size;

    ret = av_new_packet(pkt, size);
    if (ret < 0)
        return ret;

    if (ctx->initial) {
        offset = ctx->initial->size;
        memcpy(pkt->data, ctx->initial->data, offset);
        av_buffer_unref(&ctx->initial);
    }

    pkt->pos = avio_tell(pb) - offset;

    ret = avio_read(pb, pkt->data + offset, size - offset);
    if (ret < 0)
        return ret;
    if (ret < size - offset)
        pkt->size = ret + offset;

    return 0;
}

static int jpegxl_anim_close(AVFormatContext *s)
{
    JXLAnimDemuxContext *ctx = s->priv_data;
    if (ctx->initial)
        av_buffer_unref(&ctx->initial);

    return 0;
}

const FFInputFormat ff_jpegxl_anim_demuxer = {
    .p.name         = "jpegxl_anim",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Animated JPEG XL"),
    .p.flags        = AVFMT_GENERIC_INDEX | AVFMT_NOTIMESTAMPS,
    .p.mime_type    = "image/jxl",
    .p.extensions   = "jxl",
    .priv_data_size = sizeof(JXLAnimDemuxContext),
    .read_probe     = jpegxl_anim_probe,
    .read_header    = jpegxl_anim_read_header,
    .read_packet    = jpegxl_anim_read_packet,
    .read_close     = jpegxl_anim_close,
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
};
