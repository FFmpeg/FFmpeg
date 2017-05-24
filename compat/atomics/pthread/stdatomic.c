/*
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

/*
 * based on vlc_atomic.h from VLC
 * Copyright (C) 2010 Rémi Denis-Courmont
 */

#include <pthread.h>
#include <stdint.h>

#include "stdatomic.h"

static pthread_mutex_t atomic_lock = PTHREAD_MUTEX_INITIALIZER;

void avpriv_atomic_lock(void)
{
    pthread_mutex_lock(&atomic_lock);
}

void avpriv_atomic_unlock(void)
{
    pthread_mutex_unlock(&atomic_lock);
}
