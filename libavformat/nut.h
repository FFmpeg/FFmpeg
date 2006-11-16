/*
 * "NUT" Container Format (de)muxer
 * Copyright (c) 2006 Michael Niedermayer
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
 *
 */

//#include <limits.h>
#include "avformat.h"
#include "crc.h"
//#include "mpegaudio.h"
#include "riff.h"
//#include "adler32.h"

#define      MAIN_STARTCODE (0x7A561F5F04ADULL + (((uint64_t)('N'<<8) + 'M')<<48))
#define    STREAM_STARTCODE (0x11405BF2F9DBULL + (((uint64_t)('N'<<8) + 'S')<<48))
#define SYNCPOINT_STARTCODE (0xE4ADEECA4569ULL + (((uint64_t)('N'<<8) + 'K')<<48))
#define     INDEX_STARTCODE (0xDD672F23E64EULL + (((uint64_t)('N'<<8) + 'X')<<48))
#define      INFO_STARTCODE (0xAB68B596BA78ULL + (((uint64_t)('N'<<8) + 'I')<<48))

#define ID_STRING "nut/multimedia container\0"

#define MAX_DISTANCE (1024*32-1)

typedef enum{
    FLAG_KEY        =   1, ///<if set, frame is keyframe
    FLAG_EOR        =   2, ///<if set, stream has no relevance on presentation. (EOR)
    FLAG_CODED_PTS  =   8, ///<if set, coded_pts is in the frame header
    FLAG_STREAM_ID  =  16, ///<if set, stream_id is coded in the frame header
    FLAG_SIZE_MSB   =  32, ///<if set, data_size_msb is at frame header, otherwise data_size_msb is 0
    FLAG_CHECKSUM   =  64, ///<if set then the frame header contains a checksum
    FLAG_RESERVED   = 128, ///<if set, reserved_count is coded in the frame header
    FLAG_CODED      =4096, ///<if set, coded_flags are stored in the frame header.
    FLAG_INVALID    =8192, ///<if set, frame_code is invalid.
}flag_t;

typedef struct {
    uint64_t pos;
    uint64_t back_ptr;
//    uint64_t global_key_pts;
    int64_t ts;
} syncpoint_t;

typedef struct {
    uint16_t flags;
    uint8_t  stream_id;
    uint16_t size_mul;
    uint16_t size_lsb;
    int16_t  pts_delta;
    uint8_t  reserved_count;
} FrameCode; // maybe s/FrameCode/framecode_t/ or change all to java style but dont mix

typedef struct {
    int last_flags;
    int skip_until_key_frame;
    int64_t last_pts;
    int time_base_id;
    AVRational time_base;
    int msb_pts_shift;
    int max_pts_distance;
    int decode_delay; //FIXME duplicate of has_b_frames
} StreamContext;// maybe s/StreamContext/streamcontext_t/

typedef struct {
    AVFormatContext *avf;
//    int written_packet_size;
//    int64_t packet_start[3]; //0-> startcode less, 1-> short startcode 2-> long startcodes
    FrameCode frame_code[256];
    uint64_t next_startcode;     ///< stores the next startcode if it has alraedy been parsed but the stream isnt seekable
    StreamContext *stream;
    unsigned int max_distance;
    unsigned int time_base_count;
    int64_t last_syncpoint_pos;
    AVRational *time_base;
    struct AVTreeNode *syncpoints;
} NUTContext;


//FIXME move to a common spot, like crc.c/h
static unsigned long av_crc04C11DB7_update(unsigned long checksum, const uint8_t *buf, unsigned int len){
    return av_crc(av_crc04C11DB7, checksum, buf, len);
}
