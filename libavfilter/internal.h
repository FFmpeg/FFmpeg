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

#ifndef AVFILTER_INTERNAL_H
#define AVFILTER_INTERNAL_H

/**
 * @file
 * internal API functions
 */

#include "avfilter.h"

void ff_dprintf_ref(void *ctx, AVFilterBufferRef *ref, int end);

char *ff_get_ref_perms_string(char *buf, size_t buf_size, int perms);

void ff_dprintf_link(void *ctx, AVFilterLink *link, int end);

#define FF_DPRINTF_START(ctx, func) dprintf(NULL, "%-16s: ", #func)

#endif  /* AVFILTER_INTERNAL_H */
