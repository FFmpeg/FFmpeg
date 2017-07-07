/*
 * Image format
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 * Copyright (c) 2004 Michael Niedermayer
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

#include "libavutil/avstring.h"
#include "internal.h"
#include "img2.h"

const IdStrMap ff_img_tags[] = {
    { AV_CODEC_ID_MJPEG,      "jpeg"     },
    { AV_CODEC_ID_MJPEG,      "jpg"      },
    { AV_CODEC_ID_MJPEG,      "jps"      },
    { AV_CODEC_ID_MJPEG,      "mpo"      },
    { AV_CODEC_ID_LJPEG,      "ljpg"     },
    { AV_CODEC_ID_JPEGLS,     "jls"      },
    { AV_CODEC_ID_PNG,        "png"      },
    { AV_CODEC_ID_PNG,        "pns"      },
    { AV_CODEC_ID_PNG,        "mng"      },
    { AV_CODEC_ID_PPM,        "ppm"      },
    { AV_CODEC_ID_PPM,        "pnm"      },
    { AV_CODEC_ID_PGM,        "pgm"      },
    { AV_CODEC_ID_PGMYUV,     "pgmyuv"   },
    { AV_CODEC_ID_PBM,        "pbm"      },
    { AV_CODEC_ID_PAM,        "pam"      },
    { AV_CODEC_ID_ALIAS_PIX,  "pix"      },
    { AV_CODEC_ID_DDS,        "dds"      },
    { AV_CODEC_ID_MPEG1VIDEO, "mpg1-img" },
    { AV_CODEC_ID_MPEG2VIDEO, "mpg2-img" },
    { AV_CODEC_ID_MPEG4,      "mpg4-img" },
    { AV_CODEC_ID_RAWVIDEO,   "y"        },
    { AV_CODEC_ID_RAWVIDEO,   "raw"      },
    { AV_CODEC_ID_BMP,        "bmp"      },
    { AV_CODEC_ID_TARGA,      "tga"      },
    { AV_CODEC_ID_TIFF,       "tiff"     },
    { AV_CODEC_ID_TIFF,       "tif"      },
    { AV_CODEC_ID_SGI,        "sgi"      },
    { AV_CODEC_ID_PTX,        "ptx"      },
    { AV_CODEC_ID_PCX,        "pcx"      },
    { AV_CODEC_ID_QDRAW,      "pic"      },
    { AV_CODEC_ID_QDRAW,      "pct"      },
    { AV_CODEC_ID_QDRAW,      "pict"     },
    { AV_CODEC_ID_SUNRAST,    "sun"      },
    { AV_CODEC_ID_SUNRAST,    "ras"      },
    { AV_CODEC_ID_SUNRAST,    "rs"       },
    { AV_CODEC_ID_SUNRAST,    "im1"      },
    { AV_CODEC_ID_SUNRAST,    "im8"      },
    { AV_CODEC_ID_SUNRAST,    "im24"     },
    { AV_CODEC_ID_SUNRAST,    "im32"     },
    { AV_CODEC_ID_SUNRAST,    "sunras"   },
    { AV_CODEC_ID_SVG,        "svg"      },
    { AV_CODEC_ID_SVG,        "svgz"     },
    { AV_CODEC_ID_JPEG2000,   "j2c"      },
    { AV_CODEC_ID_JPEG2000,   "jp2"      },
    { AV_CODEC_ID_JPEG2000,   "jpc"      },
    { AV_CODEC_ID_JPEG2000,   "j2k"      },
    { AV_CODEC_ID_DPX,        "dpx"      },
    { AV_CODEC_ID_EXR,        "exr"      },
    { AV_CODEC_ID_PICTOR,     "pic"      },
    { AV_CODEC_ID_V210X,      "yuv10"    },
    { AV_CODEC_ID_WEBP,       "webp"     },
    { AV_CODEC_ID_XBM,        "xbm"      },
    { AV_CODEC_ID_XPM,        "xpm"      },
    { AV_CODEC_ID_XFACE,      "xface"    },
    { AV_CODEC_ID_XWD,        "xwd"      },
    { AV_CODEC_ID_NONE,       NULL       }
};

static enum AVCodecID str2id(const IdStrMap *tags, const char *str)
{
    str = strrchr(str, '.');
    if (!str)
        return AV_CODEC_ID_NONE;
    str++;

    while (tags->id) {
        if (!av_strcasecmp(str, tags->str))
            return tags->id;

        tags++;
    }
    return AV_CODEC_ID_NONE;
}

enum AVCodecID ff_guess_image2_codec(const char *filename)
{
    return str2id(ff_img_tags, filename);
}
