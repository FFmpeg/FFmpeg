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

#include <jni.h>

#include "ffjni.h"
#include "mediacodec_surface.h"

void *ff_mediacodec_surface_ref(void *surface, void *log_ctx)
{
    JNIEnv *env = NULL;

    void *reference = NULL;

    env = ff_jni_get_env(log_ctx);
    if (!env) {
        return NULL;
    }

    reference = (*env)->NewGlobalRef(env, surface);

    return reference;
}

int ff_mediacodec_surface_unref(void *surface, void *log_ctx)
{
    JNIEnv *env = NULL;

    env = ff_jni_get_env(log_ctx);
    if (!env) {
        return AVERROR_EXTERNAL;
    }

    (*env)->DeleteGlobalRef(env, surface);

    return 0;
}
