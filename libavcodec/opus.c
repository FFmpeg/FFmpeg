/*
 * Copyright (c) 2012 Andrew D'Addesio
 * Copyright (c) 2013-2014 Mozilla Corporation
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
 * Opus decoder/parser shared code
 */

#include <stdint.h>

#include "libavutil/error.h"
#include "libavutil/internal.h"

#include "opus.h"
#include "vorbis.h"

static const uint16_t opus_frame_duration[32] = {
    480, 960, 1920, 2880,
    480, 960, 1920, 2880,
    480, 960, 1920, 2880,
    480, 960,
    480, 960,
    120, 240,  480,  960,
    120, 240,  480,  960,
    120, 240,  480,  960,
    120, 240,  480,  960,
};

/**
 * Read a 1- or 2-byte frame length
 */
static inline int xiph_lacing_16bit(const uint8_t **ptr, const uint8_t *end)
{
    int val;

    if (*ptr >= end)
        return AVERROR_INVALIDDATA;
    val = *(*ptr)++;
    if (val >= 252) {
        if (*ptr >= end)
            return AVERROR_INVALIDDATA;
        val += 4 * *(*ptr)++;
    }
    return val;
}

/**
 * Read a multi-byte length (used for code 3 packet padding size)
 */
static inline int xiph_lacing_full(const uint8_t **ptr, const uint8_t *end)
{
    int val = 0;
    int next;

    while (1) {
        if (*ptr >= end || val > INT_MAX - 254)
            return AVERROR_INVALIDDATA;
        next = *(*ptr)++;
        val += next;
        if (next < 255)
            break;
        else
            val--;
    }
    return val;
}

/**
 * Parse Opus packet info from raw packet data
 */
int ff_opus_parse_packet(OpusPacket *pkt, const uint8_t *buf, int buf_size,
                         int self_delimiting)
{
    const uint8_t *ptr = buf;
    const uint8_t *end = buf + buf_size;
    int padding = 0;
    int frame_bytes, i;

    if (buf_size < 1)
        goto fail;

    /* TOC byte */
    i = *ptr++;
    pkt->code   = (i     ) & 0x3;
    pkt->stereo = (i >> 2) & 0x1;
    pkt->config = (i >> 3) & 0x1F;

    /* code 2 and code 3 packets have at least 1 byte after the TOC */
    if (pkt->code >= 2 && buf_size < 2)
        goto fail;

    switch (pkt->code) {
    case 0:
        /* 1 frame */
        pkt->frame_count = 1;
        pkt->vbr         = 0;

        if (self_delimiting) {
            int len = xiph_lacing_16bit(&ptr, end);
            if (len < 0 || len > end - ptr)
                goto fail;
            end      = ptr + len;
            buf_size = end - buf;
        }

        frame_bytes = end - ptr;
        if (frame_bytes > MAX_FRAME_SIZE)
            goto fail;
        pkt->frame_offset[0] = ptr - buf;
        pkt->frame_size[0]   = frame_bytes;
        break;
    case 1:
        /* 2 frames, equal size */
        pkt->frame_count = 2;
        pkt->vbr         = 0;

        if (self_delimiting) {
            int len = xiph_lacing_16bit(&ptr, end);
            if (len < 0 || 2 * len > end - ptr)
                goto fail;
            end      = ptr + 2 * len;
            buf_size = end - buf;
        }

        frame_bytes = end - ptr;
        if (frame_bytes & 1 || frame_bytes >> 1 > MAX_FRAME_SIZE)
            goto fail;
        pkt->frame_offset[0] = ptr - buf;
        pkt->frame_size[0]   = frame_bytes >> 1;
        pkt->frame_offset[1] = pkt->frame_offset[0] + pkt->frame_size[0];
        pkt->frame_size[1]   = frame_bytes >> 1;
        break;
    case 2:
        /* 2 frames, different sizes */
        pkt->frame_count = 2;
        pkt->vbr         = 1;

        /* read 1st frame size */
        frame_bytes = xiph_lacing_16bit(&ptr, end);
        if (frame_bytes < 0)
            goto fail;

        if (self_delimiting) {
            int len = xiph_lacing_16bit(&ptr, end);
            if (len < 0 || len + frame_bytes > end - ptr)
                goto fail;
            end      = ptr + frame_bytes + len;
            buf_size = end - buf;
        }

        pkt->frame_offset[0] = ptr - buf;
        pkt->frame_size[0]   = frame_bytes;

        /* calculate 2nd frame size */
        frame_bytes = end - ptr - pkt->frame_size[0];
        if (frame_bytes < 0 || frame_bytes > MAX_FRAME_SIZE)
            goto fail;
        pkt->frame_offset[1] = pkt->frame_offset[0] + pkt->frame_size[0];
        pkt->frame_size[1]   = frame_bytes;
        break;
    case 3:
        /* 1 to 48 frames, can be different sizes */
        i = *ptr++;
        pkt->frame_count = (i     ) & 0x3F;
        padding          = (i >> 6) & 0x01;
        pkt->vbr         = (i >> 7) & 0x01;

        if (pkt->frame_count == 0 || pkt->frame_count > MAX_FRAMES)
            goto fail;

        /* read padding size */
        if (padding) {
            padding = xiph_lacing_full(&ptr, end);
            if (padding < 0)
                goto fail;
        }

        /* read frame sizes */
        if (pkt->vbr) {
            /* for VBR, all frames except the final one have their size coded
               in the bitstream. the last frame size is implicit. */
            int total_bytes = 0;
            for (i = 0; i < pkt->frame_count - 1; i++) {
                frame_bytes = xiph_lacing_16bit(&ptr, end);
                if (frame_bytes < 0)
                    goto fail;
                pkt->frame_size[i] = frame_bytes;
                total_bytes += frame_bytes;
            }

            if (self_delimiting) {
                int len = xiph_lacing_16bit(&ptr, end);
                if (len < 0 || len + total_bytes + padding > end - ptr)
                    goto fail;
                end      = ptr + total_bytes + len + padding;
                buf_size = end - buf;
            }

            frame_bytes = end - ptr - padding;
            if (total_bytes > frame_bytes)
                goto fail;
            pkt->frame_offset[0] = ptr - buf;
            for (i = 1; i < pkt->frame_count; i++)
                pkt->frame_offset[i] = pkt->frame_offset[i-1] + pkt->frame_size[i-1];
            pkt->frame_size[pkt->frame_count-1] = frame_bytes - total_bytes;
        } else {
            /* for CBR, the remaining packet bytes are divided evenly between
               the frames */
            if (self_delimiting) {
                frame_bytes = xiph_lacing_16bit(&ptr, end);
                if (frame_bytes < 0 || pkt->frame_count * frame_bytes + padding > end - ptr)
                    goto fail;
                end      = ptr + pkt->frame_count * frame_bytes + padding;
                buf_size = end - buf;
            } else {
                frame_bytes = end - ptr - padding;
                if (frame_bytes % pkt->frame_count ||
                    frame_bytes / pkt->frame_count > MAX_FRAME_SIZE)
                    goto fail;
                frame_bytes /= pkt->frame_count;
            }

            pkt->frame_offset[0] = ptr - buf;
            pkt->frame_size[0]   = frame_bytes;
            for (i = 1; i < pkt->frame_count; i++) {
                pkt->frame_offset[i] = pkt->frame_offset[i-1] + pkt->frame_size[i-1];
                pkt->frame_size[i]   = frame_bytes;
            }
        }
    }

    pkt->packet_size = buf_size;
    pkt->data_size   = pkt->packet_size - padding;

    /* total packet duration cannot be larger than 120ms */
    pkt->frame_duration = opus_frame_duration[pkt->config];
    if (pkt->frame_duration * pkt->frame_count > MAX_PACKET_DUR)
        goto fail;

    /* set mode and bandwidth */
    if (pkt->config < 12) {
        pkt->mode = OPUS_MODE_SILK;
        pkt->bandwidth = pkt->config >> 2;
    } else if (pkt->config < 16) {
        pkt->mode = OPUS_MODE_HYBRID;
        pkt->bandwidth = OPUS_BANDWIDTH_SUPERWIDEBAND + (pkt->config >= 14);
    } else {
        pkt->mode = OPUS_MODE_CELT;
        pkt->bandwidth = (pkt->config - 16) >> 2;
        /* skip mediumband */
        if (pkt->bandwidth)
            pkt->bandwidth++;
    }

    return 0;

fail:
    memset(pkt, 0, sizeof(*pkt));
    return AVERROR_INVALIDDATA;
}

static int channel_reorder_vorbis(int nb_channels, int channel_idx)
{
    return ff_vorbis_channel_layout_offsets[nb_channels - 1][channel_idx];
}

static int channel_reorder_unknown(int nb_channels, int channel_idx)
{
    return channel_idx;
}

av_cold int ff_opus_parse_extradata(AVCodecContext *avctx,
                                    OpusContext *s)
{
    static const uint8_t default_channel_map[2] = { 0, 1 };

    int (*channel_reorder)(int, int) = channel_reorder_unknown;

    const uint8_t *extradata, *channel_map;
    int extradata_size;
    int version, channels, map_type, streams, stereo_streams, i, j;
    uint64_t layout;

    if (!avctx->extradata) {
        if (avctx->channels > 2) {
            av_log(avctx, AV_LOG_ERROR,
                   "Multichannel configuration without extradata.\n");
            return AVERROR(EINVAL);
        }
        extradata      = opus_default_extradata;
        extradata_size = sizeof(opus_default_extradata);
    } else {
        extradata = avctx->extradata;
        extradata_size = avctx->extradata_size;
    }

    if (extradata_size < 19) {
        av_log(avctx, AV_LOG_ERROR, "Invalid extradata size: %d\n",
               extradata_size);
        return AVERROR_INVALIDDATA;
    }

    version = extradata[8];
    if (version > 15) {
        avpriv_request_sample(avctx, "Extradata version %d", version);
        return AVERROR_PATCHWELCOME;
    }

    avctx->delay = AV_RL16(extradata + 10);

    channels = avctx->extradata ? extradata[9] : (avctx->channels == 1) ? 1 : 2;
    if (!channels) {
        av_log(avctx, AV_LOG_ERROR, "Zero channel count specified in the extadata\n");
        return AVERROR_INVALIDDATA;
    }

    s->gain_i = AV_RL16(extradata + 16);
    if (s->gain_i)
        s->gain = ff_exp10(s->gain_i / (20.0 * 256));

    map_type = extradata[18];
    if (!map_type) {
        if (channels > 2) {
            av_log(avctx, AV_LOG_ERROR,
                   "Channel mapping 0 is only specified for up to 2 channels\n");
            return AVERROR_INVALIDDATA;
        }
        layout         = (channels == 1) ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
        streams        = 1;
        stereo_streams = channels - 1;
        channel_map    = default_channel_map;
    } else if (map_type == 1 || map_type == 255) {
        if (extradata_size < 21 + channels) {
            av_log(avctx, AV_LOG_ERROR, "Invalid extradata size: %d\n",
                   extradata_size);
            return AVERROR_INVALIDDATA;
        }

        streams        = extradata[19];
        stereo_streams = extradata[20];
        if (!streams || stereo_streams > streams ||
            streams + stereo_streams > 255) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid stream/stereo stream count: %d/%d\n", streams, stereo_streams);
            return AVERROR_INVALIDDATA;
        }

        if (map_type == 1) {
            if (channels > 8) {
                av_log(avctx, AV_LOG_ERROR,
                       "Channel mapping 1 is only specified for up to 8 channels\n");
                return AVERROR_INVALIDDATA;
            }
            layout = ff_vorbis_channel_layouts[channels - 1];
            channel_reorder = channel_reorder_vorbis;
        } else
            layout = 0;

        channel_map = extradata + 21;
    } else {
        avpriv_request_sample(avctx, "Mapping type %d", map_type);
        return AVERROR_PATCHWELCOME;
    }

    s->channel_maps = av_mallocz_array(channels, sizeof(*s->channel_maps));
    if (!s->channel_maps)
        return AVERROR(ENOMEM);

    for (i = 0; i < channels; i++) {
        ChannelMap *map = &s->channel_maps[i];
        uint8_t     idx = channel_map[channel_reorder(channels, i)];

        if (idx == 255) {
            map->silence = 1;
            continue;
        } else if (idx >= streams + stereo_streams) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid channel map for output channel %d: %d\n", i, idx);
            return AVERROR_INVALIDDATA;
        }

        /* check that we din't see this index yet */
        map->copy = 0;
        for (j = 0; j < i; j++)
            if (channel_map[channel_reorder(channels, j)] == idx) {
                map->copy     = 1;
                map->copy_idx = j;
                break;
            }

        if (idx < 2 * stereo_streams) {
            map->stream_idx  = idx / 2;
            map->channel_idx = idx & 1;
        } else {
            map->stream_idx  = idx - stereo_streams;
            map->channel_idx = 0;
        }
    }

    avctx->channels       = channels;
    avctx->channel_layout = layout;
    s->nb_streams         = streams;
    s->nb_stereo_streams  = stereo_streams;

    return 0;
}
