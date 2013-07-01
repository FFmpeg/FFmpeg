/*
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

#ifndef AVFILTER_THREAD_H
#define AVFILTER_THREAD_H

#include "avfilter.h"

typedef int (action_func)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);

int ff_graph_thread_init(AVFilterGraph *graph);

void ff_graph_thread_free(AVFilterGraph *graph);

#endif /* AVFILTER_THREAD_H */
