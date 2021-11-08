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

#include "vulkan.h"

/**
 * Un/initialize glslang's global state. Thread-safe and reference counted.
 */
int ff_vk_glslang_init(void);
void ff_vk_glslang_uninit(void);

/**
 * Compile GLSL into SPIR-V using glslang.
 */
int ff_vk_glslang_shader_compile(AVFilterContext *avctx, FFSPIRVShader *shd,
                                 uint8_t **data, size_t *size, void **opaque);

/**
 * Frees the shader-specific context.
 */
void ff_vk_glslang_shader_free(void *opaque);

#endif /* AVFILTER_GLSLANG_H */
