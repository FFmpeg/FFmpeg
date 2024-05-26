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

#define COBJMACROS
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif

#include "encode.h"
#include "mf_utils.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "codec_internal.h"
#include "internal.h"
#include "compat/w32dlfcn.h"

typedef struct MFContext {
    AVClass *av_class;
    HMODULE library;
    MFFunctions functions;
    AVFrame *frame;
    int is_video, is_audio;
    GUID main_subtype;
    IMFTransform *mft;
    IMFMediaEventGenerator *async_events;
    DWORD in_stream_id, out_stream_id;
    MFT_INPUT_STREAM_INFO in_info;
    MFT_OUTPUT_STREAM_INFO out_info;
    int out_stream_provides_samples;
    int draining, draining_done;
    int sample_sent;
    int async_need_input, async_have_output, async_marker;
    int64_t reorder_delay;
    ICodecAPI *codec_api;
    // set by AVOption
    int opt_enc_rc;
    int opt_enc_quality;
    int opt_enc_scenario;
    int opt_enc_hw;
} MFContext;

static int mf_choose_output_type(AVCodecContext *avctx);
static int mf_setup_context(AVCodecContext *avctx);

#define MF_TIMEBASE (AVRational){1, 10000000}
// Sentinel value only used by us.
#define MF_INVALID_TIME AV_NOPTS_VALUE

static int mf_wait_events(AVCodecContext *avctx)
{
    MFContext *c = avctx->priv_data;

    if (!c->async_events)
        return 0;

    while (!(c->async_need_input || c->async_have_output || c->draining_done || c->async_marker)) {
        IMFMediaEvent *ev = NULL;
        MediaEventType ev_id = 0;
        HRESULT hr = IMFMediaEventGenerator_GetEvent(c->async_events, 0, &ev);
        if (FAILED(hr)) {
            av_log(avctx, AV_LOG_ERROR, "IMFMediaEventGenerator_GetEvent() failed: %s\n",
                   ff_hr_str(hr));
            return AVERROR_EXTERNAL;
        }
        IMFMediaEvent_GetType(ev, &ev_id);
        switch (ev_id) {
        case ff_METransformNeedInput:
            if (!c->draining)
                c->async_need_input = 1;
            break;
        case ff_METransformHaveOutput:
            c->async_have_output = 1;
            break;
        case ff_METransformDrainComplete:
            c->draining_done = 1;
            break;
        case ff_METransformMarker:
            c->async_marker = 1;
            break;
        default: ;
        }
        IMFMediaEvent_Release(ev);
    }

    return 0;
}

static AVRational mf_get_tb(AVCodecContext *avctx)
{
    if (avctx->time_base.num > 0 && avctx->time_base.den > 0)
        return avctx->time_base;
    return MF_TIMEBASE;
}

static LONGLONG mf_to_mf_time(AVCodecContext *avctx, int64_t av_pts)
{
    if (av_pts == AV_NOPTS_VALUE)
        return MF_INVALID_TIME;
    return av_rescale_q(av_pts, mf_get_tb(avctx), MF_TIMEBASE);
}

static void mf_sample_set_pts(AVCodecContext *avctx, IMFSample *sample, int64_t av_pts)
{
    LONGLONG stime = mf_to_mf_time(avctx, av_pts);
    if (stime != MF_INVALID_TIME)
        IMFSample_SetSampleTime(sample, stime);
}

static int64_t mf_from_mf_time(AVCodecContext *avctx, LONGLONG stime)
{
    return av_rescale_q(stime, MF_TIMEBASE, mf_get_tb(avctx));
}

static int64_t mf_sample_get_pts(AVCodecContext *avctx, IMFSample *sample)
{
    LONGLONG pts;
    HRESULT hr = IMFSample_GetSampleTime(sample, &pts);
    if (FAILED(hr))
        return AV_NOPTS_VALUE;
    return mf_from_mf_time(avctx, pts);
}

static int mf_enca_output_type_get(AVCodecContext *avctx, IMFMediaType *type)
{
    MFContext *c = avctx->priv_data;
    HRESULT hr;
    UINT32 sz;

    if (avctx->codec_id != AV_CODEC_ID_MP3 && avctx->codec_id != AV_CODEC_ID_AC3) {
        hr = IMFAttributes_GetBlobSize(type, &MF_MT_USER_DATA, &sz);
        if (!FAILED(hr) && sz > 0) {
            avctx->extradata = av_mallocz(sz + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!avctx->extradata)
                return AVERROR(ENOMEM);
            avctx->extradata_size = sz;
            hr = IMFAttributes_GetBlob(type, &MF_MT_USER_DATA, avctx->extradata, sz, NULL);
            if (FAILED(hr))
                return AVERROR_EXTERNAL;

            if (avctx->codec_id == AV_CODEC_ID_AAC && avctx->extradata_size >= 12) {
                // Get rid of HEAACWAVEINFO (after wfx field, 12 bytes).
                avctx->extradata_size = avctx->extradata_size - 12;
                memmove(avctx->extradata, avctx->extradata + 12, avctx->extradata_size);
            }
        }
    }

    // I don't know where it's documented that we need this. It happens with the
    // MS mp3 encoder MFT. The idea for the workaround is taken from NAudio.
    // (Certainly any lossy codec will have frames much smaller than 1 second.)
    if (!c->out_info.cbSize && !c->out_stream_provides_samples) {
        hr = IMFAttributes_GetUINT32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &sz);
        if (!FAILED(hr)) {
            av_log(avctx, AV_LOG_VERBOSE, "MFT_OUTPUT_STREAM_INFO.cbSize set to 0, "
                   "assuming %d bytes instead.\n", (int)sz);
            c->out_info.cbSize = sz;
        }
    }

    return 0;
}

static int mf_encv_output_type_get(AVCodecContext *avctx, IMFMediaType *type)
{
    HRESULT hr;
    UINT32 sz;

    hr = IMFAttributes_GetBlobSize(type, &MF_MT_MPEG_SEQUENCE_HEADER, &sz);
    if (!FAILED(hr) && sz > 0) {
        uint8_t *extradata = av_mallocz(sz + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!extradata)
            return AVERROR(ENOMEM);
        hr = IMFAttributes_GetBlob(type, &MF_MT_MPEG_SEQUENCE_HEADER, extradata, sz, NULL);
        if (FAILED(hr)) {
            av_free(extradata);
            return AVERROR_EXTERNAL;
        }
        av_freep(&avctx->extradata);
        avctx->extradata = extradata;
        avctx->extradata_size = sz;
    }

    return 0;
}

static int mf_output_type_get(AVCodecContext *avctx)
{
    MFContext *c = avctx->priv_data;
    HRESULT hr;
    IMFMediaType *type;
    int ret;

    hr = IMFTransform_GetOutputCurrentType(c->mft, c->out_stream_id, &type);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "could not get output type\n");
        return AVERROR_EXTERNAL;
    }

    av_log(avctx, AV_LOG_VERBOSE, "final output type:\n");
    ff_media_type_dump(avctx, type);

    ret = 0;
    if (c->is_video) {
        ret = mf_encv_output_type_get(avctx, type);
    } else if (c->is_audio) {
        ret = mf_enca_output_type_get(avctx, type);
    }

    if (ret < 0)
        av_log(avctx, AV_LOG_ERROR, "output type not supported\n");

    IMFMediaType_Release(type);
    return ret;
}

static int mf_sample_to_avpacket(AVCodecContext *avctx, IMFSample *sample, AVPacket *avpkt)
{
    MFContext *c = avctx->priv_data;
    HRESULT hr;
    int ret;
    DWORD len;
    IMFMediaBuffer *buffer;
    BYTE *data;
    UINT64 t;
    UINT32 t32;

    hr = IMFSample_GetTotalLength(sample, &len);
    if (FAILED(hr))
        return AVERROR_EXTERNAL;

    if ((ret = ff_get_encode_buffer(avctx, avpkt, len, 0)) < 0)
        return ret;

    hr = IMFSample_ConvertToContiguousBuffer(sample, &buffer);
    if (FAILED(hr))
        return AVERROR_EXTERNAL;

    hr = IMFMediaBuffer_Lock(buffer, &data, NULL, NULL);
    if (FAILED(hr)) {
        IMFMediaBuffer_Release(buffer);
        return AVERROR_EXTERNAL;
    }

    memcpy(avpkt->data, data, len);

    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_Release(buffer);

    avpkt->pts = avpkt->dts = mf_sample_get_pts(avctx, sample);

    hr = IMFAttributes_GetUINT32(sample, &MFSampleExtension_CleanPoint, &t32);
    if (c->is_audio || (!FAILED(hr) && t32 != 0))
        avpkt->flags |= AV_PKT_FLAG_KEY;

    hr = IMFAttributes_GetUINT64(sample, &MFSampleExtension_DecodeTimestamp, &t);
    if (!FAILED(hr)) {
        avpkt->dts = mf_from_mf_time(avctx, t);
        // At least on Qualcomm's HEVC encoder on SD 835, the output dts
        // starts from the input pts of the first frame, while the output pts
        // is shifted forward. Therefore, shift the output values back so that
        // the output pts matches the input.
        if (c->reorder_delay == AV_NOPTS_VALUE)
            c->reorder_delay = avpkt->pts - avpkt->dts;
        avpkt->dts -= c->reorder_delay;
        avpkt->pts -= c->reorder_delay;
    }

    return 0;
}

static IMFSample *mf_a_avframe_to_sample(AVCodecContext *avctx, const AVFrame *frame)
{
    MFContext *c = avctx->priv_data;
    size_t len;
    size_t bps;
    IMFSample *sample;

    bps = av_get_bytes_per_sample(avctx->sample_fmt) * avctx->ch_layout.nb_channels;
    len = frame->nb_samples * bps;

    sample = ff_create_memory_sample(&c->functions, frame->data[0], len,
                                     c->in_info.cbAlignment);
    if (sample)
        IMFSample_SetSampleDuration(sample, mf_to_mf_time(avctx, frame->nb_samples));
    return sample;
}

static IMFSample *mf_v_avframe_to_sample(AVCodecContext *avctx, const AVFrame *frame)
{
    MFContext *c = avctx->priv_data;
    IMFSample *sample;
    IMFMediaBuffer *buffer;
    BYTE *data;
    HRESULT hr;
    int ret;
    int size;

    size = av_image_get_buffer_size(avctx->pix_fmt, avctx->width, avctx->height, 1);
    if (size < 0)
        return NULL;

    sample = ff_create_memory_sample(&c->functions, NULL, size,
                                     c->in_info.cbAlignment);
    if (!sample)
        return NULL;

    hr = IMFSample_GetBufferByIndex(sample, 0, &buffer);
    if (FAILED(hr)) {
        IMFSample_Release(sample);
        return NULL;
    }

    hr = IMFMediaBuffer_Lock(buffer, &data, NULL, NULL);
    if (FAILED(hr)) {
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
        return NULL;
    }

    ret = av_image_copy_to_buffer((uint8_t *)data, size, (void *)frame->data, frame->linesize,
                                  avctx->pix_fmt, avctx->width, avctx->height, 1);
    IMFMediaBuffer_SetCurrentLength(buffer, size);
    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_Release(buffer);
    if (ret < 0) {
        IMFSample_Release(sample);
        return NULL;
    }

    IMFSample_SetSampleDuration(sample, mf_to_mf_time(avctx, frame->duration));

    return sample;
}

static IMFSample *mf_avframe_to_sample(AVCodecContext *avctx, const AVFrame *frame)
{
    MFContext *c = avctx->priv_data;
    IMFSample *sample;

    if (c->is_audio) {
        sample = mf_a_avframe_to_sample(avctx, frame);
    } else {
        sample = mf_v_avframe_to_sample(avctx, frame);
    }

    if (sample)
        mf_sample_set_pts(avctx, sample, frame->pts);

    return sample;
}

static int mf_send_sample(AVCodecContext *avctx, IMFSample *sample)
{
    MFContext *c = avctx->priv_data;
    HRESULT hr;
    int ret;

    if (sample) {
        if (c->async_events) {
            if ((ret = mf_wait_events(avctx)) < 0)
                return ret;
            if (!c->async_need_input)
                return AVERROR(EAGAIN);
        }
        if (!c->sample_sent)
            IMFSample_SetUINT32(sample, &MFSampleExtension_Discontinuity, TRUE);
        c->sample_sent = 1;
        hr = IMFTransform_ProcessInput(c->mft, c->in_stream_id, sample, 0);
        if (hr == MF_E_NOTACCEPTING) {
            return AVERROR(EAGAIN);
        } else if (FAILED(hr)) {
            av_log(avctx, AV_LOG_ERROR, "failed processing input: %s\n", ff_hr_str(hr));
            return AVERROR_EXTERNAL;
        }
        c->async_need_input = 0;
    } else if (!c->draining) {
        hr = IMFTransform_ProcessMessage(c->mft, MFT_MESSAGE_COMMAND_DRAIN, 0);
        if (FAILED(hr))
            av_log(avctx, AV_LOG_ERROR, "failed draining: %s\n", ff_hr_str(hr));
        // Some MFTs (AC3) will send a frame after each drain command (???), so
        // this is required to make draining actually terminate.
        c->draining = 1;
        c->async_need_input = 0;
    } else {
        return AVERROR_EOF;
    }
    return 0;
}

static int mf_receive_sample(AVCodecContext *avctx, IMFSample **out_sample)
{
    MFContext *c = avctx->priv_data;
    HRESULT hr;
    DWORD st;
    MFT_OUTPUT_DATA_BUFFER out_buffers;
    IMFSample *sample;
    int ret = 0;

    while (1) {
        *out_sample = NULL;
        sample = NULL;

        if (c->async_events) {
            if ((ret = mf_wait_events(avctx)) < 0)
                return ret;
            if (!c->async_have_output || c->draining_done) {
                ret = 0;
                break;
            }
        }

        if (!c->out_stream_provides_samples) {
            sample = ff_create_memory_sample(&c->functions, NULL,
                                             c->out_info.cbSize,
                                             c->out_info.cbAlignment);
            if (!sample)
                return AVERROR(ENOMEM);
        }

        out_buffers = (MFT_OUTPUT_DATA_BUFFER) {
            .dwStreamID = c->out_stream_id,
            .pSample = sample,
        };

        st = 0;
        hr = IMFTransform_ProcessOutput(c->mft, 0, 1, &out_buffers, &st);

        if (out_buffers.pEvents)
            IMFCollection_Release(out_buffers.pEvents);

        if (!FAILED(hr)) {
            *out_sample = out_buffers.pSample;
            ret = 0;
            break;
        }

        if (out_buffers.pSample)
            IMFSample_Release(out_buffers.pSample);

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            if (c->draining)
                c->draining_done = 1;
            ret = 0;
        } else if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            av_log(avctx, AV_LOG_WARNING, "stream format change\n");
            ret = mf_choose_output_type(avctx);
            if (ret == 0) // we don't expect renegotiating the input type
                ret = AVERROR_EXTERNAL;
            if (ret > 0) {
                ret = mf_setup_context(avctx);
                if (ret >= 0) {
                    c->async_have_output = 0;
                    continue;
                }
            }
        } else {
            av_log(avctx, AV_LOG_ERROR, "failed processing output: %s\n", ff_hr_str(hr));
            ret = AVERROR_EXTERNAL;
        }

        break;
    }

    c->async_have_output = 0;

    if (ret >= 0 && !*out_sample)
        ret = c->draining_done ? AVERROR_EOF : AVERROR(EAGAIN);

    return ret;
}

static int mf_receive_packet(AVCodecContext *avctx, AVPacket *avpkt)
{
    MFContext *c = avctx->priv_data;
    IMFSample *sample = NULL;
    int ret;

    if (!c->frame->buf[0]) {
        ret = ff_encode_get_frame(avctx, c->frame);
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;
    }

    if (c->frame->buf[0]) {
        sample = mf_avframe_to_sample(avctx, c->frame);
        if (!sample) {
            av_frame_unref(c->frame);
            return AVERROR(ENOMEM);
        }
        if (c->is_video && c->codec_api) {
            if (c->frame->pict_type == AV_PICTURE_TYPE_I || !c->sample_sent)
                ICodecAPI_SetValue(c->codec_api, &ff_CODECAPI_AVEncVideoForceKeyFrame, FF_VAL_VT_UI4(1));
        }
    }

    ret = mf_send_sample(avctx, sample);
    if (sample)
        IMFSample_Release(sample);
    if (ret != AVERROR(EAGAIN))
        av_frame_unref(c->frame);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        return ret;

    ret = mf_receive_sample(avctx, &sample);
    if (ret < 0)
        return ret;

    ret = mf_sample_to_avpacket(avctx, sample, avpkt);
    IMFSample_Release(sample);

    return ret;
}

// Most encoders seem to enumerate supported audio formats on the output types,
// at least as far as channel configuration and sample rate is concerned. Pick
// the one which seems to match best.
static int64_t mf_enca_output_score(AVCodecContext *avctx, IMFMediaType *type)
{
    MFContext *c = avctx->priv_data;
    HRESULT hr;
    UINT32 t;
    GUID tg;
    int64_t score = 0;

    hr = IMFAttributes_GetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &t);
    if (!FAILED(hr) && t == avctx->sample_rate)
        score |= 1LL << 32;

    hr = IMFAttributes_GetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, &t);
    if (!FAILED(hr) && t == avctx->ch_layout.nb_channels)
        score |= 2LL << 32;

    hr = IMFAttributes_GetGUID(type, &MF_MT_SUBTYPE, &tg);
    if (!FAILED(hr)) {
        if (IsEqualGUID(&c->main_subtype, &tg))
            score |= 4LL << 32;
    }

    // Select the bitrate (lowest priority).
    hr = IMFAttributes_GetUINT32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &t);
    if (!FAILED(hr)) {
        int diff = (int)t - avctx->bit_rate / 8;
        if (diff >= 0) {
            score |= (1LL << 31) - diff; // prefer lower bitrate
        } else {
            score |= (1LL << 30) + diff; // prefer higher bitrate
        }
    }

    hr = IMFAttributes_GetUINT32(type, &MF_MT_AAC_PAYLOAD_TYPE, &t);
    if (!FAILED(hr) && t != 0)
        return -1;

    return score;
}

static int mf_enca_output_adjust(AVCodecContext *avctx, IMFMediaType *type)
{
    // (some decoders allow adjusting this freely, but it can also cause failure
    //  to set the output type - so it's commented for being too fragile)
    //IMFAttributes_SetUINT32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, avctx->bit_rate / 8);
    //IMFAttributes_SetUINT32(type, &MF_MT_AVG_BITRATE, avctx->bit_rate);

    return 0;
}

static int64_t mf_enca_input_score(AVCodecContext *avctx, IMFMediaType *type)
{
    HRESULT hr;
    UINT32 t;
    int64_t score = 0;

    enum AVSampleFormat sformat = ff_media_type_to_sample_fmt((IMFAttributes *)type);
    if (sformat == AV_SAMPLE_FMT_NONE)
        return -1; // can not use

    if (sformat == avctx->sample_fmt)
        score |= 1;

    hr = IMFAttributes_GetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &t);
    if (!FAILED(hr) && t == avctx->sample_rate)
        score |= 2;

    hr = IMFAttributes_GetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, &t);
    if (!FAILED(hr) && t == avctx->ch_layout.nb_channels)
        score |= 4;

    return score;
}

static int mf_enca_input_adjust(AVCodecContext *avctx, IMFMediaType *type)
{
    HRESULT hr;
    UINT32 t;

    enum AVSampleFormat sformat = ff_media_type_to_sample_fmt((IMFAttributes *)type);
    if (sformat != avctx->sample_fmt) {
        av_log(avctx, AV_LOG_ERROR, "unsupported input sample format set\n");
        return AVERROR(EINVAL);
    }

    hr = IMFAttributes_GetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &t);
    if (FAILED(hr) || t != avctx->sample_rate) {
        av_log(avctx, AV_LOG_ERROR, "unsupported input sample rate set\n");
        return AVERROR(EINVAL);
    }

    hr = IMFAttributes_GetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, &t);
    if (FAILED(hr) || t != avctx->ch_layout.nb_channels) {
        av_log(avctx, AV_LOG_ERROR, "unsupported input channel number set\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int64_t mf_encv_output_score(AVCodecContext *avctx, IMFMediaType *type)
{
    MFContext *c = avctx->priv_data;
    GUID tg;
    HRESULT hr;
    int score = -1;

    hr = IMFAttributes_GetGUID(type, &MF_MT_SUBTYPE, &tg);
    if (!FAILED(hr)) {
        if (IsEqualGUID(&c->main_subtype, &tg))
            score = 1;
    }

    return score;
}

static int mf_encv_output_adjust(AVCodecContext *avctx, IMFMediaType *type)
{
    MFContext *c = avctx->priv_data;
    AVRational framerate;

    ff_MFSetAttributeSize((IMFAttributes *)type, &MF_MT_FRAME_SIZE, avctx->width, avctx->height);
    IMFAttributes_SetUINT32(type, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        framerate = avctx->framerate;
    } else {
        framerate = av_inv_q(avctx->time_base);
#if FF_API_TICKS_PER_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
        framerate.den *= avctx->ticks_per_frame;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    }

    ff_MFSetAttributeRatio((IMFAttributes *)type, &MF_MT_FRAME_RATE, framerate.num, framerate.den);

    // (MS HEVC supports eAVEncH265VProfile_Main_420_8 only.)
    if (avctx->codec_id == AV_CODEC_ID_H264) {
        UINT32 profile = ff_eAVEncH264VProfile_Base;
        switch (avctx->profile) {
        case AV_PROFILE_H264_MAIN:
            profile = ff_eAVEncH264VProfile_Main;
            break;
        case AV_PROFILE_H264_HIGH:
            profile = ff_eAVEncH264VProfile_High;
            break;
        }
        IMFAttributes_SetUINT32(type, &MF_MT_MPEG2_PROFILE, profile);
    }

    IMFAttributes_SetUINT32(type, &MF_MT_AVG_BITRATE, avctx->bit_rate);

    // Note that some of the ICodecAPI options must be set before SetOutputType.
    if (c->codec_api) {
        if (avctx->bit_rate)
            ICodecAPI_SetValue(c->codec_api, &ff_CODECAPI_AVEncCommonMeanBitRate, FF_VAL_VT_UI4(avctx->bit_rate));

        if (c->opt_enc_rc >= 0)
            ICodecAPI_SetValue(c->codec_api, &ff_CODECAPI_AVEncCommonRateControlMode, FF_VAL_VT_UI4(c->opt_enc_rc));

        if (c->opt_enc_quality >= 0)
            ICodecAPI_SetValue(c->codec_api, &ff_CODECAPI_AVEncCommonQuality, FF_VAL_VT_UI4(c->opt_enc_quality));

        if (avctx->rc_max_rate > 0)
            ICodecAPI_SetValue(c->codec_api, &ff_CODECAPI_AVEncCommonMaxBitRate, FF_VAL_VT_UI4(avctx->rc_max_rate));

        if (avctx->gop_size > 0)
            ICodecAPI_SetValue(c->codec_api, &ff_CODECAPI_AVEncMPVGOPSize, FF_VAL_VT_UI4(avctx->gop_size));

        if(avctx->rc_buffer_size > 0)
            ICodecAPI_SetValue(c->codec_api, &ff_CODECAPI_AVEncCommonBufferSize, FF_VAL_VT_UI4(avctx->rc_buffer_size));

        if(avctx->compression_level >= 0)
            ICodecAPI_SetValue(c->codec_api, &ff_CODECAPI_AVEncCommonQualityVsSpeed, FF_VAL_VT_UI4(avctx->compression_level));

        if(avctx->global_quality > 0)
            ICodecAPI_SetValue(c->codec_api, &ff_CODECAPI_AVEncVideoEncodeQP, FF_VAL_VT_UI4(avctx->global_quality ));

        // Always set the number of b-frames. Qualcomm's HEVC encoder on SD835
        // defaults this to 1, and that setting is buggy with many of the
        // rate control modes. (0 or 2 b-frames works fine with most rate
        // control modes, but 2 seems buggy with the u_vbr mode.) Setting
        // "scenario" to "camera_record" sets it in CFR mode (where the default
        // is VFR), which makes the encoder avoid dropping frames.
        ICodecAPI_SetValue(c->codec_api, &ff_CODECAPI_AVEncMPVDefaultBPictureCount, FF_VAL_VT_UI4(avctx->max_b_frames));
        avctx->has_b_frames = avctx->max_b_frames > 0;

        ICodecAPI_SetValue(c->codec_api, &ff_CODECAPI_AVEncH264CABACEnable, FF_VAL_VT_BOOL(1));

        if (c->opt_enc_scenario >= 0)
            ICodecAPI_SetValue(c->codec_api, &ff_CODECAPI_AVScenarioInfo, FF_VAL_VT_UI4(c->opt_enc_scenario));
    }

    return 0;
}

static int64_t mf_encv_input_score(AVCodecContext *avctx, IMFMediaType *type)
{
    enum AVPixelFormat pix_fmt = ff_media_type_to_pix_fmt((IMFAttributes *)type);
    if (pix_fmt != avctx->pix_fmt)
        return -1; // can not use

    return 0;
}

static int mf_encv_input_adjust(AVCodecContext *avctx, IMFMediaType *type)
{
    enum AVPixelFormat pix_fmt = ff_media_type_to_pix_fmt((IMFAttributes *)type);
    if (pix_fmt != avctx->pix_fmt) {
        av_log(avctx, AV_LOG_ERROR, "unsupported input pixel format set\n");
        return AVERROR(EINVAL);
    }

    //ff_MFSetAttributeSize((IMFAttributes *)type, &MF_MT_FRAME_SIZE, avctx->width, avctx->height);

    return 0;
}

static int mf_choose_output_type(AVCodecContext *avctx)
{
    MFContext *c = avctx->priv_data;
    HRESULT hr;
    int ret;
    IMFMediaType *out_type = NULL;
    int64_t out_type_score = -1;
    int out_type_index = -1;
    int n;

    av_log(avctx, AV_LOG_VERBOSE, "output types:\n");
    for (n = 0; ; n++) {
        IMFMediaType *type;
        int64_t score = -1;

        hr = IMFTransform_GetOutputAvailableType(c->mft, c->out_stream_id, n, &type);
        if (hr == MF_E_NO_MORE_TYPES || hr == E_NOTIMPL)
            break;
        if (hr == MF_E_TRANSFORM_TYPE_NOT_SET) {
            av_log(avctx, AV_LOG_VERBOSE, "(need to set input type)\n");
            ret = 0;
            goto done;
        }
        if (FAILED(hr)) {
            av_log(avctx, AV_LOG_ERROR, "error getting output type: %s\n", ff_hr_str(hr));
            ret = AVERROR_EXTERNAL;
            goto done;
        }

        av_log(avctx, AV_LOG_VERBOSE, "output type %d:\n", n);
        ff_media_type_dump(avctx, type);

        if (c->is_video) {
            score = mf_encv_output_score(avctx, type);
        } else if (c->is_audio) {
            score = mf_enca_output_score(avctx, type);
        }

        if (score > out_type_score) {
            if (out_type)
                IMFMediaType_Release(out_type);
            out_type = type;
            out_type_score = score;
            out_type_index = n;
            IMFMediaType_AddRef(out_type);
        }

        IMFMediaType_Release(type);
    }

    if (out_type) {
        av_log(avctx, AV_LOG_VERBOSE, "picking output type %d.\n", out_type_index);
    } else {
        hr = c->functions.MFCreateMediaType(&out_type);
        if (FAILED(hr)) {
            ret = AVERROR(ENOMEM);
            goto done;
        }
    }

    ret = 0;
    if (c->is_video) {
        ret = mf_encv_output_adjust(avctx, out_type);
    } else if (c->is_audio) {
        ret = mf_enca_output_adjust(avctx, out_type);
    }

    if (ret >= 0) {
        av_log(avctx, AV_LOG_VERBOSE, "setting output type:\n");
        ff_media_type_dump(avctx, out_type);

        hr = IMFTransform_SetOutputType(c->mft, c->out_stream_id, out_type, 0);
        if (!FAILED(hr)) {
            ret = 1;
        } else if (hr == MF_E_TRANSFORM_TYPE_NOT_SET) {
            av_log(avctx, AV_LOG_VERBOSE, "rejected - need to set input type\n");
            ret = 0;
        } else {
            av_log(avctx, AV_LOG_ERROR, "could not set output type (%s)\n", ff_hr_str(hr));
            ret = AVERROR_EXTERNAL;
        }
    }

done:
    if (out_type)
        IMFMediaType_Release(out_type);
    return ret;
}

static int mf_choose_input_type(AVCodecContext *avctx)
{
    MFContext *c = avctx->priv_data;
    HRESULT hr;
    int ret;
    IMFMediaType *in_type = NULL;
    int64_t in_type_score = -1;
    int in_type_index = -1;
    int n;

    av_log(avctx, AV_LOG_VERBOSE, "input types:\n");
    for (n = 0; ; n++) {
        IMFMediaType *type = NULL;
        int64_t score = -1;

        hr = IMFTransform_GetInputAvailableType(c->mft, c->in_stream_id, n, &type);
        if (hr == MF_E_NO_MORE_TYPES || hr == E_NOTIMPL)
            break;
        if (hr == MF_E_TRANSFORM_TYPE_NOT_SET) {
            av_log(avctx, AV_LOG_VERBOSE, "(need to set output type 1)\n");
            ret = 0;
            goto done;
        }
        if (FAILED(hr)) {
            av_log(avctx, AV_LOG_ERROR, "error getting input type: %s\n", ff_hr_str(hr));
            ret = AVERROR_EXTERNAL;
            goto done;
        }

        av_log(avctx, AV_LOG_VERBOSE, "input type %d:\n", n);
        ff_media_type_dump(avctx, type);

        if (c->is_video) {
            score = mf_encv_input_score(avctx, type);
        } else if (c->is_audio) {
            score = mf_enca_input_score(avctx, type);
        }

        if (score > in_type_score) {
            if (in_type)
                IMFMediaType_Release(in_type);
            in_type = type;
            in_type_score = score;
            in_type_index = n;
            IMFMediaType_AddRef(in_type);
        }

        IMFMediaType_Release(type);
    }

    if (in_type) {
        av_log(avctx, AV_LOG_VERBOSE, "picking input type %d.\n", in_type_index);
    } else {
        // Some buggy MFTs (WMA encoder) fail to return MF_E_TRANSFORM_TYPE_NOT_SET.
        av_log(avctx, AV_LOG_VERBOSE, "(need to set output type 2)\n");
        ret = 0;
        goto done;
    }

    ret = 0;
    if (c->is_video) {
        ret = mf_encv_input_adjust(avctx, in_type);
    } else if (c->is_audio) {
        ret = mf_enca_input_adjust(avctx, in_type);
    }

    if (ret >= 0) {
        av_log(avctx, AV_LOG_VERBOSE, "setting input type:\n");
        ff_media_type_dump(avctx, in_type);

        hr = IMFTransform_SetInputType(c->mft, c->in_stream_id, in_type, 0);
        if (!FAILED(hr)) {
            ret = 1;
        } else if (hr == MF_E_TRANSFORM_TYPE_NOT_SET) {
            av_log(avctx, AV_LOG_VERBOSE, "rejected - need to set output type\n");
            ret = 0;
        } else {
            av_log(avctx, AV_LOG_ERROR, "could not set input type (%s)\n", ff_hr_str(hr));
            ret = AVERROR_EXTERNAL;
        }
    }

done:
    if (in_type)
        IMFMediaType_Release(in_type);
    return ret;
}

static int mf_negotiate_types(AVCodecContext *avctx)
{
    // This follows steps 1-5 on:
    //  https://msdn.microsoft.com/en-us/library/windows/desktop/aa965264(v=vs.85).aspx
    // If every MFT implementer does this correctly, this loop should at worst
    // be repeated once.
    int need_input = 1, need_output = 1;
    int n;
    for (n = 0; n < 2 && (need_input || need_output); n++) {
        int ret;
        ret = mf_choose_input_type(avctx);
        if (ret < 0)
            return ret;
        need_input = ret < 1;
        ret = mf_choose_output_type(avctx);
        if (ret < 0)
            return ret;
        need_output = ret < 1;
    }
    if (need_input || need_output) {
        av_log(avctx, AV_LOG_ERROR, "format negotiation failed (%d/%d)\n",
               need_input, need_output);
        return AVERROR_EXTERNAL;
    }
    return 0;
}

static int mf_setup_context(AVCodecContext *avctx)
{
    MFContext *c = avctx->priv_data;
    HRESULT hr;
    int ret;

    hr = IMFTransform_GetInputStreamInfo(c->mft, c->in_stream_id, &c->in_info);
    if (FAILED(hr))
        return AVERROR_EXTERNAL;
    av_log(avctx, AV_LOG_VERBOSE, "in_info: size=%d, align=%d\n",
           (int)c->in_info.cbSize, (int)c->in_info.cbAlignment);

    hr = IMFTransform_GetOutputStreamInfo(c->mft, c->out_stream_id, &c->out_info);
    if (FAILED(hr))
        return AVERROR_EXTERNAL;
    c->out_stream_provides_samples =
        (c->out_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) ||
        (c->out_info.dwFlags & MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES);
    av_log(avctx, AV_LOG_VERBOSE, "out_info: size=%d, align=%d%s\n",
           (int)c->out_info.cbSize, (int)c->out_info.cbAlignment,
           c->out_stream_provides_samples ? " (provides samples)" : "");

    if ((ret = mf_output_type_get(avctx)) < 0)
        return ret;

    return 0;
}

static int mf_unlock_async(AVCodecContext *avctx)
{
    MFContext *c = avctx->priv_data;
    HRESULT hr;
    IMFAttributes *attrs;
    UINT32 v;
    int res = AVERROR_EXTERNAL;

    // For hw encoding we unfortunately need to use async mode, otherwise
    // play it safe and avoid it.
    if (!(c->is_video && c->opt_enc_hw))
        return 0;

    hr = IMFTransform_GetAttributes(c->mft, &attrs);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "error retrieving MFT attributes: %s\n", ff_hr_str(hr));
        goto err;
    }

    hr = IMFAttributes_GetUINT32(attrs, &MF_TRANSFORM_ASYNC, &v);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "error querying async: %s\n", ff_hr_str(hr));
        goto err;
    }

    if (!v) {
        av_log(avctx, AV_LOG_ERROR, "hardware MFT is not async\n");
        goto err;
    }

    hr = IMFAttributes_SetUINT32(attrs, &MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "could not set async unlock: %s\n", ff_hr_str(hr));
        goto err;
    }

    hr = IMFTransform_QueryInterface(c->mft, &IID_IMFMediaEventGenerator, (void **)&c->async_events);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "could not get async interface\n");
        goto err;
    }

    res = 0;

err:
    IMFAttributes_Release(attrs);
    return res;
}

static int mf_create(void *log, MFFunctions *f, IMFTransform **mft,
                     const AVCodec *codec, int use_hw)
{
    int is_audio = codec->type == AVMEDIA_TYPE_AUDIO;
    const CLSID *subtype = ff_codec_to_mf_subtype(codec->id);
    MFT_REGISTER_TYPE_INFO reg = {0};
    GUID category;
    int ret;

    *mft = NULL;

    if (!subtype)
        return AVERROR(ENOSYS);

    reg.guidSubtype = *subtype;

    if (is_audio) {
        reg.guidMajorType = MFMediaType_Audio;
        category = MFT_CATEGORY_AUDIO_ENCODER;
    } else {
        reg.guidMajorType = MFMediaType_Video;
        category = MFT_CATEGORY_VIDEO_ENCODER;
    }

    if ((ret = ff_instantiate_mf(log, f, category, NULL, &reg, use_hw, mft)) < 0)
        return ret;

    return 0;
}

static int mf_init_encoder(AVCodecContext *avctx)
{
    MFContext *c = avctx->priv_data;
    HRESULT hr;
    int ret;
    const CLSID *subtype = ff_codec_to_mf_subtype(avctx->codec_id);
    int use_hw = 0;

    c->frame = av_frame_alloc();
    if (!c->frame)
        return AVERROR(ENOMEM);

    c->is_audio = avctx->codec_type == AVMEDIA_TYPE_AUDIO;
    c->is_video = !c->is_audio;
    c->reorder_delay = AV_NOPTS_VALUE;

    if (c->is_video && c->opt_enc_hw)
        use_hw = 1;

    if (!subtype)
        return AVERROR(ENOSYS);

    c->main_subtype = *subtype;

    if ((ret = mf_create(avctx, &c->functions, &c->mft, avctx->codec, use_hw)) < 0)
        return ret;

    if ((ret = mf_unlock_async(avctx)) < 0)
        return ret;

    hr = IMFTransform_QueryInterface(c->mft, &IID_ICodecAPI, (void **)&c->codec_api);
    if (!FAILED(hr))
        av_log(avctx, AV_LOG_VERBOSE, "MFT supports ICodecAPI.\n");


    hr = IMFTransform_GetStreamIDs(c->mft, 1, &c->in_stream_id, 1, &c->out_stream_id);
    if (hr == E_NOTIMPL) {
        c->in_stream_id = c->out_stream_id = 0;
    } else if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "could not get stream IDs (%s)\n", ff_hr_str(hr));
        return AVERROR_EXTERNAL;
    }

    if ((ret = mf_negotiate_types(avctx)) < 0)
        return ret;

    if ((ret = mf_setup_context(avctx)) < 0)
        return ret;

    hr = IMFTransform_ProcessMessage(c->mft, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "could not start streaming (%s)\n", ff_hr_str(hr));
        return AVERROR_EXTERNAL;
    }

    hr = IMFTransform_ProcessMessage(c->mft, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "could not start stream (%s)\n", ff_hr_str(hr));
        return AVERROR_EXTERNAL;
    }

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER && c->async_events &&
        c->is_video && !avctx->extradata) {
        int sleep = 10000, total = 0;
        av_log(avctx, AV_LOG_VERBOSE, "Awaiting extradata\n");
        while (total < 70*1000) {
            // The Qualcomm H264 encoder on SD835 doesn't provide extradata
            // immediately, but it becomes available soon after init (without
            // any waitable event). In practice, it's available after less
            // than 10 ms, but wait for up to 70 ms before giving up.
            // Some encoders (Qualcomm's HEVC encoder on SD835, some versions
            // of the QSV H264 encoder at least) don't provide extradata this
            // way at all, not even after encoding a frame - it's only
            // available prepended to frames.
            av_usleep(sleep);
            total += sleep;
            mf_output_type_get(avctx);
            if (avctx->extradata)
                break;
            sleep *= 2;
        }
        av_log(avctx, AV_LOG_VERBOSE, "%s extradata in %d ms\n",
               avctx->extradata ? "Got" : "Didn't get", total / 1000);
    }

    return 0;
}

#if !HAVE_UWP
#define LOAD_MF_FUNCTION(context, func_name) \
    context->functions.func_name = (void *)dlsym(context->library, #func_name); \
    if (!context->functions.func_name) { \
        av_log(context, AV_LOG_ERROR, "DLL mfplat.dll failed to find function "\
           #func_name "\n"); \
        return AVERROR_UNKNOWN; \
    }
#else
// In UWP (which lacks LoadLibrary), just link directly against
// the functions - this requires building with new/complete enough
// import libraries.
#define LOAD_MF_FUNCTION(context, func_name) \
    context->functions.func_name = func_name; \
    if (!context->functions.func_name) { \
        av_log(context, AV_LOG_ERROR, "Failed to find function " #func_name \
               "\n"); \
        return AVERROR_UNKNOWN; \
    }
#endif

// Windows N editions does not provide MediaFoundation by default.
// So to avoid DLL loading error, MediaFoundation is dynamically loaded except
// on UWP build since LoadLibrary is not available on it.
static int mf_load_library(AVCodecContext *avctx)
{
    MFContext *c = avctx->priv_data;

#if !HAVE_UWP
    c->library = dlopen("mfplat.dll", 0);

    if (!c->library) {
        av_log(c, AV_LOG_ERROR, "DLL mfplat.dll failed to open\n");
        return AVERROR_UNKNOWN;
    }
#endif

    LOAD_MF_FUNCTION(c, MFStartup);
    LOAD_MF_FUNCTION(c, MFShutdown);
    LOAD_MF_FUNCTION(c, MFCreateAlignedMemoryBuffer);
    LOAD_MF_FUNCTION(c, MFCreateSample);
    LOAD_MF_FUNCTION(c, MFCreateMediaType);
    // MFTEnumEx is missing in Windows Vista's mfplat.dll.
    LOAD_MF_FUNCTION(c, MFTEnumEx);

    return 0;
}

static int mf_close(AVCodecContext *avctx)
{
    MFContext *c = avctx->priv_data;

    if (c->codec_api)
        ICodecAPI_Release(c->codec_api);

    if (c->async_events)
        IMFMediaEventGenerator_Release(c->async_events);

#if !HAVE_UWP
    if (c->library)
        ff_free_mf(&c->functions, &c->mft);

    dlclose(c->library);
    c->library = NULL;
#else
    ff_free_mf(&c->functions, &c->mft);
#endif

    av_frame_free(&c->frame);

    av_freep(&avctx->extradata);
    avctx->extradata_size = 0;

    return 0;
}

static int mf_init(AVCodecContext *avctx)
{
    int ret;
    if ((ret = mf_load_library(avctx)) == 0) {
           if ((ret = mf_init_encoder(avctx)) == 0) {
                return 0;
        }
    }
    return ret;
}

#define OFFSET(x) offsetof(MFContext, x)

#define MF_ENCODER(MEDIATYPE, NAME, ID, OPTS, FMTS, CAPS, DEFAULTS) \
    static const AVClass ff_ ## NAME ## _mf_encoder_class = {                  \
        .class_name = #NAME "_mf",                                             \
        .item_name  = av_default_item_name,                                    \
        .option     = OPTS,                                                    \
        .version    = LIBAVUTIL_VERSION_INT,                                   \
    };                                                                         \
    const FFCodec ff_ ## NAME ## _mf_encoder = {                               \
        .p.priv_class   = &ff_ ## NAME ## _mf_encoder_class,                   \
        .p.name         = #NAME "_mf",                                         \
        CODEC_LONG_NAME(#ID " via MediaFoundation"),                           \
        .p.type         = AVMEDIA_TYPE_ ## MEDIATYPE,                          \
        .p.id           = AV_CODEC_ID_ ## ID,                                  \
        .priv_data_size = sizeof(MFContext),                                   \
        .init           = mf_init,                                             \
        .close          = mf_close,                                            \
        FF_CODEC_RECEIVE_PACKET_CB(mf_receive_packet),                         \
        FMTS                                                                   \
        CAPS                                                                   \
        .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,                           \
        .defaults       = DEFAULTS,                                            \
    };

#define AFMTS \
        .p.sample_fmts  = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,    \
                                                         AV_SAMPLE_FMT_NONE },
#define ACAPS \
        .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HYBRID |           \
                          AV_CODEC_CAP_DR1 | AV_CODEC_CAP_VARIABLE_FRAME_SIZE,

MF_ENCODER(AUDIO, aac,         AAC, NULL, AFMTS, ACAPS, NULL);
MF_ENCODER(AUDIO, ac3,         AC3, NULL, AFMTS, ACAPS, NULL);
MF_ENCODER(AUDIO, mp3,         MP3, NULL, AFMTS, ACAPS, NULL);

#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption venc_opts[] = {
    {"rate_control",  "Select rate control mode", OFFSET(opt_enc_rc), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, VE, .unit = "rate_control"},
    { "default",      "Default mode", 0, AV_OPT_TYPE_CONST, {.i64 = -1}, 0, 0, VE, .unit = "rate_control"},
    { "cbr",          "CBR mode", 0, AV_OPT_TYPE_CONST, {.i64 = ff_eAVEncCommonRateControlMode_CBR}, 0, 0, VE, .unit = "rate_control"},
    { "pc_vbr",       "Peak constrained VBR mode", 0, AV_OPT_TYPE_CONST, {.i64 = ff_eAVEncCommonRateControlMode_PeakConstrainedVBR}, 0, 0, VE, .unit = "rate_control"},
    { "u_vbr",        "Unconstrained VBR mode", 0, AV_OPT_TYPE_CONST, {.i64 = ff_eAVEncCommonRateControlMode_UnconstrainedVBR}, 0, 0, VE, .unit = "rate_control"},
    { "quality",      "Quality mode", 0, AV_OPT_TYPE_CONST, {.i64 = ff_eAVEncCommonRateControlMode_Quality}, 0, 0, VE, .unit = "rate_control" },
    // The following rate_control modes require Windows 8.
    { "ld_vbr",       "Low delay VBR mode", 0, AV_OPT_TYPE_CONST, {.i64 = ff_eAVEncCommonRateControlMode_LowDelayVBR}, 0, 0, VE, .unit = "rate_control"},
    { "g_vbr",        "Global VBR mode", 0, AV_OPT_TYPE_CONST, {.i64 = ff_eAVEncCommonRateControlMode_GlobalVBR}, 0, 0, VE, .unit = "rate_control" },
    { "gld_vbr",      "Global low delay VBR mode", 0, AV_OPT_TYPE_CONST, {.i64 = ff_eAVEncCommonRateControlMode_GlobalLowDelayVBR}, 0, 0, VE, .unit = "rate_control"},

    {"scenario",          "Select usage scenario", OFFSET(opt_enc_scenario), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, VE, .unit = "scenario"},
    { "default",          "Default scenario", 0, AV_OPT_TYPE_CONST, {.i64 = -1}, 0, 0, VE, .unit = "scenario"},
    { "display_remoting", "Display remoting", 0, AV_OPT_TYPE_CONST, {.i64 = ff_eAVScenarioInfo_DisplayRemoting}, 0, 0, VE, .unit = "scenario"},
    { "video_conference", "Video conference", 0, AV_OPT_TYPE_CONST, {.i64 = ff_eAVScenarioInfo_VideoConference}, 0, 0, VE, .unit = "scenario"},
    { "archive",          "Archive", 0, AV_OPT_TYPE_CONST, {.i64 = ff_eAVScenarioInfo_Archive}, 0, 0, VE, .unit = "scenario"},
    { "live_streaming",   "Live streaming", 0, AV_OPT_TYPE_CONST, {.i64 = ff_eAVScenarioInfo_LiveStreaming}, 0, 0, VE, .unit = "scenario"},
    { "camera_record",    "Camera record", 0, AV_OPT_TYPE_CONST, {.i64 = ff_eAVScenarioInfo_CameraRecord}, 0, 0, VE, .unit = "scenario"},
    { "display_remoting_with_feature_map", "Display remoting with feature map", 0, AV_OPT_TYPE_CONST, {.i64 = ff_eAVScenarioInfo_DisplayRemotingWithFeatureMap}, 0, 0, VE, .unit = "scenario"},

    {"quality",       "Quality", OFFSET(opt_enc_quality), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 100, VE},
    {"hw_encoding",   "Force hardware encoding", OFFSET(opt_enc_hw), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, VE},
    {NULL}
};

static const FFCodecDefault defaults[] = {
    { "g", "0" },
    { NULL },
};

#define VFMTS \
        .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_NV12,       \
                                                        AV_PIX_FMT_YUV420P,    \
                                                        AV_PIX_FMT_NONE },
#define VCAPS \
        .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HYBRID |           \
                          AV_CODEC_CAP_DR1,

MF_ENCODER(VIDEO, h264,        H264, venc_opts, VFMTS, VCAPS, defaults);
MF_ENCODER(VIDEO, hevc,        HEVC, venc_opts, VFMTS, VCAPS, defaults);
