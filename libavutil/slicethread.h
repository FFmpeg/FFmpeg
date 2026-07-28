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

#ifndef AVUTIL_SLICETHREAD_H
#define AVUTIL_SLICETHREAD_H

#include "attributes.h"
#include "version.h"

typedef struct AVSliceThread AVSliceThread;

/**
 * Create slice threading context.
 * @param pctx slice threading context returned here
 * @param priv private pointer to be passed to callback function
 * @param worker_func callback function to be executed
 * @param main_func special callback function, called from main thread, may be NULL
 * @param nb_threads number of threads, 0 for automatic, must be >= 0
 * @return return number of threads or negative AVERROR on failure
 */
int avpriv_slicethread_create2(AVSliceThread **pctx, void *priv,
                               int (*worker_func)(void *priv, int jobnr, int threadnr, int nb_jobs, int nb_threads),
                               int (*main_func)(void *priv),
                               int nb_threads);

/**
 * Execute slice threading. If any job returns a nonzero value,
 * all remaining jobs will be aborted and that value returned.
 *
 * If `main_func` itself returns a nonzero value, that value is guaranteed
 * to be returned, even if a worker thread returns a nonzero value first.
 *
 * @param ctx slice threading context
 * @param nb_jobs number of jobs, must be > 0
 * @param execute_main also execute main_func
 * @return 0 on success, else first seen return value
 */
int avpriv_slicethread_execute2(AVSliceThread *ctx, int nb_jobs, int execute_main);

/**
 * Destroy slice threading context.
 * @param pctx pointer to context
 */
void avpriv_slicethread_free(AVSliceThread **pctx);

#if LIBAVUTIL_VERSION_MAJOR < 62
/**
 * @deprecated
 * Wrapper around avpriv_slicethread_create2() for ABI backwards compatibility.
 */
attribute_deprecated
int avpriv_slicethread_create(AVSliceThread **pctx, void *priv,
                              void (*worker_func)(void *priv, int jobnr, int threadnr, int nb_jobs, int nb_threads),
                              void (*main_func)(void *priv),
                              int nb_threads);

/**
 * @deprecated
 * Wrapper around avpriv_slicethread_execute2() for ABI backwards compatibility.
 */
attribute_deprecated
void avpriv_slicethread_execute(AVSliceThread *ctx, int nb_jobs, int execute_main);
#endif /* LIBAVUTIL_VERSION_MAJOR < 62 */

#endif
