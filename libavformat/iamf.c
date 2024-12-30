/*
 * Immersive Audio Model and Formats common helpers and structs
 * Copyright (c) 2023 James Almer <jamrial@gmail.com>
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

#include "libavutil/channel_layout.h"
#include "libavutil/iamf.h"
#include "libavutil/mem.h"
#include "iamf.h"

const AVChannelLayout ff_iamf_scalable_ch_layouts[10] = {
    AV_CHANNEL_LAYOUT_MONO,
    AV_CHANNEL_LAYOUT_STEREO,
    // "Loudspeaker configuration for Sound System B"
    AV_CHANNEL_LAYOUT_5POINT1,
    // "Loudspeaker configuration for Sound System C"
    AV_CHANNEL_LAYOUT_5POINT1POINT2,
    // "Loudspeaker configuration for Sound System D"
    AV_CHANNEL_LAYOUT_5POINT1POINT4_BACK,
    // "Loudspeaker configuration for Sound System I"
    AV_CHANNEL_LAYOUT_7POINT1,
    // "Loudspeaker configuration for Sound System I" + Ltf + Rtf
    AV_CHANNEL_LAYOUT_7POINT1POINT2,
    // "Loudspeaker configuration for Sound System J"
    AV_CHANNEL_LAYOUT_7POINT1POINT4_BACK,
    // Front subset of "Loudspeaker configuration for Sound System J"
    AV_CHANNEL_LAYOUT_3POINT1POINT2,
    // Binaural
    AV_CHANNEL_LAYOUT_BINAURAL,
};

const AVChannelLayout ff_iamf_expanded_scalable_ch_layouts[13] = {
    // The low-frequency effects subset (LFE) of "Loudspeaker configuration for Sound System J"
    {
        .nb_channels = 1,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_LOW_FREQUENCY,
    },
    // The surround subset (Ls/Rs) of "Loudspeaker configuration for Sound System I"
    {
        .nb_channels = 2,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_SIDE_LEFT | AV_CH_SIDE_RIGHT,
    },
    // The side surround subset (Lss/Rss) of "Loudspeaker configuration for Sound System J"
    {
        .nb_channels = 2,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_SIDE_LEFT | AV_CH_SIDE_RIGHT,
    },
    // The rear surround subset (Lrs/Rrs) of "Loudspeaker configuration for Sound System J"
    {
        .nb_channels = 2,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_BACK_LEFT | AV_CH_BACK_RIGHT,
    },
    // The top front subset (Ltf/Rtf) of "Loudspeaker configuration for Sound System J"
    {
        .nb_channels = 2,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_TOP_FRONT_LEFT | AV_CH_TOP_FRONT_RIGHT,
    },
    // The top back subset (Ltb/Rtb) of "Loudspeaker configuration for Sound System J"
    {
        .nb_channels = 2,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_TOP_BACK_LEFT | AV_CH_TOP_BACK_RIGHT,
    },
    // The top 4 channels (Ltf/Rtf/Ltb/Rtb) of "Loudspeaker configuration for Sound System J"
    {
        .nb_channels = 4,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_TOP_FRONT_LEFT | AV_CH_TOP_FRONT_RIGHT |
                       AV_CH_TOP_BACK_LEFT | AV_CH_TOP_BACK_RIGHT,
    },
    // The front 3 channels (L/C/R)  of "Loudspeaker configuration for Sound System J"
    AV_CHANNEL_LAYOUT_SURROUND,
    // Subset of "Loudspeaker configuration for Sound System H"
    AV_CHANNEL_LAYOUT_9POINT1POINT6,
    // Front subset of "Loudspeaker configuration for Sound System H"
    AV_CHANNEL_LAYOUT_STEREO,
    // The side subset (SiL/SiR) of "Loudspeaker configuration for Sound System H"
    {
        .nb_channels = 2,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_SIDE_LEFT | AV_CH_SIDE_RIGHT,
    },
    // The top side subset (TpSiL/TpSiR) of "Loudspeaker configuration for Sound System H"
    {
        .nb_channels = 2,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_TOP_SIDE_LEFT | AV_CH_TOP_SIDE_RIGHT,
    },
    // The top 6 channels (TpFL/TpFR/TpSiL/TpSiR/TpBL/TpBR) of "Loudspeaker configuration for Sound System H"
    {
        .nb_channels = 6,
        .order       = AV_CHANNEL_ORDER_NATIVE,
        .u.mask      = AV_CH_TOP_FRONT_LEFT | AV_CH_TOP_FRONT_RIGHT |
                       AV_CH_TOP_BACK_LEFT | AV_CH_TOP_BACK_RIGHT |
                       AV_CH_TOP_SIDE_LEFT | AV_CH_TOP_SIDE_RIGHT,
    },
};

const struct IAMFSoundSystemMap ff_iamf_sound_system_map[14] = {
    { SOUND_SYSTEM_A_0_2_0, AV_CHANNEL_LAYOUT_STEREO },
    { SOUND_SYSTEM_B_0_5_0, AV_CHANNEL_LAYOUT_5POINT1 },
    { SOUND_SYSTEM_C_2_5_0, AV_CHANNEL_LAYOUT_5POINT1POINT2 },
    { SOUND_SYSTEM_D_4_5_0, AV_CHANNEL_LAYOUT_5POINT1POINT4_BACK },
    { SOUND_SYSTEM_E_4_5_1,
        {
            .nb_channels = 11,
            .order       = AV_CHANNEL_ORDER_NATIVE,
            .u.mask      = AV_CH_LAYOUT_5POINT1POINT4_BACK | AV_CH_BOTTOM_FRONT_CENTER,
        },
    },
    { SOUND_SYSTEM_F_3_7_0,  AV_CHANNEL_LAYOUT_7POINT2POINT3 },
    { SOUND_SYSTEM_G_4_9_0,  AV_CHANNEL_LAYOUT_9POINT1POINT4_BACK },
    { SOUND_SYSTEM_H_9_10_3, AV_CHANNEL_LAYOUT_22POINT2 },
    { SOUND_SYSTEM_I_0_7_0,  AV_CHANNEL_LAYOUT_7POINT1 },
    { SOUND_SYSTEM_J_4_7_0,  AV_CHANNEL_LAYOUT_7POINT1POINT4_BACK },
    { SOUND_SYSTEM_10_2_7_0, AV_CHANNEL_LAYOUT_7POINT1POINT2 },
    { SOUND_SYSTEM_11_2_3_0, AV_CHANNEL_LAYOUT_3POINT1POINT2 },
    { SOUND_SYSTEM_12_0_1_0, AV_CHANNEL_LAYOUT_MONO },
    { SOUND_SYSTEM_13_9_1_6, AV_CHANNEL_LAYOUT_9POINT1POINT6 },
};

void ff_iamf_free_audio_element(IAMFAudioElement **paudio_element)
{
    IAMFAudioElement *audio_element = *paudio_element;

    if (!audio_element)
        return;

    for (int i = 0; i < audio_element->nb_substreams; i++)
        avcodec_parameters_free(&audio_element->substreams[i].codecpar);
    av_free(audio_element->substreams);
    av_free(audio_element->layers);
    av_iamf_audio_element_free(&audio_element->element);
    av_freep(paudio_element);
}

void ff_iamf_free_mix_presentation(IAMFMixPresentation **pmix_presentation)
{
    IAMFMixPresentation *mix_presentation = *pmix_presentation;

    if (!mix_presentation)
        return;

    for (int i = 0; i < mix_presentation->count_label; i++)
        av_free(mix_presentation->language_label[i]);
    av_free(mix_presentation->language_label);
    av_iamf_mix_presentation_free(&mix_presentation->mix);
    av_freep(pmix_presentation);
}

void ff_iamf_uninit_context(IAMFContext *c)
{
    if (!c)
        return;

    for (int i = 0; i < c->nb_codec_configs; i++) {
        av_free(c->codec_configs[i]->extradata);
        av_free(c->codec_configs[i]);
    }
    av_freep(&c->codec_configs);
    c->nb_codec_configs = 0;

    for (int i = 0; i < c->nb_audio_elements; i++)
        ff_iamf_free_audio_element(&c->audio_elements[i]);
    av_freep(&c->audio_elements);
    c->nb_audio_elements = 0;

    for (int i = 0; i < c->nb_mix_presentations; i++)
        ff_iamf_free_mix_presentation(&c->mix_presentations[i]);
    av_freep(&c->mix_presentations);
    c->nb_mix_presentations = 0;

    for (int i = 0; i < c->nb_param_definitions; i++)
        av_free(c->param_definitions[i]);
    av_freep(&c->param_definitions);
    c->nb_param_definitions = 0;
}
