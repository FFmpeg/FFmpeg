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

#include "libavutil/pixdesc.c"

static const enum AVPixelFormat pixfmt_list[] = {
    AV_PIX_FMT_MONOWHITE,
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GRAY10,
    AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV420P16,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV422P16,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_RGB565,
    AV_PIX_FMT_RGB24,
    AV_PIX_FMT_RGB48,
    AV_PIX_FMT_VDPAU,
    AV_PIX_FMT_VAAPI,
};

static enum AVPixelFormat find_best(enum AVPixelFormat pixfmt)
{
    enum AVPixelFormat best = AV_PIX_FMT_NONE;
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(pixfmt_list); i++)
        best = av_find_best_pix_fmt_of_2(best, pixfmt_list[i],
                                         pixfmt, 0, NULL);
    return best;
}

int main(void)
{
    enum AVPixelFormat output;
    int i, pass = 0, fail = 0;

#define TEST(input, expected) do {                              \
        output = find_best(input);                              \
        if (output != expected) {                               \
            printf("Matching %s: got %s, expected %s\n",        \
                   av_get_pix_fmt_name(input),                  \
                   av_get_pix_fmt_name(output),                 \
                   av_get_pix_fmt_name(expected));              \
            ++fail;                                             \
        } else                                                  \
            ++pass;                                             \
    } while (0)

    // Same formats.
    for (i = 0; i < FF_ARRAY_ELEMS(pixfmt_list); i++)
        TEST(pixfmt_list[i], pixfmt_list[i]);

    // Formats containing the same data in different layouts.
    TEST(AV_PIX_FMT_MONOBLACK, AV_PIX_FMT_MONOWHITE);
    TEST(AV_PIX_FMT_NV12,      AV_PIX_FMT_YUV420P);
    TEST(AV_PIX_FMT_P010,      AV_PIX_FMT_YUV420P10);
    TEST(AV_PIX_FMT_P016,      AV_PIX_FMT_YUV420P16);
    TEST(AV_PIX_FMT_NV16,      AV_PIX_FMT_YUV422P);
    TEST(AV_PIX_FMT_NV24,      AV_PIX_FMT_YUV444P);
    TEST(AV_PIX_FMT_YUYV422,   AV_PIX_FMT_YUV422P);
    TEST(AV_PIX_FMT_UYVY422,   AV_PIX_FMT_YUV422P);
    TEST(AV_PIX_FMT_BGR565,    AV_PIX_FMT_RGB565);
    TEST(AV_PIX_FMT_BGR24,     AV_PIX_FMT_RGB24);
    TEST(AV_PIX_FMT_GBRP,      AV_PIX_FMT_RGB24);
    TEST(AV_PIX_FMT_0RGB,      AV_PIX_FMT_RGB24);
    TEST(AV_PIX_FMT_GBRP16,    AV_PIX_FMT_RGB48);

    // Formats additionally containing alpha (here ignored).
    TEST(AV_PIX_FMT_YA8,       AV_PIX_FMT_GRAY8);
    TEST(AV_PIX_FMT_YA16,      AV_PIX_FMT_GRAY16);
    TEST(AV_PIX_FMT_YUVA420P,  AV_PIX_FMT_YUV420P);
    TEST(AV_PIX_FMT_YUVA422P,  AV_PIX_FMT_YUV422P);
    TEST(AV_PIX_FMT_YUVA444P,  AV_PIX_FMT_YUV444P);
    TEST(AV_PIX_FMT_AYUV64,    AV_PIX_FMT_YUV444P16);
    TEST(AV_PIX_FMT_RGBA,      AV_PIX_FMT_RGB24);
    TEST(AV_PIX_FMT_ABGR,      AV_PIX_FMT_RGB24);
    TEST(AV_PIX_FMT_GBRAP,     AV_PIX_FMT_RGB24);
    TEST(AV_PIX_FMT_RGBA64,    AV_PIX_FMT_RGB48);
    TEST(AV_PIX_FMT_BGRA64,    AV_PIX_FMT_RGB48);
    TEST(AV_PIX_FMT_GBRAP16,   AV_PIX_FMT_RGB48);

    // Formats requiring upsampling to represent exactly.
    TEST(AV_PIX_FMT_GRAY12,    AV_PIX_FMT_GRAY16);
    TEST(AV_PIX_FMT_YUV410P,   AV_PIX_FMT_YUV420P);
    TEST(AV_PIX_FMT_YUV411P,   AV_PIX_FMT_YUV422P);
    TEST(AV_PIX_FMT_UYYVYY411, AV_PIX_FMT_YUV422P);
    TEST(AV_PIX_FMT_YUV440P,   AV_PIX_FMT_YUV444P);
    TEST(AV_PIX_FMT_YUV440P10, AV_PIX_FMT_YUV444P10);
    TEST(AV_PIX_FMT_YUV440P12, AV_PIX_FMT_YUV444P16);
    TEST(AV_PIX_FMT_YUV420P9,  AV_PIX_FMT_YUV420P10);
    TEST(AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV420P16);
    TEST(AV_PIX_FMT_YUV444P9,  AV_PIX_FMT_YUV444P10);
    TEST(AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV444P16);
    TEST(AV_PIX_FMT_BGR4,      AV_PIX_FMT_RGB565);
    TEST(AV_PIX_FMT_RGB444,    AV_PIX_FMT_RGB565);
    TEST(AV_PIX_FMT_RGB555,    AV_PIX_FMT_RGB565);
    TEST(AV_PIX_FMT_GBRP10,    AV_PIX_FMT_RGB48);
    TEST(AV_PIX_FMT_GBRAP10,   AV_PIX_FMT_RGB48);
    TEST(AV_PIX_FMT_GBRAP12,   AV_PIX_FMT_RGB48);

    // Formats containing the same data in different endianness.
    TEST(AV_PIX_FMT_GRAY10BE,    AV_PIX_FMT_GRAY10);
    TEST(AV_PIX_FMT_GRAY10LE,    AV_PIX_FMT_GRAY10);
    TEST(AV_PIX_FMT_GRAY16BE,    AV_PIX_FMT_GRAY16);
    TEST(AV_PIX_FMT_GRAY16LE,    AV_PIX_FMT_GRAY16);
    TEST(AV_PIX_FMT_YUV422P10BE, AV_PIX_FMT_YUV422P10);
    TEST(AV_PIX_FMT_YUV422P10LE, AV_PIX_FMT_YUV422P10);
    TEST(AV_PIX_FMT_YUV444P16BE, AV_PIX_FMT_YUV444P16);
    TEST(AV_PIX_FMT_YUV444P16LE, AV_PIX_FMT_YUV444P16);
    TEST(AV_PIX_FMT_RGB565BE,    AV_PIX_FMT_RGB565);
    TEST(AV_PIX_FMT_RGB565LE,    AV_PIX_FMT_RGB565);
    TEST(AV_PIX_FMT_RGB48BE,     AV_PIX_FMT_RGB48);
    TEST(AV_PIX_FMT_RGB48LE,     AV_PIX_FMT_RGB48);

    // Opaque formats are least unlike each other.
    TEST(AV_PIX_FMT_DXVA2_VLD, AV_PIX_FMT_VDPAU);

    printf("%d tests passed, %d tests failed.\n", pass, fail);
    return !!fail;
}
