/*
 * Copyright (c) 2008 BBC, Anuradha Suraparaju <asuraparaju at gmail dot com >
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

/**
* @file
* data structures common to libschroedinger decoder and encoder
*/

#ifndef AVCODEC_LIBSCHROEDINGER_H
#define AVCODEC_LIBSCHROEDINGER_H

#include <schroedinger/schrobitstream.h>
#include <schroedinger/schroframe.h>

#include "avcodec.h"

typedef struct SchroVideoFormatInfo {
    uint16_t width;
    uint16_t height;
    uint16_t frame_rate_num;
    uint16_t frame_rate_denom;
} SchroVideoFormatInfo;

/**
* contains a single encoded frame returned from Dirac or Schroedinger
*/
typedef struct FFSchroEncodedFrame {
    /** encoded frame data */
    uint8_t *p_encbuf;

    /** encoded frame size */
    uint32_t size;

    /** encoded frame number. Will be used as pts */
    uint32_t frame_num;

    /** key frame flag. 1 : is key frame , 0 : in not key frame */
    uint16_t key_frame;
} FFSchroEncodedFrame;

/**
* queue element
*/
typedef struct FFSchroQueueElement {
    /** Data to be stored in queue*/
    void *data;
    /** Pointer to next element queue */
    struct FFSchroQueueElement *next;
} FFSchroQueueElement;


/**
* A simple queue implementation used in libschroedinger
*/
typedef struct FFSchroQueue {
    /** Pointer to head of queue */
    FFSchroQueueElement *p_head;
    /** Pointer to tail of queue */
    FFSchroQueueElement *p_tail;
    /** Queue size*/
    int size;
} FFSchroQueue;

/**
* Initialise the queue
*/
void ff_schro_queue_init(FFSchroQueue *queue);

/**
* Add an element to the end of the queue
*/
int ff_schro_queue_push_back(FFSchroQueue *queue, void *p_data);

/**
* Return the first element in the queue
*/
void *ff_schro_queue_pop(FFSchroQueue *queue);

/**
* Free the queue resources. free_func is a function supplied by the caller to
* free any resources allocated by the caller. The data field of the queue
* element is passed to it.
*/
void ff_schro_queue_free(FFSchroQueue *queue, void (*free_func)(void *));

static const struct {
    enum AVPixelFormat  ff_pix_fmt;
    SchroChromaFormat schro_pix_fmt;
    SchroFrameFormat  schro_frame_fmt;
} schro_pixel_format_map[] = {
    { AV_PIX_FMT_YUV420P, SCHRO_CHROMA_420, SCHRO_FRAME_FORMAT_U8_420 },
    { AV_PIX_FMT_YUV422P, SCHRO_CHROMA_422, SCHRO_FRAME_FORMAT_U8_422 },
    { AV_PIX_FMT_YUV444P, SCHRO_CHROMA_444, SCHRO_FRAME_FORMAT_U8_444 },
};

/**
* Returns the video format preset matching the input video dimensions and
* time base.
*/
SchroVideoFormatEnum ff_get_schro_video_format_preset (AVCodecContext *avctx);

/**
* Sets the Schroedinger frame format corresponding to the Schro chroma format
* passed. Returns 0 on success, -1 on failure.
*/
int ff_get_schro_frame_format(SchroChromaFormat schro_chroma_fmt,
                              SchroFrameFormat  *schro_frame_fmt);

/**
* Create a Schro frame based on the dimensions and frame format
* passed. Returns a pointer to a frame on success, NULL on failure.
*/
SchroFrame *ff_create_schro_frame(AVCodecContext *avctx,
                                  SchroFrameFormat schro_frame_fmt);

#endif /* AVCODEC_LIBSCHROEDINGER_H */
