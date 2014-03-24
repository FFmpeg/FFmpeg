/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_VDA_INTERNAL_H
#define AVCODEC_VDA_INTERNAL_H

#include "vda.h"

void ff_vda_output_callback(void *vda_hw_ctx,
                            CFDictionaryRef user_info,
                            OSStatus status,
                            uint32_t infoFlags,
                            CVImageBufferRef image_buffer);

int ff_vda_default_init(AVCodecContext *avctx);
void ff_vda_default_free(AVCodecContext *avctx);

#endif /* AVCODEC_VDA_INTERNAL_H */
