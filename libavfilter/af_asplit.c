/*
 * Copyright (c) 2011 Stefano Sabatini
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
 * audio splitter
 */

#include "avfilter.h"

static void filter_samples(AVFilterLink *inlink, AVFilterBufferRef *insamples)
{
    ff_filter_samples(inlink->dst->outputs[0],
                            avfilter_ref_buffer(insamples, ~AV_PERM_WRITE));
    ff_filter_samples(inlink->dst->outputs[1],
                            avfilter_ref_buffer(insamples, ~AV_PERM_WRITE));
    avfilter_unref_buffer(insamples);
}

AVFilter avfilter_af_asplit = {
    .name        = "asplit",
    .description = NULL_IF_CONFIG_SMALL("Pass on the audio input to two outputs."),

    .inputs = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_AUDIO,
          .get_audio_buffer = ff_null_get_audio_buffer,
          .filter_samples   = filter_samples, },
        { .name = NULL}
    },
    .outputs = (const AVFilterPad[]) {
        { .name             = "output1",
          .type             = AVMEDIA_TYPE_AUDIO, },
        { .name             = "output2",
          .type             = AVMEDIA_TYPE_AUDIO, },
        { .name = NULL}
    },
};
