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

typedef struct SafeQueue SafeQueue;

SafeQueue *ff_safe_queue_create(void);
void ff_safe_queue_destroy(SafeQueue *sq);

size_t ff_safe_queue_size(SafeQueue *sq);

int ff_safe_queue_push_front(SafeQueue *sq, void *v);
int ff_safe_queue_push_back(SafeQueue *sq, void *v);

void *ff_safe_queue_pop_front(SafeQueue *sq);

#endif
