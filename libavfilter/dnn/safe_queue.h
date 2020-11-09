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

#ifndef AVFILTER_DNN_SAFE_QUEUE_H
#define AVFILTER_DNN_SAFE_QUEUE_H

typedef struct _safe_queue safe_queue;

safe_queue *safe_queue_create(void);
void safe_queue_destroy(safe_queue *sq);

size_t safe_queue_size(safe_queue *sq);

void safe_queue_push_front(safe_queue *sq, void *v);
void safe_queue_push_back(safe_queue *sq, void *v);

void *safe_queue_pop_front(safe_queue *sq);

#endif
