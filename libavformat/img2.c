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

#include <assert.h>

#include "libavutil/avstring.h"
#include "internal.h"
#include "img2.h"

#define IMG_TAGS(TAG)               \
    TAG(MJPEG,           jpeg     ) \
    TAG(MJPEG,           jpg      ) \
    TAG(MJPEG,           jps      ) \
    TAG(MJPEG,           mpo      ) \
    TAG(LJPEG,           ljpg     ) \
    TAG(JPEGLS,          jls      ) \
    TAG(PNG,             png      ) \
    TAG(PNG,             pns      ) \
    TAG(PNG,             mng      ) \
    TAG(PPM,             ppm      ) \
    TAG(PPM,             pnm      ) \
    TAG(PGM,             pgm      ) \
    TAG(PGMYUV,          pgmyuv   ) \
    TAG(PBM,             pbm      ) \
    TAG(PAM,             pam      ) \
    TAG(PFM,             pfm      ) \
    TAG(PHM,             phm      ) \
    TAG(CRI,             cri      ) \
    TAG(ALIAS_PIX,       pix      ) \
    TAG(DDS,             dds      ) \
    TAG(MPEG1VIDEO,      mpg1-img ) \
    TAG(MPEG2VIDEO,      mpg2-img ) \
    TAG(MPEG4,           mpg4-img ) \
    TAG(RAWVIDEO,        y        ) \
    TAG(RAWVIDEO,        raw      ) \
    TAG(BMP,             bmp      ) \
    TAG(TARGA,           tga      ) \
    TAG(TIFF,            tiff     ) \
    TAG(TIFF,            tif      ) \
    TAG(TIFF,            dng      ) \
    TAG(SGI,             sgi      ) \
    TAG(PTX,             ptx      ) \
    TAG(PHOTOCD,         pcd      ) \
    TAG(PCX,             pcx      ) \
    TAG(QDRAW,           pic      ) \
    TAG(QDRAW,           pct      ) \
    TAG(QDRAW,           pict     ) \
    TAG(SUNRAST,         sun      ) \
    TAG(SUNRAST,         ras      ) \
    TAG(SUNRAST,         rs       ) \
    TAG(SUNRAST,         im1      ) \
    TAG(SUNRAST,         im8      ) \
    TAG(SUNRAST,         im24     ) \
    TAG(SUNRAST,         im32     ) \
    TAG(SUNRAST,         sunras   ) \
    TAG(SVG,             svg      ) \
    TAG(SVG,             svgz     ) \
    TAG(JPEG2000,        j2c      ) \
    TAG(JPEG2000,        jp2      ) \
    TAG(JPEG2000,        jpc      ) \
    TAG(JPEG2000,        j2k      ) \
    TAG(DPX,             dpx      ) \
    TAG(EXR,             exr      ) \
    TAG(PICTOR,          pic      ) \
    TAG(V210X,           yuv10    ) \
    TAG(WEBP,            webp     ) \
    TAG(XBM,             xbm      ) \
    TAG(XPM,             xpm      ) \
    TAG(XFACE,           xface    ) \
    TAG(XWD,             xwd      ) \
    TAG(GEM,             img      ) \
    TAG(GEM,             ximg     ) \
    TAG(GEM,             timg     ) \
    TAG(VBN,             vbn      ) \
    TAG(JPEGXL,          jxl      ) \
    TAG(QOI,             qoi      ) \
    TAG(RADIANCE_HDR,    hdr      ) \
    TAG(WBMP,            wbmp     ) \
    TAG(NONE,                     )

#define LENGTH_CHECK(CODECID, STR) \
    static_assert(sizeof(#STR) <= sizeof(ff_img_tags->str), #STR " does not fit into IdStrMap.str\n");
IMG_TAGS(LENGTH_CHECK)

const IdStrMap ff_img_tags[] = {
#define TAG(CODECID, STR) { AV_CODEC_ID_ ## CODECID, #STR },
IMG_TAGS(TAG)
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
