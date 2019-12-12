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

#include "v4l2-common.h"

const struct fmt_map ff_fmt_conversion_table[] = {
    //ff_fmt              codec_id              v4l2_fmt
    { AV_PIX_FMT_YUV420P, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_YUV420  },
    { AV_PIX_FMT_YUV420P, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_YVU420  },
    { AV_PIX_FMT_YUV422P, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_YUV422P },
    { AV_PIX_FMT_YUYV422, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_YUYV    },
    { AV_PIX_FMT_UYVY422, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_UYVY    },
    { AV_PIX_FMT_YUV411P, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_YUV411P },
    { AV_PIX_FMT_YUV410P, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_YUV410  },
    { AV_PIX_FMT_YUV410P, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_YVU410  },
    { AV_PIX_FMT_RGB555LE,AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_RGB555  },
    { AV_PIX_FMT_RGB555BE,AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_RGB555X },
    { AV_PIX_FMT_RGB565LE,AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_RGB565  },
    { AV_PIX_FMT_RGB565BE,AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_RGB565X },
    { AV_PIX_FMT_BGR24,   AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_BGR24   },
    { AV_PIX_FMT_RGB24,   AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_RGB24   },
#ifdef V4L2_PIX_FMT_XBGR32
    { AV_PIX_FMT_BGR0,    AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_XBGR32  },
    { AV_PIX_FMT_0RGB,    AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_XRGB32  },
    { AV_PIX_FMT_BGRA,    AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_ABGR32  },
    { AV_PIX_FMT_ARGB,    AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_ARGB32  },
#endif
    { AV_PIX_FMT_BGR0,    AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_BGR32   },
    { AV_PIX_FMT_0RGB,    AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_RGB32   },
    { AV_PIX_FMT_GRAY8,   AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_GREY    },
#ifdef V4L2_PIX_FMT_Y16
    { AV_PIX_FMT_GRAY16LE,AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_Y16     },
#endif
#ifdef V4L2_PIX_FMT_Z16
    { AV_PIX_FMT_GRAY16LE,AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_Z16     },
#endif
    { AV_PIX_FMT_NV12,    AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_NV12    },
    { AV_PIX_FMT_NONE,    AV_CODEC_ID_MJPEG,    V4L2_PIX_FMT_MJPEG   },
    { AV_PIX_FMT_NONE,    AV_CODEC_ID_MJPEG,    V4L2_PIX_FMT_JPEG    },
#ifdef V4L2_PIX_FMT_H264
    { AV_PIX_FMT_NONE,    AV_CODEC_ID_H264,     V4L2_PIX_FMT_H264    },
#endif
#ifdef V4L2_PIX_FMT_MPEG4
    { AV_PIX_FMT_NONE,    AV_CODEC_ID_MPEG4,    V4L2_PIX_FMT_MPEG4   },
#endif
#ifdef V4L2_PIX_FMT_CPIA1
    { AV_PIX_FMT_NONE,    AV_CODEC_ID_CPIA,     V4L2_PIX_FMT_CPIA1   },
#endif
#ifdef V4L2_PIX_FMT_SRGGB8
    { AV_PIX_FMT_BAYER_BGGR8, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_SBGGR8 },
    { AV_PIX_FMT_BAYER_GBRG8, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_SGBRG8 },
    { AV_PIX_FMT_BAYER_GRBG8, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_SGRBG8 },
    { AV_PIX_FMT_BAYER_RGGB8, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_SRGGB8 },
#endif
    { AV_PIX_FMT_NONE,    AV_CODEC_ID_NONE,     0                    },
};

uint32_t ff_fmt_ff2v4l(enum AVPixelFormat pix_fmt, enum AVCodecID codec_id)
{
    int i;

    for (i = 0; ff_fmt_conversion_table[i].codec_id != AV_CODEC_ID_NONE; i++) {
        if ((codec_id == AV_CODEC_ID_NONE ||
             ff_fmt_conversion_table[i].codec_id == codec_id) &&
            (pix_fmt == AV_PIX_FMT_NONE ||
             ff_fmt_conversion_table[i].ff_fmt == pix_fmt)) {
            return ff_fmt_conversion_table[i].v4l2_fmt;
        }
    }

    return 0;
}

enum AVPixelFormat ff_fmt_v4l2ff(uint32_t v4l2_fmt, enum AVCodecID codec_id)
{
    int i;

    for (i = 0; ff_fmt_conversion_table[i].codec_id != AV_CODEC_ID_NONE; i++) {
        if (ff_fmt_conversion_table[i].v4l2_fmt == v4l2_fmt &&
            ff_fmt_conversion_table[i].codec_id == codec_id) {
            return ff_fmt_conversion_table[i].ff_fmt;
        }
    }

    return AV_PIX_FMT_NONE;
}

enum AVCodecID ff_fmt_v4l2codec(uint32_t v4l2_fmt)
{
    int i;

    for (i = 0; ff_fmt_conversion_table[i].codec_id != AV_CODEC_ID_NONE; i++) {
        if (ff_fmt_conversion_table[i].v4l2_fmt == v4l2_fmt) {
            return ff_fmt_conversion_table[i].codec_id;
        }
    }

    return AV_CODEC_ID_NONE;
}
