/*
 * Flash Compatible Streaming Format common header.
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2003 Tinic Uro
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

#ifndef AVFORMAT_SWF_H
#define AVFORMAT_SWF_H

#include "config.h"

#if CONFIG_ZLIB
#include <zlib.h>
#endif

#include "libavutil/fifo.h"
#include "avformat.h"
#include "avio.h"
#include "riff.h"    /* for CodecTag */

/* should have a generic way to indicate probable size */
#define DUMMY_FILE_SIZE   (100 * 1024 * 1024)
#define DUMMY_DURATION    600 /* in seconds */

#define TAG_END                           0
#define TAG_SHOWFRAME                     1
#define TAG_DEFINESHAPE                   2
#define TAG_FREECHARACTER                 3
#define TAG_PLACEOBJECT                   4
#define TAG_REMOVEOBJECT                  5
#define TAG_DEFINEBITS                    6
#define TAG_DEFINEBUTTON                  7
#define TAG_JPEGTABLES                    8
#define TAG_SETBACKGROUNDCOLOR            9
#define TAG_DEFINEFONT                   10
#define TAG_DEFINETEXT                   11
#define TAG_DOACTION                     12
#define TAG_DEFINEFONTINFO               13
#define TAG_DEFINESOUND                  14
#define TAG_STARTSOUND                   15
#define TAG_DEFINEBUTTONSOUND            17
#define TAG_STREAMHEAD                   18
#define TAG_STREAMBLOCK                  19
#define TAG_DEFINEBITSLOSSLESS           20
#define TAG_JPEG2                        21
#define TAG_DEFINESHAPE2                 22
#define TAG_DEFINEBUTTONCXFORM           23
#define TAG_PROTECT                      24
#define TAG_PLACEOBJECT2                 26
#define TAG_REMOVEOBJECT2                28
#define TAG_DEFINESHAPE3                 32
#define TAG_DEFINETEXT2                  33
#define TAG_DEFINEBUTTON2                34
#define TAG_DEFINEBITSJPEG3              35
#define TAG_DEFINEBITSLOSSLESS2          36
#define TAG_DEFINEEDITTEXT               37
#define TAG_DEFINESPRITE                 39
#define TAG_FRAMELABEL                   43
#define TAG_STREAMHEAD2                  45
#define TAG_DEFINEMORPHSHAPE             46
#define TAG_DEFINEFONT2                  48
#define TAG_EXPORTASSETS                 56
#define TAG_IMPORTASSETS                 57
#define TAG_ENABLEDEBUGGER               58
#define TAG_DOINITACTION                 59
#define TAG_VIDEOSTREAM                  60
#define TAG_VIDEOFRAME                   61
#define TAG_DEFINEFONTINFO2              62
#define TAG_ENABLEDEBUGGER2              64
#define TAG_SCRIPTLIMITS                 65
#define TAG_SETTABINDEX                  66
#define TAG_FILEATTRIBUTES               69
#define TAG_PLACEOBJECT3                 70
#define TAG_IMPORTASSETS2                71
#define TAG_DEFINEFONTALIGNZONES         73
#define TAG_CSMTEXTSETTINGS              74
#define TAG_DEFINEFONT3                  75
#define TAG_SYMBOLCLASS                  76
#define TAG_METADATA                     77
#define TAG_DEFINESCALINGGRID            78
#define TAG_DOABC                        82
#define TAG_DEFINESHAPE4                 83
#define TAG_DEFINEMORPHSHAPE2            84
#define TAG_DEFINESCENEANDFRAMELABELDATA 86
#define TAG_DEFINEBINARYDATA             87
#define TAG_DEFINEFONTNAME               88
#define TAG_STARTSOUND2                  89
#define TAG_DEFINEBITSJPEG4              90
#define TAG_DEFINEFONT4                  91

#define TAG_LONG         0x100

/* flags for shape definition */
#define FLAG_MOVETO      0x01
#define FLAG_SETFILL0    0x02
#define FLAG_SETFILL1    0x04

#define AUDIO_FIFO_SIZE 65536

/* character id used */
#define BITMAP_ID 0
#define VIDEO_ID 0
#define SHAPE_ID  1

#undef NDEBUG
#include <assert.h>

typedef struct SWFContext {
    int64_t duration_pos;
    int64_t tag_pos;
    int64_t vframes_pos;
    int samples_per_frame;
    int sound_samples;
    int swf_frame_number;
    int video_frame_number;
    int frame_rate;
    int tag;
    AVFifoBuffer *audio_fifo;
    AVCodecContext *audio_enc, *video_enc;
#if CONFIG_ZLIB
    AVIOContext *zpb;
#define ZBUF_SIZE 4096
    uint8_t *zbuf_in;
    uint8_t *zbuf_out;
    z_stream zstream;
#endif
} SWFContext;

extern const AVCodecTag ff_swf_codec_tags[];

#endif /* AVFORMAT_SWF_H */
