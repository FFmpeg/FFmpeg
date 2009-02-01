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
* @file libavcodec/libdirac_libschro.h
* data structures common to libdirac and libschroedinger
*/

#ifndef AVCODEC_LIBDIRAC_LIBSCHRO_H
#define AVCODEC_LIBDIRAC_LIBSCHRO_H

#include "avcodec.h"

typedef struct
{
    uint16_t width;
    uint16_t height;
    uint16_t frame_rate_num;
    uint16_t frame_rate_denom;
} FfmpegDiracSchroVideoFormatInfo;

/**
* Returns the index into the Dirac Schro common video format info table
*/
unsigned int ff_dirac_schro_get_video_format_idx (AVCodecContext *avccontext);

/**
* contains a single encoded frame returned from Dirac or Schroedinger
*/
typedef struct FfmpegDiracSchroEncodedFrame
{
    /** encoded frame data */
    uint8_t *p_encbuf;

    /** encoded frame size */
    uint32_t size;

    /** encoded frame number. Will be used as pts */
    uint32_t frame_num;

    /** key frame flag. 1 : is key frame , 0 : in not key frame */
    uint16_t key_frame;
} FfmpegDiracSchroEncodedFrame;

/**
* queue element
*/
typedef struct FfmpegDiracSchroQueueElement
{
    /** Data to be stored in queue*/
    void *data;
    /** Pointer to next element queue */
    struct FfmpegDiracSchroQueueElement *next;
} FfmpegDiracSchroQueueElement;


/**
* A simple queue implementation used in libdirac and libschroedinger
*/
typedef struct FfmpegDiracSchroQueue
{
    /** Pointer to head of queue */
    FfmpegDiracSchroQueueElement *p_head;
    /** Pointer to tail of queue */
    FfmpegDiracSchroQueueElement *p_tail;
    /** Queue size*/
    int size;
} FfmpegDiracSchroQueue;

/**
* Initialise the queue
*/
void ff_dirac_schro_queue_init(FfmpegDiracSchroQueue *queue);

/**
* Add an element to the end of the queue
*/
int ff_dirac_schro_queue_push_back (FfmpegDiracSchroQueue *queue, void *p_data);

/**
* Return the first element in the queue
*/
void *ff_dirac_schro_queue_pop (FfmpegDiracSchroQueue *queue);

/**
* Free the queue resources. free_func is a function supplied by the caller to
* free any resources allocated by the caller. The data field of the queue
* element is passed to it.
*/
void ff_dirac_schro_queue_free(FfmpegDiracSchroQueue *queue,
                               void (*free_func)(void *));
#endif /* AVCODEC_LIBDIRAC_LIBSCHRO_H */
