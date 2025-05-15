/*
 * Copyright (c) 2025 - softworkz
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

#ifndef FFTOOLS_RESOURCES_RESMAN_H
#define FFTOOLS_RESOURCES_RESMAN_H

#include <stdint.h>

#include "config.h"
#include "fftools/ffmpeg.h"
#include "libavutil/avutil.h"
#include "libavutil/bprint.h"
#include "fftools/textformat/avtextformat.h"

typedef enum {
    FF_RESOURCE_GRAPH_CSS,
    FF_RESOURCE_GRAPH_HTML,
} FFResourceId;

typedef struct FFResourceDefinition {
    FFResourceId resource_id;
    const char *name;

    const unsigned char *data;
    const unsigned *data_len;

} FFResourceDefinition;

void ff_resman_uninit(void);

char *ff_resman_get_string(FFResourceId resource_id);

#endif /* FFTOOLS_RESOURCES_RESMAN_H */
