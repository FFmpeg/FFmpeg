/*
 * ISO Media common code
 * copyright (c) 2001 Fabrice Bellard.
 * copyright (c) 2002 Francois Revol <revol@free.fr>
 * copyright (c) 2006 Baptiste Coudurier <baptiste.coudurier@free.fr>
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

#ifndef FFMPEG_ISOM_H
#define FFMPEG_ISOM_H

#include "riff.h"

/* isom.c */
extern const AVCodecTag ff_mp4_obj_type[];
extern const AVCodecTag codec_movvideo_tags[];
extern const AVCodecTag codec_movaudio_tags[];
extern const AVCodecTag ff_codec_movsubtitle_tags[];

int ff_mov_iso639_to_lang(const char *lang, int mp4);
int ff_mov_lang_to_iso639(int code, char *to);

typedef struct {
    int count;
    int duration;
} MOV_stts_t;

#endif /* FFMPEG_ISOM_H */
