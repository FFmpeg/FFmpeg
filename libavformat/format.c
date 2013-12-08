/*
 * Format register and lookup
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#include "avformat.h"
#include "internal.h"
#include "libavutil/atomic.h"
#include "libavutil/avstring.h"

/**
 * @file
 * Format register and lookup
 */
/** head of registered input format linked list */
static AVInputFormat *first_iformat = NULL;
/** head of registered output format linked list */
static AVOutputFormat *first_oformat = NULL;

static AVInputFormat **last_iformat = &first_iformat;
static AVOutputFormat **last_oformat = &first_oformat;

AVInputFormat *av_iformat_next(AVInputFormat *f)
{
    if (f)
        return f->next;
    else
        return first_iformat;
}

AVOutputFormat *av_oformat_next(AVOutputFormat *f)
{
    if (f)
        return f->next;
    else
        return first_oformat;
}

void av_register_input_format(AVInputFormat *format)
{
    AVInputFormat **p = last_iformat;

    format->next = NULL;
    while(*p || avpriv_atomic_ptr_cas((void * volatile *)p, NULL, format))
        p = &(*p)->next;
    last_iformat = &format->next;
}

void av_register_output_format(AVOutputFormat *format)
{
    AVOutputFormat **p = last_oformat;

    format->next = NULL;
    while(*p || avpriv_atomic_ptr_cas((void * volatile *)p, NULL, format))
        p = &(*p)->next;
    last_oformat = &format->next;
}

int av_match_ext(const char *filename, const char *extensions)
{
    const char *ext, *p;
    char ext1[32], *q;

    if (!filename)
        return 0;

    ext = strrchr(filename, '.');
    if (ext) {
        ext++;
        p = extensions;
        for (;;) {
            q = ext1;
            while (*p != '\0' && *p != ','  && q - ext1 < sizeof(ext1) - 1)
                *q++ = *p++;
            *q = '\0';
            if (!av_strcasecmp(ext1, ext))
                return 1;
            if (*p == '\0')
                break;
            p++;
        }
    }
    return 0;
}

static int match_format(const char *name, const char *names)
{
    const char *p;
    int len, namelen;

    if (!name || !names)
        return 0;

    namelen = strlen(name);
    while ((p = strchr(names, ','))) {
        len = FFMAX(p - names, namelen);
        if (!av_strncasecmp(name, names, len))
            return 1;
        names = p + 1;
    }
    return !av_strcasecmp(name, names);
}

AVOutputFormat *av_guess_format(const char *short_name, const char *filename,
                                const char *mime_type)
{
    AVOutputFormat *fmt = NULL, *fmt_found;
    int score_max, score;

    /* specific test for image sequences */
#if CONFIG_IMAGE2_MUXER
    if (!short_name && filename &&
        av_filename_number_test(filename) &&
        ff_guess_image2_codec(filename) != AV_CODEC_ID_NONE) {
        return av_guess_format("image2", NULL, NULL);
    }
#endif
    /* Find the proper file type. */
    fmt_found = NULL;
    score_max = 0;
    while ((fmt = av_oformat_next(fmt))) {
        score = 0;
        if (fmt->name && short_name && match_format(short_name, fmt->name))
            score += 100;
        if (fmt->mime_type && mime_type && !strcmp(fmt->mime_type, mime_type))
            score += 10;
        if (filename && fmt->extensions &&
            av_match_ext(filename, fmt->extensions)) {
            score += 5;
        }
        if (score > score_max) {
            score_max = score;
            fmt_found = fmt;
        }
    }
    return fmt_found;
}

enum AVCodecID av_guess_codec(AVOutputFormat *fmt, const char *short_name,
                              const char *filename, const char *mime_type,
                              enum AVMediaType type)
{
    if (!strcmp(fmt->name, "segment") || !strcmp(fmt->name, "ssegment")) {
        fmt = av_guess_format(NULL, filename, NULL);
    }

    if (type == AVMEDIA_TYPE_VIDEO) {
        enum AVCodecID codec_id = AV_CODEC_ID_NONE;

#if CONFIG_IMAGE2_MUXER
        if (!strcmp(fmt->name, "image2") || !strcmp(fmt->name, "image2pipe")) {
            codec_id = ff_guess_image2_codec(filename);
        }
#endif
        if (codec_id == AV_CODEC_ID_NONE)
            codec_id = fmt->video_codec;
        return codec_id;
    } else if (type == AVMEDIA_TYPE_AUDIO)
        return fmt->audio_codec;
    else if (type == AVMEDIA_TYPE_SUBTITLE)
        return fmt->subtitle_codec;
    else
        return AV_CODEC_ID_NONE;
}

AVInputFormat *av_find_input_format(const char *short_name)
{
    AVInputFormat *fmt = NULL;
    while ((fmt = av_iformat_next(fmt)))
        if (match_format(short_name, fmt->name))
            return fmt;
    return NULL;
}
