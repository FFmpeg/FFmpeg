/*
 * MxPEG clip file demuxer
 * Copyright (c) 2010 Anatoly Nenashev
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

#include "libavutil/intreadwrite.h"
#include "libavcodec/mjpeg.h"
#include "avformat.h"
#include "avio.h"

#define DEFAULT_PACKET_SIZE 1024
#define OVERREAD_SIZE 3

typedef struct MXGContext {
    uint8_t *buffer;
    uint8_t *buffer_ptr;
    uint8_t *soi_ptr;
    unsigned int buffer_size;
    int64_t dts;
    unsigned int cache_size;
} MXGContext;

static int mxg_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVStream *video_st, *audio_st;
    MXGContext *mxg = s->priv_data;

    /* video parameters will be extracted from the compressed bitstream */
    video_st = avformat_new_stream(s, NULL);
    if (!video_st)
        return AVERROR(ENOMEM);
    video_st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    video_st->codec->codec_id = CODEC_ID_MXPEG;
    av_set_pts_info(video_st, 64, 1, 1000000);

    audio_st = avformat_new_stream(s, NULL);
    if (!audio_st)
        return AVERROR(ENOMEM);
    audio_st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    audio_st->codec->codec_id = CODEC_ID_PCM_ALAW;
    audio_st->codec->channels = 1;
    audio_st->codec->sample_rate = 8000;
    audio_st->codec->bits_per_coded_sample = 8;
    audio_st->codec->block_align = 1;
    av_set_pts_info(audio_st, 64, 1, 1000000);

    mxg->soi_ptr = mxg->buffer_ptr = mxg->buffer = 0;
    mxg->buffer_size = 0;
    mxg->dts = AV_NOPTS_VALUE;
    mxg->cache_size = 0;

    return 0;
}

static uint8_t* mxg_find_startmarker(uint8_t *p, uint8_t *end)
{
    for (; p < end - 3; p += 4) {
        uint32_t x = *(uint32_t*)p;

        if (x & (~(x+0x01010101)) & 0x80808080) {
            if (p[0] == 0xff) {
                return p;
            } else if (p[1] == 0xff) {
                return p+1;
            } else if (p[2] == 0xff) {
                return p+2;
            } else if (p[3] == 0xff) {
                return p+3;
            }
        }
    }

    for (; p < end; ++p) {
        if (*p == 0xff) return p;
    }

    return end;
}

static int mxg_update_cache(AVFormatContext *s, unsigned int cache_size)
{
    MXGContext *mxg = s->priv_data;
    unsigned int current_pos = mxg->buffer_ptr - mxg->buffer;
    unsigned int soi_pos;
    int ret;

    /* reallocate internal buffer */
    if (current_pos > current_pos + cache_size)
        return AVERROR(ENOMEM);
    if (mxg->soi_ptr) soi_pos = mxg->soi_ptr - mxg->buffer;
    mxg->buffer = av_fast_realloc(mxg->buffer, &mxg->buffer_size,
                                  current_pos + cache_size +
                                  FF_INPUT_BUFFER_PADDING_SIZE);
    if (!mxg->buffer)
        return AVERROR(ENOMEM);
    mxg->buffer_ptr = mxg->buffer + current_pos;
    if (mxg->soi_ptr) mxg->soi_ptr = mxg->buffer + soi_pos;

    /* get data */
    ret = avio_read(s->pb, mxg->buffer_ptr + mxg->cache_size,
                     cache_size - mxg->cache_size);
    if (ret < 0)
        return ret;

    mxg->cache_size += ret;

    return ret;
}

static int mxg_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    unsigned int size;
    uint8_t *startmarker_ptr, *end, *search_end, marker;
    MXGContext *mxg = s->priv_data;

    while (!url_feof(s->pb) && !s->pb->error){
        if (mxg->cache_size <= OVERREAD_SIZE) {
            /* update internal buffer */
            ret = mxg_update_cache(s, DEFAULT_PACKET_SIZE + OVERREAD_SIZE);
            if (ret < 0)
                return ret;
        }
        end = mxg->buffer_ptr + mxg->cache_size;

        /* find start marker - 0xff */
        if (mxg->cache_size > OVERREAD_SIZE) {
            search_end = end - OVERREAD_SIZE;
            startmarker_ptr = mxg_find_startmarker(mxg->buffer_ptr, search_end);
        } else {
            search_end = end;
            startmarker_ptr = mxg_find_startmarker(mxg->buffer_ptr, search_end);
            if (startmarker_ptr >= search_end - 1 ||
                *(startmarker_ptr + 1) != EOI) break;
        }

        if (startmarker_ptr != search_end) { /* start marker found */
            marker = *(startmarker_ptr + 1);
            mxg->buffer_ptr = startmarker_ptr + 2;
            mxg->cache_size = end - mxg->buffer_ptr;

            if (marker == SOI) {
                mxg->soi_ptr = startmarker_ptr;
            } else if (marker == EOI) {
                if (!mxg->soi_ptr) {
                    av_log(s, AV_LOG_WARNING, "Found EOI before SOI, skipping\n");
                    continue;
                }

                pkt->pts = pkt->dts = mxg->dts;
                pkt->stream_index = 0;
                pkt->destruct = NULL;
                pkt->size = mxg->buffer_ptr - mxg->soi_ptr;
                pkt->data = mxg->soi_ptr;

                if (mxg->soi_ptr - mxg->buffer > mxg->cache_size) {
                    if (mxg->cache_size > 0) {
                        memcpy(mxg->buffer, mxg->buffer_ptr, mxg->cache_size);
                    }

                    mxg->buffer_ptr = mxg->buffer;
                }
                mxg->soi_ptr = 0;

                return pkt->size;
            } else if ( (SOF0 <= marker && marker <= SOF15) ||
                        (SOS  <= marker && marker <= COM) ) {
                /* all other markers that start marker segment also contain
                   length value (see specification for JPEG Annex B.1) */
                size = AV_RB16(mxg->buffer_ptr);
                if (size < 2)
                    return AVERROR(EINVAL);

                if (mxg->cache_size < size) {
                    ret = mxg_update_cache(s, size);
                    if (ret < 0)
                        return ret;
                    startmarker_ptr = mxg->buffer_ptr - 2;
                    mxg->cache_size = 0;
                } else {
                    mxg->cache_size -= size;
                }

                mxg->buffer_ptr += size;

                if (marker == APP13 && size >= 16) { /* audio data */
                    /* time (GMT) of first sample in usec since 1970, little-endian */
                    pkt->pts = pkt->dts = AV_RL64(startmarker_ptr + 8);
                    pkt->stream_index = 1;
                    pkt->destruct = NULL;
                    pkt->size = size - 14;
                    pkt->data = startmarker_ptr + 16;

                    if (startmarker_ptr - mxg->buffer > mxg->cache_size) {
                        if (mxg->cache_size > 0) {
                            memcpy(mxg->buffer, mxg->buffer_ptr, mxg->cache_size);
                        }
                        mxg->buffer_ptr = mxg->buffer;
                    }

                    return pkt->size;
                } else if (marker == COM && size >= 18 &&
                           !strncmp(startmarker_ptr + 4, "MXF", 3)) {
                    /* time (GMT) of video frame in usec since 1970, little-endian */
                    mxg->dts = AV_RL64(startmarker_ptr + 12);
                }
            }
        } else {
            /* start marker not found */
            mxg->buffer_ptr = search_end;
            mxg->cache_size = OVERREAD_SIZE;
        }
    }

    return AVERROR_EOF;
}

static int mxg_close(struct AVFormatContext *s)
{
    MXGContext *mxg = s->priv_data;
    av_freep(&mxg->buffer);
    return 0;
}

AVInputFormat ff_mxg_demuxer = {
    .name = "mxg",
    .long_name = NULL_IF_CONFIG_SMALL("MxPEG clip file format"),
    .priv_data_size = sizeof(MXGContext),
    .read_header = mxg_read_header,
    .read_packet = mxg_read_packet,
    .read_close = mxg_close,
    .extensions = "mxg"
};
