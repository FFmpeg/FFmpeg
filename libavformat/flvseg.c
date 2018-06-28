/*
 * Copyright (c) 2018 hongjucheng
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
* flvseg is just a output protocol, which can output flv segment file.
* options:
* first_name: first segment file name, if is 0 then refine to current unix time.
* duration  : segment duration
* command examples:
* ffmpeg -i input.mp4 ... -f flv -first_name 1 -duration 3 flvseg:work_dir
* flvseg" represents protocol; "work_dir" represents output directory
*/

#include "libavutil/avstring.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/fifo.h"
#include "avformat.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "os_support.h"
#include "url.h"

#if CONFIG_FLVSEG_PROTOCOL

#define FLVSEG_FIFO_SIZE 16 * 1024 * 1024
#define FLVSEG_HEAD_SIZE 1  * 1024 * 1024
#define FLVSEG_TAGS_SIZE 4  * 1024 * 1024

#define FLVSEG_PREV_TAG_SIZE  4
#define FLVSEG_FILE_HEAD_SIZE 9 + FLVSEG_PREV_TAG_SIZE
#define FLVSEG_TAGS_HEAD_SIZE 11

#define FLVSEG_AUDIO_TAG 8
#define FLVSEG_VIDEO_TAG 9
#define FLVSEG_METADATA_TAG 18

/* flv file segment protocol */

typedef struct FlvTagHeader {
    uint8_t  tag_type;
    uint32_t data_size;
    int64_t  timestamp;
    uint32_t stream_id;
    uint32_t is_video_key_frame;
} FlvTagHeader;

typedef struct FlvSegContext {
    const AVClass *class;
    //option
    int64_t first_name;
    int     duration;
    //about file
    char    *work_dir;
    char    file_path[256];
    int     fd;
    uint64_t file_counts;
    //about flv tags
    FlvTagHeader flv_header;
    AVFifoBuffer *fifo;
    uint8_t  *head_buf;
    uint8_t  *tag_buf;
    uint64_t tag_offset;
    uint64_t need_read_size;
    int64_t  video_ts;
    int64_t  prev_video_ts;
    int  metadata_size;
    int  video_head_size;
    int  audio_head_size;
    int  is_write_header;
    int  is_found_metadata;
    int  is_found_file_head;
    int  is_found_video_head;
    int  is_found_audio_head;
} FlvSegContext;

static const AVOption flvseg_options[] = {
    { "first_name", "first flv file name", offsetof(FlvSegContext, first_name), AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT64_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "duration", "flv segment duration (second)", offsetof(FlvSegContext, duration), AV_OPT_TYPE_INT, { .i64 = 5 }, 1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL }
};

static const AVClass flvseg_class = {
    .class_name = "flvseg",
    .item_name  = av_default_item_name,
    .option     = flvseg_options,
    .version    = LIBAVUTIL_VERSION_INT
};

static inline char *make_file_path(FlvSegContext *c, int64_t file_name)
{
    memset(c->file_path, 0, sizeof(c->file_path));
    snprintf(c->file_path, 255, "%s/%lld.flv", c->work_dir, file_name);
    return c->file_path;
}

static inline uint32_t flvseg_rb24(const uint8_t *buf)
{
    uint32_t tmp = 0;
    tmp |= buf[2];
    tmp |= (buf[1] << 8);
    tmp |= (buf[0] << 16);
    return tmp; 
}

static int flvseg_parse_tag_header(const uint8_t *buf, int size, FlvTagHeader *flv_header)
{
    memset(flv_header, 0, sizeof(FlvTagHeader));
    if (size < FLVSEG_TAGS_HEAD_SIZE)
        return 0;
    
    flv_header->tag_type  = buf[0];
    flv_header->data_size = flvseg_rb24(buf + 1);
    flv_header->timestamp = buf[7] << 24 | flvseg_rb24(buf + 4);
    flv_header->stream_id = flvseg_rb24(buf + 8);
    return 1;
}

static int flvseg_is_header(FlvSegContext *c)
{
    int64_t cts = 0;
    uint8_t tag_type = c->flv_header.tag_type;
    if (tag_type == FLVSEG_VIDEO_TAG) {
        uint8_t video_flag = c->tag_buf[FLVSEG_TAGS_HEAD_SIZE];
        uint8_t frame_type = (video_flag & 0xf0) >> 4;
        uint8_t codec_id   = video_flag & 0x0f;

        if (codec_id == 7) { //avc
            c->flv_header.is_video_key_frame = (frame_type == 1);
            cts = flvseg_rb24(c->tag_buf + FLVSEG_TAGS_HEAD_SIZE + 1 + 1); // tag_header + video_flag + avc_packet_type
            c->flv_header.timestamp += cts;

            if (!c->is_found_video_head) {
                uint8_t avc_packet_type = c->tag_buf[FLVSEG_TAGS_HEAD_SIZE + 1];
                if (avc_packet_type == 0) { //avc seq header
                    uint32_t head_offset = FLVSEG_FILE_HEAD_SIZE + c->video_head_size + c->audio_head_size + c->metadata_size;
                    uint32_t tag_size    = FLVSEG_TAGS_HEAD_SIZE + c->flv_header.data_size + FLVSEG_PREV_TAG_SIZE;
                    memcpy(c->head_buf + head_offset, c->tag_buf, tag_size);
                    c->video_head_size = tag_size;
                    c->is_found_video_head = 1;
                    av_log(NULL, AV_LOG_INFO, "[flvseg] found avc sequence header.\n");
                    return 1;
                }
            }
        }
    } else if (tag_type == FLVSEG_AUDIO_TAG) {
        uint8_t audio_flag   = c->tag_buf[FLVSEG_TAGS_HEAD_SIZE];
        uint8_t sound_format = (audio_flag & 0xf0) >> 4;

        if (sound_format == 10) { //aac
            if (!c->is_found_audio_head) {
                uint8_t aac_packet_type = c->tag_buf[FLVSEG_TAGS_HEAD_SIZE + 1];
                if (aac_packet_type == 0) { //aac seq header
                    uint32_t head_offset = FLVSEG_FILE_HEAD_SIZE + c->video_head_size + c->audio_head_size + c->metadata_size;
                    uint32_t tag_size    = FLVSEG_TAGS_HEAD_SIZE + c->flv_header.data_size + FLVSEG_PREV_TAG_SIZE;
                    memcpy(c->head_buf + head_offset, c->tag_buf, tag_size);
                    c->audio_head_size = tag_size;
                    c->is_found_audio_head = 1;
                    av_log(NULL, AV_LOG_INFO, "[flvseg] found aac sequence header.\n");
                    return 1;
                }
            }
        }
    } else if (tag_type == FLVSEG_METADATA_TAG){
        if (!c->is_found_metadata) {
            uint32_t head_offset = FLVSEG_FILE_HEAD_SIZE + c->video_head_size + c->audio_head_size + c->metadata_size;
            uint32_t tag_size    = FLVSEG_TAGS_HEAD_SIZE + c->flv_header.data_size + FLVSEG_PREV_TAG_SIZE;
            memcpy(c->head_buf + head_offset, c->tag_buf, tag_size);
            c->metadata_size     = tag_size;
            c->is_found_metadata = 1;
            av_log(NULL, AV_LOG_INFO, "[flvseg] found flv metadata.\n");
            return 1;
        }
    }
    return 0;
}

static void flvseg_write_file(FlvSegContext *c)
{
    int access;
    char *file_path = NULL;
    uint32_t header_size = 0;

    if (flvseg_is_header(c))
        return;

    if (!c->is_write_header) {
        header_size = FLVSEG_FILE_HEAD_SIZE + c->video_head_size + c->audio_head_size + c->metadata_size;
        write(c->fd, c->head_buf, header_size);
        c->is_write_header = 1;
    }

    if (c->flv_header.tag_type == FLVSEG_VIDEO_TAG) {
        c->video_ts = c->flv_header.timestamp;

        if (c->prev_video_ts == -1)
            c->prev_video_ts = c->flv_header.timestamp;
    }

    if ((((int64_t)c->video_ts - (int64_t)c->prev_video_ts) >= c->duration * 1000) && 
         (c->flv_header.tag_type == FLVSEG_VIDEO_TAG) && c->flv_header.is_video_key_frame) {
        //next file
        c->file_counts++;

        // close old file
        if (c->fd != -1) {
            close(c->fd); c->fd = -1;
        }

        // open new file
        c->first_name += c->duration;
        file_path = make_file_path(c, c->first_name);
        access = O_CREAT | O_WRONLY;
#ifdef O_BINARY
        access |= O_BINARY;
#endif
        c->fd = avpriv_open(file_path, access, 0666);
        if (c->fd != -1) {
            av_log(NULL, AV_LOG_INFO, "[flvseg] open %s ok.\n", file_path);
        } else {
            av_log(NULL, AV_LOG_ERROR, "[flvseg] open %s failed.\n", file_path);
        }
        header_size = FLVSEG_FILE_HEAD_SIZE + c->video_head_size + c->audio_head_size + c->metadata_size;
        write(c->fd, c->head_buf, header_size);
        write(c->fd, c->tag_buf, FLVSEG_TAGS_HEAD_SIZE + c->flv_header.data_size + FLVSEG_PREV_TAG_SIZE);

        c->is_write_header = 1;
        c->prev_video_ts = c->flv_header.timestamp;

    } else {
        write(c->fd, c->tag_buf, FLVSEG_TAGS_HEAD_SIZE + c->flv_header.data_size + FLVSEG_PREV_TAG_SIZE);
    }
}

static int flvseg_write(URLContext *h, const unsigned char *buf, int size)
{
    FlvSegContext *c = h->priv_data;

    if (av_fifo_space(c->fifo) >= size) {
        //store data to fifo
        av_fifo_generic_write(c->fifo, (void *)buf, size, NULL);

        //find file head
        if(!c->is_found_file_head) {
            if (av_fifo_size(c->fifo) >= FLVSEG_FILE_HEAD_SIZE) {
                av_fifo_generic_read(c->fifo, c->head_buf, FLVSEG_FILE_HEAD_SIZE, NULL); // for followed files
                av_log(NULL, AV_LOG_INFO, "[flvseg] found flv file head.\n");
                c->is_found_file_head = 1;
            }
        } else {
            if (!c->need_read_size) {
                //find tag
                if (av_fifo_size(c->fifo) >= FLVSEG_TAGS_HEAD_SIZE) {
                    av_fifo_generic_read(c->fifo, c->tag_buf, FLVSEG_TAGS_HEAD_SIZE, NULL);
                    c->tag_offset = FLVSEG_TAGS_HEAD_SIZE;

                    //parse tag properties
                    flvseg_parse_tag_header(c->tag_buf, FLVSEG_TAGS_HEAD_SIZE, &c->flv_header);
                    if (av_fifo_size(c->fifo) >= (c->flv_header.data_size + FLVSEG_PREV_TAG_SIZE)) {
                        av_fifo_generic_read(c->fifo, c->tag_buf + c->tag_offset, c->flv_header.data_size + FLVSEG_PREV_TAG_SIZE, NULL);
                        flvseg_write_file(c);

                        c->need_read_size = 0;
                        c->tag_offset     = 0;
                    } else {
                        c->need_read_size = c->flv_header.data_size + FLVSEG_PREV_TAG_SIZE;
                    }
                }
            } else {
                if (av_fifo_size(c->fifo) >= c->need_read_size) {
                    av_fifo_generic_read(c->fifo, c->tag_buf + c->tag_offset, c->flv_header.data_size + FLVSEG_PREV_TAG_SIZE, NULL);
                    flvseg_write_file(c);

                    c->need_read_size = 0;
                    c->tag_offset     = 0;
                }
            }
        }
    } else {
        av_log(NULL, AV_LOG_ERROR, "[flvseg] cycle buffer overrun bad things happened\n");
        return AVERROR(EFAULT);
    }

    return size;
}

static int flvseg_get_handle(URLContext *h)
{
    FlvSegContext *c = h->priv_data;
    return c->fd;
}

static int flvseg_open(URLContext *h, const char *filename, int flags)
{
    FlvSegContext *c = h->priv_data;
    char *file_path = NULL;
    int access, fd;

    if (c->duration <= 0) {
        av_log(NULL, AV_LOG_WARNING, "[flvseg] duration is <= 0 refine to 5\n");
        c->duration = 5;
    }

    // filename: protocol + work_dir
    av_strstart(filename, "flvseg:", (const char **)&c->work_dir);
    if (!c->work_dir) {
        av_log(NULL, AV_LOG_ERROR, "[flvseg] work directory is empty\n");
        return AVERROR(EINVAL);
    } else {
        av_log(NULL, AV_LOG_INFO, "[flvseg] work directory is %s\n", c->work_dir);
    }
    if (mkdir(c->work_dir, 0777) == -1 && errno != EEXIST) {
        av_log(NULL, AV_LOG_ERROR , "[flvseg]  failed to create directory %s\n", c->work_dir);
        return AVERROR(errno);
    }

    if (c->first_name == 0) {
        c->first_name = av_gettime() / 1000000;
    }
    c->first_name = c->first_name / c->duration * c->duration;
    file_path = make_file_path(c, c->first_name);

    // open first file
    access = O_CREAT | O_WRONLY;
#ifdef O_BINARY
    access |= O_BINARY;
#endif
    fd = avpriv_open(file_path, access, 0666);
    if (fd == -1) {
        av_log(NULL, AV_LOG_ERROR, "[flvseg] open %s error\n", file_path);
        return AVERROR(errno);
    }
    c->fd = fd;
    av_log(NULL, AV_LOG_INFO, "[flvseg] open %s ok\n", file_path);

    // malloc memories
    c->fifo = av_fifo_alloc(FLVSEG_FIFO_SIZE);
    if (!c->fifo) {
        av_log(NULL, AV_LOG_ERROR, "[flvseg] malloc fifo error\n");
        return AVERROR(ENOMEM);
    }

    c->head_buf = av_mallocz(FLVSEG_HEAD_SIZE);
    if (!c->head_buf) {
        av_log(NULL, AV_LOG_ERROR, "[flvseg] malloc head buffer error\n");
        return AVERROR(ENOMEM);
    }

    c->tag_buf = av_mallocz(FLVSEG_TAGS_SIZE);
    if (!c->tag_buf) {
        av_log(NULL, AV_LOG_ERROR, "[flvseg] malloc tag buffer error\n");
        return AVERROR(ENOMEM);
    }

    //init 
    c->file_counts         = 0;
    c->tag_offset          = 0;
    c->need_read_size      = 0;
    c->video_ts            = 0;
    c->prev_video_ts       = -1;
    c->metadata_size       = 0;
    c->video_head_size     = 0;
    c->audio_head_size     = 0;
    c->is_write_header     = 0;
    c->is_found_file_head  = 0;
    c->is_found_video_head = 0;
    c->is_found_audio_head = 0;

    return 0;
}

static int flvseg_close(URLContext *h)
{
    FlvSegContext *c = h->priv_data;
    close(c->fd);
    av_fifo_freep(&c->fifo);
    av_freep(&c->head_buf);
    return 0;
}

const URLProtocol ff_flvseg_protocol = {
    .name                = "flvseg",
    .url_open            = flvseg_open,
    .url_write           = flvseg_write,
    .url_close           = flvseg_close,
    .url_get_file_handle = flvseg_get_handle,
    .priv_data_size      = sizeof(FlvSegContext),
    .priv_data_class     = &flvseg_class,
    .default_whitelist   = "flvseg"
};

#endif  /* CONFIG_FLV_SEG_PROTOCOL */
