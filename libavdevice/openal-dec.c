/*
 * Copyright (c) 2011 Jonathan Baldwin
 *
 * This file is part of FFmpeg.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * @file
 * OpenAL 1.1 capture device for libavdevice
 **/

#include <AL/al.h>
#include <AL/alc.h>

#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavformat/internal.h"
#include "avdevice.h"

typedef struct {
    AVClass *class;
    /** OpenAL capture device context. **/
    ALCdevice *device;
    /** The number of channels in the captured audio. **/
    int channels;
    /** The sample rate (in Hz) of the captured audio. **/
    int sample_rate;
    /** The sample size (in bits) of the captured audio. **/
    int sample_size;
    /** The OpenAL sample format of the captured audio. **/
    ALCenum sample_format;
    /** The number of bytes between two consecutive samples of the same channel/component. **/
    ALCint sample_step;
    /** If true, print a list of capture devices on this system and exit. **/
    int list_devices;
} al_data;

typedef struct {
    ALCenum al_fmt;
    enum AVCodecID codec_id;
    int channels;
} al_format_info;

#define LOWEST_AL_FORMAT FFMIN(FFMIN(AL_FORMAT_MONO8,AL_FORMAT_MONO16),FFMIN(AL_FORMAT_STEREO8,AL_FORMAT_STEREO16))

/**
 * Get information about an AL_FORMAT value.
 * @param al_fmt the AL_FORMAT value to find information about.
 * @return A pointer to a structure containing information about the AL_FORMAT value.
 */
static inline al_format_info* get_al_format_info(ALCenum al_fmt)
{
    static al_format_info info_table[] = {
        [AL_FORMAT_MONO8-LOWEST_AL_FORMAT]    = {AL_FORMAT_MONO8, AV_CODEC_ID_PCM_U8, 1},
        [AL_FORMAT_MONO16-LOWEST_AL_FORMAT]   = {AL_FORMAT_MONO16, AV_NE (AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE), 1},
        [AL_FORMAT_STEREO8-LOWEST_AL_FORMAT]  = {AL_FORMAT_STEREO8, AV_CODEC_ID_PCM_U8, 2},
        [AL_FORMAT_STEREO16-LOWEST_AL_FORMAT] = {AL_FORMAT_STEREO16, AV_NE (AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE), 2},
    };

    return &info_table[al_fmt-LOWEST_AL_FORMAT];
}

/**
 * Get the OpenAL error code, translated into an av/errno error code.
 * @param device The ALC device to check for errors.
 * @param error_msg_ret A pointer to a char* in which to return the error message, or NULL if desired.
 * @return The error code, or 0 if there is no error.
 */
static inline int al_get_error(ALCdevice *device, const char** error_msg_ret)
{
    ALCenum error = alcGetError(device);
    if (error_msg_ret)
        *error_msg_ret = (const char*) alcGetString(device, error);
    switch (error) {
    case ALC_NO_ERROR:
        return 0;
    case ALC_INVALID_DEVICE:
        return AVERROR(ENODEV);
        break;
    case ALC_INVALID_CONTEXT:
    case ALC_INVALID_ENUM:
    case ALC_INVALID_VALUE:
        return AVERROR(EINVAL);
        break;
    case ALC_OUT_OF_MEMORY:
        return AVERROR(ENOMEM);
        break;
    default:
        return AVERROR(EIO);
    }
}

/**
 * Print out a list of OpenAL capture devices on this system.
 */
static inline void print_al_capture_devices(void *log_ctx)
{
    const char *devices;

    if (!(devices = alcGetString(NULL, ALC_CAPTURE_DEVICE_SPECIFIER)))
        return;

    av_log(log_ctx, AV_LOG_INFO, "List of OpenAL capture devices on this system:\n");

    for (; *devices != '\0'; devices += strlen(devices) + 1)
        av_log(log_ctx, AV_LOG_INFO, "  %s\n", devices);
}

static int read_header(AVFormatContext *ctx)
{
    al_data *ad = ctx->priv_data;
    static const ALCenum sample_formats[2][2] = {
        { AL_FORMAT_MONO8,  AL_FORMAT_STEREO8  },
        { AL_FORMAT_MONO16, AL_FORMAT_STEREO16 }
    };
    int error = 0;
    const char *error_msg;
    AVStream *st = NULL;
    AVCodecContext *codec = NULL;

    if (ad->list_devices) {
        print_al_capture_devices(ctx);
        return AVERROR_EXIT;
    }

    ad->sample_format = sample_formats[ad->sample_size/8-1][ad->channels-1];

    /* Open device for capture */
    ad->device =
        alcCaptureOpenDevice(ctx->filename[0] ? ctx->filename : NULL,
                             ad->sample_rate,
                             ad->sample_format,
                             ad->sample_rate); /* Maximum 1 second of sample data to be read at once */

    if (error = al_get_error(ad->device, &error_msg)) goto fail;

    /* Create stream */
    if (!(st = avformat_new_stream(ctx, NULL))) {
        error = AVERROR(ENOMEM);
        goto fail;
    }

    /* We work in microseconds */
    avpriv_set_pts_info(st, 64, 1, 1000000);

    /* Set codec parameters */
    codec = st->codec;
    codec->codec_type = AVMEDIA_TYPE_AUDIO;
    codec->sample_rate = ad->sample_rate;
    codec->channels = get_al_format_info(ad->sample_format)->channels;
    codec->codec_id = get_al_format_info(ad->sample_format)->codec_id;

    /* This is needed to read the audio data */
    ad->sample_step = (av_get_bits_per_sample(get_al_format_info(ad->sample_format)->codec_id) *
                       get_al_format_info(ad->sample_format)->channels) / 8;

    /* Finally, start the capture process */
    alcCaptureStart(ad->device);

    return 0;

fail:
    /* Handle failure */
    if (ad->device)
        alcCaptureCloseDevice(ad->device);
    if (error_msg)
        av_log(ctx, AV_LOG_ERROR, "Cannot open device: %s\n", error_msg);
    return error;
}

static int read_packet(AVFormatContext* ctx, AVPacket *pkt)
{
    al_data *ad = ctx->priv_data;
    int error=0;
    const char *error_msg;
    ALCint nb_samples;

    /* Get number of samples available */
    alcGetIntegerv(ad->device, ALC_CAPTURE_SAMPLES, (ALCsizei) sizeof(ALCint), &nb_samples);
    if (error = al_get_error(ad->device, &error_msg)) goto fail;

    /* Create a packet of appropriate size */
    if ((error = av_new_packet(pkt, nb_samples*ad->sample_step)) < 0)
        goto fail;
    pkt->pts = av_gettime();

    /* Fill the packet with the available samples */
    alcCaptureSamples(ad->device, pkt->data, nb_samples);
    if (error = al_get_error(ad->device, &error_msg)) goto fail;

    return pkt->size;
fail:
    /* Handle failure */
    if (pkt->data)
        av_destruct_packet(pkt);
    if (error_msg)
        av_log(ctx, AV_LOG_ERROR, "Error: %s\n", error_msg);
    return error;
}

static int read_close(AVFormatContext* ctx)
{
    al_data *ad = ctx->priv_data;

    if (ad->device) {
        alcCaptureStop(ad->device);
        alcCaptureCloseDevice(ad->device);
    }
    return 0;
}

#define OFFSET(x) offsetof(al_data, x)

static const AVOption options[] = {
    {"channels", "set number of channels",     OFFSET(channels),     AV_OPT_TYPE_INT, {.i64=2},     1, 2,      AV_OPT_FLAG_DECODING_PARAM },
    {"sample_rate", "set sample rate",         OFFSET(sample_rate),  AV_OPT_TYPE_INT, {.i64=44100}, 1, 192000, AV_OPT_FLAG_DECODING_PARAM },
    {"sample_size", "set sample size",         OFFSET(sample_size),  AV_OPT_TYPE_INT, {.i64=16},    8, 16,     AV_OPT_FLAG_DECODING_PARAM },
    {"list_devices", "list available devices", OFFSET(list_devices), AV_OPT_TYPE_INT, {.i64=0},     0, 1,      AV_OPT_FLAG_DECODING_PARAM, "list_devices"  },
    {"true",  "", 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, "list_devices" },
    {"false", "", 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, "list_devices" },
    {NULL},
};

static const AVClass class = {
    .class_name = "openal",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
    .category = AV_CLASS_CATEGORY_DEVICE_AUDIO_INPUT,
};

AVInputFormat ff_openal_demuxer = {
    .name = "openal",
    .long_name = NULL_IF_CONFIG_SMALL("OpenAL audio capture device"),
    .priv_data_size = sizeof(al_data),
    .read_probe = NULL,
    .read_header = read_header,
    .read_packet = read_packet,
    .read_close = read_close,
    .flags = AVFMT_NOFILE,
    .priv_class = &class
};
