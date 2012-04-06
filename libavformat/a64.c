/*
 * a64 muxer
 * Copyright (c) 2009 Tobias Bindhammer
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavcodec/avcodec.h"
#include "libavcodec/a64enc.h"
#include "libavcodec/bytestream.h"
#include "avformat.h"

typedef struct A64MuxerContext {
    int interleaved;
    AVPacket prev_pkt;
    int prev_frame_count;
} A64MuxerContext;

static int a64_write_header(struct AVFormatContext *s)
{
    AVCodecContext *avctx = s->streams[0]->codec;
    A64MuxerContext *c = s->priv_data;
    uint8_t header[5] = {
        0x00, //load
        0x40, //address
        0x00, //mode
        0x00, //charset_lifetime (multi only)
        0x00  //fps in 50/fps;
    };
    c->interleaved = 0;
    switch (avctx->codec->id) {
    case CODEC_ID_A64_MULTI:
        header[2] = 0x00;
        header[3] = AV_RB32(avctx->extradata+0);
        header[4] = 2;
        break;
    case CODEC_ID_A64_MULTI5:
        header[2] = 0x01;
        header[3] = AV_RB32(avctx->extradata+0);
        header[4] = 3;
        break;
    default:
        return AVERROR(EINVAL);
    }
    avio_write(s->pb, header, 2);
    c->prev_pkt.size = 0;
    c->prev_frame_count = 0;
    return 0;
}

static int a64_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    AVCodecContext *avctx = s->streams[0]->codec;
    A64MuxerContext *c = s->priv_data;
    int i, j;
    int ch_chunksize;
    int lifetime;
    int frame_count;
    int charset_size;
    int frame_size;
    int num_frames;

    /* fetch values from extradata */
    switch (avctx->codec->id) {
    case CODEC_ID_A64_MULTI:
    case CODEC_ID_A64_MULTI5:
        if(c->interleaved) {
            /* Write interleaved, means we insert chunks of the future charset before each current frame.
             * Reason: if we load 1 charset + corresponding frames in one block on c64, we need to store
             * them first and then display frame by frame to keep in sync. Thus we would read and write
             * the data for colram from/to ram first and waste too much time. If we interleave and send the
             * charset beforehand, we assemble a new charset chunk by chunk, write current screen data to
             * screen-ram to be displayed and decode the colram directly to colram-location $d800 during
             * the overscan, while reading directly from source.
             * This is the only way so far, to achieve 25fps on c64 */
            if(avctx->extradata) {
                /* fetch values from extradata */
                lifetime     = AV_RB32(avctx->extradata + 0);
                frame_count  = AV_RB32(avctx->extradata + 4);
                charset_size = AV_RB32(avctx->extradata + 8);
                frame_size   = AV_RB32(avctx->extradata + 12);

                /* TODO: sanity checks? */
            } else {
                av_log(avctx, AV_LOG_ERROR, "extradata not set\n");
                return AVERROR(EINVAL);
            }

            ch_chunksize=charset_size/lifetime;
            /* TODO: check if charset/size is % lifetime, but maybe check in codec */

            if(pkt->data) num_frames = lifetime;
            else num_frames = c->prev_frame_count;

            for(i = 0; i < num_frames; i++) {
                if(pkt->data) {
                    /* if available, put newest charset chunk into buffer */
                    avio_write(s->pb, pkt->data + ch_chunksize * i, ch_chunksize);
                } else {
                    /* a bit ugly, but is there an alternative to put many zeros? */
                    for(j = 0; j < ch_chunksize; j++) avio_w8(s->pb, 0);
                }

                if(c->prev_pkt.data) {
                    /* put frame (screen + colram) from last packet into buffer */
                    avio_write(s->pb, c->prev_pkt.data + charset_size + frame_size * i, frame_size);
                } else {
                    /* a bit ugly, but is there an alternative to put many zeros? */
                    for(j = 0; j < frame_size; j++) avio_w8(s->pb, 0);
                }
            }

            /* backup current packet for next turn */
            if(pkt->data) {
                /* no backup packet yet? create one! */
                if(!c->prev_pkt.data) av_new_packet(&c->prev_pkt, pkt->size);
                /* we have a packet and data is big enough, reuse it */
                if(c->prev_pkt.data && c->prev_pkt.size >= pkt->size) {
                    memcpy(c->prev_pkt.data, pkt->data, pkt->size);
                    c->prev_pkt.size = pkt->size;
                } else {
                    av_log(avctx, AV_LOG_ERROR, "Too less memory for prev_pkt.\n");
                    return AVERROR(ENOMEM);
                }
            }

            c->prev_frame_count = frame_count;
            break;
        }
        default:
            /* Write things as is. Nice for self-contained frames from non-multicolor modes or if played
             * directly from ram and not from a streaming device (rrnet/mmc) */
            if(pkt) avio_write(s->pb, pkt->data, pkt->size);
        break;
    }

    avio_flush(s->pb);
    return 0;
}

static int a64_write_trailer(struct AVFormatContext *s)
{
    A64MuxerContext *c = s->priv_data;
    AVPacket pkt = {0};
    /* need to flush last packet? */
    if(c->interleaved) a64_write_packet(s, &pkt);
    /* discard backed up packet */
    if(c->prev_pkt.data) av_destruct_packet(&c->prev_pkt);
    return 0;
}

AVOutputFormat ff_a64_muxer = {
    .name           = "a64",
    .long_name      = NULL_IF_CONFIG_SMALL("a64 - video for Commodore 64"),
    .extensions     = "a64, A64",
    .priv_data_size = sizeof (A64Context),
    .video_codec    = CODEC_ID_A64_MULTI,
    .write_header   = a64_write_header,
    .write_packet   = a64_write_packet,
    .write_trailer  = a64_write_trailer,
};
