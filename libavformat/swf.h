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

#include "libavutil/fifo.h"
#include "avformat.h"
#include "avio.h"
#include "riff.h"    /* for CodecTag */

/* should have a generic way to indicate probable size */
#define DUMMY_FILE_SIZE   (100 * 1024 * 1024)
#define DUMMY_DURATION    600 /* in seconds */

#define TAG_END           0
#define TAG_SHOWFRAME     1
#define TAG_DEFINESHAPE   2
#define TAG_FREECHARACTER 3
#define TAG_PLACEOBJECT   4
#define TAG_REMOVEOBJECT  5
#define TAG_STREAMHEAD    18
#define TAG_STREAMBLOCK   19
#define TAG_JPEG2         21
#define TAG_PLACEOBJECT2  26
#define TAG_STREAMHEAD2   45
#define TAG_VIDEOSTREAM   60
#define TAG_VIDEOFRAME    61
#define TAG_FILEATTRIBUTES 69

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

typedef struct {
    int audio_stream_index;
    int64_t duration_pos;
    int64_t tag_pos;
    int64_t vframes_pos;
    int samples_per_frame;
    int sound_samples;
    int swf_frame_number;
    int video_frame_number;
    int frame_rate;
    int tag;
    AVFifoBuffer audio_fifo;
    AVCodecContext *audio_enc, *video_enc;
} SWFContext;

static const AVCodecTag swf_codec_tags[] = {
    {CODEC_ID_FLV1, 0x02},
    {CODEC_ID_VP6F, 0x04},
    {0, 0},
};

static const AVCodecTag swf_audio_codec_tags[] = {
    {CODEC_ID_PCM_S16LE,  0x00},
    {CODEC_ID_ADPCM_SWF,  0x01},
    {CODEC_ID_MP3,        0x02},
    {CODEC_ID_PCM_S16LE,  0x03},
  //{CODEC_ID_NELLYMOSER, 0x06},
    {0, 0},
};

#endif /* AVFORMAT_SWF_H */
