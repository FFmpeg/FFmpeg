/*
 * Dolby Vision RPU decoder
 *
 * Copyright (C) 2021 Jan Ekström
 * Copyright (C) 2021 Niklas Haas
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

#ifndef AVCODEC_DOVI_RPU_H
#define AVCODEC_DOVI_RPU_H

#include "libavutil/dovi_meta.h"
#include "libavutil/frame.h"

#define DOVI_MAX_DM_ID 15
typedef struct DOVIContext {
    void *logctx;

    /**
     * Currently active RPU data header, updates on every dovi_rpu_parse().
     */
    AVDOVIRpuDataHeader header;

    /**
     * Currently active data mappings, or NULL. Points into memory owned by the
     * corresponding rpu/vdr_ref, which becomes invalid on the next call to
     * dovi_rpu_parse.
     */
    const AVDOVIDataMapping *mapping;
    const AVDOVIColorMetadata *color;

    /**
     * Private fields internal to dovi_rpu.c
     */
    AVBufferRef *vdr_ref[DOVI_MAX_DM_ID+1];
    uint8_t dv_profile;

} DOVIContext;

int ff_dovi_ctx_replace(DOVIContext *s, const DOVIContext *s0);

/**
 * Completely reset a DOVIContext, preserving only logctx.
 */
void ff_dovi_ctx_unref(DOVIContext *s);

/**
 * Partially reset the internal state. Resets per-frame state while preserving
 * fields parsed from the configuration record.
 */
void ff_dovi_ctx_flush(DOVIContext *s);

/**
 * Read the contents of an AVDOVIDecoderConfigurationRecord (usually provided
 * by stream side data) and update internal state accordingly.
 */
void ff_dovi_update_cfg(DOVIContext *s, const AVDOVIDecoderConfigurationRecord *cfg);

/**
 * Parse the contents of a Dovi RPU NAL and update the parsed values in the
 * DOVIContext struct.
 *
 * Returns 0 or an error code.
 */
int ff_dovi_rpu_parse(DOVIContext *s, const uint8_t *rpu, size_t rpu_size);

/**
 * Attach the decoded AVDOVIMetadata as side data to an AVFrame.
 */
int ff_dovi_attach_side_data(DOVIContext *s, AVFrame *frame);

#endif /* AVCODEC_DOVI_RPU_H */
