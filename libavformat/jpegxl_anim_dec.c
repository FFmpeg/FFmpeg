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

#include "libavcodec/bytestream.h"
#define BITSTREAM_READER_LE
#include "libavcodec/get_bits.h"

#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

#include "avformat.h"
#include "internal.h"
#include "jpegxl_probe.h"

typedef struct JXLAnimDemuxContext {
    AVBufferRef *initial;
} JXLAnimDemuxContext;

/*
 * copies as much of the codestream into the buffer as possible
 * pass a shorter buflen to request less
 * returns the number of bytes consumed from input, may be greater than input_len
 * if the input doesn't end on an ISOBMFF-box boundary
 */
static int jpegxl_collect_codestream_header(const uint8_t *input_buffer, int input_len,
                                            uint8_t *buffer, int buflen, int *copied) {
    GetByteContext gb;
    *copied = 0;
    bytestream2_init(&gb, input_buffer, input_len);

    while (1) {
        uint64_t size;
        uint32_t tag;
        int head_size = 8;

        if (bytestream2_get_bytes_left(&gb) < 16)
            break;

        size = bytestream2_get_be32(&gb);
        if (size == 1) {
            size = bytestream2_get_be64(&gb);
            head_size = 16;
        }
        /* invalid ISOBMFF size */
        if (size && size <= head_size)
            return AVERROR_INVALIDDATA;
        if (size)
            size -= head_size;

        tag = bytestream2_get_le32(&gb);
        if (tag == MKTAG('j', 'x', 'l', 'p')) {
            if (bytestream2_get_bytes_left(&gb) < 4)
                break;
            bytestream2_skip(&gb, 4);
            if (size) {
                if (size <= 4)
                    return AVERROR_INVALIDDATA;
                size -= 4;
            }
        }
        /*
         * size = 0 means "until EOF". this is legal but uncommon
         * here we just set it to the remaining size of the probe buffer
         */
        if (!size)
            size = bytestream2_get_bytes_left(&gb);

        if (tag == MKTAG('j', 'x', 'l', 'c') || tag == MKTAG('j', 'x', 'l', 'p')) {
            if (size > buflen - *copied)
                size = buflen - *copied;
            /*
             * arbitrary chunking of the payload makes this memcpy hard to avoid
             * in practice this will only be performed one or two times at most
             */
            *copied += bytestream2_get_buffer(&gb, buffer + *copied, size);
        } else {
            bytestream2_skip(&gb, size);
        }
        if (bytestream2_get_bytes_left(&gb) <= 0 || *copied >= buflen)
            break;
    }

    return bytestream2_tell(&gb);
}

static int jpegxl_anim_probe(const AVProbeData *p)
{
    uint8_t buffer[4096 + AV_INPUT_BUFFER_PADDING_SIZE];
    int copied;

    /* this is a raw codestream */
    if (AV_RL16(p->buf) == FF_JPEGXL_CODESTREAM_SIGNATURE_LE) {
        if (ff_jpegxl_verify_codestream_header(p->buf, p->buf_size, 1) >= 1)
            return AVPROBE_SCORE_MAX;

        return 0;
    }

    /* not a JPEG XL file at all */
    if (AV_RL64(p->buf) != FF_JPEGXL_CONTAINER_SIGNATURE_LE)
        return 0;

    if (jpegxl_collect_codestream_header(p->buf, p->buf_size, buffer, sizeof(buffer) - AV_INPUT_BUFFER_PADDING_SIZE, &copied) <= 0 || copied <= 0)
        return 0;

    if (ff_jpegxl_verify_codestream_header(buffer, copied, 0) >= 1)
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int jpegxl_anim_read_header(AVFormatContext *s)
{
    JXLAnimDemuxContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    int offset = 0;
    uint8_t head[256 + AV_INPUT_BUFFER_PADDING_SIZE];
    const int sizeofhead = sizeof(head) - AV_INPUT_BUFFER_PADDING_SIZE;
    int headsize = 0;
    int ctrl;
    AVRational tb;
    GetBitContext gbi, *gb = &gbi;

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
            int copied;
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
            jpegxl_collect_codestream_header(buf, read, head + headsize, sizeofhead - headsize, &copied);
            memcpy(ctx->initial->data + (ctx->initial->size - read), buf, read);
            headsize += copied;
            if (headsize >= sizeofhead || read < sizeof(buf))
                break;
        }
    }
    /* offset in bits of the animation header */
    offset = ff_jpegxl_verify_codestream_header(head, headsize, 0);
    if (offset <= 0)
        return AVERROR_INVALIDDATA;
    if (init_get_bits8(gb, head, headsize) < 0)
        return AVERROR_INVALIDDATA;
    skip_bits_long(gb, offset);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_JPEGXL;
    ctrl = get_bits(gb, 2);
    tb.den = (const uint32_t[]){100, 1000, 1, 1}[ctrl] + get_bits_long(gb, (const uint32_t[]){0, 0, 10, 30}[ctrl]);
    ctrl = get_bits(gb, 2);
    tb.num = (const uint32_t[]){1, 1001, 1, 1}[ctrl] + get_bits_long(gb, (const uint32_t[]){0, 0, 8, 10}[ctrl]);
    avpriv_set_pts_info(st, 1, tb.num, tb.den);

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

    if ((size = avio_size(pb)) < 0)
        return size;

    /* animated JXL this big should not exist */
    if (size > INT_MAX)
        return AVERROR_INVALIDDATA;

    if (ctx->initial && size < ctx->initial->size)
        size = ctx->initial->size;

    if ((ret = av_new_packet(pkt, size)) < 0)
        return ret;

    if (ctx->initial) {
        offset = ctx->initial->size;
        memcpy(pkt->data, ctx->initial->data, offset);
        av_buffer_unref(&ctx->initial);
    }

    if ((ret = avio_read(pb, pkt->data + offset, size - offset)) < 0)
        return ret;

    return 0;
}

static int jpegxl_anim_close(AVFormatContext *s)
{
    JXLAnimDemuxContext *ctx = s->priv_data;
    if (ctx->initial)
        av_buffer_unref(&ctx->initial);

    return 0;
}

const AVInputFormat ff_jpegxl_anim_demuxer = {
    .name           = "jpegxl_anim",
    .long_name      = NULL_IF_CONFIG_SMALL("Animated JPEG XL"),
    .priv_data_size = sizeof(JXLAnimDemuxContext),
    .read_probe     = jpegxl_anim_probe,
    .read_header    = jpegxl_anim_read_header,
    .read_packet    = jpegxl_anim_read_packet,
    .read_close     = jpegxl_anim_close,
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .flags          = AVFMT_GENERIC_INDEX,
    .mime_type      = "image/jxl",
    .extensions     = "jxl",
};
