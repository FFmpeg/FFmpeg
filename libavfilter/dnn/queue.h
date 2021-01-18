/*
 * Copyright (c) 2020
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


#ifndef AVFILTER_DNN_QUEUE_H
#define AVFILTER_DNN_QUEUE_H

typedef struct Queue Queue;

Queue *ff_queue_create(void);
void ff_queue_destroy(Queue *q);

size_t ff_queue_size(Queue *q);

void *ff_queue_peek_front(Queue *q);
void *ff_queue_peek_back(Queue *q);

int ff_queue_push_front(Queue *q, void *v);
int ff_queue_push_back(Queue *q, void *v);

void *ff_queue_pop_front(Queue *q);
void *ff_queue_pop_back(Queue *q);

#endif
