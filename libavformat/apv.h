/*
 * APV helper functions for muxers
 * Copyright (c) 2025 Dawid Kozinski <d.kozinski@samsung.com>
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

#ifndef AVFORMAT_APV_H
#define AVFORMAT_APV_H

#include <stdint.h>

#include "avio.h"

struct APVDecoderConfigurationRecord;
struct AVPacket;

/**
 * Writes APV sample metadata to the provided AVIOContext.
 *
 * @param pb pointer to the AVIOContext where the apv sample metadata shall be written
 * @param buf input data buffer
 * @param size size in bytes of the input data buffer
 *
 * @return 0 in case of success, a negative error code in case of failure
 */
void ff_isom_write_apvc(AVIOContext *pb, const struct APVDecoderConfigurationRecord *apvc,
                        void *logctx);

int ff_isom_init_apvc(struct APVDecoderConfigurationRecord **papvc, void *logctx);
int ff_isom_parse_apvc(struct APVDecoderConfigurationRecord *apvc,
                       const struct AVPacket *pkt, void *logctx);
void ff_isom_close_apvc(struct APVDecoderConfigurationRecord **papvc);

#endif // AVFORMAT_APV_H
