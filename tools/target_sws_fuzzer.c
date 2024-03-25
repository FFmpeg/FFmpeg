/*
 * Copyright (c) 2024 Michael Niedermayer <michael-ffmpeg@niedermayer.cc>
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

#include "config.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/cpu.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "libavcodec/bytestream.h"

#include "libswscale/swscale.h"


int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void error(const char *err)
{
    fprintf(stderr, "%s", err);
    exit(1);
}

static int alloc_plane(uint8_t *data[AV_VIDEO_MAX_PLANES], int stride[AV_VIDEO_MAX_PLANES], int w, int h, int format, int *hshift, int *vshift)
{
    size_t size[AV_VIDEO_MAX_PLANES];
    ptrdiff_t ptrdiff_stride[AV_VIDEO_MAX_PLANES];
    int ret = av_image_fill_linesizes(stride, format, w);
    if (ret < 0)
        return -1;

    av_assert0(AV_VIDEO_MAX_PLANES == 4); // Some of the libavutil API has 4 hardcoded so this has undefined behaviour if its not 4

    av_pix_fmt_get_chroma_sub_sample(format, hshift, vshift);

    for(int p=0; p<AV_VIDEO_MAX_PLANES; p++) {
        stride[p] =
        ptrdiff_stride[p] = FFALIGN(stride[p], 32);
    }
    ret = av_image_fill_plane_sizes(size, format, h, ptrdiff_stride);
    if (ret < 0)
        return ret;

    for(int p=0; p<AV_VIDEO_MAX_PLANES; p++) {
        if (size[p]) {
            data[p] = av_mallocz(size[p] + 32);
            if (!data[p])
                return -1;
        } else
            data[p] = NULL;
    }
    return 0;
}

static void free_plane(uint8_t *data[AV_VIDEO_MAX_PLANES])
{
    for(int p=0; p<AV_VIDEO_MAX_PLANES; p++)
        av_freep(&data[p]);
}

static void mapres(unsigned *r0, unsigned *r1) {
    double d = (double)(*r0*10ll - 9ll*UINT32_MAX) / UINT32_MAX;
    double a = exp(d) * 16384 / exp(1) ;
    int ai = (int)round(a);
    uint64_t maxb = 16384 / ai;
    *r0 = ai;
    *r1 = 1 + (*r1 * maxb) / UINT32_MAX;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    int srcW= 48, srcH = 48;
    int dstW= 48, dstH = 48;
    int srcHShift, srcVShift;
    int dstHShift, dstVShift;
    unsigned flags = 1;
    int srcStride[AV_VIDEO_MAX_PLANES] = {0};
    int dstStride[AV_VIDEO_MAX_PLANES] = {0};
    int ret;
    const uint8_t *end = data + size;
    enum AVPixelFormat srcFormat = AV_PIX_FMT_YUV420P;
    enum AVPixelFormat dstFormat = AV_PIX_FMT_YUV420P;
    uint8_t *src[AV_VIDEO_MAX_PLANES] = { 0 };
    uint8_t *dst[AV_VIDEO_MAX_PLANES] = { 0 };
    struct SwsContext *sws = NULL;
    const AVPixFmtDescriptor *desc_src, *desc_dst;

    if (size > 128) {
        GetByteContext gbc;
        int64_t flags64;

        size -= 128;
        bytestream2_init(&gbc, data + size, 128);
        srcW = bytestream2_get_le32(&gbc);
        srcH = bytestream2_get_le32(&gbc);
        dstW = bytestream2_get_le32(&gbc);
        dstH = bytestream2_get_le32(&gbc);

        mapres(&srcW, &srcH);
        mapres(&dstW, &dstH);

        flags = bytestream2_get_le32(&gbc);

        unsigned mask = flags & (SWS_POINT         |
                                SWS_AREA          |
                                SWS_BILINEAR      |
                                SWS_FAST_BILINEAR |
                                SWS_BICUBIC       |
                                SWS_X             |
                                SWS_GAUSS         |
                                SWS_LANCZOS       |
                                SWS_SINC          |
                                SWS_SPLINE        |
                                SWS_BICUBLIN);
        mask &= flags;
        if (mask && (mask & (mask -1)))
            return 0; // multiple scalers are set, not possible

        srcFormat = bytestream2_get_le32(&gbc) % AV_PIX_FMT_NB;
        dstFormat = bytestream2_get_le32(&gbc) % AV_PIX_FMT_NB;

        flags64 = bytestream2_get_le64(&gbc);
        if (flags64 & 0x10)
            av_force_cpu_flags(0);

        if (av_image_check_size(srcW, srcH, srcFormat, NULL) < 0)
            srcW = srcH = 23;
        if (av_image_check_size(dstW, dstH, dstFormat, NULL) < 0)
            dstW = dstH = 23;
        //TODO alphablend
    }

    desc_src = av_pix_fmt_desc_get(srcFormat);
    desc_dst = av_pix_fmt_desc_get(dstFormat);

    fprintf(stderr, "%d x %d %s -> %d x %d %s\n", srcW, srcH, desc_src->name, dstW, dstH, desc_dst->name);

    ret = alloc_plane(src, srcStride, srcW, srcH, srcFormat, &srcHShift, &srcVShift);
    if (ret < 0)
        goto end;

    ret = alloc_plane(dst, dstStride, dstW, dstH, dstFormat, &dstHShift, &dstVShift);
    if (ret < 0)
        goto end;


    for(int p=0; p<AV_VIDEO_MAX_PLANES; p++) {
        int psize = srcStride[p] * AV_CEIL_RSHIFT(srcH, (p == 1 || p == 2) ? srcVShift : 0);
        if (psize > size)
            psize = size;
        if (psize) {
            memcpy(src[p], data, psize);
            data += psize;
            size -= psize;
        }
    }

    sws = sws_alloc_context();
    if (!sws)
        error("Failed sws allocation");

    av_opt_set_int(sws, "sws_flags",  flags, 0);
    av_opt_set_int(sws, "srcw",       srcW, 0);
    av_opt_set_int(sws, "srch",       srcH, 0);
    av_opt_set_int(sws, "dstw",       dstW, 0);
    av_opt_set_int(sws, "dsth",       dstH, 0);
    av_opt_set_int(sws, "src_format", srcFormat, 0);
    av_opt_set_int(sws, "dst_format", dstFormat, 0);
    av_opt_set(sws, "alphablend", "none", 0);

    ret = sws_init_context(sws, NULL, NULL);
    if (ret < 0)
        goto end;

    //TODO Slices
    sws_scale(sws, (const uint8_t * const*)src, srcStride, 0, srcH, dst, dstStride);

end:
    sws_freeContext(sws);

    free_plane(src);
    free_plane(dst);

    return 0;
}
