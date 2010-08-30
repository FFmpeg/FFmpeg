/*
 * audio encoder psychoacoustic model
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

#ifndef AVCODEC_PSYMODEL_H
#define AVCODEC_PSYMODEL_H

#include "avcodec.h"

/** maximum possible number of bands */
#define PSY_MAX_BANDS 128

/**
 * single band psychoacoustic information
 */
typedef struct FFPsyBand {
    int   bits;
    float energy;
    float threshold;
    float distortion;
    float perceptual_weight;
} FFPsyBand;

/**
 * windowing related information
 */
typedef struct FFPsyWindowInfo {
    int window_type[3];               ///< window type (short/long/transitional, etc.) - current, previous and next
    int window_shape;                 ///< window shape (sine/KBD/whatever)
    int num_windows;                  ///< number of windows in a frame
    int grouping[8];                  ///< window grouping (for e.g. AAC)
    int *window_sizes;                ///< sequence of window sizes inside one frame (for eg. WMA)
} FFPsyWindowInfo;

/**
 * context used by psychoacoustic model
 */
typedef struct FFPsyContext {
    AVCodecContext *avctx;            ///< encoder context
    const struct FFPsyModel *model;   ///< encoder-specific model functions

    FFPsyBand *psy_bands;             ///< frame bands information

    uint8_t **bands;                  ///< scalefactor band sizes for possible frame sizes
    int     *num_bands;               ///< number of scalefactor bands for possible frame sizes
    int num_lens;                     ///< number of scalefactor band sets

    void* model_priv_data;            ///< psychoacoustic model implementation private data
} FFPsyContext;

/**
 * codec-specific psychoacoustic model implementation
 */
typedef struct FFPsyModel {
    const char *name;
    int  (*init)   (FFPsyContext *apc);
    FFPsyWindowInfo (*window)(FFPsyContext *ctx, const int16_t *audio, const int16_t *la, int channel, int prev_type);
    void (*analyze)(FFPsyContext *ctx, int channel, const float *coeffs, const FFPsyWindowInfo *wi);
    void (*end)    (FFPsyContext *apc);
} FFPsyModel;

/**
 * Initialize psychoacoustic model.
 *
 * @param ctx        model context
 * @param avctx      codec context
 * @param num_lens   number of possible frame lengths
 * @param bands      scalefactor band lengths for all frame lengths
 * @param num_bands  number of scalefactor bands for all frame lengths
 *
 * @return zero if successful, a negative value if not
 */
av_cold int ff_psy_init(FFPsyContext *ctx, AVCodecContext *avctx,
                        int num_lens,
                        const uint8_t **bands, const int* num_bands);

/**
 * Suggest window sequence for channel.
 *
 * @param ctx       model context
 * @param audio     samples for the current frame
 * @param la        lookahead samples (NULL when unavailable)
 * @param channel   number of channel element to analyze
 * @param prev_type previous window type
 *
 * @return suggested window information in a structure
 */
FFPsyWindowInfo ff_psy_suggest_window(FFPsyContext *ctx,
                                      const int16_t *audio, const int16_t *la,
                                      int channel, int prev_type);


/**
 * Perform psychoacoustic analysis and set band info (threshold, energy).
 *
 * @param ctx     model context
 * @param channel audio channel number
 * @param coeffs  pointer to the transformed coefficients
 * @param wi      window information
 */
void ff_psy_set_band_info(FFPsyContext *ctx, int channel, const float *coeffs,
                          const FFPsyWindowInfo *wi);

/**
 * Cleanup model context at the end.
 *
 * @param ctx model context
 */
av_cold void ff_psy_end(FFPsyContext *ctx);


/**************************************************************************
 *                       Audio preprocessing stuff.                       *
 *       This should be moved into some audio filter eventually.          *
 **************************************************************************/
struct FFPsyPreprocessContext;

/**
 * psychoacoustic model audio preprocessing initialization
 */
av_cold struct FFPsyPreprocessContext* ff_psy_preprocess_init(AVCodecContext *avctx);

/**
 * Preprocess several channel in audio frame in order to compress it better.
 *
 * @param ctx      preprocessing context
 * @param audio    samples to preprocess
 * @param dest     place to put filtered samples
 * @param tag      channel number
 * @param channels number of channel to preprocess (some additional work may be done on stereo pair)
 */
void ff_psy_preprocess(struct FFPsyPreprocessContext *ctx,
                       const int16_t *audio, int16_t *dest,
                       int tag, int channels);

/**
 * Cleanup audio preprocessing module.
 */
av_cold void ff_psy_preprocess_end(struct FFPsyPreprocessContext *ctx);

#endif /* AVCODEC_PSYMODEL_H */
