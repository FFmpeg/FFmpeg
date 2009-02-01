/*
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
#include "libavcodec/opt.h"

/**
 * @file libavformat/options.c
 * Options definition for AVFormatContext.
 */

static const char* format_to_name(void* ptr)
{
    AVFormatContext* fc = (AVFormatContext*) ptr;
    if(fc->iformat) return fc->iformat->name;
    else if(fc->oformat) return fc->oformat->name;
    else return "NULL";
}

#define OFFSET(x) offsetof(AVFormatContext,x)
#define DEFAULT 0 //should be NAN but it does not work as it is not a constant in glibc as required by ANSI/ISO C
//these names are too long to be readable
#define E AV_OPT_FLAG_ENCODING_PARAM
#define D AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[]={
{"probesize", NULL, OFFSET(probesize), FF_OPT_TYPE_INT, 32000, 32, INT_MAX, D}, /* 32000 from mpegts.c: 1.0 second at 24Mbit/s */
{"muxrate", "set mux rate", OFFSET(mux_rate), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, E},
{"packetsize", "set packet size", OFFSET(packet_size), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, E},
{"fflags", NULL, OFFSET(flags), FF_OPT_TYPE_FLAGS, DEFAULT, INT_MIN, INT_MAX, D|E, "fflags"},
{"ignidx", "ignore index", 0, FF_OPT_TYPE_CONST, AVFMT_FLAG_IGNIDX, INT_MIN, INT_MAX, D, "fflags"},
{"genpts", "generate pts", 0, FF_OPT_TYPE_CONST, AVFMT_FLAG_GENPTS, INT_MIN, INT_MAX, D, "fflags"},
{"track", " set the track number", OFFSET(track), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, E},
{"year", "set the year", OFFSET(year), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, E},
{"analyzeduration", "how many microseconds are analyzed to estimate duration", OFFSET(max_analyze_duration), FF_OPT_TYPE_INT, 3*AV_TIME_BASE, 0, INT_MAX, D},
{"cryptokey", "decryption key", OFFSET(key), FF_OPT_TYPE_BINARY, 0, 0, 0, D},
{"indexmem", "max memory used for timestamp index (per stream)", OFFSET(max_index_size), FF_OPT_TYPE_INT, 1<<20, 0, INT_MAX, D},
{"rtbufsize", "max memory used for buffering real-time frames", OFFSET(max_picture_buffer), FF_OPT_TYPE_INT, 3041280, 0, INT_MAX, D}, /* defaults to 1s of 15fps 352x288 YUYV422 video */
{"fdebug", "print specific debug info", OFFSET(debug), FF_OPT_TYPE_FLAGS, DEFAULT, 0, INT_MAX, E|D, "fdebug"},
{"ts", NULL, 0, FF_OPT_TYPE_CONST, FF_FDEBUG_TS, INT_MIN, INT_MAX, E|D, "fdebug"},
{NULL},
};

#undef E
#undef D
#undef DEFAULT

static const AVClass av_format_context_class = { "AVFormatContext", format_to_name, options };

static void avformat_get_context_defaults(AVFormatContext *s)
{
    memset(s, 0, sizeof(AVFormatContext));

    s->av_class = &av_format_context_class;

    av_opt_set_defaults(s);
}

AVFormatContext *av_alloc_format_context(void)
{
    AVFormatContext *ic;
    ic = av_malloc(sizeof(AVFormatContext));
    if (!ic) return ic;
    avformat_get_context_defaults(ic);
    ic->av_class = &av_format_context_class;
    return ic;
}
