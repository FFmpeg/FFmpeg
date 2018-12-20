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

#if VA_CHECK_VERSION(1, 0, 0)
#include <va/va_str.h>
#endif

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_vaapi.h"

#include "avcodec.h"

struct VAAPIEncodeType;
struct VAAPIEncodePicture;

enum {
    MAX_CONFIG_ATTRIBUTES  = 4,
    MAX_GLOBAL_PARAMS      = 4,
    MAX_DPB_SIZE           = 16,
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
    int             row_start;
    int             row_size;
    int             block_start;
    int             block_size;
    void           *priv_data;
    void           *codec_slice_params;
} VAAPIEncodeSlice;

typedef struct VAAPIEncodePicture {
    struct VAAPIEncodePicture *next;

    int64_t         display_order;
    int64_t         encode_order;
    int64_t         pts;
    int             force_idr;

    int             type;
    int             b_depth;
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

    // Whether this picture is a reference picture.
    int             is_reference;

    // The contents of the DPB after this picture has been decoded.
    // This will contain the picture itself if it is a reference picture,
    // but not if it isn't.
    int                     nb_dpb_pics;
    struct VAAPIEncodePicture *dpb[MAX_DPB_SIZE];
    // The reference pictures used in decoding this picture.  If they are
    // used by later pictures they will also appear in the DPB.
    int                     nb_refs;
    struct VAAPIEncodePicture *refs[MAX_PICTURE_REFERENCES];
    // The previous reference picture in encode order.  Must be in at least
    // one of the reference list and DPB list.
    struct VAAPIEncodePicture *prev;
    // Reference count for other pictures referring to this one through
    // the above pointers, directly from incomplete pictures and indirectly
    // through completed pictures.
    int             ref_count[2];
    int             ref_removed[2];

    int          nb_slices;
    VAAPIEncodeSlice *slices;
} VAAPIEncodePicture;

typedef struct VAAPIEncodeProfile {
    // lavc profile value (FF_PROFILE_*).
    int       av_profile;
    // Supported bit depth.
    int       depth;
    // Number of components.
    int       nb_components;
    // Chroma subsampling in width dimension.
    int       log2_chroma_w;
    // Chroma subsampling in height dimension.
    int       log2_chroma_h;
    // VAAPI profile value.
    VAProfile va_profile;
} VAAPIEncodeProfile;

typedef struct VAAPIEncodeContext {
    const AVClass *class;

    // Codec-specific hooks.
    const struct VAAPIEncodeType *codec;

    // Global options.

    // Use low power encoding mode.
    int             low_power;

    // Number of I frames between IDR frames.
    int             idr_interval;

    // Desired B frame reference depth.
    int             desired_b_depth;

    // Desired packed headers.
    unsigned int    desired_packed_headers;

    // The required size of surfaces.  This is probably the input
    // size (AVCodecContext.width|height) aligned up to whatever
    // block size is required by the codec.
    int             surface_width;
    int             surface_height;

    // The block size for slice calculations.
    int             slice_block_width;
    int             slice_block_height;

    // Everything above this point must be set before calling
    // ff_vaapi_encode_init().

    // Chosen encoding profile details.
    const VAAPIEncodeProfile *profile;

    // Encoding profile (VAProfile*).
    VAProfile       va_profile;
    // Encoding entrypoint (VAEntryoint*).
    VAEntrypoint    va_entrypoint;
    // Rate control mode.
    unsigned int    va_rc_mode;
    // Bitrate for codec-specific encoder parameters.
    unsigned int    va_bit_rate;
    // Packed headers which will actually be sent.
    unsigned int    va_packed_headers;

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
    // The next picture to use as the previous reference picture in
    // encoding order.
    VAAPIEncodePicture *next_prev;

    // Next input order index (display order).
    int64_t         input_order;
    // Number of frames that output is behind input.
    int64_t         output_delay;
    // Next encode order index.
    int64_t         encode_order;
    // Number of frames decode output will need to be delayed.
    int64_t         decode_delay;
    // Next output order index (in encode order).
    int64_t         output_order;

    // Timestamp handling.
    int64_t         first_pts;
    int64_t         dts_pts_diff;
    int64_t         ts_ring[MAX_REORDER_DELAY * 3];

    // Slice structure.
    int slice_block_rows;
    int slice_block_cols;
    int nb_slices;
    int slice_size;

    // Frame type decision.
    int gop_size;
    int closed_gop;
    int gop_per_idr;
    int p_per_i;
    int max_b_depth;
    int b_per_p;
    int force_idr;
    int idr_counter;
    int gop_counter;
    int end_of_stream;
} VAAPIEncodeContext;

enum {
    // Codec supports controlling the subdivision of pictures into slices.
    FLAG_SLICE_CONTROL         = 1 << 0,
    // Codec only supports constant quality (no rate control).
    FLAG_CONSTANT_QUALITY_ONLY = 1 << 1,
    // Codec is intra-only.
    FLAG_INTRA_ONLY            = 1 << 2,
    // Codec supports B-pictures.
    FLAG_B_PICTURES            = 1 << 3,
    // Codec supports referencing B-pictures.
    FLAG_B_PICTURE_REFERENCES  = 1 << 4,
    // Codec supports non-IDR key pictures (that is, key pictures do
    // not necessarily empty the DPB).
    FLAG_NON_IDR_KEY_PICTURES  = 1 << 5,
};

typedef struct VAAPIEncodeType {
    // List of supported profiles and corresponding VAAPI profiles.
    // (Must end with FF_PROFILE_UNKNOWN.)
    const VAAPIEncodeProfile *profiles;

    // Codec feature flags.
    int flags;

    // Perform any extra codec-specific configuration after the
    // codec context is initialised (set up the private data and
    // add any necessary global parameters).
    int (*configure)(AVCodecContext *avctx);

    // The size of any private data structure associated with each
    // picture (can be zero if not required).
    size_t picture_priv_data_size;

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

int ff_vaapi_encode_send_frame(AVCodecContext *avctx, const AVFrame *frame);
int ff_vaapi_encode_receive_packet(AVCodecContext *avctx, AVPacket *pkt);

int ff_vaapi_encode_init(AVCodecContext *avctx);
int ff_vaapi_encode_close(AVCodecContext *avctx);


#define VAAPI_ENCODE_COMMON_OPTIONS \
    { "low_power", \
      "Use low-power encoding mode (only available on some platforms; " \
      "may not support all encoding features)", \
      OFFSET(common.low_power), AV_OPT_TYPE_BOOL, \
      { .i64 = 0 }, 0, 1, FLAGS }, \
    { "idr_interval", \
      "Distance (in I-frames) between IDR frames", \
      OFFSET(common.idr_interval), AV_OPT_TYPE_INT, \
      { .i64 = 0 }, 0, INT_MAX, FLAGS }, \
    { "b_depth", \
      "Maximum B-frame reference depth", \
      OFFSET(common.desired_b_depth), AV_OPT_TYPE_INT, \
      { .i64 = 1 }, 1, INT_MAX, FLAGS }


#endif /* AVCODEC_VAAPI_ENCODE_H */
