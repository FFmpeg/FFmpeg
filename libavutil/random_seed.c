/*
 * Copyright (c) 2009 Baptiste Coudurier <baptiste.coudurier@gmail.com>
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

#include <unistd.h>
#include <fcntl.h>
#include "timer.h"
#include "random_seed.h"

uint32_t ff_random_get_seed(void)
{
    uint32_t seed;
    int fd;

    if ((fd = open("/dev/random", O_RDONLY)) == -1)
        fd = open("/dev/urandom", O_RDONLY);
    if (fd != -1){
        read(fd, &seed, 4);
        close(fd);
        return seed;
    }
#ifdef AV_READ_TIME
    seed = AV_READ_TIME();
#endif
    // XXX what to do ?
    return seed;
}
