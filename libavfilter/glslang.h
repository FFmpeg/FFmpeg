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

#ifndef AVFILTER_GLSLANG_H
#define AVFILTER_GLSLANG_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

int glslang_init(void);
void glslang_uninit(void);

typedef struct GLSlangResult {
    int rval;
    char *error_msg;

    void *data; /* Shader data or NULL */
    size_t size;
} GLSlangResult;

enum GLSlangStage {
    GLSLANG_VERTEX,
    GLSLANG_FRAGMENT,
    GLSLANG_COMPUTE,
};

/* Compile GLSL into a SPIRV stream, if possible */
GLSlangResult *glslang_compile(const char *glsl, enum GLSlangStage stage);

#ifdef __cplusplus
}
#endif

#endif /* AVFILTER_GLSLANG_H */
