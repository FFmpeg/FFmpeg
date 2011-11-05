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

#include "libavutil/audioconvert.h"
#include "avfilter.h"

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

typedef struct {
    int16_t taps[NUMTAPS * 2];
} EarwaxContext;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    avfilter_add_format(&formats, AV_SAMPLE_FMT_S16);
    avfilter_set_common_sample_formats(ctx, formats);
    formats = NULL;
    avfilter_add_format(&formats, AV_CH_LAYOUT_STEREO);
    avfilter_set_common_channel_layouts(ctx, formats);
    formats = NULL;
    avfilter_add_format(&formats, AVFILTER_PACKED);
    avfilter_set_common_packing_formats(ctx, formats);

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    if (inlink->sample_rate != 44100) {
        av_log(inlink->dst, AV_LOG_ERROR,
               "The earwax filter only works for 44.1kHz audio. Insert "
               "a resample filter before this\n");
        return AVERROR(EINVAL);
    }
    return 0;
}

//FIXME: replace with DSPContext.scalarproduct_int16
static inline int16_t *scalarproduct(const int16_t *in, const int16_t *endin, int16_t *out)
{
    int32_t sample;
    int16_t j;

    while (in < endin) {
        sample = 32;
        for (j = 0; j < NUMTAPS; j++)
            sample += in[j] * filt[j];
        *out = sample >> 6;
        out++;
        in++;
    }

    return out;
}

static void filter_samples(AVFilterLink *inlink, AVFilterBufferRef *insamples)
{
    AVFilterLink *outlink = inlink->dst->outputs[0];
    int16_t *taps, *endin, *in, *out;
    AVFilterBufferRef *outsamples =
        avfilter_get_audio_buffer(inlink, AV_PERM_WRITE,
                                  insamples->audio->nb_samples);
    avfilter_copy_buffer_ref_props(outsamples, insamples);

    taps  = ((EarwaxContext *)inlink->dst->priv)->taps;
    out   = (int16_t *)outsamples->data[0];
    in    = (int16_t *)insamples ->data[0];

    // copy part of new input and process with saved input
    memcpy(taps+NUMTAPS, in, NUMTAPS * sizeof(*taps));
    out   = scalarproduct(taps, taps + NUMTAPS, out);

    // process current input
    endin = in + insamples->audio->nb_samples * 2 - NUMTAPS;
    out   = scalarproduct(in, endin, out);

    // save part of input for next round
    memcpy(taps, endin, NUMTAPS * sizeof(*taps));

    avfilter_filter_samples(outlink, outsamples);
    avfilter_unref_buffer(insamples);
}

AVFilter avfilter_af_earwax = {
    .name           = "earwax",
    .description    = NULL_IF_CONFIG_SMALL("Widen the stereo image."),
    .query_formats  = query_formats,
    .priv_size      = sizeof(EarwaxContext),
    .inputs  = (const AVFilterPad[])  {{  .name     = "default",
                                    .type           = AVMEDIA_TYPE_AUDIO,
                                    .filter_samples = filter_samples,
                                    .config_props   = config_input,
                                    .min_perms      = AV_PERM_READ, },
                                 {  .name = NULL}},

    .outputs = (const AVFilterPad[])  {{  .name     = "default",
                                    .type           = AVMEDIA_TYPE_AUDIO, },
                                 {  .name = NULL}},
};
