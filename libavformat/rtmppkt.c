/*
 * RTMP input format
 * Copyright (c) 2009 Konstantin Shishkov
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
#include "libavutil/intfloat.h"
#include "libavutil/mem.h"

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

void ff_amf_write_array_start(uint8_t **dst, uint32_t length)
{
    bytestream_put_byte(dst, AMF_DATA_TYPE_ARRAY);
    bytestream_put_be32(dst, length);
}

void ff_amf_write_string(uint8_t **dst, const char *str)
{
    bytestream_put_byte(dst, AMF_DATA_TYPE_STRING);
    bytestream_put_be16(dst, strlen(str));
    bytestream_put_buffer(dst, str, strlen(str));
}

void ff_amf_write_string2(uint8_t **dst, const char *str1, const char *str2)
{
    int len1 = 0, len2 = 0;
    if (str1)
        len1 = strlen(str1);
    if (str2)
        len2 = strlen(str2);
    bytestream_put_byte(dst, AMF_DATA_TYPE_STRING);
    bytestream_put_be16(dst, len1 + len2);
    bytestream_put_buffer(dst, str1, len1);
    bytestream_put_buffer(dst, str2, len2);
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

int ff_amf_read_number(GetByteContext *bc, double *val)
{
    uint64_t read;
    if (bytestream2_get_byte(bc) != AMF_DATA_TYPE_NUMBER)
        return AVERROR_INVALIDDATA;
    read = bytestream2_get_be64(bc);
    *val = av_int2double(read);
    return 0;
}

int ff_amf_get_string(GetByteContext *bc, uint8_t *str,
                      int strsize, int *length)
{
    int stringlen = 0;
    int readsize;
    stringlen = bytestream2_get_be16(bc);
    if (stringlen + 1 > strsize)
        return AVERROR(EINVAL);
    readsize = bytestream2_get_buffer(bc, str, stringlen);
    if (readsize != stringlen) {
        av_log(NULL, AV_LOG_WARNING,
               "Unable to read as many bytes as AMF string signaled\n");
    }
    str[readsize] = '\0';
    *length = FFMIN(stringlen, readsize);
    return 0;
}

int ff_amf_read_string(GetByteContext *bc, uint8_t *str,
                       int strsize, int *length)
{
    if (bytestream2_get_byte(bc) != AMF_DATA_TYPE_STRING)
        return AVERROR_INVALIDDATA;
    return ff_amf_get_string(bc, str, strsize, length);
}

int ff_amf_read_null(GetByteContext *bc)
{
    if (bytestream2_get_byte(bc) != AMF_DATA_TYPE_NULL)
        return AVERROR_INVALIDDATA;
    return 0;
}

int ff_rtmp_check_alloc_array(RTMPPacket **prev_pkt, int *nb_prev_pkt,
                              int channel)
{
    int nb_alloc;
    RTMPPacket *ptr;
    if (channel < *nb_prev_pkt)
        return 0;

    nb_alloc = channel + 16;
    // This can't use the av_reallocp family of functions, since we
    // would need to free each element in the array before the array
    // itself is freed.
    ptr = av_realloc_array(*prev_pkt, nb_alloc, sizeof(**prev_pkt));
    if (!ptr)
        return AVERROR(ENOMEM);
    memset(ptr + *nb_prev_pkt, 0, (nb_alloc - *nb_prev_pkt) * sizeof(*ptr));
    *prev_pkt = ptr;
    *nb_prev_pkt = nb_alloc;
    return 0;
}

int ff_rtmp_packet_read(URLContext *h, RTMPPacket *p,
                        int chunk_size, RTMPPacket **prev_pkt, int *nb_prev_pkt)
{
    uint8_t hdr;

    if (ffurl_read(h, &hdr, 1) != 1)
        return AVERROR(EIO);

    return ff_rtmp_packet_read_internal(h, p, chunk_size, prev_pkt,
                                        nb_prev_pkt, hdr);
}

static int rtmp_packet_read_one_chunk(URLContext *h, RTMPPacket *p,
                                      int chunk_size, RTMPPacket **prev_pkt_ptr,
                                      int *nb_prev_pkt, uint8_t hdr)
{

    uint8_t buf[16];
    int channel_id, timestamp, size;
    uint32_t ts_field; // non-extended timestamp or delta field
    uint32_t extra = 0;
    enum RTMPPacketType type;
    int written = 0;
    int ret, toread;
    RTMPPacket *prev_pkt;

    written++;
    channel_id = hdr & 0x3F;

    if (channel_id < 2) { //special case for channel number >= 64
        buf[1] = 0;
        if (ffurl_read_complete(h, buf, channel_id + 1) != channel_id + 1)
            return AVERROR(EIO);
        written += channel_id + 1;
        channel_id = AV_RL16(buf) + 64;
    }
    if ((ret = ff_rtmp_check_alloc_array(prev_pkt_ptr, nb_prev_pkt,
                                         channel_id)) < 0)
        return ret;
    prev_pkt = *prev_pkt_ptr;
    size  = prev_pkt[channel_id].size;
    type  = prev_pkt[channel_id].type;
    extra = prev_pkt[channel_id].extra;

    hdr >>= 6; // header size indicator
    if (hdr == RTMP_PS_ONEBYTE) {
        ts_field = prev_pkt[channel_id].ts_field;
    } else {
        if (ffurl_read_complete(h, buf, 3) != 3)
            return AVERROR(EIO);
        written += 3;
        ts_field = AV_RB24(buf);
        if (hdr != RTMP_PS_FOURBYTES) {
            if (ffurl_read_complete(h, buf, 3) != 3)
                return AVERROR(EIO);
            written += 3;
            size = AV_RB24(buf);
            if (ffurl_read_complete(h, buf, 1) != 1)
                return AVERROR(EIO);
            written++;
            type = buf[0];
            if (hdr == RTMP_PS_TWELVEBYTES) {
                if (ffurl_read_complete(h, buf, 4) != 4)
                    return AVERROR(EIO);
                written += 4;
                extra = AV_RL32(buf);
            }
        }
    }
    if (ts_field == 0xFFFFFF) {
        if (ffurl_read_complete(h, buf, 4) != 4)
            return AVERROR(EIO);
        timestamp = AV_RB32(buf);
    } else {
        timestamp = ts_field;
    }
    if (hdr != RTMP_PS_TWELVEBYTES)
        timestamp += prev_pkt[channel_id].timestamp;

    if (prev_pkt[channel_id].read && size != prev_pkt[channel_id].size) {
        av_log(h, AV_LOG_ERROR, "RTMP packet size mismatch %d != %d\n",
                                size, prev_pkt[channel_id].size);
        ff_rtmp_packet_destroy(&prev_pkt[channel_id]);
        prev_pkt[channel_id].read = 0;
        return AVERROR_INVALIDDATA;
    }

    if (!prev_pkt[channel_id].read) {
        if ((ret = ff_rtmp_packet_create(p, channel_id, type, timestamp,
                                         size)) < 0)
            return ret;
        p->read = written;
        p->offset = 0;
        prev_pkt[channel_id].ts_field   = ts_field;
        prev_pkt[channel_id].timestamp  = timestamp;
    } else {
        // previous packet in this channel hasn't completed reading
        RTMPPacket *prev = &prev_pkt[channel_id];
        p->data          = prev->data;
        p->size          = prev->size;
        p->channel_id    = prev->channel_id;
        p->type          = prev->type;
        p->ts_field      = prev->ts_field;
        p->extra         = prev->extra;
        p->offset        = prev->offset;
        p->read          = prev->read + written;
        p->timestamp     = prev->timestamp;
        prev->data       = NULL;
    }
    p->extra = extra;
    // save history
    prev_pkt[channel_id].channel_id = channel_id;
    prev_pkt[channel_id].type       = type;
    prev_pkt[channel_id].size       = size;
    prev_pkt[channel_id].extra      = extra;
    size = size - p->offset;

    toread = FFMIN(size, chunk_size);
    if (ffurl_read_complete(h, p->data + p->offset, toread) != toread) {
        ff_rtmp_packet_destroy(p);
        return AVERROR(EIO);
    }
    size      -= toread;
    p->read   += toread;
    p->offset += toread;

    if (size > 0) {
       RTMPPacket *prev = &prev_pkt[channel_id];
       prev->data = p->data;
       prev->read = p->read;
       prev->offset = p->offset;
       p->data      = NULL;
       return AVERROR(EAGAIN);
    }

    prev_pkt[channel_id].read = 0; // read complete; reset if needed
    return p->read;
}

int ff_rtmp_packet_read_internal(URLContext *h, RTMPPacket *p, int chunk_size,
                                 RTMPPacket **prev_pkt, int *nb_prev_pkt,
                                 uint8_t hdr)
{
    while (1) {
        int ret = rtmp_packet_read_one_chunk(h, p, chunk_size, prev_pkt,
                                             nb_prev_pkt, hdr);
        if (ret > 0 || ret != AVERROR(EAGAIN))
            return ret;

        if (ffurl_read(h, &hdr, 1) != 1)
            return AVERROR(EIO);
    }
}

int ff_rtmp_packet_write(URLContext *h, RTMPPacket *pkt,
                         int chunk_size, RTMPPacket **prev_pkt_ptr,
                         int *nb_prev_pkt)
{
    uint8_t pkt_hdr[16], *p = pkt_hdr;
    int mode = RTMP_PS_TWELVEBYTES;
    int off = 0;
    int written = 0;
    int ret;
    RTMPPacket *prev_pkt;
    int use_delta; // flag if using timestamp delta, not RTMP_PS_TWELVEBYTES
    uint32_t timestamp; // full 32-bit timestamp or delta value

    if ((ret = ff_rtmp_check_alloc_array(prev_pkt_ptr, nb_prev_pkt,
                                         pkt->channel_id)) < 0)
        return ret;
    prev_pkt = *prev_pkt_ptr;

    //if channel_id = 0, this is first presentation of prev_pkt, send full hdr.
    use_delta = prev_pkt[pkt->channel_id].channel_id &&
        pkt->extra == prev_pkt[pkt->channel_id].extra &&
        pkt->timestamp >= prev_pkt[pkt->channel_id].timestamp;

    timestamp = pkt->timestamp;
    if (use_delta) {
        timestamp -= prev_pkt[pkt->channel_id].timestamp;
    }
    if (timestamp >= 0xFFFFFF) {
        pkt->ts_field = 0xFFFFFF;
    } else {
        pkt->ts_field = timestamp;
    }

    if (use_delta) {
        if (pkt->type == prev_pkt[pkt->channel_id].type &&
            pkt->size == prev_pkt[pkt->channel_id].size) {
            mode = RTMP_PS_FOURBYTES;
            if (pkt->ts_field == prev_pkt[pkt->channel_id].ts_field)
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
        bytestream_put_be24(&p, pkt->ts_field);
        if (mode != RTMP_PS_FOURBYTES) {
            bytestream_put_be24(&p, pkt->size);
            bytestream_put_byte(&p, pkt->type);
            if (mode == RTMP_PS_TWELVEBYTES)
                bytestream_put_le32(&p, pkt->extra);
        }
    }
    if (pkt->ts_field == 0xFFFFFF)
        bytestream_put_be32(&p, timestamp);
    // save history
    prev_pkt[pkt->channel_id].channel_id = pkt->channel_id;
    prev_pkt[pkt->channel_id].type       = pkt->type;
    prev_pkt[pkt->channel_id].size       = pkt->size;
    prev_pkt[pkt->channel_id].timestamp  = pkt->timestamp;
    prev_pkt[pkt->channel_id].ts_field   = pkt->ts_field;
    prev_pkt[pkt->channel_id].extra      = pkt->extra;

    // FIXME:
    // Writing packets is currently not optimized to minimize system calls.
    // Since system calls flush on exit which we cannot change in a system-independant way.
    // We should fix this behavior and by writing packets in a single or in as few as possible system calls.
    // Protocols like TCP and RTMP should benefit from this when enabling TCP_NODELAY.

    if ((ret = ffurl_write(h, pkt_hdr, p - pkt_hdr)) < 0)
        return ret;
    written = p - pkt_hdr + pkt->size;
    while (off < pkt->size) {
        int towrite = FFMIN(chunk_size, pkt->size - off);
        if ((ret = ffurl_write(h, pkt->data + off, towrite)) < 0)
            return ret;
        off += towrite;
        if (off < pkt->size) {
            uint8_t marker = 0xC0 | pkt->channel_id;
            if ((ret = ffurl_write(h, &marker, 1)) < 0)
                return ret;
            written++;
            if (pkt->ts_field == 0xFFFFFF) {
                uint8_t ts_header[4];
                AV_WB32(ts_header, timestamp);
                if ((ret = ffurl_write(h, ts_header, 4)) < 0)
                    return ret;
                written += 4;
            }
        }
    }
    return written;
}

int ff_rtmp_packet_create(RTMPPacket *pkt, int channel_id, RTMPPacketType type,
                          int timestamp, int size)
{
    if (size) {
        pkt->data = av_realloc(NULL, size);
        if (!pkt->data)
            return AVERROR(ENOMEM);
    }
    pkt->size       = size;
    pkt->channel_id = channel_id;
    pkt->type       = type;
    pkt->timestamp  = timestamp;
    pkt->extra      = 0;
    pkt->ts_field   = 0;

    return 0;
}

void ff_rtmp_packet_destroy(RTMPPacket *pkt)
{
    if (!pkt)
        return;
    av_freep(&pkt->data);
    pkt->size = 0;
}

static int amf_tag_skip(GetByteContext *gb)
{
    AMFDataType type;
    unsigned nb   = -1;

    if (bytestream2_get_bytes_left(gb) < 1)
        return -1;

    type = bytestream2_get_byte(gb);
    switch (type) {
    case AMF_DATA_TYPE_NUMBER:
        bytestream2_get_be64(gb);
        return 0;
    case AMF_DATA_TYPE_BOOL:
        bytestream2_get_byte(gb);
        return 0;
    case AMF_DATA_TYPE_STRING:
        bytestream2_skip(gb, bytestream2_get_be16(gb));
        return 0;
    case AMF_DATA_TYPE_LONG_STRING:
        bytestream2_skip(gb, bytestream2_get_be32(gb));
        return 0;
    case AMF_DATA_TYPE_NULL:
        return 0;
    case AMF_DATA_TYPE_DATE:
        bytestream2_skip(gb, 10);
        return 0;
    case AMF_DATA_TYPE_ARRAY:
    case AMF_DATA_TYPE_MIXEDARRAY:
        nb = bytestream2_get_be32(gb);
    case AMF_DATA_TYPE_OBJECT:
        while (type != AMF_DATA_TYPE_ARRAY || nb-- > 0) {
            int t;
            if (type != AMF_DATA_TYPE_ARRAY) {
                int size = bytestream2_get_be16(gb);
                if (!size) {
                    bytestream2_get_byte(gb);
                    break;
                }
                if (size < 0 || size >= bytestream2_get_bytes_left(gb))
                    return -1;
                bytestream2_skip(gb, size);
            }
            t = amf_tag_skip(gb);
            if (t < 0 || bytestream2_get_bytes_left(gb) <= 0)
                return -1;
        }
        return 0;
    case AMF_DATA_TYPE_OBJECT_END:  return 0;
    default:                        return -1;
    }
}

int ff_amf_tag_size(const uint8_t *data, const uint8_t *data_end)
{
    GetByteContext gb;
    int ret;

    if (data >= data_end)
        return -1;

    bytestream2_init(&gb, data, data_end - data);

    ret = amf_tag_skip(&gb);
    if (ret < 0 || bytestream2_get_bytes_left(&gb) <= 0)
        return -1;
    av_assert0(bytestream2_tell(&gb) >= 0 && bytestream2_tell(&gb) <= data_end - data);
    return bytestream2_tell(&gb);
}

static int amf_get_field_value2(GetByteContext *gb,
                           const uint8_t *name, uint8_t *dst, int dst_size)
{
    int namelen = strlen(name);
    int len;

    while (bytestream2_peek_byte(gb) != AMF_DATA_TYPE_OBJECT && bytestream2_get_bytes_left(gb) > 0) {
        int ret = amf_tag_skip(gb);
        if (ret < 0)
            return -1;
    }
    if (bytestream2_get_bytes_left(gb) < 3)
        return -1;
    bytestream2_get_byte(gb);

    for (;;) {
        int size = bytestream2_get_be16(gb);
        if (!size)
            break;
        if (size < 0 || size >= bytestream2_get_bytes_left(gb))
            return -1;
        bytestream2_skip(gb, size);
        if (size == namelen && !memcmp(gb->buffer-size, name, namelen)) {
            switch (bytestream2_get_byte(gb)) {
            case AMF_DATA_TYPE_NUMBER:
                snprintf(dst, dst_size, "%g", av_int2double(bytestream2_get_be64(gb)));
                break;
            case AMF_DATA_TYPE_BOOL:
                snprintf(dst, dst_size, "%s", bytestream2_get_byte(gb) ? "true" : "false");
                break;
            case AMF_DATA_TYPE_STRING:
                len = bytestream2_get_be16(gb);
                if (dst_size < 1)
                    return -1;
                if (dst_size < len + 1)
                    len = dst_size - 1;
                bytestream2_get_buffer(gb, dst, len);
                dst[len] = 0;
                break;
            default:
                return -1;
            }
            return 0;
        }
        len = amf_tag_skip(gb);
        if (len < 0 || bytestream2_get_bytes_left(gb) <= 0)
            return -1;
    }
    return -1;
}

int ff_amf_get_field_value(const uint8_t *data, const uint8_t *data_end,
                           const uint8_t *name, uint8_t *dst, int dst_size)
{
    GetByteContext gb;

    if (data >= data_end)
        return -1;

    bytestream2_init(&gb, data, data_end - data);

    return amf_get_field_value2(&gb, name, dst, dst_size);
}

#ifdef DEBUG
static const char* rtmp_packet_type(int type)
{
    switch (type) {
    case RTMP_PT_CHUNK_SIZE:     return "chunk size";
    case RTMP_PT_BYTES_READ:     return "bytes read";
    case RTMP_PT_USER_CONTROL:   return "user control";
    case RTMP_PT_WINDOW_ACK_SIZE: return "window acknowledgement size";
    case RTMP_PT_SET_PEER_BW:    return "set peer bandwidth";
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

static void amf_tag_contents(void *ctx, const uint8_t *data,
                             const uint8_t *data_end)
{
    unsigned int size, nb = -1;
    char buf[1024];
    AMFDataType type;
    int parse_key = 1;

    if (data >= data_end)
        return;
    switch ((type = *data++)) {
    case AMF_DATA_TYPE_NUMBER:
        av_log(ctx, AV_LOG_DEBUG, " number %g\n", av_int2double(AV_RB64(data)));
        return;
    case AMF_DATA_TYPE_BOOL:
        av_log(ctx, AV_LOG_DEBUG, " bool %d\n", *data);
        return;
    case AMF_DATA_TYPE_STRING:
    case AMF_DATA_TYPE_LONG_STRING:
        if (type == AMF_DATA_TYPE_STRING) {
            size = bytestream_get_be16(&data);
        } else {
            size = bytestream_get_be32(&data);
        }
        size = FFMIN(size, sizeof(buf) - 1);
        memcpy(buf, data, size);
        buf[size] = 0;
        av_log(ctx, AV_LOG_DEBUG, " string '%s'\n", buf);
        return;
    case AMF_DATA_TYPE_NULL:
        av_log(ctx, AV_LOG_DEBUG, " NULL\n");
        return;
    case AMF_DATA_TYPE_ARRAY:
        parse_key = 0;
    case AMF_DATA_TYPE_MIXEDARRAY:
        nb = bytestream_get_be32(&data);
    case AMF_DATA_TYPE_OBJECT:
        av_log(ctx, AV_LOG_DEBUG, " {\n");
        while (nb-- > 0 || type != AMF_DATA_TYPE_ARRAY) {
            int t;
            if (parse_key) {
                size = bytestream_get_be16(&data);
                size = FFMIN(size, sizeof(buf) - 1);
                if (!size) {
                    av_log(ctx, AV_LOG_DEBUG, " }\n");
                    data++;
                    break;
                }
                memcpy(buf, data, size);
                buf[size] = 0;
                if (size >= data_end - data)
                    return;
                data += size;
                av_log(ctx, AV_LOG_DEBUG, "  %s: ", buf);
            }
            amf_tag_contents(ctx, data, data_end);
            t = ff_amf_tag_size(data, data_end);
            if (t < 0 || t >= data_end - data)
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
           rtmp_packet_type(p->type), p->type, p->channel_id, p->timestamp, p->extra, p->size);
    if (p->type == RTMP_PT_INVOKE || p->type == RTMP_PT_NOTIFY) {
        uint8_t *src = p->data, *src_end = p->data + p->size;
        while (src < src_end) {
            int sz;
            amf_tag_contents(ctx, src, src_end);
            sz = ff_amf_tag_size(src, src_end);
            if (sz < 0)
                break;
            src += sz;
        }
    } else if (p->type == RTMP_PT_WINDOW_ACK_SIZE) {
        av_log(ctx, AV_LOG_DEBUG, "Window acknowledgement size = %d\n", AV_RB32(p->data));
    } else if (p->type == RTMP_PT_SET_PEER_BW) {
        av_log(ctx, AV_LOG_DEBUG, "Set Peer BW = %d\n", AV_RB32(p->data));
    } else if (p->type != RTMP_PT_AUDIO && p->type != RTMP_PT_VIDEO && p->type != RTMP_PT_METADATA) {
        int i;
        for (i = 0; i < p->size; i++)
            av_log(ctx, AV_LOG_DEBUG, " %02X", p->data[i]);
        av_log(ctx, AV_LOG_DEBUG, "\n");
    }
}
#endif

int ff_amf_match_string(const uint8_t *data, int size, const char *str)
{
    int len = strlen(str);
    int amf_len, type;

    if (size < 1)
        return 0;

    type = *data++;

    if (type != AMF_DATA_TYPE_LONG_STRING &&
        type != AMF_DATA_TYPE_STRING)
        return 0;

    if (type == AMF_DATA_TYPE_LONG_STRING) {
        if ((size -= 4 + 1) < 0)
            return 0;
        amf_len = bytestream_get_be32(&data);
    } else {
        if ((size -= 2 + 1) < 0)
            return 0;
        amf_len = bytestream_get_be16(&data);
    }

    if (amf_len > size)
        return 0;

    if (amf_len != len)
        return 0;

    return !memcmp(data, str, len);
}
