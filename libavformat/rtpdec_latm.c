/**
 * RTP Depacketization of MP4A-LATM, RFC 3016
 * Copyright (c) 2010 Martin Storsjo
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

#include "rtpdec_formats.h"
#include "internal.h"
#include "libavutil/avstring.h"
#include "libavcodec/get_bits.h"
#include <strings.h>

struct PayloadContext {
    ByteIOContext *dyn_buf;
    uint8_t *buf;
    int pos, len;
    uint32_t timestamp;
};

static PayloadContext *latm_new_context(void)
{
    return av_mallocz(sizeof(PayloadContext));
}

static void latm_free_context(PayloadContext *data)
{
    if (!data)
        return;
    if (data->dyn_buf) {
        uint8_t *p;
        url_close_dyn_buf(data->dyn_buf, &p);
        av_free(p);
    }
    av_free(data->buf);
    av_free(data);
}

static int latm_parse_packet(AVFormatContext *ctx, PayloadContext *data,
                             AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                             const uint8_t *buf, int len, int flags)
{
    int ret, cur_len;

    if (buf) {
        if (!data->dyn_buf || data->timestamp != *timestamp) {
            av_freep(&data->buf);
            if (data->dyn_buf)
                url_close_dyn_buf(data->dyn_buf, &data->buf);
            data->dyn_buf = NULL;
            av_freep(&data->buf);

            data->timestamp = *timestamp;
            if ((ret = url_open_dyn_buf(&data->dyn_buf)) < 0)
                return ret;
        }
        put_buffer(data->dyn_buf, buf, len);

        if (!(flags & RTP_FLAG_MARKER))
            return AVERROR(EAGAIN);
        av_free(data->buf);
        data->len = url_close_dyn_buf(data->dyn_buf, &data->buf);
        data->dyn_buf = NULL;
        data->pos = 0;
    }

    if (!data->buf) {
        av_log(ctx, AV_LOG_ERROR, "No data available yet\n");
        return AVERROR(EIO);
    }

    cur_len = 0;
    while (data->pos < data->len) {
        uint8_t val = data->buf[data->pos++];
        cur_len += val;
        if (val != 0xff)
            break;
    }
    if (data->pos + cur_len > data->len) {
        av_log(ctx, AV_LOG_ERROR, "Malformed LATM packet\n");
        return AVERROR(EIO);
    }

    if ((ret = av_new_packet(pkt, cur_len)) < 0)
        return ret;
    memcpy(pkt->data, data->buf + data->pos, cur_len);
    data->pos += cur_len;
    pkt->stream_index = st->index;
    return data->pos < data->len;
}

static int parse_fmtp_config(AVStream *st, char *value)
{
    int len = ff_hex_to_data(NULL, value), i, ret = 0;
    GetBitContext gb;
    uint8_t *config;
    int audio_mux_version, same_time_framing, num_sub_frames,
        num_programs, num_layers;

    /* Pad this buffer, too, to avoid out of bounds reads with get_bits below */
    config = av_mallocz(len + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!config)
        return AVERROR(ENOMEM);
    ff_hex_to_data(config, value);
    init_get_bits(&gb, config, len*8);
    audio_mux_version = get_bits(&gb, 1);
    same_time_framing = get_bits(&gb, 1);
    num_sub_frames    = get_bits(&gb, 6);
    num_programs      = get_bits(&gb, 4);
    num_layers        = get_bits(&gb, 3);
    if (audio_mux_version != 0 || same_time_framing != 1 || num_programs != 0 ||
        num_layers != 0) {
        av_log(NULL, AV_LOG_WARNING, "Unsupported LATM config (%d,%d,%d,%d)\n",
                                     audio_mux_version, same_time_framing,
                                     num_programs, num_layers);
        ret = AVERROR_PATCHWELCOME;
        goto end;
    }
    av_freep(&st->codec->extradata);
    st->codec->extradata_size = (get_bits_left(&gb) + 7)/8;
    st->codec->extradata = av_mallocz(st->codec->extradata_size +
                                      FF_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codec->extradata) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    for (i = 0; i < st->codec->extradata_size; i++)
        st->codec->extradata[i] = get_bits(&gb, 8);

end:
    av_free(config);
    return ret;
}

static int parse_fmtp(AVStream *stream, PayloadContext *data,
                      char *attr, char *value)
{
    int res;

    if (!strcmp(attr, "config")) {
        res = parse_fmtp_config(stream, value);
        if (res < 0)
            return res;
    } else if (!strcmp(attr, "cpresent")) {
        int cpresent = atoi(value);
        if (cpresent != 0)
            av_log_missing_feature(NULL, "RTP MP4A-LATM with in-band "
                                         "configuration", 1);
    }

    return 0;
}

static int latm_parse_sdp_line(AVFormatContext *s, int st_index,
                               PayloadContext *data, const char *line)
{
    const char *p;

    if (av_strstart(line, "fmtp:", &p))
        return ff_parse_fmtp(s->streams[st_index], data, p, parse_fmtp);

    return 0;
}

RTPDynamicProtocolHandler ff_mp4a_latm_dynamic_handler = {
    .enc_name           = "MP4A-LATM",
    .codec_type         = AVMEDIA_TYPE_AUDIO,
    .codec_id           = CODEC_ID_AAC,
    .parse_sdp_a_line   = latm_parse_sdp_line,
    .open               = latm_new_context,
    .close              = latm_free_context,
    .parse_packet       = latm_parse_packet
};
