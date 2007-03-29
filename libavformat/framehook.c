/*
 * Video processing hooks
 * Copyright (c) 2000, 2001 Fabrice Bellard.
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
#include <errno.h>
#include "config.h"
#include "avformat.h"
#include "framehook.h"

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif


typedef struct _FrameHookEntry {
    struct _FrameHookEntry *next;
    FrameHookConfigureFn Configure;
    FrameHookProcessFn Process;
    FrameHookReleaseFn Release;
    void *ctx;
} FrameHookEntry;

static FrameHookEntry *first_hook;

/* Returns 0 on OK */
int frame_hook_add(int argc, char *argv[])
{
#ifdef CONFIG_VHOOK
    void *loaded;
    FrameHookEntry *fhe, **fhep;

    if (argc < 1) {
        return ENOENT;
    }

    loaded = dlopen(argv[0], RTLD_NOW);
    if (!loaded) {
        av_log(NULL, AV_LOG_ERROR, "%s\n", dlerror());
        return -1;
    }

    fhe = av_mallocz(sizeof(*fhe));
    if (!fhe) {
        return AVERROR(ENOMEM);
    }

    fhe->Configure = dlsym(loaded, "Configure");
    fhe->Process = dlsym(loaded, "Process");
    fhe->Release = dlsym(loaded, "Release");    /* Optional */

    if (!fhe->Process) {
        av_log(NULL, AV_LOG_ERROR, "Failed to find Process entrypoint in %s\n", argv[0]);
        return AVERROR(ENOENT);
    }

    if (!fhe->Configure && argc > 1) {
        av_log(NULL, AV_LOG_ERROR, "Failed to find Configure entrypoint in %s\n", argv[0]);
        return AVERROR(ENOENT);
    }

    if (argc > 1 || fhe->Configure) {
        if (fhe->Configure(&fhe->ctx, argc, argv)) {
            av_log(NULL, AV_LOG_ERROR, "Failed to Configure %s\n", argv[0]);
            return AVERROR(EINVAL);
        }
    }

    for (fhep = &first_hook; *fhep; fhep = &((*fhep)->next)) {
    }

    *fhep = fhe;

    return 0;
#else
    av_log(NULL, AV_LOG_ERROR, "Video hooking not compiled into this version\n");
    return 1;
#endif
}

void frame_hook_process(AVPicture *pict, enum PixelFormat pix_fmt, int width, int height, int64_t pts)
{
    if (first_hook) {
        FrameHookEntry *fhe;

        for (fhe = first_hook; fhe; fhe = fhe->next) {
            fhe->Process(fhe->ctx, pict, pix_fmt, width, height, pts);
        }
    }
}

void frame_hook_release(void)
{
    FrameHookEntry *fhe;
    FrameHookEntry *fhenext;

    for (fhe = first_hook; fhe; fhe = fhenext) {
        fhenext = fhe->next;
        if (fhe->Release)
            fhe->Release(fhe->ctx);
        av_free(fhe);
    }

    first_hook = NULL;
}
