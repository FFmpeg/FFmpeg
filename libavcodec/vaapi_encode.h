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

#ifndef AVCODEC_VAAPI_ENCODE_H
#define AVCODEC_VAAPI_ENCODE_H

#include <stdint.h>

#include <va/va.h>

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_vaapi.h"

#include "avcodec.h"

struct VAAPIEncodeType;
struct VAAPIEncodePicture;

enum {
    MAX_CONFIG_ATTRIBUTES  = 4,
    MAX_GLOBAL_PARAMS      = 4,
    MAX_PICTURE_REFERENCES = 2,
    MAX_REORDER_DELAY      = 16,
    MAX_PARAM_BUFFER_SIZE  = 1024,
};

enum {
    PICTURE_TYPE_IDR = 0,
    PICTURE_TYPE_I   = 1,
    PICTURE_TYPE_P   = 2,
    PICTURE_TYPE_B   = 3,
};

typedef struct VAAPIEncodeSlice {
    int             index;
    void           *priv_data;
    void           *codec_slice_params;
} VAAPIEncodeSlice;

typedef struct VAAPIEncodePicture {
    struct VAAPIEncodePicture *next;

    int64_t         display_order;
    int64_t         encode_order;
    int64_t         pts;

    int             type;
    int             input_available;
    int             encode_issued;
    int             encode_complete;

    AVFrame        *input_image;
    VASurfaceID     input_surface;

    AVFrame        *recon_image;
    VASurfaceID     recon_surface;

    int          nb_param_buffers;
    VABufferID     *param_buffers;

    AVBufferRef    *output_buffer_ref;
    VABufferID      output_buffer;

    void           *priv_data;
    void           *codec_picture_params;

    int          nb_refs;
    struct VAAPIEncodePicture *refs[MAX_PICTURE_REFERENCES];

    int          nb_slices;
    VAAPIEncodeSlice *slices;
} VAAPIEncodePicture;

typedef struct VAAPIEncodeContext {
    const AVClass *class;

    // Codec-specific hooks.
    const struct VAAPIEncodeType *codec;

    // Encoding profile (VAProfileXXX).
    VAProfile       va_profile;
    // Encoding entrypoint (usually VAEntryointEncSlice).
    VAEntrypoint    va_entrypoint;
    // Surface colour/sampling format (usually VA_RT_FORMAT_YUV420).
    unsigned int    va_rt_format;
    // Rate control mode.
    unsigned int    va_rc_mode;
    // Supported packed headers (initially the desired set, modified
    // later to what is actually supported).
    unsigned int    va_packed_headers;

    // The required size of surfaces.  This is probably the input
    // size (AVCodecContext.width|height) aligned up to whatever
    // block size is required by the codec.
    int             surface_width;
    int             surface_height;

    // Everything above this point must be set before calling
    // ff_vaapi_encode_init().

    // Configuration attributes to use when creating va_config.
    VAConfigAttrib  config_attributes[MAX_CONFIG_ATTRIBUTES];
    int          nb_config_attributes;

    VAConfigID      va_config;
    VAContextID     va_context;

    AVBufferRef    *device_ref;
    AVHWDeviceContext *device;
    AVVAAPIDeviceContext *hwctx;

    // The hardware frame context containing the input frames.
    AVBufferRef    *input_frames_ref;
    AVHWFramesContext *input_frames;

    // The hardware frame context containing the reconstructed frames.
    AVBufferRef    *recon_frames_ref;
    AVHWFramesContext *recon_frames;

    // Pool of (reusable) bitstream output buffers.
    AVBufferPool   *output_buffer_pool;

    // Global parameters which will be applied at the start of the
    // sequence (includes rate control parameters below).
    VAEncMiscParameterBuffer *global_params[MAX_GLOBAL_PARAMS];
    size_t          global_params_size[MAX_GLOBAL_PARAMS];
    int          nb_global_params;

    // Rate control parameters.
    struct {
        VAEncMiscParameterBuffer misc;
        VAEncMiscParameterRateControl rc;
    } rc_params;
    struct {
        VAEncMiscParameterBuffer misc;
        VAEncMiscParameterHRD hrd;
    } hrd_params;
    struct {
        VAEncMiscParameterBuffer misc;
        VAEncMiscParameterFrameRate fr;
    } fr_params;
#if VA_CHECK_VERSION(0, 36, 0)
    struct {
        VAEncMiscParameterBuffer misc;
        VAEncMiscParameterBufferQualityLevel quality;
    } quality_params;
#endif

    // Per-sequence parameter structure (VAEncSequenceParameterBuffer*).
    void           *codec_sequence_params;

    // Per-sequence parameters found in the per-picture parameter
    // structure (VAEncPictureParameterBuffer*).
    void           *codec_picture_params;

    // Current encoding window, in display (input) order.
    VAAPIEncodePicture *pic_start, *pic_end;

    // Next input order index (display order).
    int64_t         input_order;
    // Number of frames that output is behind input.
    int64_t         output_delay;
    // Number of frames decode output will need to be delayed.
    int64_t         decode_delay;
    // Next output order index (encode order).
    int64_t         output_order;

    enum {
        // All encode operations are done independently (synchronise
        // immediately after every operation).
        ISSUE_MODE_SERIALISE_EVERYTHING = 0,
        // Overlap as many operations as possible.
        ISSUE_MODE_MAXIMISE_THROUGHPUT,
        // Overlap operations only when satisfying parallel dependencies.
        ISSUE_MODE_MINIMISE_LATENCY,
    } issue_mode;

    // Timestamp handling.
    int64_t         first_pts;
    int64_t         dts_pts_diff;
    int64_t         ts_ring[MAX_REORDER_DELAY * 3];

    // Frame type decision.
    int p_per_i;
    int b_per_p;
    int force_idr;
    int gop_counter;
    int p_counter;
    int end_of_stream;
} VAAPIEncodeContext;


typedef struct VAAPIEncodeType {
    // Perform any extra codec-specific configuration after the
    // codec context is initialised (set up the private data and
    // add any necessary global parameters).
    int (*configure)(AVCodecContext *avctx);

    // The size of the parameter structures:
    // sizeof(VAEnc{type}ParameterBuffer{codec}).
    size_t sequence_params_size;
    size_t picture_params_size;
    size_t slice_params_size;

    // Fill the parameter structures.
    int  (*init_sequence_params)(AVCodecContext *avctx);
    int   (*init_picture_params)(AVCodecContext *avctx,
                                 VAAPIEncodePicture *pic);
    int     (*init_slice_params)(AVCodecContext *avctx,
                                 VAAPIEncodePicture *pic,
                                 VAAPIEncodeSlice *slice);

    // The type used by the packed header: this should look like
    // VAEncPackedHeader{something}.
    int sequence_header_type;
    int picture_header_type;
    int slice_header_type;

    // Write the packed header data to the provided buffer.
    // The sequence header is also used to fill the codec extradata
    // when the encoder is starting.
    int (*write_sequence_header)(AVCodecContext *avctx,
                                 char *data, size_t *data_len);
    int  (*write_picture_header)(AVCodecContext *avctx,
                                 VAAPIEncodePicture *pic,
                                 char *data, size_t *data_len);
    int    (*write_slice_header)(AVCodecContext *avctx,
                                 VAAPIEncodePicture *pic,
                                 VAAPIEncodeSlice *slice,
                                 char *data, size_t *data_len);

    // Fill an extra parameter structure, which will then be
    // passed to vaRenderPicture().  Will be called repeatedly
    // with increasing index argument until AVERROR_EOF is
    // returned.
    int    (*write_extra_buffer)(AVCodecContext *avctx,
                                 VAAPIEncodePicture *pic,
                                 int index, int *type,
                                 char *data, size_t *data_len);

    // Write an extra packed header.  Will be called repeatedly
    // with increasing index argument until AVERROR_EOF is
    // returned.
    int    (*write_extra_header)(AVCodecContext *avctx,
                                 VAAPIEncodePicture *pic,
                                 int index, int *type,
                                 char *data, size_t *data_len);
} VAAPIEncodeType;


int ff_vaapi_encode2(AVCodecContext *avctx, AVPacket *pkt,
                     const AVFrame *input_image, int *got_packet);

int ff_vaapi_encode_init(AVCodecContext *avctx);
int ff_vaapi_encode_close(AVCodecContext *avctx);

#endif /* AVCODEC_VAAPI_ENCODE_H */
