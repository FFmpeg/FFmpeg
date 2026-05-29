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

#ifndef AVCODEC_ITUT35_H
#define AVCODEC_ITUT35_H

#include <stdint.h>
#include <stddef.h>

#include "libavutil/frame.h"
#include "avcodec.h"
#include "aom_film_grain.h"
#include "dovi_rpu.h"

#define ITU_T_T35_COUNTRY_CODE_CN 0x26
#define ITU_T_T35_COUNTRY_CODE_UK 0xB4
#define ITU_T_T35_COUNTRY_CODE_US 0xB5

// The Terminal Provider Code (or "Manufacturer Code") identifies the
// manufacturer within a country. An Assignment Authority appointed by the
// national body assigns this code nationally. The manufacturer code is always
// used in conjunction with a country code.
// - CN providers
#define ITU_T_T35_PROVIDER_CODE_HDR_VIVID    0x0004
// - UK providers
// https://www.cix.co.uk/~bpechey/H221/h221code.htm
#define ITU_T_T35_PROVIDER_CODE_VNOVA        0x5000
// - US providers
#define ITU_T_T35_PROVIDER_CODE_ATSC         0x0031
#define ITU_T_T35_PROVIDER_CODE_DOLBY        0x003B
#define ITU_T_T35_PROVIDER_CODE_AOM          0x5890
#define ITU_T_T35_PROVIDER_CODE_SAMSUNG      0x003C
#define ITU_T_T35_PROVIDER_CODE_SMPTE        0x0090

typedef struct FFITUTT35 {
    int country_code;

    int provider_code;
    unsigned int provider_oriented_code;

    const uint8_t *payload;
    size_t payload_size;
} FFITUTT35;

typedef struct FFITUTT35Meta {
    AVBufferRef *afd;
    AVBufferRef *a53_cc;
    AVBufferRef *lcevc;
    AVBufferRef *hdr_plus;
    AVBufferRef *hdr_smpte2094_app5;
    AVBufferRef *hdr_vivid;
    AVBufferRef *dovi;
    AVFilmGrainAFGS1Params aom_film_grain;
} FFITUTT35Meta;

typedef struct FFITUTT35Aux {
    /**
     * A DOVIContext. Must be set to a valid pointer in order to be parsed
     * and filled.
     */
    DOVIContext *dovi;
} FFITUTT35Aux;

/**
 * country_code is assumed to not be the first byte of the buffer and must
 * be set by the caller beforehand.
 */
#define FF_ITUT_T35_FLAG_COUNTRY_CODE (1 << 0)

/**
 * Parse a raw ITU-T T35 buffer to get the country code, provider code,
 * and set them plus the pointer and size in the FFITUTT35 struct to the
 * start of the actual payload.
 *
 * @param  itut_t35 The struct to fill
 * @param  buf      The input buffer
 * @param  size     Size of the input buffer
 * @param  flags    A combination of FF_ITUT_T35_FLAG_*
 * @return          0 if nothing was done (e.g. the payload is of an unsupported
 *                  type), 1 on success, or a negative AVERROR code on failure
 *
 * @note buf will remain owned by the caller, and no new allocations will
 *       be made. Any pointer in the resulting struct will be valid as long
 *       as buf is valid.
 */
int ff_itut_t35_parse_buffer(FFITUTT35 *itut_t35, const uint8_t *buf,
                             size_t size, int flags);

/**
 * Parse a pre-processed ITU-T T35 payload to fill the metadata struct.
 *
 * @param  itut_t35        The pre-filled struct
 * @param  aux             A struct containing extra contexts required by certain
 *                         payload types. Any pointer present is owned by the caller.
 *                         May be NULL, in which case the relevant payloads will not
 *                         be parsed.
 * @param  metadata        A metadata struct. All the allocated buffer references
 *                         are owned by the caller and must be freed accordingly.
 * @param  err_recognition A combination of AV_EF_* flags
 * @return                 0 on success, or a negative AVERROR code on failure
 */
int ff_itut_t35_parse_payload_to_struct(FFITUTT35 *itut_t35, FFITUTT35Aux *aux,
                                        FFITUTT35Meta *metadata, int err_recognition);

/**
 * Parse a pre-processed ITU-T T35 payload to fill a frame's side data.
 *
 * @param  itut_t35        The pre-filled struct
 * @param  aux             A struct containing extra contexts required by certain
 *                         payload types. Any pointer present is owned by the caller.
 *                         May be NULL, in which case the relevant payloads will not
 *                         be parsed.
 * @param  avctx           The context that generated the frame
 * @param  frame           A frame
 * @return                 0 on success, or a negative AVERROR code on failure
 */
int ff_itut_t35_parse_payload_to_frame(FFITUTT35 *itut_t35, FFITUTT35Aux *aux,
                                       AVCodecContext *avctx, AVFrame *frame);

/**
 * Unref all references in metadata
 */
void ff_itut_t35_unref(FFITUTT35Meta *metadata);

#endif /* AVCODEC_ITUT35_H */
