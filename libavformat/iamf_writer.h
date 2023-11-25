/*
 * Immersive Audio Model and Formats muxing helpers and structs
 * Copyright (c) 2023 James Almer <jamrial@gmail.com>
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

#ifndef AVFORMAT_IAMF_WRITER_H
#define AVFORMAT_IAMF_WRITER_H

#include <stdint.h>

#include "libavutil/common.h"
#include "avformat.h"
#include "avio.h"
#include "iamf.h"

static inline IAMFParamDefinition *ff_iamf_get_param_definition(const IAMFContext *iamf,
                                                                unsigned int parameter_id)
{
    IAMFParamDefinition *param_definition = NULL;

    for (int i = 0; i < iamf->nb_param_definitions; i++)
        if (iamf->param_definitions[i]->param->parameter_id == parameter_id) {
            param_definition = iamf->param_definitions[i];
            break;
        }

    return param_definition;
}

int ff_iamf_add_audio_element(IAMFContext *iamf, const AVStreamGroup *stg, void *log_ctx);
int ff_iamf_add_mix_presentation(IAMFContext *iamf, const AVStreamGroup *stg, void *log_ctx);

int ff_iamf_write_descriptors(const IAMFContext *iamf, AVIOContext *pb, void *log_ctx);

#endif /* AVFORMAT_IAMF_WRITER_H */
