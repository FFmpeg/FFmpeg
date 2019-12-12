/*
 * Copyright (c) 2011 Mina Nagy Zaki
 * Copyright (c) 2000 Edward Beingessner And Sundry Contributors.
 * This source code is freely redistributable and may be used for any purpose.
 * This copyright notice must be maintained.  Edward Beingessner And Sundry
 * Contributors are not responsible for the consequences of using this
 * software.
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
 * Stereo Widening Effect. Adds audio cues to move stereo image in
 * front of the listener. Adapted from the libsox earwax effect.
 */

#include "libavutil/channel_layout.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"

#define NUMTAPS 64

static const int8_t filt[NUMTAPS] = {
/* 30°  330° */
    4,   -6,     /* 32 tap stereo FIR filter. */
    4,  -11,     /* One side filters as if the */
   -1,   -5,     /* signal was from 30 degrees */
    3,    3,     /* from the ear, the other as */
   -2,    5,     /* if 330 degrees. */
   -5,    0,
    9,    1,
    6,    3,     /*                         Input                         */
   -4,   -1,     /*                   Left         Right                  */
   -5,   -3,     /*                __________   __________                */
   -2,   -5,     /*               |          | |          |               */
   -7,    1,     /*           .---|  Hh,0(f) | |  Hh,0(f) |---.           */
    6,   -7,     /*          /    |__________| |__________|    \          */
   30,  -29,     /*         /                \ /                \         */
   12,   -3,     /*        /                  X                  \        */
  -11,    4,     /*       /                  / \                  \       */
   -3,    7,     /*  ____V_____   __________V   V__________   _____V____  */
  -20,   23,     /* |          | |          |   |          | |          | */
    2,    0,     /* | Hh,30(f) | | Hh,330(f)|   | Hh,330(f)| | Hh,30(f) | */
    1,   -6,     /* |__________| |__________|   |__________| |__________| */
  -14,   -5,     /*      \     ___      /           \      ___     /      */
   15,  -18,     /*       \   /   \    /    _____    \    /   \   /       */
    6,    7,     /*        `->| + |<--'    /     \    `-->| + |<-'        */
   15,  -10,     /*           \___/      _/       \_      \___/           */
  -14,   22,     /*               \     / \       / \     /               */
   -7,   -2,     /*                `--->| |       | |<---'                */
   -4,    9,     /*                     \_/       \_/                     */
    6,  -12,     /*                                                       */
    6,   -6,     /*                       Headphones                      */
    0,  -11,
    0,   -5,
    4,    0};

typedef struct EarwaxContext {
    int16_t taps[NUMTAPS * 2];
} EarwaxContext;

static int query_formats(AVFilterContext *ctx)
{
    static const int sample_rates[] = { 44100, -1 };
    int ret;

    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layout = NULL;

    if ((ret = ff_add_format                 (&formats, AV_SAMPLE_FMT_S16                 )) < 0 ||
        (ret = ff_set_common_formats         (ctx     , formats                           )) < 0 ||
        (ret = ff_add_channel_layout         (&layout , AV_CH_LAYOUT_STEREO               )) < 0 ||
        (ret = ff_set_common_channel_layouts (ctx     , layout                            )) < 0 ||
        (ret = ff_set_common_samplerates     (ctx     , ff_make_format_list(sample_rates) )) < 0)
        return ret;

    return 0;
}

//FIXME: replace with DSPContext.scalarproduct_int16
static inline int16_t *scalarproduct(const int16_t *in, const int16_t *endin, int16_t *out)
{
    int32_t sample;
    int16_t j;

    while (in < endin) {
        sample = 0;
        for (j = 0; j < NUMTAPS; j++)
            sample += in[j] * filt[j];
        *out = av_clip_int16(sample >> 6);
        out++;
        in++;
    }

    return out;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterLink *outlink = inlink->dst->outputs[0];
    int16_t *taps, *endin, *in, *out;
    AVFrame *outsamples = ff_get_audio_buffer(outlink, insamples->nb_samples);
    int len;

    if (!outsamples) {
        av_frame_free(&insamples);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(outsamples, insamples);

    taps  = ((EarwaxContext *)inlink->dst->priv)->taps;
    out   = (int16_t *)outsamples->data[0];
    in    = (int16_t *)insamples ->data[0];

    len = FFMIN(NUMTAPS, 2*insamples->nb_samples);
    // copy part of new input and process with saved input
    memcpy(taps+NUMTAPS, in, len * sizeof(*taps));
    out   = scalarproduct(taps, taps + len, out);

    // process current input
    if (2*insamples->nb_samples >= NUMTAPS ){
        endin = in + insamples->nb_samples * 2 - NUMTAPS;
        scalarproduct(in, endin, out);

        // save part of input for next round
        memcpy(taps, endin, NUMTAPS * sizeof(*taps));
    } else
        memmove(taps, taps + 2*insamples->nb_samples, NUMTAPS * sizeof(*taps));

    av_frame_free(&insamples);
    return ff_filter_frame(outlink, outsamples);
}

static const AVFilterPad earwax_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad earwax_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_earwax = {
    .name           = "earwax",
    .description    = NULL_IF_CONFIG_SMALL("Widen the stereo image."),
    .query_formats  = query_formats,
    .priv_size      = sizeof(EarwaxContext),
    .inputs         = earwax_inputs,
    .outputs        = earwax_outputs,
};
