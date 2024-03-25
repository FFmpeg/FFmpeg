/*
 * AAC encoder wrapper
 * Copyright (c) 2012 Martin Storsjo
 *
 * This file is part of FFmpeg.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <fdk-aac/aacenc_lib.h>

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "audio_frame_queue.h"
#include "codec_internal.h"
#include "encode.h"
#include "profiles.h"

#ifdef AACENCODER_LIB_VL0
#define FDKENC_VER_AT_LEAST(vl0, vl1) \
    ((AACENCODER_LIB_VL0 > vl0) || \
     (AACENCODER_LIB_VL0 == vl0 && AACENCODER_LIB_VL1 >= vl1))
#else
#define FDKENC_VER_AT_LEAST(vl0, vl1) 0
#endif

typedef struct AACContext {
    const AVClass *class;
    HANDLE_AACENCODER handle;
    int afterburner;
    int eld_sbr;
    int eld_v2;
    int signaling;
    int latm;
    int header_period;
    int vbr;
    int drc_profile;
    int drc_target_ref;
    int comp_profile;
    int comp_target_ref;
    int prog_ref;
    int metadata_mode;
    AACENC_MetaData metaDataSetup;
    int delay_sent;
    int frame_length;

    AudioFrameQueue afq;
} AACContext;

static const AVOption aac_enc_options[] = {
    { "afterburner", "Afterburner (improved quality)", offsetof(AACContext, afterburner), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { "eld_sbr", "Enable SBR for ELD (for SBR in other configurations, use the -profile parameter)", offsetof(AACContext, eld_sbr), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
#if FDKENC_VER_AT_LEAST(4, 0) // 4.0.0
    { "eld_v2", "Enable ELDv2 (LD-MPS extension for ELD stereo signals)", offsetof(AACContext, eld_v2), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
#endif
    { "signaling", "SBR/PS signaling style", offsetof(AACContext, signaling), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 2, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM, .unit = "signaling" },
    { "default", "Choose signaling implicitly (explicit hierarchical by default, implicit if global header is disabled)", 0, AV_OPT_TYPE_CONST, { .i64 = -1 }, 0, 0, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM, .unit = "signaling" },
    { "implicit", "Implicit backwards compatible signaling", 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM, .unit = "signaling" },
    { "explicit_sbr", "Explicit SBR, implicit PS signaling", 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM, .unit = "signaling" },
    { "explicit_hierarchical", "Explicit hierarchical signaling", 0, AV_OPT_TYPE_CONST, { .i64 = 2 }, 0, 0, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM, .unit = "signaling" },
    { "latm", "Output LATM/LOAS encapsulated data", offsetof(AACContext, latm), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { "header_period", "StreamMuxConfig and PCE repetition period (in frames)", offsetof(AACContext, header_period), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 0xffff, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { "vbr", "VBR mode (1-5)", offsetof(AACContext, vbr), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 5, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { "drc_profile", "The desired compression profile for AAC DRC", offsetof(AACContext, drc_profile), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 256, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { "drc_target_ref", "Expected target reference level at decoder side in dB (for clipping prevention/limiter)", offsetof(AACContext, drc_target_ref), AV_OPT_TYPE_INT, { .i64 = 0.0 }, -31.75, 0, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { "comp_profile", "The desired compression profile for AAC DRC", offsetof(AACContext, comp_profile), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 256, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { "comp_target_ref", "Expected target reference level at decoder side in dB (for clipping prevention/limiter)", offsetof(AACContext, comp_target_ref), AV_OPT_TYPE_INT, { .i64 = 0.0 }, -31.75, 0, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { "prog_ref", "The program reference level or dialog level in dB", offsetof(AACContext, prog_ref), AV_OPT_TYPE_INT, { .i64 = 0.0 }, -31.75, 0, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { "frame_length", "The desired frame length", offsetof(AACContext, frame_length), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 1024, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    FF_AAC_PROFILE_OPTS
    { NULL }
};

static const AVClass aac_enc_class = {
    .class_name = "libfdk_aac",
    .item_name  = av_default_item_name,
    .option     = aac_enc_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const char *aac_get_error(AACENC_ERROR err)
{
    switch (err) {
    case AACENC_OK:
        return "No error";
    case AACENC_INVALID_HANDLE:
        return "Invalid handle";
    case AACENC_MEMORY_ERROR:
        return "Memory allocation error";
    case AACENC_UNSUPPORTED_PARAMETER:
        return "Unsupported parameter";
    case AACENC_INVALID_CONFIG:
        return "Invalid config";
    case AACENC_INIT_ERROR:
        return "Initialization error";
    case AACENC_INIT_AAC_ERROR:
        return "AAC library initialization error";
    case AACENC_INIT_SBR_ERROR:
        return "SBR library initialization error";
    case AACENC_INIT_TP_ERROR:
        return "Transport library initialization error";
    case AACENC_INIT_META_ERROR:
        return "Metadata library initialization error";
    case AACENC_ENCODE_ERROR:
        return "Encoding error";
    case AACENC_ENCODE_EOF:
        return "End of file";
    default:
        return "Unknown error";
    }
}

static int aac_encode_close(AVCodecContext *avctx)
{
    AACContext *s = avctx->priv_data;

    if (s->handle)
        aacEncClose(&s->handle);
    ff_af_queue_close(&s->afq);

    return 0;
}

static void aac_encode_flush(AVCodecContext *avctx)
{
    AACContext *s = avctx->priv_data;
    AACENC_BufDesc in_buf   = { 0 }, out_buf = { 0 };
    AACENC_InArgs  in_args  = { 0 };
    AACENC_OutArgs out_args;
    int64_t pts, duration;
    uint8_t dummy_in[1], dummy_out[1];
    int in_buffer_identifiers[] = { IN_AUDIO_DATA, IN_METADATA_SETUP };
    int in_buffer_element_sizes[] = { 2, sizeof(AACENC_MetaData) };
    int in_buffer_sizes[] = { 0, sizeof(s->metaDataSetup) };
    int out_buffer_identifier = OUT_BITSTREAM_DATA;
    int out_buffer_size = sizeof(dummy_out), out_buffer_element_size = 1;
    void* inBuffer[] = { dummy_in, &s->metaDataSetup };
    void *out_ptr = dummy_out;
    AACENC_ERROR err;

    ff_af_queue_remove(&s->afq, s->afq.frame_count, &pts, &duration);

    in_buf.bufs              = (void **)inBuffer;
    in_buf.numBufs           = s->metadata_mode == 0 ? 1 : 2;
    in_buf.bufferIdentifiers = in_buffer_identifiers;
    in_buf.bufSizes          = in_buffer_sizes;
    in_buf.bufElSizes        = in_buffer_element_sizes;

    out_buf.numBufs           = 1;
    out_buf.bufs              = &out_ptr;
    out_buf.bufferIdentifiers = &out_buffer_identifier;
    out_buf.bufSizes          = &out_buffer_size;
    out_buf.bufElSizes        = &out_buffer_element_size;

    err = aacEncEncode(s->handle, &in_buf, &out_buf, &in_args, &out_args);
    if (err != AACENC_OK) {
        av_log(avctx, AV_LOG_ERROR, "Unexpected error while flushing: %s\n",
               aac_get_error(err));
    }
}

static av_cold int aac_encode_init(AVCodecContext *avctx)
{
    AACContext *s = avctx->priv_data;
    int ret = AVERROR(EINVAL);
    AACENC_InfoStruct info = { 0 };
    CHANNEL_MODE mode;
    AACENC_ERROR err;
    int aot = AV_PROFILE_AAC_LOW + 1;
    int sce = 0, cpe = 0;

    if ((err = aacEncOpen(&s->handle, 0, avctx->ch_layout.nb_channels)) != AACENC_OK) {
        av_log(avctx, AV_LOG_ERROR, "Unable to open the encoder: %s\n",
               aac_get_error(err));
        goto error;
    }

    if (avctx->profile != AV_PROFILE_UNKNOWN)
        aot = avctx->profile + 1;

    if ((err = aacEncoder_SetParam(s->handle, AACENC_AOT, aot)) != AACENC_OK) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set the AOT %d: %s\n",
               aot, aac_get_error(err));
        goto error;
    }

    if (aot == AV_PROFILE_AAC_ELD + 1 && s->eld_sbr) {
        if ((err = aacEncoder_SetParam(s->handle, AACENC_SBR_MODE,
                                       1)) != AACENC_OK) {
            av_log(avctx, AV_LOG_ERROR, "Unable to enable SBR for ELD: %s\n",
                   aac_get_error(err));
            goto error;
        }
    }

    if (s->frame_length >= 0) {
        if ((err = aacEncoder_SetParam(s->handle, AACENC_GRANULE_LENGTH,
                                       s->frame_length)) != AACENC_OK) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set granule length: %s\n",
                   aac_get_error(err));
            goto error;
        }
    }

    if ((err = aacEncoder_SetParam(s->handle, AACENC_SAMPLERATE,
                                   avctx->sample_rate)) != AACENC_OK) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set the sample rate %d: %s\n",
               avctx->sample_rate, aac_get_error(err));
        goto error;
    }

    switch (avctx->ch_layout.nb_channels) {
    case 1: mode = MODE_1;       sce = 1; cpe = 0; break;
    case 2:
#if FDKENC_VER_AT_LEAST(4, 0) // 4.0.0
      // (profile + 1) to map from profile range to AOT range
      if (aot == AV_PROFILE_AAC_ELD + 1 && s->eld_v2) {
          if ((err = aacEncoder_SetParam(s->handle, AACENC_CHANNELMODE,
                                         128)) != AACENC_OK) {
              av_log(avctx, AV_LOG_ERROR, "Unable to enable ELDv2: %s\n",
                     aac_get_error(err));
              goto error;
          } else {
            mode = MODE_212;
            sce = 1;
            cpe = 0;
          }
      } else
#endif
      {
        mode = MODE_2;
        sce = 0;
        cpe = 1;
      }
      break;
    case 3: mode = MODE_1_2;     sce = 1; cpe = 1; break;
    case 4: mode = MODE_1_2_1;   sce = 2; cpe = 1; break;
    case 5: mode = MODE_1_2_2;   sce = 1; cpe = 2; break;
    case 6: mode = MODE_1_2_2_1; sce = 2; cpe = 2; break;
#if FDKENC_VER_AT_LEAST(4, 0) // 4.0.0
    case 7: mode = MODE_6_1;     sce = 3; cpe = 2; break;
#endif
/* The version macro is introduced the same time as the 7.1 support, so this
   should suffice. */
#if FDKENC_VER_AT_LEAST(3, 4) // 3.4.12
    case 8:
        sce = 2;
        cpe = 3;
        if (!av_channel_layout_compare(&avctx->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_7POINT1)) {
            mode = MODE_7_1_REAR_SURROUND;
#if FDKENC_VER_AT_LEAST(4, 0) // 4.0.0
        } else if (!av_channel_layout_compare(&avctx->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_7POINT1_TOP_BACK)) {
            mode = MODE_7_1_TOP_FRONT;
#endif
        } else {
            // MODE_1_2_2_2_1 and MODE_7_1_FRONT_CENTER use the same channel layout
            mode = MODE_7_1_FRONT_CENTER;
        }
        break;
#endif
    default:
        av_log(avctx, AV_LOG_ERROR,
               "Unsupported number of channels %d\n", avctx->ch_layout.nb_channels);
        goto error;
    }

    if ((err = aacEncoder_SetParam(s->handle, AACENC_CHANNELMODE,
                                   mode)) != AACENC_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Unable to set channel mode %d: %s\n", mode, aac_get_error(err));
        goto error;
    }

    if ((err = aacEncoder_SetParam(s->handle, AACENC_CHANNELORDER,
                                   1)) != AACENC_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Unable to set wav channel order %d: %s\n",
               mode, aac_get_error(err));
        goto error;
    }

    if (avctx->flags & AV_CODEC_FLAG_QSCALE || s->vbr) {
        int mode = s->vbr ? s->vbr : avctx->global_quality;
        if (mode <  1 || mode > 5) {
            av_log(avctx, AV_LOG_WARNING,
                   "VBR quality %d out of range, should be 1-5\n", mode);
            mode = av_clip(mode, 1, 5);
        }
        av_log(avctx, AV_LOG_WARNING,
               "Note, the VBR setting is unsupported and only works with "
               "some parameter combinations\n");
        if ((err = aacEncoder_SetParam(s->handle, AACENC_BITRATEMODE,
                                       mode)) != AACENC_OK) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set the VBR bitrate mode %d: %s\n",
                   mode, aac_get_error(err));
            goto error;
        }
    } else {
        if (avctx->bit_rate <= 0) {
            if (avctx->profile == AV_PROFILE_AAC_HE_V2) {
                sce = 1;
                cpe = 0;
            }
            avctx->bit_rate = (96*sce + 128*cpe) * avctx->sample_rate / 44;
            if (avctx->profile == AV_PROFILE_AAC_HE ||
                avctx->profile == AV_PROFILE_AAC_HE_V2 ||
                avctx->profile == AV_PROFILE_MPEG2_AAC_HE ||
                s->eld_sbr)
                avctx->bit_rate /= 2;
        }
        if ((err = aacEncoder_SetParam(s->handle, AACENC_BITRATE,
                                       avctx->bit_rate)) != AACENC_OK) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set the bitrate %"PRId64": %s\n",
                   avctx->bit_rate, aac_get_error(err));
            goto error;
        }
    }

    /* Choose bitstream format - if global header is requested, use
     * raw access units, otherwise use ADTS. */
    if ((err = aacEncoder_SetParam(s->handle, AACENC_TRANSMUX,
                                   avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER ? TT_MP4_RAW :
                                   s->latm ? TT_MP4_LOAS : TT_MP4_ADTS)) != AACENC_OK) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set the transmux format: %s\n",
               aac_get_error(err));
        goto error;
    }

    if (s->latm && s->header_period) {
        if ((err = aacEncoder_SetParam(s->handle, AACENC_HEADER_PERIOD,
                                       s->header_period)) != AACENC_OK) {
             av_log(avctx, AV_LOG_ERROR, "Unable to set header period: %s\n",
                    aac_get_error(err));
             goto error;
        }
    }

    /* If no signaling mode is chosen, use explicit hierarchical signaling
     * if using mp4 mode (raw access units, with global header) and
     * implicit signaling if using ADTS. */
    if (s->signaling < 0)
        s->signaling = avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER ? 2 : 0;

    if ((err = aacEncoder_SetParam(s->handle, AACENC_SIGNALING_MODE,
                                   s->signaling)) != AACENC_OK) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set signaling mode %d: %s\n",
               s->signaling, aac_get_error(err));
        goto error;
    }

    if ((err = aacEncoder_SetParam(s->handle, AACENC_AFTERBURNER,
                                   s->afterburner)) != AACENC_OK) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set afterburner to %d: %s\n",
               s->afterburner, aac_get_error(err));
        goto error;
    }

    if (avctx->cutoff > 0) {
        if (avctx->cutoff < (avctx->sample_rate + 255) >> 8 || avctx->cutoff > 20000) {
            av_log(avctx, AV_LOG_ERROR, "cutoff valid range is %d-20000\n",
                   (avctx->sample_rate + 255) >> 8);
            goto error;
        }
        if ((err = aacEncoder_SetParam(s->handle, AACENC_BANDWIDTH,
                                       avctx->cutoff)) != AACENC_OK) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set the encoder bandwidth to %d: %s\n",
                   avctx->cutoff, aac_get_error(err));
            goto error;
        }
    }

    s->metadata_mode = 0;
    if (s->prog_ref) {
        s->metadata_mode = 1;
        s->metaDataSetup.prog_ref_level_present = 1;
        s->metaDataSetup.prog_ref_level = s->prog_ref << 16;
    }
    if (s->drc_profile) {
        s->metadata_mode = 1;
        s->metaDataSetup.drc_profile = s->drc_profile;
        s->metaDataSetup.drc_TargetRefLevel = s->drc_target_ref << 16;
        if (s->comp_profile) {
            /* Including the comp_profile means that we need to set the mode to ETSI */
            s->metadata_mode = 2;
            s->metaDataSetup.comp_profile = s->comp_profile;
            s->metaDataSetup.comp_TargetRefLevel = s->comp_target_ref << 16;
        }
    }

    if ((err = aacEncoder_SetParam(s->handle, AACENC_METADATA_MODE, s->metadata_mode)) != AACENC_OK) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set metadata mode to %d: %s\n",
                s->metadata_mode, aac_get_error(err));
        goto error;
    }

    if ((err = aacEncEncode(s->handle, NULL, NULL, NULL, NULL)) != AACENC_OK) {
        av_log(avctx, AV_LOG_ERROR, "Unable to initialize the encoder: %s\n",
               aac_get_error(err));
        return AVERROR(EINVAL);
    }

    if ((err = aacEncInfo(s->handle, &info)) != AACENC_OK) {
        av_log(avctx, AV_LOG_ERROR, "Unable to get encoder info: %s\n",
               aac_get_error(err));
        goto error;
    }

    avctx->frame_size = info.frameLength;
#if FDKENC_VER_AT_LEAST(4, 0) // 4.0.0
    avctx->initial_padding = info.nDelay;
#else
    avctx->initial_padding = info.encoderDelay;
#endif
    ff_af_queue_init(avctx, &s->afq);

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        avctx->extradata_size = info.confSize;
        avctx->extradata      = av_mallocz(avctx->extradata_size +
                                           AV_INPUT_BUFFER_PADDING_SIZE);
        if (!avctx->extradata) {
            ret = AVERROR(ENOMEM);
            goto error;
        }

        memcpy(avctx->extradata, info.confBuf, info.confSize);
    }
    return 0;
error:
    aac_encode_close(avctx);
    return ret;
}

static int aac_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet_ptr)
{
    AACContext    *s        = avctx->priv_data;
    AACENC_BufDesc in_buf   = { 0 }, out_buf = { 0 };
    AACENC_InArgs  in_args  = { 0 };
    AACENC_OutArgs out_args = { 0 };
    void* inBuffer[] = { 0, &s->metaDataSetup };
    int in_buffer_identifiers[] = { IN_AUDIO_DATA, IN_METADATA_SETUP };
    int in_buffer_element_sizes[] = { 2, sizeof(AACENC_MetaData) };
    int in_buffer_sizes[] = { 0, sizeof(s->metaDataSetup) };
    int out_buffer_identifier = OUT_BITSTREAM_DATA;
    int out_buffer_size, out_buffer_element_size;
    void *out_ptr;
    int ret, discard_padding;
    uint8_t dummy_buf[1];
    AACENC_ERROR err;

    /* handle end-of-stream small frame and flushing */
    if (!frame) {
        /* Must be a non-null pointer, even if it's a dummy. We could use
         * the address of anything else on the stack as well. */
        inBuffer[0]          = dummy_buf;

        in_args.numInSamples = -1;
    } else {
        inBuffer[0]          = frame->data[0];
        in_buffer_sizes[0]   = 2 * avctx->ch_layout.nb_channels * frame->nb_samples;

        in_args.numInSamples = avctx->ch_layout.nb_channels * frame->nb_samples;

        /* add current frame to the queue */
        if ((ret = ff_af_queue_add(&s->afq, frame)) < 0)
            return ret;
    }

    if (s->metadata_mode == 0) {
        in_buf.numBufs       = 1;
    } else {
        in_buf.numBufs       = 2;
    }

    in_buf.bufs              = (void**)inBuffer;
    in_buf.bufferIdentifiers = in_buffer_identifiers;
    in_buf.bufSizes          = in_buffer_sizes;
    in_buf.bufElSizes        = in_buffer_element_sizes;

    /* The maximum packet size is 6144 bits aka 768 bytes per channel. */
    ret = ff_alloc_packet(avctx, avpkt, FFMAX(8192, 768 * avctx->ch_layout.nb_channels));
    if (ret < 0)
        return ret;

    out_ptr                   = avpkt->data;
    out_buffer_size           = avpkt->size;
    out_buffer_element_size   = 1;
    out_buf.numBufs           = 1;
    out_buf.bufs              = &out_ptr;
    out_buf.bufferIdentifiers = &out_buffer_identifier;
    out_buf.bufSizes          = &out_buffer_size;
    out_buf.bufElSizes        = &out_buffer_element_size;

    if ((err = aacEncEncode(s->handle, &in_buf, &out_buf, &in_args,
                            &out_args)) != AACENC_OK) {
        if (!frame && err == AACENC_ENCODE_EOF)
            return 0;
        av_log(avctx, AV_LOG_ERROR, "Unable to encode frame: %s\n",
               aac_get_error(err));
        return AVERROR(EINVAL);
    }

    if (!out_args.numOutBytes)
        return 0;

    /* Get the next frame pts & duration */
    ff_af_queue_remove(&s->afq, avctx->frame_size, &avpkt->pts,
                       &avpkt->duration);

    discard_padding = avctx->frame_size - avpkt->duration;
    // Check if subtraction resulted in an overflow
    if ((discard_padding < avctx->frame_size) != (avpkt->duration > 0)) {
        av_log(avctx, AV_LOG_ERROR, "discard padding overflow\n");
        return AVERROR(EINVAL);
    }
    if ((!s->delay_sent && avctx->initial_padding > 0) || discard_padding > 0) {
        uint8_t *side_data =
            av_packet_new_side_data(avpkt, AV_PKT_DATA_SKIP_SAMPLES, 10);
        if (!side_data)
            return AVERROR(ENOMEM);
        if (!s->delay_sent) {
            AV_WL32(side_data, avctx->initial_padding);
            s->delay_sent = 1;
        }
        AV_WL32(side_data + 4, discard_padding);
    }

    avpkt->size     = out_args.numOutBytes;
    *got_packet_ptr = 1;
    return 0;
}

static const AVProfile profiles[] = {
    { AV_PROFILE_AAC_LOW,   "LC"       },
    { AV_PROFILE_AAC_HE,    "HE-AAC"   },
    { AV_PROFILE_AAC_HE_V2, "HE-AACv2" },
    { AV_PROFILE_AAC_LD,    "LD"       },
    { AV_PROFILE_AAC_ELD,   "ELD"      },
    { AV_PROFILE_UNKNOWN },
};

static const FFCodecDefault aac_encode_defaults[] = {
    { "b", "0" },
    { NULL }
};

static const AVChannelLayout aac_ch_layouts[16] = {
    AV_CHANNEL_LAYOUT_MONO,
    AV_CHANNEL_LAYOUT_STEREO,
    AV_CHANNEL_LAYOUT_SURROUND,
    AV_CHANNEL_LAYOUT_4POINT0,
    AV_CHANNEL_LAYOUT_5POINT0_BACK,
    AV_CHANNEL_LAYOUT_5POINT1_BACK,
#if FDKENC_VER_AT_LEAST(4, 0) // 4.0.0
    AV_CHANNEL_LAYOUT_6POINT1_BACK,
#endif
#if FDKENC_VER_AT_LEAST(3, 4) // 3.4.12
    AV_CHANNEL_LAYOUT_7POINT1_WIDE_BACK,
    AV_CHANNEL_LAYOUT_7POINT1,
#endif
#if FDKENC_VER_AT_LEAST(4, 0) // 4.0.0
    AV_CHANNEL_LAYOUT_7POINT1_TOP_BACK,
#endif
    { 0 },
};

static const int aac_sample_rates[] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 0
};

const FFCodec ff_libfdk_aac_encoder = {
    .p.name                = "libfdk_aac",
    CODEC_LONG_NAME("Fraunhofer FDK AAC"),
    .p.type                = AVMEDIA_TYPE_AUDIO,
    .p.id                  = AV_CODEC_ID_AAC,
    .p.capabilities        = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                             AV_CODEC_CAP_ENCODER_FLUSH |
                             AV_CODEC_CAP_SMALL_LAST_FRAME,
    .caps_internal         = FF_CODEC_CAP_NOT_INIT_THREADSAFE,
    .priv_data_size        = sizeof(AACContext),
    .init                  = aac_encode_init,
    FF_CODEC_ENCODE_CB(aac_encode_frame),
    .flush                 = aac_encode_flush,
    .close                 = aac_encode_close,
    .p.sample_fmts         = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                            AV_SAMPLE_FMT_NONE },
    .p.priv_class          = &aac_enc_class,
    .defaults              = aac_encode_defaults,
    .p.profiles            = profiles,
    .p.supported_samplerates = aac_sample_rates,
    .p.wrapper_name        = "libfdk",
    .p.ch_layouts          = aac_ch_layouts,
};
