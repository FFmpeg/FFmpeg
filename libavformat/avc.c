/*
 * AVC helper functions for muxers
 * Copyright (c) 2006 Baptiste Coudurier <baptiste.coudurier@smartjog.com>
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
#include "libavcodec/h264.h"
#include "avformat.h"
#include "avio.h"
#include "avc.h"

static const uint8_t *ff_avc_find_startcode_internal(const uint8_t *p, const uint8_t *end)
{
    const uint8_t *a = p + 4 - ((intptr_t)p & 3);

    for (end -= 3; p < a && p < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    for (end -= 3; p < end; p += 4) {
        uint32_t x = *(const uint32_t*)p;
//      if ((x - 0x01000100) & (~x) & 0x80008000) // little endian
//      if ((x - 0x00010001) & (~x) & 0x00800080) // big endian
        if ((x - 0x01010101) & (~x) & 0x80808080) { // generic
            if (p[1] == 0) {
                if (p[0] == 0 && p[2] == 1)
                    return p;
                if (p[2] == 0 && p[3] == 1)
                    return p+1;
            }
            if (p[3] == 0) {
                if (p[2] == 0 && p[4] == 1)
                    return p+2;
                if (p[4] == 0 && p[5] == 1)
                    return p+3;
            }
        }
    }

    for (end += 3; p < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    return end + 3;
}

const uint8_t *ff_avc_find_startcode(const uint8_t *p, const uint8_t *end){
    const uint8_t *out= ff_avc_find_startcode_internal(p, end);
    if(p<out && out<end && !out[-1]) out--;
    return out;
}

int ff_avc_parse_nal_units(AVIOContext *pb, const uint8_t *buf_in, int size)
{
    const uint8_t *p = buf_in;
    const uint8_t *end = p + size;
    const uint8_t *nal_start, *nal_end;

    size = 0;
    nal_start = ff_avc_find_startcode(p, end);
    for (;;) {
        while (nal_start < end && !*(nal_start++));
        if (nal_start == end)
            break;

        nal_end = ff_avc_find_startcode(nal_start, end);
        avio_wb32(pb, nal_end - nal_start);
        avio_write(pb, nal_start, nal_end - nal_start);
        size += 4 + nal_end - nal_start;
        nal_start = nal_end;
    }
    return size;
}

int ff_avc_parse_nal_units_buf(const uint8_t *buf_in, uint8_t **buf, int *size)
{
    AVIOContext *pb;
    int ret = avio_open_dyn_buf(&pb);
    if(ret < 0)
        return ret;

    ff_avc_parse_nal_units(pb, buf_in, *size);

    av_freep(buf);
    *size = avio_close_dyn_buf(pb, buf);
    return 0;
}

int ff_isom_write_avcc(AVIOContext *pb, const uint8_t *data, int len)
{
    AVIOContext *sps_pb = NULL, *pps_pb = NULL;
    uint8_t *buf = NULL, *end, *start = NULL;
    uint8_t *sps = NULL, *pps = NULL;
    uint32_t sps_size = 0, pps_size = 0;
    int ret, nb_sps = 0, nb_pps = 0;

    if (len <= 6)
        return AVERROR_INVALIDDATA;

    /* check for H.264 start code */
    if (AV_RB32(data) != 0x00000001 &&
        AV_RB24(data) != 0x000001) {
        avio_write(pb, data, len);
        return 0;
    }

    ret = ff_avc_parse_nal_units_buf(data, &buf, &len);
    if (ret < 0)
        return ret;
    start = buf;
    end = buf + len;

    ret = avio_open_dyn_buf(&sps_pb);
    if (ret < 0)
        goto fail;
    ret = avio_open_dyn_buf(&pps_pb);
    if (ret < 0)
        goto fail;

    /* look for sps and pps */
    while (end - buf > 4) {
        uint32_t size;
        uint8_t nal_type;
        size = FFMIN(AV_RB32(buf), end - buf - 4);
        buf += 4;
        nal_type = buf[0] & 0x1f;

        if (nal_type == 7) { /* SPS */
            nb_sps++;
            if (size > UINT16_MAX || nb_sps >= H264_MAX_SPS_COUNT) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            avio_wb16(sps_pb, size);
            avio_write(sps_pb, buf, size);
        } else if (nal_type == 8) { /* PPS */
            nb_pps++;
            if (size > UINT16_MAX || nb_pps >= H264_MAX_PPS_COUNT) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            avio_wb16(pps_pb, size);
            avio_write(pps_pb, buf, size);
        }

        buf += size;
    }
    sps_size = avio_close_dyn_buf(sps_pb, &sps);
    pps_size = avio_close_dyn_buf(pps_pb, &pps);

    if (sps_size < 6 || !pps_size) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    avio_w8(pb, 1); /* version */
    avio_w8(pb, sps[3]); /* profile */
    avio_w8(pb, sps[4]); /* profile compat */
    avio_w8(pb, sps[5]); /* level */
    avio_w8(pb, 0xff); /* 6 bits reserved (111111) + 2 bits nal size length - 1 (11) */
    avio_w8(pb, 0xe0 | nb_sps); /* 3 bits reserved (111) + 5 bits number of sps */

    avio_write(pb, sps, sps_size);
    avio_w8(pb, nb_pps); /* number of pps */
    avio_write(pb, pps, pps_size);

fail:
    if (!sps)
        avio_close_dyn_buf(sps_pb, &sps);
    if (!pps)
        avio_close_dyn_buf(pps_pb, &pps);
    av_free(sps);
    av_free(pps);
    av_free(start);

    return ret;
}

int ff_avc_write_annexb_extradata(const uint8_t *in, uint8_t **buf, int *size)
{
    uint16_t sps_size, pps_size;
    uint8_t *out;
    int out_size;

    *buf = NULL;
    if (*size >= 4 && (AV_RB32(in) == 0x00000001 || AV_RB24(in) == 0x000001))
        return 0;
    if (*size < 11 || in[0] != 1)
        return AVERROR_INVALIDDATA;

    sps_size = AV_RB16(&in[6]);
    if (11 + sps_size > *size)
        return AVERROR_INVALIDDATA;
    pps_size = AV_RB16(&in[9 + sps_size]);
    if (11 + sps_size + pps_size > *size)
        return AVERROR_INVALIDDATA;
    out_size = 8 + sps_size + pps_size;
    out = av_mallocz(out_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!out)
        return AVERROR(ENOMEM);
    AV_WB32(&out[0], 0x00000001);
    memcpy(out + 4, &in[8], sps_size);
    AV_WB32(&out[4 + sps_size], 0x00000001);
    memcpy(out + 8 + sps_size, &in[11 + sps_size], pps_size);
    *buf = out;
    *size = out_size;
    return 0;
}

const uint8_t *ff_avc_mp4_find_startcode(const uint8_t *start,
                                         const uint8_t *end,
                                         int nal_length_size)
{
    unsigned int res = 0;

    if (end - start < nal_length_size)
        return NULL;
    while (nal_length_size--)
        res = (res << 8) | *start++;

    if (res > end - start)
        return NULL;

    return start + res;
}
