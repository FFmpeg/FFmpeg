/*
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

#include <stdio.h>
#include <string.h>

#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libswscale/swscale.h"

static AVFrame *alloc_frame(enum AVPixelFormat format, int height)
{
    AVFrame *frame = av_frame_alloc();

    if (!frame)
        return NULL;
    frame->format = format;
    frame->width  = 64;
    frame->height = height;
    if (av_frame_get_buffer(frame, 32) < 0)
        av_frame_free(&frame);
    return frame;
}

static int convert(AVFrame *dst, const AVFrame *src, int threads, int legacy)
{
    struct SwsContext *sws = sws_alloc_context();
    int ret;

    if (!sws)
        return AVERROR(ENOMEM);
    av_opt_set_int(sws, "threads", threads, 0);
    av_opt_set_int(sws, "sws_flags", SWS_BILINEAR, 0);
    av_opt_set(sws, "sws_dither", "none", 0);
    if (legacy) {
        av_opt_set_int(sws, "srcw", src->width, 0);
        av_opt_set_int(sws, "srch", src->height, 0);
        av_opt_set_int(sws, "src_format", src->format, 0);
        av_opt_set_int(sws, "dstw", dst->width, 0);
        av_opt_set_int(sws, "dsth", dst->height, 0);
        av_opt_set_int(sws, "dst_format", dst->format, 0);
        ret = sws_init_context(sws, NULL, NULL);
        if (ret < 0)
            goto end;
    }
    ret = sws_scale_frame(sws, dst, src);
end:
    sws_freeContext(sws);
    return ret;
}

static int compare(enum AVPixelFormat src_fmt, enum AVPixelFormat dst_fmt,
                   int height, int legacy, unsigned *mismatches)
{
    static const int threads[] = { 2, 3, 7, 16 };
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(src_fmt);
    AVFrame *src = alloc_frame(src_fmt, height);
    AVFrame *ref = alloc_frame(dst_fmt, height);
    AVFrame *dst = alloc_frame(dst_fmt, height);
    int ret = AVERROR(ENOMEM);

    if (!src || !ref || !dst)
        goto end;
    for (int p = 0; p < 4 && src->data[p]; p++) {
        const int h = AV_CEIL_RSHIFT(height, p == 1 || p == 2 ? desc->log2_chroma_h : 0);
        const int w = av_image_get_linesize(src_fmt, src->width, p);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                src->data[p][y * src->linesize[p] + x] =
                    (x * 37 + y * 73 + x * y * 11 + p * 59) & 255;
    }
    ret = convert(ref, src, 1, legacy);
    if (ret < 0)
        goto end;
    for (int t = 0; t < FF_ARRAY_ELEMS(threads); t++) {
        ret = convert(dst, src, threads[t], legacy);
        if (ret < 0)
            goto end;
        for (int p = 0; p < 4 && dst->data[p]; p++) {
            const int w = av_image_get_linesize(dst_fmt, dst->width, p);
            for (int y = 0; y < height; y++)
                *mismatches += !!memcmp(ref->data[p] + y * ref->linesize[p],
                                        dst->data[p] + y * dst->linesize[p], w);
        }
    }
end:
    av_frame_free(&src);
    av_frame_free(&ref);
    av_frame_free(&dst);
    return ret;
}

int main(void)
{
    static const enum AVPixelFormat inputs[] = {
        AV_PIX_FMT_NV12, AV_PIX_FMT_NV21, AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV422P,
    };
    static const enum AVPixelFormat outputs[] = {
        AV_PIX_FMT_ARGB, AV_PIX_FMT_RGBA, AV_PIX_FMT_ABGR, AV_PIX_FMT_BGRA,
        AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24, AV_PIX_FMT_GBRP,
        AV_PIX_FMT_RGB565LE, AV_PIX_FMT_BGR565LE,
        AV_PIX_FMT_RGB555LE, AV_PIX_FMT_BGR555LE,
        AV_PIX_FMT_RGB565BE, AV_PIX_FMT_BGR565BE,
        AV_PIX_FMT_RGB555BE, AV_PIX_FMT_BGR555BE,
    };
    static const int heights[] = { 2, 6, 14, 48, 50 };

    av_log_set_level(AV_LOG_ERROR);

    /* Compare against the same implementation with one thread, since different
     * architectures need not produce identical rounding. Include both the
     * dynamic graph and the initialized legacy context APIs. */
    for (int s = 0; s < FF_ARRAY_ELEMS(inputs); s++) {
        for (int d = 0; d < FF_ARRAY_ELEMS(outputs); d++) {
            unsigned mismatches = 0;
            for (int legacy = 0; legacy <= 1; legacy++) {
                for (int h = 0; h < FF_ARRAY_ELEMS(heights); h++) {
                    int ret = compare(inputs[s], outputs[d], heights[h], legacy, &mismatches);
                    if (ret < 0)
                        return 1;
                }
            }
            printf("%s -> %s: %u mismatching rows\n",
                   av_get_pix_fmt_name(inputs[s]), av_get_pix_fmt_name(outputs[d]),
                   mismatches);
        }
    }
    return 0;
}
