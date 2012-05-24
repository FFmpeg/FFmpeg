/*
 * RTMP input format
 * Copyright (c) 2009 Kostya Shishkov
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

#include "libavcodec/bytestream.h"
#include "libavutil/avstring.h"
#include "libavutil/intfloat.h"
#include "avformat.h"

#include "rtmppkt.h"
#include "flv.h"
#include "url.h"

void ff_amf_write_bool(uint8_t **dst, int val)
{
    bytestream_put_byte(dst, AMF_DATA_TYPE_BOOL);
    bytestream_put_byte(dst, val);
}

void ff_amf_write_number(uint8_t **dst, double val)
{
    bytestream_put_byte(dst, AMF_DATA_TYPE_NUMBER);
    bytestream_put_be64(dst, av_double2int(val));
}

void ff_amf_write_string(uint8_t **dst, const char *str)
{
    bytestream_put_byte(dst, AMF_DATA_TYPE_STRING);
    bytestream_put_be16(dst, strlen(str));
    bytestream_put_buffer(dst, str, strlen(str));
}

void ff_amf_write_null(uint8_t **dst)
{
    bytestream_put_byte(dst, AMF_DATA_TYPE_NULL);
}

void ff_amf_write_object_start(uint8_t **dst)
{
    bytestream_put_byte(dst, AMF_DATA_TYPE_OBJECT);
}

void ff_amf_write_field_name(uint8_t **dst, const char *str)
{
    bytestream_put_be16(dst, strlen(str));
    bytestream_put_buffer(dst, str, strlen(str));
}

void ff_amf_write_object_end(uint8_t **dst)
{
    /* first two bytes are field name length = 0,
     * AMF object should end with it and end marker
     */
    bytestream_put_be24(dst, AMF_DATA_TYPE_OBJECT_END);
}

int ff_rtmp_packet_read(URLContext *h, RTMPPacket *p,
                        int chunk_size, RTMPPacket *prev_pkt)
{
    uint8_t hdr, t, buf[16];
    int channel_id, timestamp, data_size, offset = 0;
    uint32_t extra = 0;
    enum RTMPPacketType type;
    int size = 0;
    int ret;

    if (ffurl_read(h, &hdr, 1) != 1)
        return AVERROR(EIO);
    size++;
    channel_id = hdr & 0x3F;

    if (channel_id < 2) { //special case for channel number >= 64
        buf[1] = 0;
        if (ffurl_read_complete(h, buf, channel_id + 1) != channel_id + 1)
            return AVERROR(EIO);
        size += channel_id + 1;
        channel_id = AV_RL16(buf) + 64;
    }
    data_size = prev_pkt[channel_id].data_size;
    type      = prev_pkt[channel_id].type;
    extra     = prev_pkt[channel_id].extra;

    hdr >>= 6;
    if (hdr == RTMP_PS_ONEBYTE) {
        timestamp = prev_pkt[channel_id].ts_delta;
    } else {
        if (ffurl_read_complete(h, buf, 3) != 3)
            return AVERROR(EIO);
        size += 3;
        timestamp = AV_RB24(buf);
        if (hdr != RTMP_PS_FOURBYTES) {
            if (ffurl_read_complete(h, buf, 3) != 3)
                return AVERROR(EIO);
            size += 3;
            data_size = AV_RB24(buf);
            if (ffurl_read_complete(h, buf, 1) != 1)
                return AVERROR(EIO);
            size++;
            type = buf[0];
            if (hdr == RTMP_PS_TWELVEBYTES) {
                if (ffurl_read_complete(h, buf, 4) != 4)
                    return AVERROR(EIO);
                size += 4;
                extra = AV_RL32(buf);
            }
        }
        if (timestamp == 0xFFFFFF) {
            if (ffurl_read_complete(h, buf, 4) != 4)
                return AVERROR(EIO);
            timestamp = AV_RB32(buf);
        }
    }
    if (hdr != RTMP_PS_TWELVEBYTES)
        timestamp += prev_pkt[channel_id].timestamp;

    if ((ret = ff_rtmp_packet_create(p, channel_id, type, timestamp,
                                     data_size)) < 0)
        return ret;
    p->extra = extra;
    // save history
    prev_pkt[channel_id].channel_id = channel_id;
    prev_pkt[channel_id].type       = type;
    prev_pkt[channel_id].data_size  = data_size;
    prev_pkt[channel_id].ts_delta   = timestamp - prev_pkt[channel_id].timestamp;
    prev_pkt[channel_id].timestamp  = timestamp;
    prev_pkt[channel_id].extra      = extra;
    while (data_size > 0) {
        int toread = FFMIN(data_size, chunk_size);
        if (ffurl_read_complete(h, p->data + offset, toread) != toread) {
            ff_rtmp_packet_destroy(p);
            return AVERROR(EIO);
        }
        data_size -= chunk_size;
        offset    += chunk_size;
        size      += chunk_size;
        if (data_size > 0) {
            ffurl_read_complete(h, &t, 1); //marker
            size++;
            if (t != (0xC0 + channel_id))
                return -1;
        }
    }
    return size;
}

int ff_rtmp_packet_write(URLContext *h, RTMPPacket *pkt,
                         int chunk_size, RTMPPacket *prev_pkt)
{
    uint8_t pkt_hdr[16], *p = pkt_hdr;
    int mode = RTMP_PS_TWELVEBYTES;
    int off = 0;
    int size = 0;

    pkt->ts_delta = pkt->timestamp - prev_pkt[pkt->channel_id].timestamp;

    //if channel_id = 0, this is first presentation of prev_pkt, send full hdr.
    if (prev_pkt[pkt->channel_id].channel_id &&
        pkt->extra == prev_pkt[pkt->channel_id].extra) {
        if (pkt->type == prev_pkt[pkt->channel_id].type &&
            pkt->data_size == prev_pkt[pkt->channel_id].data_size) {
            mode = RTMP_PS_FOURBYTES;
            if (pkt->ts_delta == prev_pkt[pkt->channel_id].ts_delta)
                mode = RTMP_PS_ONEBYTE;
        } else {
            mode = RTMP_PS_EIGHTBYTES;
        }
    }

    if (pkt->channel_id < 64) {
        bytestream_put_byte(&p, pkt->channel_id | (mode << 6));
    } else if (pkt->channel_id < 64 + 256) {
        bytestream_put_byte(&p, 0               | (mode << 6));
        bytestream_put_byte(&p, pkt->channel_id - 64);
    } else {
        bytestream_put_byte(&p, 1               | (mode << 6));
        bytestream_put_le16(&p, pkt->channel_id - 64);
    }
    if (mode != RTMP_PS_ONEBYTE) {
        uint32_t timestamp = pkt->timestamp;
        if (mode != RTMP_PS_TWELVEBYTES)
            timestamp = pkt->ts_delta;
        bytestream_put_be24(&p, timestamp >= 0xFFFFFF ? 0xFFFFFF : timestamp);
        if (mode != RTMP_PS_FOURBYTES) {
            bytestream_put_be24(&p, pkt->data_size);
            bytestream_put_byte(&p, pkt->type);
            if (mode == RTMP_PS_TWELVEBYTES)
                bytestream_put_le32(&p, pkt->extra);
        }
        if (timestamp >= 0xFFFFFF)
            bytestream_put_be32(&p, timestamp);
    }
    // save history
    prev_pkt[pkt->channel_id].channel_id = pkt->channel_id;
    prev_pkt[pkt->channel_id].type       = pkt->type;
    prev_pkt[pkt->channel_id].data_size  = pkt->data_size;
    prev_pkt[pkt->channel_id].timestamp  = pkt->timestamp;
    if (mode != RTMP_PS_TWELVEBYTES) {
        prev_pkt[pkt->channel_id].ts_delta   = pkt->ts_delta;
    } else {
        prev_pkt[pkt->channel_id].ts_delta   = pkt->timestamp;
    }
    prev_pkt[pkt->channel_id].extra      = pkt->extra;

    ffurl_write(h, pkt_hdr, p-pkt_hdr);
    size = p - pkt_hdr + pkt->data_size;
    while (off < pkt->data_size) {
        int towrite = FFMIN(chunk_size, pkt->data_size - off);
        ffurl_write(h, pkt->data + off, towrite);
        off += towrite;
        if (off < pkt->data_size) {
            uint8_t marker = 0xC0 | pkt->channel_id;
            ffurl_write(h, &marker, 1);
            size++;
        }
    }
    return size;
}

int ff_rtmp_packet_create(RTMPPacket *pkt, int channel_id, RTMPPacketType type,
                          int timestamp, int size)
{
    if (size) {
        pkt->data = av_malloc(size);
        if (!pkt->data)
            return AVERROR(ENOMEM);
    }
    pkt->data_size  = size;
    pkt->channel_id = channel_id;
    pkt->type       = type;
    pkt->timestamp  = timestamp;
    pkt->extra      = 0;
    pkt->ts_delta   = 0;

    return 0;
}

void ff_rtmp_packet_destroy(RTMPPacket *pkt)
{
    if (!pkt)
        return;
    av_freep(&pkt->data);
    pkt->data_size = 0;
}

int ff_amf_tag_size(const uint8_t *data, const uint8_t *data_end)
{
    const uint8_t *base = data;

    if (data >= data_end)
        return -1;
    switch (*data++) {
    case AMF_DATA_TYPE_NUMBER:      return 9;
    case AMF_DATA_TYPE_BOOL:        return 2;
    case AMF_DATA_TYPE_STRING:      return 3 + AV_RB16(data);
    case AMF_DATA_TYPE_LONG_STRING: return 5 + AV_RB32(data);
    case AMF_DATA_TYPE_NULL:        return 1;
    case AMF_DATA_TYPE_ARRAY:
        data += 4;
    case AMF_DATA_TYPE_OBJECT:
        for (;;) {
            int size = bytestream_get_be16(&data);
            int t;
            if (!size) {
                data++;
                break;
            }
            if (data + size >= data_end || data + size < data)
                return -1;
            data += size;
            t = ff_amf_tag_size(data, data_end);
            if (t < 0 || data + t >= data_end)
                return -1;
            data += t;
        }
        return data - base;
    case AMF_DATA_TYPE_OBJECT_END:  return 1;
    default:                        return -1;
    }
}

int ff_amf_get_field_value(const uint8_t *data, const uint8_t *data_end,
                           const uint8_t *name, uint8_t *dst, int dst_size)
{
    int namelen = strlen(name);
    int len;

    while (*data != AMF_DATA_TYPE_OBJECT && data < data_end) {
        len = ff_amf_tag_size(data, data_end);
        if (len < 0)
            len = data_end - data;
        data += len;
    }
    if (data_end - data < 3)
        return -1;
    data++;
    for (;;) {
        int size = bytestream_get_be16(&data);
        if (!size)
            break;
        if (data + size >= data_end || data + size < data)
            return -1;
        data += size;
        if (size == namelen && !memcmp(data-size, name, namelen)) {
            switch (*data++) {
            case AMF_DATA_TYPE_NUMBER:
                snprintf(dst, dst_size, "%g", av_int2double(AV_RB64(data)));
                break;
            case AMF_DATA_TYPE_BOOL:
                snprintf(dst, dst_size, "%s", *data ? "true" : "false");
                break;
            case AMF_DATA_TYPE_STRING:
                len = bytestream_get_be16(&data);
                av_strlcpy(dst, data, FFMIN(len+1, dst_size));
                break;
            default:
                return -1;
            }
            return 0;
        }
        len = ff_amf_tag_size(data, data_end);
        if (len < 0 || data + len >= data_end || data + len < data)
            return -1;
        data += len;
    }
    return -1;
}

static const char* rtmp_packet_type(int type)
{
    switch (type) {
    case RTMP_PT_CHUNK_SIZE:     return "chunk size";
    case RTMP_PT_BYTES_READ:     return "bytes read";
    case RTMP_PT_PING:           return "ping";
    case RTMP_PT_SERVER_BW:      return "server bandwidth";
    case RTMP_PT_CLIENT_BW:      return "client bandwidth";
    case RTMP_PT_AUDIO:          return "audio packet";
    case RTMP_PT_VIDEO:          return "video packet";
    case RTMP_PT_FLEX_STREAM:    return "Flex shared stream";
    case RTMP_PT_FLEX_OBJECT:    return "Flex shared object";
    case RTMP_PT_FLEX_MESSAGE:   return "Flex shared message";
    case RTMP_PT_NOTIFY:         return "notification";
    case RTMP_PT_SHARED_OBJ:     return "shared object";
    case RTMP_PT_INVOKE:         return "invoke";
    case RTMP_PT_METADATA:       return "metadata";
    default:                     return "unknown";
    }
}

static void ff_amf_tag_contents(void *ctx, const uint8_t *data, const uint8_t *data_end)
{
    int size;
    char buf[1024];

    if (data >= data_end)
        return;
    switch (*data++) {
    case AMF_DATA_TYPE_NUMBER:
        av_log(ctx, AV_LOG_DEBUG, " number %g\n", av_int2double(AV_RB64(data)));
        return;
    case AMF_DATA_TYPE_BOOL:
        av_log(ctx, AV_LOG_DEBUG, " bool %d\n", *data);
        return;
    case AMF_DATA_TYPE_STRING:
    case AMF_DATA_TYPE_LONG_STRING:
        if (data[-1] == AMF_DATA_TYPE_STRING) {
            size = bytestream_get_be16(&data);
        } else {
            size = bytestream_get_be32(&data);
        }
        size = FFMIN(size, 1023);
        memcpy(buf, data, size);
        buf[size] = 0;
        av_log(ctx, AV_LOG_DEBUG, " string '%s'\n", buf);
        return;
    case AMF_DATA_TYPE_NULL:
        av_log(ctx, AV_LOG_DEBUG, " NULL\n");
        return;
    case AMF_DATA_TYPE_ARRAY:
        data += 4;
    case AMF_DATA_TYPE_OBJECT:
        av_log(ctx, AV_LOG_DEBUG, " {\n");
        for (;;) {
            int size = bytestream_get_be16(&data);
            int t;
            memcpy(buf, data, size);
            buf[size] = 0;
            if (!size) {
                av_log(ctx, AV_LOG_DEBUG, " }\n");
                data++;
                break;
            }
            if (data + size >= data_end || data + size < data)
                return;
            data += size;
            av_log(ctx, AV_LOG_DEBUG, "  %s: ", buf);
            ff_amf_tag_contents(ctx, data, data_end);
            t = ff_amf_tag_size(data, data_end);
            if (t < 0 || data + t >= data_end)
                return;
            data += t;
        }
        return;
    case AMF_DATA_TYPE_OBJECT_END:
        av_log(ctx, AV_LOG_DEBUG, " }\n");
        return;
    default:
        return;
    }
}

void ff_rtmp_packet_dump(void *ctx, RTMPPacket *p)
{
    av_log(ctx, AV_LOG_DEBUG, "RTMP packet type '%s'(%d) for channel %d, timestamp %d, extra field %d size %d\n",
           rtmp_packet_type(p->type), p->type, p->channel_id, p->timestamp, p->extra, p->data_size);
    if (p->type == RTMP_PT_INVOKE || p->type == RTMP_PT_NOTIFY) {
        uint8_t *src = p->data, *src_end = p->data + p->data_size;
        while (src < src_end) {
            int sz;
            ff_amf_tag_contents(ctx, src, src_end);
            sz = ff_amf_tag_size(src, src_end);
            if (sz < 0)
                break;
            src += sz;
        }
    } else if (p->type == RTMP_PT_SERVER_BW){
        av_log(ctx, AV_LOG_DEBUG, "Server BW = %d\n", AV_RB32(p->data));
    } else if (p->type == RTMP_PT_CLIENT_BW){
        av_log(ctx, AV_LOG_DEBUG, "Client BW = %d\n", AV_RB32(p->data));
    } else if (p->type != RTMP_PT_AUDIO && p->type != RTMP_PT_VIDEO && p->type != RTMP_PT_METADATA) {
        int i;
        for (i = 0; i < p->data_size; i++)
            av_log(ctx, AV_LOG_DEBUG, " %02X", p->data[i]);
        av_log(ctx, AV_LOG_DEBUG, "\n");
    }
}
