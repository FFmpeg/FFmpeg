/*
 * NAL helper functions for muxers
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

#include <stdint.h>
#include <string.h>

#include "libavutil/mem.h"
#include "libavutil/error.h"
#include "libavcodec/defs.h"
#include "avio.h"
#include "avio_internal.h"
#include "nal.h"

static const uint8_t *nal_find_startcode_internal(const uint8_t *p, const uint8_t *end)
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

const uint8_t *ff_nal_find_startcode(const uint8_t *p, const uint8_t *end){
    const uint8_t *out = nal_find_startcode_internal(p, end);
    if(p<out && out<end && !out[-1]) out--;
    return out;
}

static int nal_parse_units(AVIOContext *pb, NALUList *list,
                           const uint8_t *buf_in, int size)
{
    const uint8_t *p = buf_in;
    const uint8_t *end = p + size;
    const uint8_t *nal_start, *nal_end;

    size = 0;
    nal_start = ff_nal_find_startcode(p, end);
    for (;;) {
        const size_t nalu_limit = SIZE_MAX / sizeof(*list->nalus);
        while (nal_start < end && !*(nal_start++));
        if (nal_start == end)
            break;

        nal_end = ff_nal_find_startcode(nal_start, end);
        if (pb) {
            avio_wb32(pb, nal_end - nal_start);
            avio_write(pb, nal_start, nal_end - nal_start);
        } else if (list->nb_nalus >= nalu_limit) {
            return AVERROR(ERANGE);
        } else {
            NALU *tmp = av_fast_realloc(list->nalus, &list->nalus_array_size,
                                        (list->nb_nalus + 1) * sizeof(*list->nalus));
            if (!tmp)
                return AVERROR(ENOMEM);
            list->nalus = tmp;
            tmp[list->nb_nalus++] = (NALU){ .offset = nal_start - p,
                                            .size   = nal_end - nal_start };
        }
        size += 4 + nal_end - nal_start;
        nal_start = nal_end;
    }
    return size;
}

int ff_nal_parse_units(AVIOContext *pb, const uint8_t *buf_in, int size)
{
    return nal_parse_units(pb, NULL, buf_in, size);
}

int ff_nal_units_create_list(NALUList *list, const uint8_t *buf, int size)
{
    list->nb_nalus = 0;
    return nal_parse_units(NULL, list, buf, size);
}

void ff_nal_units_write_list(const NALUList *list, AVIOContext *pb,
                             const uint8_t *buf)
{
    for (unsigned i = 0; i < list->nb_nalus; i++) {
        avio_wb32(pb, list->nalus[i].size);
        avio_write(pb, buf + list->nalus[i].offset, list->nalus[i].size);
    }
}

int ff_nal_parse_units_buf(const uint8_t *buf_in, uint8_t **buf, int *size)
{
    AVIOContext *pb;
    int ret = avio_open_dyn_buf(&pb);
    if(ret < 0)
        return ret;

    ff_nal_parse_units(pb, buf_in, *size);

    *size = avio_close_dyn_buf(pb, buf);
    return 0;
}

const uint8_t *ff_nal_mp4_find_startcode(const uint8_t *start,
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

uint8_t *ff_nal_unit_extract_rbsp(const uint8_t *src, uint32_t src_len,
                                  uint32_t *dst_len, int header_len)
{
    uint8_t *dst;
    uint32_t i, len;

    dst = av_malloc(src_len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!dst)
        return NULL;

    /* NAL unit header */
    i = len = 0;
    while (i < header_len && i < src_len)
        dst[len++] = src[i++];

    while (i + 2 < src_len)
        if (!src[i] && !src[i + 1] && src[i + 2] == 3) {
            dst[len++] = src[i++];
            dst[len++] = src[i++];
            i++; // remove emulation_prevention_three_byte
        } else
            dst[len++] = src[i++];

    while (i < src_len)
        dst[len++] = src[i++];

    memset(dst + len, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    *dst_len = len;
    return dst;
}
