/*
 * AviSynth/AvxSynth support
 * Copyright (c) 2012 AvxSynth Team.
 *
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

#include "libavutil/internal.h"
#include "libavcodec/internal.h"
#include "avformat.h"
#include "internal.h"

/* Enable function pointer definitions for runtime loading. */
#define AVSC_NO_DECLSPEC

/* Platform-specific directives for AviSynth vs AvxSynth.
 *
 * avisynth_c.h needs to be the one provided with x264, as
 * the one in AviSynth's CVS hasn't been updated to support
 * 2.6's extra colorspaces. A temporary source of that header,
 * installable from a GNU-style Makefile is available from
 * github.com/qyot27/avisynth_headers -- AvxSynth doesn't
 * require this kind of special treatment because like any
 * standard *nix application, it installs its headers
 * alongside its libs. */
#ifdef _WIN32
  #include <windows.h>
  #undef EXTERN_C
  #include <avisynth/avisynth_c.h>
  #define AVISYNTH_LIB "avisynth"
  #define USING_AVISYNTH
#else
  #include <dlfcn.h>
  #include <avxsynth/avxsynth_c.h>
    #if defined (__APPLE__)
      #define AVISYNTH_LIB "libavxsynth.dylib"
    #else
      #define AVISYNTH_LIB "libavxsynth.so"
    #endif

  #define LoadLibrary(x) dlopen(x, RTLD_NOW | RTLD_GLOBAL)
  #define GetProcAddress dlsym
  #define FreeLibrary dlclose
#endif

typedef struct AviSynthLibrary {
    void *library;
#define AVSC_DECLARE_FUNC(name) name ## _func name
    AVSC_DECLARE_FUNC(avs_bit_blt);
    AVSC_DECLARE_FUNC(avs_clip_get_error);
    AVSC_DECLARE_FUNC(avs_create_script_environment);
    AVSC_DECLARE_FUNC(avs_delete_script_environment);
    AVSC_DECLARE_FUNC(avs_get_audio);
    AVSC_DECLARE_FUNC(avs_get_error);
    AVSC_DECLARE_FUNC(avs_get_frame);
    AVSC_DECLARE_FUNC(avs_get_version);
    AVSC_DECLARE_FUNC(avs_get_video_info);
    AVSC_DECLARE_FUNC(avs_invoke);
    AVSC_DECLARE_FUNC(avs_release_clip);
    AVSC_DECLARE_FUNC(avs_release_value);
    AVSC_DECLARE_FUNC(avs_release_video_frame);
    AVSC_DECLARE_FUNC(avs_take_clip);
#undef AVSC_DECLARE_FUNC
} AviSynthLibrary;

typedef struct AviSynthContext {
    AVS_ScriptEnvironment *env;
    AVS_Clip *clip;
    const AVS_VideoInfo *vi;

    /* avisynth_read_packet_video() iterates over this. */
    int n_planes;
    const int *planes;

    int curr_stream;
    int curr_frame;
    int64_t curr_sample;

    int error;

    /* Linked list pointers. */
    struct AviSynthContext *next;
} AviSynthContext;

static const int avs_planes_packed[1] = { 0 };
static const int avs_planes_grey[1]   = { AVS_PLANAR_Y };
static const int avs_planes_yuv[3]    = { AVS_PLANAR_Y, AVS_PLANAR_U,
                                          AVS_PLANAR_V };

/* A conflict between C++ global objects, atexit, and dynamic loading requires
 * us to register our own atexit handler to prevent double freeing. */
static AviSynthLibrary avs_library;
static int avs_atexit_called        = 0;

/* Linked list of AviSynthContexts. An atexit handler destroys this list. */
static AviSynthContext *avs_ctx_list = NULL;

static av_cold void avisynth_atexit_handler(void);

static av_cold int avisynth_load_library(void)
{
    avs_library.library = LoadLibrary(AVISYNTH_LIB);
    if (!avs_library.library)
        return AVERROR_UNKNOWN;

#define LOAD_AVS_FUNC(name, continue_on_fail)                          \
        avs_library.name =                                             \
            (void *)GetProcAddress(avs_library.library, #name);        \
        if (!continue_on_fail && !avs_library.name)                    \
            goto fail;

    LOAD_AVS_FUNC(avs_bit_blt, 0);
    LOAD_AVS_FUNC(avs_clip_get_error, 0);
    LOAD_AVS_FUNC(avs_create_script_environment, 0);
    LOAD_AVS_FUNC(avs_delete_script_environment, 0);
    LOAD_AVS_FUNC(avs_get_audio, 0);
    LOAD_AVS_FUNC(avs_get_error, 1); // New to AviSynth 2.6
    LOAD_AVS_FUNC(avs_get_frame, 0);
    LOAD_AVS_FUNC(avs_get_version, 0);
    LOAD_AVS_FUNC(avs_get_video_info, 0);
    LOAD_AVS_FUNC(avs_invoke, 0);
    LOAD_AVS_FUNC(avs_release_clip, 0);
    LOAD_AVS_FUNC(avs_release_value, 0);
    LOAD_AVS_FUNC(avs_release_video_frame, 0);
    LOAD_AVS_FUNC(avs_take_clip, 0);
#undef LOAD_AVS_FUNC

    atexit(avisynth_atexit_handler);
    return 0;

fail:
    FreeLibrary(avs_library.library);
    return AVERROR_UNKNOWN;
}

/* Note that avisynth_context_create and avisynth_context_destroy
 * do not allocate or free the actual context! That is taken care of
 * by libavformat. */
static av_cold int avisynth_context_create(AVFormatContext *s)
{
    AviSynthContext *avs = s->priv_data;
    int ret;

    if (!avs_library.library)
        if (ret = avisynth_load_library())
            return ret;

    avs->env = avs_library.avs_create_script_environment(3);
    if (avs_library.avs_get_error) {
        const char *error = avs_library.avs_get_error(avs->env);
        if (error) {
            av_log(s, AV_LOG_ERROR, "%s\n", error);
            return AVERROR_UNKNOWN;
        }
    }

    if (!avs_ctx_list) {
        avs_ctx_list = avs;
    } else {
        avs->next    = avs_ctx_list;
        avs_ctx_list = avs;
    }

    return 0;
}

static av_cold void avisynth_context_destroy(AviSynthContext *avs)
{
    if (avs_atexit_called)
        return;

    if (avs == avs_ctx_list) {
        avs_ctx_list = avs->next;
    } else {
        AviSynthContext *prev = avs_ctx_list;
        while (prev->next != avs)
            prev = prev->next;
        prev->next = avs->next;
    }

    if (avs->clip) {
        avs_library.avs_release_clip(avs->clip);
        avs->clip = NULL;
    }
    if (avs->env) {
        avs_library.avs_delete_script_environment(avs->env);
        avs->env = NULL;
    }
}

static av_cold void avisynth_atexit_handler(void)
{
    AviSynthContext *avs = avs_ctx_list;

    while (avs) {
        AviSynthContext *next = avs->next;
        avisynth_context_destroy(avs);
        avs = next;
    }
    FreeLibrary(avs_library.library);

    avs_atexit_called = 1;
}

/* Create AVStream from audio and video data. */
static int avisynth_create_stream_video(AVFormatContext *s, AVStream *st)
{
    AviSynthContext *avs = s->priv_data;
    int planar = 0; // 0: packed, 1: YUV, 2: Y8

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id   = AV_CODEC_ID_RAWVIDEO;
    st->codec->width      = avs->vi->width;
    st->codec->height     = avs->vi->height;

    st->time_base         = (AVRational) { avs->vi->fps_denominator,
                                           avs->vi->fps_numerator };
    st->avg_frame_rate    = (AVRational) { avs->vi->fps_numerator,
                                           avs->vi->fps_denominator };
    st->start_time        = 0;
    st->duration          = avs->vi->num_frames;
    st->nb_frames         = avs->vi->num_frames;

    switch (avs->vi->pixel_type) {
#ifdef USING_AVISYNTH
    case AVS_CS_YV24:
        st->codec->pix_fmt = AV_PIX_FMT_YUV444P;
        planar             = 1;
        break;
    case AVS_CS_YV16:
        st->codec->pix_fmt = AV_PIX_FMT_YUV422P;
        planar             = 1;
        break;
    case AVS_CS_YV411:
        st->codec->pix_fmt = AV_PIX_FMT_YUV411P;
        planar             = 1;
        break;
    case AVS_CS_Y8:
        st->codec->pix_fmt = AV_PIX_FMT_GRAY8;
        planar             = 2;
        break;
#endif
    case AVS_CS_BGR24:
        st->codec->pix_fmt = AV_PIX_FMT_BGR24;
        break;
    case AVS_CS_BGR32:
        st->codec->pix_fmt = AV_PIX_FMT_RGB32;
        break;
    case AVS_CS_YUY2:
        st->codec->pix_fmt = AV_PIX_FMT_YUYV422;
        break;
    case AVS_CS_YV12:
        st->codec->pix_fmt = AV_PIX_FMT_YUV420P;
        planar             = 1;
        break;
    case AVS_CS_I420: // Is this even used anywhere?
        st->codec->pix_fmt = AV_PIX_FMT_YUV420P;
        planar             = 1;
        break;
    default:
        av_log(s, AV_LOG_ERROR,
               "unknown AviSynth colorspace %d\n", avs->vi->pixel_type);
        avs->error = 1;
        return AVERROR_UNKNOWN;
    }

    switch (planar) {
    case 2: // Y8
        avs->n_planes = 1;
        avs->planes   = avs_planes_grey;
        break;
    case 1: // YUV
        avs->n_planes = 3;
        avs->planes   = avs_planes_yuv;
        break;
    default:
        avs->n_planes = 1;
        avs->planes   = avs_planes_packed;
    }
    return 0;
}

static int avisynth_create_stream_audio(AVFormatContext *s, AVStream *st)
{
    AviSynthContext *avs = s->priv_data;

    st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codec->sample_rate = avs->vi->audio_samples_per_second;
    st->codec->channels    = avs->vi->nchannels;
    st->time_base          = (AVRational) { 1,
                                            avs->vi->audio_samples_per_second };

    switch (avs->vi->sample_type) {
    case AVS_SAMPLE_INT8:
        st->codec->codec_id = AV_CODEC_ID_PCM_U8;
        break;
    case AVS_SAMPLE_INT16:
        st->codec->codec_id = AV_CODEC_ID_PCM_S16LE;
        break;
    case AVS_SAMPLE_INT24:
        st->codec->codec_id = AV_CODEC_ID_PCM_S24LE;
        break;
    case AVS_SAMPLE_INT32:
        st->codec->codec_id = AV_CODEC_ID_PCM_S32LE;
        break;
    case AVS_SAMPLE_FLOAT:
        st->codec->codec_id = AV_CODEC_ID_PCM_F32LE;
        break;
    default:
        av_log(s, AV_LOG_ERROR,
               "unknown AviSynth sample type %d\n", avs->vi->sample_type);
        avs->error = 1;
        return AVERROR_UNKNOWN;
    }
    return 0;
}

static int avisynth_create_stream(AVFormatContext *s)
{
    AviSynthContext *avs = s->priv_data;
    AVStream *st;
    int ret;
    int id = 0;

    if (avs_has_video(avs->vi)) {
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR_UNKNOWN;
        st->id = id++;
        if (ret = avisynth_create_stream_video(s, st))
            return ret;
    }
    if (avs_has_audio(avs->vi)) {
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR_UNKNOWN;
        st->id = id++;
        if (ret = avisynth_create_stream_audio(s, st))
            return ret;
    }
    return 0;
}

static int avisynth_open_file(AVFormatContext *s)
{
    AviSynthContext *avs = s->priv_data;
    AVS_Value arg, val;
    int ret;
#ifdef USING_AVISYNTH
    char filename_ansi[MAX_PATH * 4];
    wchar_t filename_wc[MAX_PATH * 4];
#endif

    if (ret = avisynth_context_create(s))
        return ret;

#ifdef USING_AVISYNTH
    /* Convert UTF-8 to ANSI code page */
    MultiByteToWideChar(CP_UTF8, 0, s->filename, -1, filename_wc, MAX_PATH * 4);
    WideCharToMultiByte(CP_THREAD_ACP, 0, filename_wc, -1, filename_ansi,
                        MAX_PATH * 4, NULL, NULL);
    arg = avs_new_value_string(filename_ansi);
#else
    arg = avs_new_value_string(s->filename);
#endif
    val = avs_library.avs_invoke(avs->env, "Import", arg, 0);
    if (avs_is_error(val)) {
        av_log(s, AV_LOG_ERROR, "%s\n", avs_as_error(val));
        ret = AVERROR_UNKNOWN;
        goto fail;
    }
    if (!avs_is_clip(val)) {
        av_log(s, AV_LOG_ERROR, "AviSynth script did not return a clip\n");
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    avs->clip = avs_library.avs_take_clip(val, avs->env);
    avs->vi   = avs_library.avs_get_video_info(avs->clip);

#ifdef USING_AVISYNTH
    /* libav only supports AviSynth 2.6 on Windows. Since AvxSynth
     * identifies itself as interface version 3 like 2.5.8, this
     * needs to be special-cased. */

    if (avs_library.avs_get_version(avs->clip) == 3) {
        av_log(s, AV_LOG_ERROR,
               "AviSynth 2.5.8 not supported. Please upgrade to 2.6.\n");
        ret = AVERROR_UNKNOWN;
        goto fail;
    }
#endif

    /* Release the AVS_Value as it will go out of scope. */
    avs_library.avs_release_value(val);

    if (ret = avisynth_create_stream(s))
        goto fail;

    return 0;

fail:
    avisynth_context_destroy(avs);
    return ret;
}

static void avisynth_next_stream(AVFormatContext *s, AVStream **st,
                                 AVPacket *pkt, int *discard)
{
    AviSynthContext *avs = s->priv_data;

    avs->curr_stream++;
    avs->curr_stream %= s->nb_streams;

    *st = s->streams[avs->curr_stream];
    if ((*st)->discard == AVDISCARD_ALL)
        *discard = 1;
    else
        *discard = 0;

    return;
}

/* Copy AviSynth clip data into an AVPacket. */
static int avisynth_read_packet_video(AVFormatContext *s, AVPacket *pkt,
                                      int discard)
{
    AviSynthContext *avs = s->priv_data;
    AVS_VideoFrame *frame;
    unsigned char *dst_p;
    const unsigned char *src_p;
    int n, i, plane, rowsize, planeheight, pitch, bits;
    const char *error;

    if (avs->curr_frame >= avs->vi->num_frames)
        return AVERROR_EOF;

    /* This must happen even if the stream is discarded to prevent desync. */
    n = avs->curr_frame++;
    if (discard)
        return 0;

#ifdef USING_AVISYNTH
    /* Define the bpp values for the new AviSynth 2.6 colorspaces.
     * Since AvxSynth doesn't have these functions, special-case
     * it in order to avoid implicit declaration errors. */

    if (avs_is_yv24(avs->vi))
        bits = 24;
    else if (avs_is_yv16(avs->vi))
        bits = 16;
    else if (avs_is_yv411(avs->vi))
        bits = 12;
    else if (avs_is_y8(avs->vi))
        bits = 8;
    else
#endif
        bits = avs_bits_per_pixel(avs->vi);

    /* Without the cast to int64_t, calculation overflows at about 9k x 9k
     * resolution. */
    pkt->size = (((int64_t)avs->vi->width *
                  (int64_t)avs->vi->height) * bits) / 8;
    if (!pkt->size)
        return AVERROR_UNKNOWN;

    if (av_new_packet(pkt, pkt->size) < 0)
        return AVERROR(ENOMEM);

    pkt->pts      = n;
    pkt->dts      = n;
    pkt->duration = 1;
    pkt->stream_index = avs->curr_stream;

    frame = avs_library.avs_get_frame(avs->clip, n);
    error = avs_library.avs_clip_get_error(avs->clip);
    if (error) {
        av_log(s, AV_LOG_ERROR, "%s\n", error);
        avs->error = 1;
        av_packet_unref(pkt);
        return AVERROR_UNKNOWN;
    }

    dst_p = pkt->data;
    for (i = 0; i < avs->n_planes; i++) {
        plane = avs->planes[i];
        src_p = avs_get_read_ptr_p(frame, plane);
        pitch = avs_get_pitch_p(frame, plane);

        rowsize     = avs_get_row_size_p(frame, plane);
        planeheight = avs_get_height_p(frame, plane);

        /* Flip RGB video. */
        if (avs_is_rgb24(avs->vi) || avs_is_rgb(avs->vi)) {
            src_p = src_p + (planeheight - 1) * pitch;
            pitch = -pitch;
        }

        avs_library.avs_bit_blt(avs->env, dst_p, rowsize, src_p, pitch,
                                 rowsize, planeheight);
        dst_p += rowsize * planeheight;
    }

    avs_library.avs_release_video_frame(frame);
    return 0;
}

static int avisynth_read_packet_audio(AVFormatContext *s, AVPacket *pkt,
                                      int discard)
{
    AviSynthContext *avs = s->priv_data;
    AVRational fps, samplerate;
    int samples;
    int64_t n;
    const char *error;

    if (avs->curr_sample >= avs->vi->num_audio_samples)
        return AVERROR_EOF;

    fps.num        = avs->vi->fps_numerator;
    fps.den        = avs->vi->fps_denominator;
    samplerate.num = avs->vi->audio_samples_per_second;
    samplerate.den = 1;

    if (avs_has_video(avs->vi)) {
        if (avs->curr_frame < avs->vi->num_frames)
            samples = av_rescale_q(avs->curr_frame, samplerate, fps) -
                      avs->curr_sample;
        else
            samples = av_rescale_q(1, samplerate, fps);
    } else {
        samples = 1000;
    }

    /* After seeking, audio may catch up with video. */
    if (samples <= 0) {
        pkt->size = 0;
        pkt->data = NULL;
        return 0;
    }

    if (avs->curr_sample + samples > avs->vi->num_audio_samples)
        samples = avs->vi->num_audio_samples - avs->curr_sample;

    /* This must happen even if the stream is discarded to prevent desync. */
    n                 = avs->curr_sample;
    avs->curr_sample += samples;
    if (discard)
        return 0;

    pkt->size = avs_bytes_per_channel_sample(avs->vi) *
                samples * avs->vi->nchannels;
    if (!pkt->size)
        return AVERROR_UNKNOWN;

    if (av_new_packet(pkt, pkt->size) < 0)
        return AVERROR(ENOMEM);

    pkt->pts      = n;
    pkt->dts      = n;
    pkt->duration = samples;
    pkt->stream_index = avs->curr_stream;

    avs_library.avs_get_audio(avs->clip, pkt->data, n, samples);
    error = avs_library.avs_clip_get_error(avs->clip);
    if (error) {
        av_log(s, AV_LOG_ERROR, "%s\n", error);
        avs->error = 1;
        av_packet_unref(pkt);
        return AVERROR_UNKNOWN;
    }
    return 0;
}

static av_cold int avisynth_read_header(AVFormatContext *s)
{
    int ret;

    // Calling library must implement a lock for thread-safe opens.
    if (ret = avpriv_lock_avformat())
        return ret;

    if (ret = avisynth_open_file(s)) {
        avpriv_unlock_avformat();
        return ret;
    }

    avpriv_unlock_avformat();
    return 0;
}

static int avisynth_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AviSynthContext *avs = s->priv_data;
    AVStream *st;
    int discard = 0;
    int ret;

    if (avs->error)
        return AVERROR_UNKNOWN;

    /* If either stream reaches EOF, try to read the other one before
     * giving up. */
    avisynth_next_stream(s, &st, pkt, &discard);
    if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = avisynth_read_packet_video(s, pkt, discard);
        if (ret == AVERROR_EOF && avs_has_audio(avs->vi)) {
            avisynth_next_stream(s, &st, pkt, &discard);
            return avisynth_read_packet_audio(s, pkt, discard);
        }
    } else {
        ret = avisynth_read_packet_audio(s, pkt, discard);
        if (ret == AVERROR_EOF && avs_has_video(avs->vi)) {
            avisynth_next_stream(s, &st, pkt, &discard);
            return avisynth_read_packet_video(s, pkt, discard);
        }
    }

    return ret;
}

static av_cold int avisynth_read_close(AVFormatContext *s)
{
    if (avpriv_lock_avformat())
        return AVERROR_UNKNOWN;

    avisynth_context_destroy(s->priv_data);
    avpriv_unlock_avformat();
    return 0;
}

static int avisynth_read_seek(AVFormatContext *s, int stream_index,
                              int64_t timestamp, int flags)
{
    AviSynthContext *avs = s->priv_data;
    AVStream *st;
    AVRational fps, samplerate;

    if (avs->error)
        return AVERROR_UNKNOWN;

    fps        = (AVRational) { avs->vi->fps_numerator,
                                avs->vi->fps_denominator };
    samplerate = (AVRational) { avs->vi->audio_samples_per_second, 1 };

    st = s->streams[stream_index];
    if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        /* AviSynth frame counts are signed int. */
        if ((timestamp >= avs->vi->num_frames) ||
            (timestamp > INT_MAX)              ||
            (timestamp < 0))
            return AVERROR_EOF;
        avs->curr_frame = timestamp;
        if (avs_has_audio(avs->vi))
            avs->curr_sample = av_rescale_q(timestamp, samplerate, fps);
    } else {
        if ((timestamp >= avs->vi->num_audio_samples) || (timestamp < 0))
            return AVERROR_EOF;
        /* Force frame granularity for seeking. */
        if (avs_has_video(avs->vi)) {
            avs->curr_frame  = av_rescale_q(timestamp, fps, samplerate);
            avs->curr_sample = av_rescale_q(avs->curr_frame, samplerate, fps);
        } else {
            avs->curr_sample = timestamp;
        }
    }

    return 0;
}

AVInputFormat ff_avisynth_demuxer = {
    .name           = "avisynth",
    .long_name      = NULL_IF_CONFIG_SMALL("AviSynth script"),
    .priv_data_size = sizeof(AviSynthContext),
    .read_header    = avisynth_read_header,
    .read_packet    = avisynth_read_packet,
    .read_close     = avisynth_read_close,
    .read_seek      = avisynth_read_seek,
    .extensions     = "avs",
};
