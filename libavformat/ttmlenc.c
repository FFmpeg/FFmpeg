/*
 * TTML subtitle muxer
 * Copyright (c) 2020 24i
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
 * TTML subtitle muxer
 * @see https://www.w3.org/TR/ttml1/
 * @see https://www.w3.org/TR/ttml2/
 * @see https://www.w3.org/TR/ttml-imsc/rec
 */

#include "avformat.h"
#include "internal.h"
#include "libavcodec/ttmlenc.h"
#include "libavutil/internal.h"

enum TTMLPacketType {
    PACKET_TYPE_PARAGRAPH,
    PACKET_TYPE_DOCUMENT,
};

typedef struct TTMLMuxContext {
    enum TTMLPacketType input_type;
    unsigned int document_written;
} TTMLMuxContext;

static const char ttml_header_text[] =
"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
"<tt\n"
"  xmlns=\"http://www.w3.org/ns/ttml\"\n"
"  xmlns:ttm=\"http://www.w3.org/ns/ttml#metadata\"\n"
"  xmlns:tts=\"http://www.w3.org/ns/ttml#styling\"\n"
"  xml:lang=\"%s\">\n"
"  <body>\n"
"    <div>\n";

static const char ttml_footer_text[] =
"    </div>\n"
"  </body>\n"
"</tt>\n";

static void ttml_write_time(AVIOContext *pb, const char tag[],
                            int64_t millisec)
{
    int64_t sec, min, hour;
    sec = millisec / 1000;
    millisec -= 1000 * sec;
    min = sec / 60;
    sec -= 60 * min;
    hour = min / 60;
    min -= 60 * hour;

    avio_printf(pb, "%s=\"%02"PRId64":%02"PRId64":%02"PRId64".%03"PRId64"\"",
                tag, hour, min, sec, millisec);
}

static int ttml_write_header(AVFormatContext *ctx)
{
    TTMLMuxContext *ttml_ctx = ctx->priv_data;
    ttml_ctx->document_written = 0;

    if (ctx->nb_streams != 1 ||
        ctx->streams[0]->codecpar->codec_id != AV_CODEC_ID_TTML) {
        av_log(ctx, AV_LOG_ERROR, "Exactly one TTML stream is required!\n");
        return AVERROR(EINVAL);
    }

    {
        AVStream    *st = ctx->streams[0];
        AVIOContext *pb = ctx->pb;

        AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", NULL,
                                              0);
        const char *printed_lang = (lang && lang->value) ? lang->value : "";

        // Not perfect, but decide whether the packet is a document or not
        // by the existence of the lavc ttmlenc extradata.
        ttml_ctx->input_type = (st->codecpar->extradata &&
                                st->codecpar->extradata_size >= TTMLENC_EXTRADATA_SIGNATURE_SIZE &&
                                !memcmp(st->codecpar->extradata,
                                        TTMLENC_EXTRADATA_SIGNATURE,
                                        TTMLENC_EXTRADATA_SIGNATURE_SIZE)) ?
                               PACKET_TYPE_PARAGRAPH :
                               PACKET_TYPE_DOCUMENT;

        avpriv_set_pts_info(st, 64, 1, 1000);

        if (ttml_ctx->input_type == PACKET_TYPE_PARAGRAPH)
            avio_printf(pb, ttml_header_text, printed_lang);
    }

    return 0;
}

static int ttml_write_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    TTMLMuxContext *ttml_ctx = ctx->priv_data;
    AVIOContext    *pb       = ctx->pb;

    switch (ttml_ctx->input_type) {
    case PACKET_TYPE_PARAGRAPH:
        // write out a paragraph element with the given contents.
        avio_printf(pb,     "      <p\n");
        ttml_write_time(pb, "        begin", pkt->pts);
        avio_w8(pb, '\n');
        ttml_write_time(pb, "        end",   pkt->pts + pkt->duration);
        avio_printf(pb, ">");
        avio_write(pb, pkt->data, pkt->size);
        avio_printf(pb, "</p>\n");
        break;
    case PACKET_TYPE_DOCUMENT:
        // dump the given document out as-is.
        if (ttml_ctx->document_written) {
            av_log(ctx, AV_LOG_ERROR,
                   "Attempting to write multiple TTML documents into a "
                   "single document! The XML specification forbids this "
                   "as there has to be a single root tag.\n");
            return AVERROR(EINVAL);
        }
        avio_write(pb, pkt->data, pkt->size);
        ttml_ctx->document_written = 1;
        break;
    default:
        av_log(ctx, AV_LOG_ERROR,
               "Internal error: invalid TTML input packet type: %d!\n",
               ttml_ctx->input_type);
        return AVERROR_BUG;
    }

    return 0;
}

static int ttml_write_trailer(AVFormatContext *ctx)
{
    TTMLMuxContext *ttml_ctx = ctx->priv_data;
    AVIOContext    *pb       = ctx->pb;

    if (ttml_ctx->input_type == PACKET_TYPE_PARAGRAPH)
        avio_printf(pb, ttml_footer_text);

    return 0;
}

AVOutputFormat ff_ttml_muxer = {
    .name              = "ttml",
    .long_name         = NULL_IF_CONFIG_SMALL("TTML subtitle"),
    .extensions        = "ttml",
    .mime_type         = "text/ttml",
    .priv_data_size    = sizeof(TTMLMuxContext),
    .flags             = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS |
                         AVFMT_TS_NONSTRICT,
    .subtitle_codec    = AV_CODEC_ID_TTML,
    .write_header      = ttml_write_header,
    .write_packet      = ttml_write_packet,
    .write_trailer     = ttml_write_trailer,
};
