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

typedef struct {
    enum CodecID id;
    const char *str;
} IdStrMap;

static const IdStrMap img_tags[] = {
    { CODEC_ID_MJPEG     , "jpeg"},
    { CODEC_ID_MJPEG     , "jpg"},
    { CODEC_ID_MJPEG     , "jps"},
    { CODEC_ID_LJPEG     , "ljpg"},
    { CODEC_ID_JPEGLS    , "jls"},
    { CODEC_ID_PNG       , "png"},
    { CODEC_ID_PNG       , "pns"},
    { CODEC_ID_PNG       , "mng"},
    { CODEC_ID_PPM       , "ppm"},
    { CODEC_ID_PPM       , "pnm"},
    { CODEC_ID_PGM       , "pgm"},
    { CODEC_ID_PGMYUV    , "pgmyuv"},
    { CODEC_ID_PBM       , "pbm"},
    { CODEC_ID_PAM       , "pam"},
    { CODEC_ID_MPEG1VIDEO, "mpg1-img"},
    { CODEC_ID_MPEG2VIDEO, "mpg2-img"},
    { CODEC_ID_MPEG4     , "mpg4-img"},
    { CODEC_ID_FFV1      , "ffv1-img"},
    { CODEC_ID_RAWVIDEO  , "y"},
    { CODEC_ID_RAWVIDEO  , "raw"},
    { CODEC_ID_BMP       , "bmp"},
    { CODEC_ID_GIF       , "gif"},
    { CODEC_ID_TARGA     , "tga"},
    { CODEC_ID_TIFF      , "tiff"},
    { CODEC_ID_TIFF      , "tif"},
    { CODEC_ID_SGI       , "sgi"},
    { CODEC_ID_PTX       , "ptx"},
    { CODEC_ID_PCX       , "pcx"},
    { CODEC_ID_SUNRAST   , "sun"},
    { CODEC_ID_SUNRAST   , "ras"},
    { CODEC_ID_SUNRAST   , "rs"},
    { CODEC_ID_SUNRAST   , "im1"},
    { CODEC_ID_SUNRAST   , "im8"},
    { CODEC_ID_SUNRAST   , "im24"},
    { CODEC_ID_SUNRAST   , "im32"},
    { CODEC_ID_SUNRAST   , "sunras"},
    { CODEC_ID_JPEG2000  , "j2c"},
    { CODEC_ID_JPEG2000  , "j2k"},
    { CODEC_ID_JPEG2000  , "jp2"},
    { CODEC_ID_JPEG2000  , "jpc"},
    { CODEC_ID_DPX       , "dpx"},
    { CODEC_ID_EXR       , "exr"},
    { CODEC_ID_PICTOR    , "pic"},
    { CODEC_ID_XBM       , "xbm"},
    { CODEC_ID_XWD       , "xwd"},
    { CODEC_ID_NONE      , NULL}
};

static enum CodecID av_str2id(const IdStrMap *tags, const char *str)
{
    str= strrchr(str, '.');
    if(!str) return CODEC_ID_NONE;
    str++;

    while (tags->id) {
        if (!av_strcasecmp(str, tags->str))
            return tags->id;

        tags++;
    }
    return CODEC_ID_NONE;
}

enum CodecID ff_guess_image2_codec(const char *filename)
{
    return av_str2id(img_tags, filename);
}
