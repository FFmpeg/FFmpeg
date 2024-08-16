/*
 * Copyright (c) 2012 Stefano Sabatini
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

/**
 * @file
 * flite voice synth source
 */

#include <flite/flite.h>
#include "libavutil/audio_fifo.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/file.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"
#include "avfilter.h"
#include "filters.h"
#include "audio.h"
#include "formats.h"

typedef struct FliteContext {
    const AVClass *class;
    char *voice_str;
    char *textfile;
    char *text;
    char *text_p;
    char *text_saveptr;
    int nb_channels;
    int sample_rate;
    AVAudioFifo *fifo;
    int list_voices;
    cst_voice *voice;
    cst_audio_streaming_info *asi;
    struct voice_entry *voice_entry;
    int64_t pts;
    int frame_nb_samples; ///< number of samples per frame
} FliteContext;

#define OFFSET(x) offsetof(FliteContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption flite_options[] = {
    { "list_voices", "list voices and exit",              OFFSET(list_voices), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "nb_samples",  "set number of samples per frame",   OFFSET(frame_nb_samples), AV_OPT_TYPE_INT, {.i64=512}, 0, INT_MAX, FLAGS },
    { "n",           "set number of samples per frame",   OFFSET(frame_nb_samples), AV_OPT_TYPE_INT, {.i64=512}, 0, INT_MAX, FLAGS },
    { "text",        "set text to speak",                 OFFSET(text),      AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "textfile",    "set filename of the text to speak", OFFSET(textfile),  AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "v",           "set voice",                         OFFSET(voice_str), AV_OPT_TYPE_STRING, {.str="kal"}, 0, 0, FLAGS },
    { "voice",       "set voice",                         OFFSET(voice_str), AV_OPT_TYPE_STRING, {.str="kal"}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(flite);

static AVMutex flite_mutex = AV_MUTEX_INITIALIZER;

static int flite_inited = 0;

/* declare functions for all the supported voices */
#define DECLARE_REGISTER_VOICE_FN(name) \
    cst_voice *register_cmu_us_## name(const char *); \
    void     unregister_cmu_us_## name(cst_voice *)
DECLARE_REGISTER_VOICE_FN(awb);
DECLARE_REGISTER_VOICE_FN(kal);
DECLARE_REGISTER_VOICE_FN(kal16);
DECLARE_REGISTER_VOICE_FN(rms);
DECLARE_REGISTER_VOICE_FN(slt);

struct voice_entry {
    const char *name;
    cst_voice * (*register_fn)(const char *);
    void (*unregister_fn)(cst_voice *);
    cst_voice *voice;
    unsigned usage_count;
};

#define MAKE_VOICE_STRUCTURE(voice_name) {             \
    .name          =                      #voice_name, \
    .register_fn   =   register_cmu_us_ ## voice_name, \
    .unregister_fn = unregister_cmu_us_ ## voice_name, \
}
static struct voice_entry voice_entries[] = {
    MAKE_VOICE_STRUCTURE(awb),
    MAKE_VOICE_STRUCTURE(kal),
    MAKE_VOICE_STRUCTURE(kal16),
    MAKE_VOICE_STRUCTURE(rms),
    MAKE_VOICE_STRUCTURE(slt),
};

static void list_voices(void *log_ctx, const char *sep)
{
    int i, n = FF_ARRAY_ELEMS(voice_entries);
    for (i = 0; i < n; i++)
        av_log(log_ctx, AV_LOG_INFO, "%s%s",
               voice_entries[i].name, i < (n-1) ? sep : "\n");
}

static int select_voice(struct voice_entry **entry_ret, const char *voice_name, void *log_ctx)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(voice_entries); i++) {
        struct voice_entry *entry = &voice_entries[i];
        if (!strcmp(entry->name, voice_name)) {
            cst_voice *voice;
            pthread_mutex_lock(&flite_mutex);
            if (!entry->voice)
                entry->voice = entry->register_fn(NULL);
            voice = entry->voice;
            if (voice)
                entry->usage_count++;
            pthread_mutex_unlock(&flite_mutex);
            if (!voice) {
                av_log(log_ctx, AV_LOG_ERROR,
                       "Could not register voice '%s'\n", voice_name);
                return AVERROR_UNKNOWN;
            }
            *entry_ret = entry;
            return 0;
        }
    }

    av_log(log_ctx, AV_LOG_ERROR, "Could not find voice '%s'\n", voice_name);
    av_log(log_ctx, AV_LOG_INFO, "Choose between the voices: ");
    list_voices(log_ctx, ", ");

    return AVERROR(EINVAL);
}

static int audio_stream_chunk_by_word(const cst_wave *wave, int start, int size,
                                      int last, cst_audio_streaming_info *asi)
{
    FliteContext *flite = asi->userdata;
    void *const ptr[8] = { &wave->samples[start] };

    flite->nb_channels = wave->num_channels;
    flite->sample_rate = wave->sample_rate;
    if (!flite->fifo) {
        flite->fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, flite->nb_channels, size);
        if (!flite->fifo)
            return CST_AUDIO_STREAM_STOP;
    }

    av_audio_fifo_write(flite->fifo, ptr, size);

    return CST_AUDIO_STREAM_CONT;
}

static av_cold int init(AVFilterContext *ctx)
{
    FliteContext *flite = ctx->priv;
    int ret = 0;
    char *text;

    if (flite->list_voices) {
        list_voices(ctx, "\n");
        return AVERROR_EXIT;
    }

    pthread_mutex_lock(&flite_mutex);
    if (!flite_inited) {
        if ((ret = flite_init()) >= 0)
            flite_inited = 1;
    }
    pthread_mutex_unlock(&flite_mutex);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "flite initialization failed\n");
        return AVERROR_EXTERNAL;
    }

    if ((ret = select_voice(&flite->voice_entry, flite->voice_str, ctx)) < 0)
        return ret;
    flite->voice = flite->voice_entry->voice;

    if (flite->textfile && flite->text) {
        av_log(ctx, AV_LOG_ERROR,
               "Both text and textfile options set: only one must be specified\n");
        return AVERROR(EINVAL);
    }

    if (flite->textfile) {
        uint8_t *textbuf;
        size_t textbuf_size;

        if ((ret = av_file_map(flite->textfile, &textbuf, &textbuf_size, 0, ctx)) < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "The text file '%s' could not be read: %s\n",
                   flite->textfile, av_err2str(ret));
            return ret;
        }

        if (!(flite->text = av_malloc(textbuf_size+1))) {
            av_file_unmap(textbuf, textbuf_size);
            return AVERROR(ENOMEM);
        }
        memcpy(flite->text, textbuf, textbuf_size);
        flite->text[textbuf_size] = 0;
        av_file_unmap(textbuf, textbuf_size);
    }

    if (!flite->text) {
        av_log(ctx, AV_LOG_ERROR,
               "No speech text specified, specify the 'text' or 'textfile' option\n");
        return AVERROR(EINVAL);
    }

    flite->asi = new_audio_streaming_info();
    if (!flite->asi)
        return AVERROR_BUG;

    flite->asi->asc = audio_stream_chunk_by_word;
    flite->asi->userdata = flite;
    feat_set(flite->voice->features, "streaming_info", audio_streaming_info_val(flite->asi));

    flite->text_p = flite->text;
    if (!(text = av_strtok(flite->text_p, "\n", &flite->text_saveptr)))
        return AVERROR(EINVAL);
    flite->text_p = NULL;

    flite_text_to_speech(text, flite->voice, "none");

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FliteContext *flite = ctx->priv;

    if (flite->voice_entry) {
        pthread_mutex_lock(&flite_mutex);
        if (!--flite->voice_entry->usage_count) {
            flite->voice_entry->unregister_fn(flite->voice);
            flite->voice_entry->voice = NULL;
        }
        pthread_mutex_unlock(&flite_mutex);
    }
    av_audio_fifo_free(flite->fifo);
}

static int query_formats(AVFilterContext *ctx)
{
    FliteContext *flite = ctx->priv;
    int ret;

    AVFilterChannelLayouts *chlayouts = NULL;
    AVFilterFormats *sample_formats = NULL;
    AVFilterFormats *sample_rates = NULL;
    AVChannelLayout chlayout = { 0 };

    av_channel_layout_default(&chlayout, flite->nb_channels);

    if ((ret = ff_add_channel_layout         (&chlayouts     , &chlayout               )) < 0 ||
        (ret = ff_set_common_channel_layouts (ctx            , chlayouts               )) < 0 ||
        (ret = ff_add_format                 (&sample_formats, AV_SAMPLE_FMT_S16       )) < 0 ||
        (ret = ff_set_common_formats         (ctx            , sample_formats          )) < 0 ||
        (ret = ff_add_format                 (&sample_rates  , flite->sample_rate      )) < 0 ||
        (ret = ff_set_common_samplerates     (ctx            , sample_rates            )) < 0)
        return ret;

    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    FliteContext *flite = ctx->priv;

    outlink->sample_rate = flite->sample_rate;
    outlink->time_base = (AVRational){1, flite->sample_rate};

    av_log(ctx, AV_LOG_VERBOSE, "voice:%s fmt:%s sample_rate:%d\n",
           flite->voice_str,
           av_get_sample_fmt_name(outlink->format), outlink->sample_rate);

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    FliteContext *flite = ctx->priv;
    AVFrame *samplesref;
    int nb_samples;

    if (!ff_outlink_frame_wanted(outlink))
        return FFERROR_NOT_READY;

    nb_samples = FFMIN(av_audio_fifo_size(flite->fifo), flite->frame_nb_samples);
    if (!nb_samples) {
        char *text;

        if (!(text = av_strtok(flite->text_p, "\n", &flite->text_saveptr))) {
            ff_outlink_set_status(outlink, AVERROR_EOF, flite->pts);
            return 0;
        }

        flite_text_to_speech(text, flite->voice, "none");
        ff_filter_set_ready(ctx, 100);
        return 0;
    }

    samplesref = ff_get_audio_buffer(outlink, nb_samples);
    if (!samplesref)
        return AVERROR(ENOMEM);

    av_audio_fifo_read(flite->fifo, (void **)samplesref->extended_data,
                       nb_samples);

    samplesref->pts = flite->pts;
    samplesref->sample_rate = flite->sample_rate;
    flite->pts += nb_samples;

    return ff_filter_frame(outlink, samplesref);
}

static const AVFilterPad flite_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_props,
    },
};

const AVFilter ff_asrc_flite = {
    .name          = "flite",
    .description   = NULL_IF_CONFIG_SMALL("Synthesize voice from text using libflite."),
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(FliteContext),
    .activate      = activate,
    .inputs        = NULL,
    FILTER_OUTPUTS(flite_outputs),
    FILTER_QUERY_FUNC(query_formats),
    .priv_class    = &flite_class,
};
