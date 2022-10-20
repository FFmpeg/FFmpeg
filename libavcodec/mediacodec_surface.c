/*
 * Android MediaCodec Surface functions
 *
 * Copyright (c) 2016 Matthieu Bouron <matthieu.bouron stupeflix.com>
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

#include <android/native_window.h>
#include <jni.h>

#include "libavutil/mem.h"
#include "ffjni.h"
#include "mediacodec_surface.h"

FFANativeWindow *ff_mediacodec_surface_ref(void *surface, void *native_window, void *log_ctx)
{
    FFANativeWindow *ret;

    ret = av_mallocz(sizeof(*ret));
    if (!ret)
        return NULL;

    if (surface) {
        JNIEnv *env = NULL;

        env = ff_jni_get_env(log_ctx);
        if (env)
            ret->surface = (*env)->NewGlobalRef(env, surface);
    }

    if (native_window) {
        ANativeWindow_acquire(native_window);
        ret->native_window = native_window;
    }

    if (!ret->surface && !ret->native_window) {
        av_log(log_ctx, AV_LOG_ERROR, "Both surface and native_window are NULL\n");
        av_freep(&ret);
    }

    return ret;
}

int ff_mediacodec_surface_unref(FFANativeWindow *window, void *log_ctx)
{
    if (!window)
        return 0;

    if (window->surface) {
        JNIEnv *env = NULL;

        env = ff_jni_get_env(log_ctx);
        if (env)
            (*env)->DeleteGlobalRef(env, window->surface);
    }

    if (window->native_window)
        ANativeWindow_release(window->native_window);

    av_free(window);

    return 0;
}
