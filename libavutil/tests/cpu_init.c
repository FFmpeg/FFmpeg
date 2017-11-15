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
 * This test program tests whether the one-time initialization in
 * av_get_cpu_flags() has data races.
 */

#include <stdio.h>
#include <string.h>

#include "libavutil/cpu.h"
#include "libavutil/thread.h"

static void *thread_main(void *arg)
{
    int *flags = arg;

    *flags = av_get_cpu_flags();
    return NULL;
}

int main(void)
{
    int cpu_flags1;
    int cpu_flags2;
    int ret;
    pthread_t thread1;
    pthread_t thread2;

    if ((ret = pthread_create(&thread1, NULL, thread_main, &cpu_flags1))) {
        fprintf(stderr, "pthread_create failed: %s.\n", strerror(ret));
        return 1;
    }
    if ((ret = pthread_create(&thread2, NULL, thread_main, &cpu_flags2))) {
        fprintf(stderr, "pthread_create failed: %s.\n", strerror(ret));
        return 1;
    }
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    if (cpu_flags1 < 0)
        return 2;
    if (cpu_flags2 < 0)
        return 2;
    if (cpu_flags1 != cpu_flags2)
        return 3;

    return 0;
}
