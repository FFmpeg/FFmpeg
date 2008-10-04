/*
 * AAC encoder psychoacoustic model
 * Copyright (C) 2008 Konstantin Shishkov
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

#ifndef AVCODEC_AACPSY_H
#define AVCODEC_AACPSY_H

#include "avcodec.h"
#include "aac.h"
//#include "lowpass.h"

enum AACPsyModelType{
    AAC_PSY_TEST,              ///< a sample model to exercise encoder
    AAC_PSY_3GPP,              ///< model following recommendations from 3GPP TS 26.403

    AAC_NB_PSY_MODELS          ///< total number of psychoacoustic models, since it's not a part of the ABI new models can be added freely
};

/**
 * context used by psychoacoustic model
 */
typedef struct AACPsyContext {
    AVCodecContext *avctx;            ///< encoder context
}AACPsyContext;

/**
 * Cleanup model context at the end.
 *
 * @param ctx model context
 */
void ff_aac_psy_end(AACPsyContext *ctx);

#endif /* AVCODEC_AACPSY_H */
