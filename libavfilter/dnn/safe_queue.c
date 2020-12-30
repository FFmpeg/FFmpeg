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

#include <stdio.h>
#include "queue.h"
#include "safe_queue.h"
#include "libavutil/mem.h"
#include "libavutil/avassert.h"
#include "libavutil/thread.h"

struct FFSafeQueue {
    FFQueue *q;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

FFSafeQueue *ff_safe_queue_create(void)
{
    FFSafeQueue *sq = av_malloc(sizeof(*sq));
    if (!sq)
        return NULL;

    sq->q = ff_queue_create();
    if (!sq->q)
        return NULL;

    pthread_mutex_init(&sq->mutex, NULL);
    pthread_cond_init(&sq->cond, NULL);
    return sq;
}

void ff_safe_queue_destroy(FFSafeQueue *sq)
{
    if (!sq)
        return;

    ff_queue_destroy(sq->q);
    pthread_mutex_destroy(&sq->mutex);
    pthread_cond_destroy(&sq->cond);
    av_freep(&sq);
}

size_t ff_safe_queue_size(FFSafeQueue *sq)
{
    return sq ? ff_queue_size(sq->q) : 0;
}

void ff_safe_queue_push_front(FFSafeQueue *sq, void *v)
{
    pthread_mutex_lock(&sq->mutex);
    ff_queue_push_front(sq->q, v);
    pthread_cond_signal(&sq->cond);
    pthread_mutex_unlock(&sq->mutex);
}

void ff_safe_queue_push_back(FFSafeQueue *sq, void *v)
{
    pthread_mutex_lock(&sq->mutex);
    ff_queue_push_back(sq->q, v);
    pthread_cond_signal(&sq->cond);
    pthread_mutex_unlock(&sq->mutex);
}

void *ff_safe_queue_pop_front(FFSafeQueue *sq)
{
    void *value;
    pthread_mutex_lock(&sq->mutex);
    while (ff_queue_size(sq->q) == 0) {
        pthread_cond_wait(&sq->cond, &sq->mutex);
    }
    value = ff_queue_pop_front(sq->q);
    pthread_cond_signal(&sq->cond);
    pthread_mutex_unlock(&sq->mutex);
    return value;
}
