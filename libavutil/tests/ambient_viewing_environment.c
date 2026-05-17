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

#include <limits.h>
#include <stdio.h>

#include "libavutil/ambient_viewing_environment.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"

static void print_env(const AVAmbientViewingEnvironment *env)
{
    printf("illuminance=%d/%d, light_x=%d/%d, light_y=%d/%d\n",
           env->ambient_illuminance.num, env->ambient_illuminance.den,
           env->ambient_light_x.num,     env->ambient_light_x.den,
           env->ambient_light_y.num,     env->ambient_light_y.den);
}

int main(void)
{
    AVAmbientViewingEnvironment *env;
    AVFrame *frame;
    size_t size = 0;

    /* av_ambient_viewing_environment_alloc with size out-param */
    printf("Testing av_ambient_viewing_environment_alloc(&size)\n");
    env = av_ambient_viewing_environment_alloc(&size);
    if (env) {
        printf("alloc: OK, size_set=%s\n", size == sizeof(*env) ? "yes" : "no");
        print_env(env);
        av_free(env);
    } else {
        printf("alloc: FAIL\n");
    }

    /* av_ambient_viewing_environment_alloc with NULL size */
    printf("\nTesting av_ambient_viewing_environment_alloc(NULL)\n");
    env = av_ambient_viewing_environment_alloc(NULL);
    if (env) {
        printf("alloc(NULL): OK\n");
        print_env(env);
        av_free(env);
    } else {
        printf("alloc(NULL): FAIL\n");
    }

    /* write and read back */
    printf("\nTesting write/read back\n");
    env = av_ambient_viewing_environment_alloc(NULL);
    if (env) {
        env->ambient_illuminance = (AVRational){ 314, 10 };
        env->ambient_light_x     = (AVRational){ 15635, 50000 };
        env->ambient_light_y     = (AVRational){ 16450, 50000 };
        print_env(env);
        av_free(env);
    }

    /* av_ambient_viewing_environment_create_side_data */
    printf("\nTesting av_ambient_viewing_environment_create_side_data()\n");
    frame = av_frame_alloc();
    if (frame) {
        env = av_ambient_viewing_environment_create_side_data(frame);
        if (env) {
            printf("side_data: OK\n");
            print_env(env);
        } else {
            printf("side_data: FAIL\n");
        }
        av_frame_free(&frame);
    }

    /* OOM paths via av_max_alloc */
    printf("\nTesting OOM paths\n");
    av_max_alloc(1);
    env = av_ambient_viewing_environment_alloc(&size);
    printf("alloc OOM: %s\n", env ? "FAIL" : "OK");
    av_free(env);
    env = av_ambient_viewing_environment_alloc(NULL);
    printf("alloc(NULL) OOM: %s\n", env ? "FAIL" : "OK");
    av_free(env);
    av_max_alloc(INT_MAX);

    frame = av_frame_alloc();
    if (frame) {
        av_max_alloc(1);
        env = av_ambient_viewing_environment_create_side_data(frame);
        printf("side_data OOM: %s\n", env ? "FAIL" : "OK");
        av_max_alloc(INT_MAX);
        av_frame_free(&frame);
    }

    return 0;
}
