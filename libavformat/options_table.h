/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVFORMAT_OPTIONS_TABLE_H
#define AVFORMAT_OPTIONS_TABLE_H

#include <limits.h>

#include "libavutil/opt.h"
#include "avformat.h"

#define OFFSET(x) offsetof(AVFormatContext,x)
#define DEFAULT 0 //should be NAN but it does not work as it is not a constant in glibc as required by ANSI/ISO C
//these names are too long to be readable
#define E AV_OPT_FLAG_ENCODING_PARAM
#define D AV_OPT_FLAG_DECODING_PARAM

static const AVOption avformat_options[] = {
{"probesize", "set probing size", OFFSET(probesize), AV_OPT_TYPE_INT, {.i64 = 5000000 }, 32, INT_MAX, D},
{"packetsize", "set packet size", OFFSET(packet_size), AV_OPT_TYPE_INT, {.i64 = DEFAULT }, 0, INT_MAX, E},
{"fflags", NULL, OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64 = AVFMT_FLAG_FLUSH_PACKETS }, INT_MIN, INT_MAX, D|E, "fflags"},
{"flush_packets", "reduce the latency by flushing out packets immediately", 0, AV_OPT_TYPE_CONST, {.i64 = AVFMT_FLAG_FLUSH_PACKETS }, INT_MIN, INT_MAX, D, "fflags"},
{"ignidx", "ignore index", 0, AV_OPT_TYPE_CONST, {.i64 = AVFMT_FLAG_IGNIDX }, INT_MIN, INT_MAX, D, "fflags"},
{"genpts", "generate pts", 0, AV_OPT_TYPE_CONST, {.i64 = AVFMT_FLAG_GENPTS }, INT_MIN, INT_MAX, D, "fflags"},
{"nofillin", "do not fill in missing values that can be exactly calculated", 0, AV_OPT_TYPE_CONST, {.i64 = AVFMT_FLAG_NOFILLIN }, INT_MIN, INT_MAX, D, "fflags"},
{"noparse", "disable AVParsers, this needs nofillin too", 0, AV_OPT_TYPE_CONST, {.i64 = AVFMT_FLAG_NOPARSE }, INT_MIN, INT_MAX, D, "fflags"},
{"igndts", "ignore dts", 0, AV_OPT_TYPE_CONST, {.i64 = AVFMT_FLAG_IGNDTS }, INT_MIN, INT_MAX, D, "fflags"},
{"discardcorrupt", "discard corrupted frames", 0, AV_OPT_TYPE_CONST, {.i64 = AVFMT_FLAG_DISCARD_CORRUPT }, INT_MIN, INT_MAX, D, "fflags"},
{"nobuffer", "reduce the latency introduced by optional buffering", 0, AV_OPT_TYPE_CONST, {.i64 = AVFMT_FLAG_NOBUFFER }, 0, INT_MAX, D, "fflags"},
{"analyzeduration", "how many microseconds are analyzed to estimate duration", OFFSET(max_analyze_duration), AV_OPT_TYPE_INT, {.i64 = 5*AV_TIME_BASE }, 0, INT_MAX, D},
{"cryptokey", "decryption key", OFFSET(key), AV_OPT_TYPE_BINARY, {.dbl = 0}, 0, 0, D},
{"indexmem", "max memory used for timestamp index (per stream)", OFFSET(max_index_size), AV_OPT_TYPE_INT, {.i64 = 1<<20 }, 0, INT_MAX, D},
{"rtbufsize", "max memory used for buffering real-time frames", OFFSET(max_picture_buffer), AV_OPT_TYPE_INT, {.i64 = 3041280 }, 0, INT_MAX, D}, /* defaults to 1s of 15fps 352x288 YUYV422 video */
{"fdebug", "print specific debug info", OFFSET(debug), AV_OPT_TYPE_FLAGS, {.i64 = DEFAULT }, 0, INT_MAX, E|D, "fdebug"},
{"ts", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = FF_FDEBUG_TS }, INT_MIN, INT_MAX, E|D, "fdebug"},
{"max_delay", "maximum muxing or demuxing delay in microseconds", OFFSET(max_delay), AV_OPT_TYPE_INT, {.i64 = -1 }, -1, INT_MAX, E|D},
{"fpsprobesize", "number of frames used to probe fps", OFFSET(fps_probe_size), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX-1, D},
/* this is a crutch for avconv, since it cannot deal with identically named options in different contexts.
 * to be removed when avconv is fixed */
{"f_err_detect", "set error detection flags (deprecated; use err_detect, save via avconv)", OFFSET(error_recognition), AV_OPT_TYPE_FLAGS, {.i64 = AV_EF_CRCCHECK }, INT_MIN, INT_MAX, D, "err_detect"},
{"err_detect", "set error detection flags", OFFSET(error_recognition), AV_OPT_TYPE_FLAGS, {.i64 = AV_EF_CRCCHECK }, INT_MIN, INT_MAX, D, "err_detect"},
{"crccheck", "verify embedded CRCs", 0, AV_OPT_TYPE_CONST, {.i64 = AV_EF_CRCCHECK }, INT_MIN, INT_MAX, D, "err_detect"},
{"bitstream", "detect bitstream specification deviations", 0, AV_OPT_TYPE_CONST, {.i64 = AV_EF_BITSTREAM }, INT_MIN, INT_MAX, D, "err_detect"},
{"buffer", "detect improper bitstream length", 0, AV_OPT_TYPE_CONST, {.i64 = AV_EF_BUFFER }, INT_MIN, INT_MAX, D, "err_detect"},
{"explode", "abort decoding on minor error detection", 0, AV_OPT_TYPE_CONST, {.i64 = AV_EF_EXPLODE }, INT_MIN, INT_MAX, D, "err_detect"},
{"max_interleave_delta", "maximum buffering duration for interleaving", OFFSET(max_interleave_delta), AV_OPT_TYPE_INT64, { .i64 = 10000000 }, 0, INT64_MAX, E },
{NULL},
};

#undef E
#undef D
#undef DEFAULT
#undef OFFSET

#endif /* AVFORMAT_OPTIONS_TABLE_H */
