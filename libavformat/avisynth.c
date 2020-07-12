/*
 * AviSynth(+) support
 * Copyright (c) 2012 AvxSynth Team
 *
 * This file is part of FFmpeg
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

#include "libavutil/attributes.h"
#include "libavutil/internal.h"

#include "libavcodec/internal.h"

#include "avformat.h"
#include "internal.h"
#include "config.h"

/* Enable function pointer definitions for runtime loading. */
#define AVSC_NO_DECLSPEC

/* Platform-specific directives. */
#ifdef _WIN32
  #include "compat/w32dlfcn.h"
  #undef EXTERN_C
  #define AVISYNTH_LIB "avisynth"
#else
  #include <dlfcn.h>
  #define AVISYNTH_NAME "libavisynth"
  #define AVISYNTH_LIB AVISYNTH_NAME SLIBSUF
#endif

#include <avisynth/avisynth_c.h>

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
    AVSC_DECLARE_FUNC(avs_bits_per_pixel);
    AVSC_DECLARE_FUNC(avs_get_height_p);
    AVSC_DECLARE_FUNC(avs_get_pitch_p);
    AVSC_DECLARE_FUNC(avs_get_read_ptr_p);
    AVSC_DECLARE_FUNC(avs_get_row_size_p);
    AVSC_DECLARE_FUNC(avs_is_planar_rgb);
    AVSC_DECLARE_FUNC(avs_is_planar_rgba);
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
static const int avs_planes_rgb[3]    = { AVS_PLANAR_G, AVS_PLANAR_B,
                                          AVS_PLANAR_R };
static const int avs_planes_yuva[4]   = { AVS_PLANAR_Y, AVS_PLANAR_U,
                                          AVS_PLANAR_V, AVS_PLANAR_A };
static const int avs_planes_rgba[4]   = { AVS_PLANAR_G, AVS_PLANAR_B,
                                          AVS_PLANAR_R, AVS_PLANAR_A };

/* A conflict between C++ global objects, atexit, and dynamic loading requires
 * us to register our own atexit handler to prevent double freeing. */
static AviSynthLibrary avs_library;
static int avs_atexit_called        = 0;

/* Linked list of AviSynthContexts. An atexit handler destroys this list. */
static AviSynthContext *avs_ctx_list = NULL;

static av_cold void avisynth_atexit_handler(void);

static av_cold int avisynth_load_library(void)
{
    avs_library.library = dlopen(AVISYNTH_LIB, RTLD_NOW | RTLD_LOCAL);
    if (!avs_library.library)
        return AVERROR_UNKNOWN;

#define LOAD_AVS_FUNC(name, continue_on_fail)                          \
        avs_library.name = (name ## _func)                             \
                           dlsym(avs_library.library, #name);          \
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
    LOAD_AVS_FUNC(avs_bits_per_pixel, 1);
    LOAD_AVS_FUNC(avs_get_height_p, 1);
    LOAD_AVS_FUNC(avs_get_pitch_p, 1);
    LOAD_AVS_FUNC(avs_get_read_ptr_p, 1);
    LOAD_AVS_FUNC(avs_get_row_size_p, 1);
    LOAD_AVS_FUNC(avs_is_planar_rgb, 1);
    LOAD_AVS_FUNC(avs_is_planar_rgba, 1);
#undef LOAD_AVS_FUNC

    atexit(avisynth_atexit_handler);
    return 0;

fail:
    dlclose(avs_library.library);
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
    dlclose(avs_library.library);

    avs_atexit_called = 1;
}

/* Create AVStream from audio and video data. */
static int avisynth_create_stream_video(AVFormatContext *s, AVStream *st)
{
    AviSynthContext *avs = s->priv_data;
    int planar = 0; // 0: packed, 1: YUV, 2: Y8, 3: Planar RGB, 4: YUVA, 5: Planar RGBA

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
    st->codecpar->width      = avs->vi->width;
    st->codecpar->height     = avs->vi->height;

    st->avg_frame_rate    = (AVRational) { avs->vi->fps_numerator,
                                           avs->vi->fps_denominator };
    st->start_time        = 0;
    st->duration          = avs->vi->num_frames;
    st->nb_frames         = avs->vi->num_frames;
    avpriv_set_pts_info(st, 32, avs->vi->fps_denominator, avs->vi->fps_numerator);

    switch (avs->vi->pixel_type) {
    /* 10~16-bit YUV pix_fmts (AviSynth+) */
    case AVS_CS_YUV444P10:
        st->codecpar->format = AV_PIX_FMT_YUV444P10;
        planar               = 1;
        break;
    case AVS_CS_YUV422P10:
        st->codecpar->format = AV_PIX_FMT_YUV422P10;
        planar               = 1;
        break;
    case AVS_CS_YUV420P10:
        st->codecpar->format = AV_PIX_FMT_YUV420P10;
        planar               = 1;
        break;
    case AVS_CS_YUV444P12:
        st->codecpar->format = AV_PIX_FMT_YUV444P12;
        planar               = 1;
        break;
    case AVS_CS_YUV422P12:
        st->codecpar->format = AV_PIX_FMT_YUV422P12;
        planar               = 1;
        break;
    case AVS_CS_YUV420P12:
        st->codecpar->format = AV_PIX_FMT_YUV420P12;
        planar               = 1;
        break;
    case AVS_CS_YUV444P14:
        st->codecpar->format = AV_PIX_FMT_YUV444P14;
        planar               = 1;
        break;
    case AVS_CS_YUV422P14:
        st->codecpar->format = AV_PIX_FMT_YUV422P14;
        planar               = 1;
        break;
    case AVS_CS_YUV420P14:
        st->codecpar->format = AV_PIX_FMT_YUV420P14;
        planar               = 1;
        break;
    case AVS_CS_YUV444P16:
        st->codecpar->format = AV_PIX_FMT_YUV444P16;
        planar               = 1;
        break;
    case AVS_CS_YUV422P16:
        st->codecpar->format = AV_PIX_FMT_YUV422P16;
        planar               = 1;
        break;
    case AVS_CS_YUV420P16:
        st->codecpar->format = AV_PIX_FMT_YUV420P16;
        planar               = 1;
        break;
    /* 8~16-bit YUV pix_fmts with Alpha (AviSynth+) */
    case AVS_CS_YUVA444:
        st->codecpar->format = AV_PIX_FMT_YUVA444P;
        planar               = 4;
        break;
    case AVS_CS_YUVA422:
        st->codecpar->format = AV_PIX_FMT_YUVA422P;
        planar               = 4;
        break;
    case AVS_CS_YUVA420:
        st->codecpar->format = AV_PIX_FMT_YUVA420P;
        planar               = 4;
        break;
    case AVS_CS_YUVA444P10:
        st->codecpar->format = AV_PIX_FMT_YUVA444P10;
        planar               = 4;
        break;
    case AVS_CS_YUVA422P10:
        st->codecpar->format = AV_PIX_FMT_YUVA422P10;
        planar               = 4;
        break;
    case AVS_CS_YUVA420P10:
        st->codecpar->format = AV_PIX_FMT_YUVA420P10;
        planar               = 4;
        break;
    case AVS_CS_YUVA422P12:
        st->codecpar->format = AV_PIX_FMT_YUVA422P12;
        planar               = 4;
        break;
    case AVS_CS_YUVA444P16:
        st->codecpar->format = AV_PIX_FMT_YUVA444P16;
        planar               = 4;
        break;
    case AVS_CS_YUVA422P16:
        st->codecpar->format = AV_PIX_FMT_YUVA422P16;
        planar               = 4;
        break;
    case AVS_CS_YUVA420P16:
        st->codecpar->format = AV_PIX_FMT_YUVA420P16;
        planar               = 4;
        break;
    /* Planar RGB pix_fmts (AviSynth+) */
    case AVS_CS_RGBP:
        st->codecpar->format = AV_PIX_FMT_GBRP;
        planar               = 3;
        break;
    case AVS_CS_RGBP10:
        st->codecpar->format = AV_PIX_FMT_GBRP10;
        planar               = 3;
        break;
    case AVS_CS_RGBP12:
        st->codecpar->format = AV_PIX_FMT_GBRP12;
        planar               = 3;
        break;
    case AVS_CS_RGBP14:
        st->codecpar->format = AV_PIX_FMT_GBRP14;
        planar               = 3;
        break;
    case AVS_CS_RGBP16:
        st->codecpar->format = AV_PIX_FMT_GBRP16;
        planar               = 3;
        break;
    /* Single precision floating point Planar RGB (AviSynth+) */
    case AVS_CS_RGBPS:
        st->codecpar->format = AV_PIX_FMT_GBRPF32;
        planar               = 3;
        break;
    /* Planar RGB pix_fmts with Alpha (AviSynth+) */
    case AVS_CS_RGBAP:
        st->codecpar->format = AV_PIX_FMT_GBRAP;
        planar               = 5;
        break;
    case AVS_CS_RGBAP10:
        st->codecpar->format = AV_PIX_FMT_GBRAP10;
        planar               = 5;
        break;
    case AVS_CS_RGBAP12:
        st->codecpar->format = AV_PIX_FMT_GBRAP12;
        planar               = 5;
        break;
    case AVS_CS_RGBAP16:
        st->codecpar->format = AV_PIX_FMT_GBRAP16;
        planar               = 5;
        break;
    /* Single precision floating point Planar RGB with Alpha (AviSynth+) */
    case AVS_CS_RGBAPS:
        st->codecpar->format = AV_PIX_FMT_GBRAPF32;
        planar               = 5;
        break;
    /* 10~16-bit gray pix_fmts (AviSynth+) */
    case AVS_CS_Y10:
        st->codecpar->format = AV_PIX_FMT_GRAY10;
        planar               = 2;
        break;
    case AVS_CS_Y12:
        st->codecpar->format = AV_PIX_FMT_GRAY12;
        planar               = 2;
        break;
    case AVS_CS_Y14:
        st->codecpar->format = AV_PIX_FMT_GRAY14;
        planar               = 2;
        break;
    case AVS_CS_Y16:
        st->codecpar->format = AV_PIX_FMT_GRAY16;
        planar               = 2;
        break;
    /* Single precision floating point gray (AviSynth+) */
    case AVS_CS_Y32:
        st->codecpar->format = AV_PIX_FMT_GRAYF32;
        planar               = 2;
        break;
    /* pix_fmts added in AviSynth 2.6 */
    case AVS_CS_YV24:
        st->codecpar->format = AV_PIX_FMT_YUV444P;
        planar               = 1;
        break;
    case AVS_CS_YV16:
        st->codecpar->format = AV_PIX_FMT_YUV422P;
        planar               = 1;
        break;
    case AVS_CS_YV411:
        st->codecpar->format = AV_PIX_FMT_YUV411P;
        planar               = 1;
        break;
    case AVS_CS_Y8:
        st->codecpar->format = AV_PIX_FMT_GRAY8;
        planar               = 2;
        break;
    /* 16-bit packed RGB pix_fmts (AviSynth+) */
    case AVS_CS_BGR48:
        st->codecpar->format = AV_PIX_FMT_BGR48;
        break;
    case AVS_CS_BGR64:
        st->codecpar->format = AV_PIX_FMT_BGRA64;
        break;
    /* AviSynth 2.5 pix_fmts */
    case AVS_CS_BGR24:
        st->codecpar->format = AV_PIX_FMT_BGR24;
        break;
    case AVS_CS_BGR32:
        st->codecpar->format = AV_PIX_FMT_RGB32;
        break;
    case AVS_CS_YUY2:
        st->codecpar->format = AV_PIX_FMT_YUYV422;
        break;
    case AVS_CS_YV12:
        st->codecpar->format = AV_PIX_FMT_YUV420P;
        planar               = 1;
        break;
    case AVS_CS_I420: // Is this even used anywhere?
        st->codecpar->format = AV_PIX_FMT_YUV420P;
        planar               = 1;
        break;
    default:
        av_log(s, AV_LOG_ERROR,
               "unknown AviSynth colorspace %d\n", avs->vi->pixel_type);
        avs->error = 1;
        return AVERROR_UNKNOWN;
    }

    switch (planar) {
    case 5: // Planar RGB + Alpha
        avs->n_planes = 4;
        avs->planes   = avs_planes_rgba;
        break;
    case 4: // YUV + Alpha
        avs->n_planes = 4;
        avs->planes   = avs_planes_yuva;
        break;
    case 3: // Planar RGB
        avs->n_planes = 3;
        avs->planes   = avs_planes_rgb;
        break;
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

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->sample_rate = avs->vi->audio_samples_per_second;
    st->codecpar->channels    = avs->vi->nchannels;
    st->duration              = avs->vi->num_audio_samples;
    avpriv_set_pts_info(st, 64, 1, avs->vi->audio_samples_per_second);

    switch (avs->vi->sample_type) {
    case AVS_SAMPLE_INT8:
        st->codecpar->codec_id = AV_CODEC_ID_PCM_U8;
        break;
    case AVS_SAMPLE_INT16:
        st->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
        break;
    case AVS_SAMPLE_INT24:
        st->codecpar->codec_id = AV_CODEC_ID_PCM_S24LE;
        break;
    case AVS_SAMPLE_INT32:
        st->codecpar->codec_id = AV_CODEC_ID_PCM_S32LE;
        break;
    case AVS_SAMPLE_FLOAT:
        st->codecpar->codec_id = AV_CODEC_ID_PCM_F32LE;
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
#ifdef _WIN32
    char filename_ansi[MAX_PATH * 4];
    wchar_t filename_wc[MAX_PATH * 4];
#endif

    if (ret = avisynth_context_create(s))
        return ret;

#ifdef _WIN32
    /* Convert UTF-8 to ANSI code page */
    MultiByteToWideChar(CP_UTF8, 0, s->url, -1, filename_wc, MAX_PATH * 4);
    WideCharToMultiByte(CP_THREAD_ACP, 0, filename_wc, -1, filename_ansi,
                        MAX_PATH * 4, NULL, NULL);
    arg = avs_new_value_string(filename_ansi);
#else
    arg = avs_new_value_string(s->url);
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

    /* On Windows, FFmpeg supports AviSynth interface version 6 or higher.
     * This includes AviSynth 2.6 RC1 or higher, and AviSynth+ r1718 or higher,
     * and excludes 2.5 and the 2.6 alphas. */

    if (avs_library.avs_get_version(avs->clip) < 6) {
        av_log(s, AV_LOG_ERROR,
               "AviSynth version is too old. Please upgrade to either AviSynth 2.6 >= RC1 or AviSynth+ >= r1718.\n");
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

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
    int n, i, plane, rowsize, planeheight, pitch, bits, ret;
    const char *error;
    int avsplus av_unused;

    if (avs->curr_frame >= avs->vi->num_frames)
        return AVERROR_EOF;

    /* This must happen even if the stream is discarded to prevent desync. */
    n = avs->curr_frame++;
    if (discard)
        return 0;

#ifdef _WIN32
    /* Detect whether we're using AviSynth 2.6 or AviSynth+ by
     * looking for whether avs_is_planar_rgb exists. */
    if (GetProcAddress(avs_library.library, "avs_is_planar_rgb") == NULL)
        avsplus = 0;
    else
        avsplus = 1;
#else
    /* AviSynth+ is now the only variant of AviSynth we support
     * on Linux and macOS. */
    avsplus = 1;
#endif

    bits = avs_library.avs_bits_per_pixel(avs->vi);

    /* Without the cast to int64_t, calculation overflows at about 9k x 9k
     * resolution. */
    pkt->size = (((int64_t)avs->vi->width *
                  (int64_t)avs->vi->height) * bits) / 8;
    if (!pkt->size)
        return AVERROR_UNKNOWN;

    if ((ret = av_new_packet(pkt, pkt->size)) < 0)
        return ret;

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
        src_p = avs_library.avs_get_read_ptr_p(frame, plane);
        pitch = avs_library.avs_get_pitch_p(frame, plane);

        rowsize     = avs_library.avs_get_row_size_p(frame, plane);
        planeheight = avs_library.avs_get_height_p(frame, plane);

        /* Flip RGB video. */
        if (avs_is_rgb24(avs->vi) || avs_is_rgb(avs->vi)) {
            src_p = src_p + (planeheight - 1) * pitch;
            pitch = -pitch;
        }

        /* Flip Planar RGB video */
        if (avsplus && (avs_library.avs_is_planar_rgb(avs->vi) ||
                        avs_library.avs_is_planar_rgba(avs->vi))) {
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
    int samples, ret;
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

    if ((ret = av_new_packet(pkt, pkt->size)) < 0)
        return ret;

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
    if (ret = ff_lock_avformat())
        return ret;

    if (ret = avisynth_open_file(s)) {
        ff_unlock_avformat();
        return ret;
    }

    ff_unlock_avformat();
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
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
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
    if (ff_lock_avformat())
        return AVERROR_UNKNOWN;

    avisynth_context_destroy(s->priv_data);
    ff_unlock_avformat();
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
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
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
