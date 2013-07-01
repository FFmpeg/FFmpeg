/*
 * AAC decoder
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
 *
 * AAC LATM decoder
 * Copyright (c) 2008-2010 Paul Kendall <paul@kcbbs.gen.nz>
 * Copyright (c) 2010      Janne Grunau <janne-libav@jannau.net>
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
 * AAC decoder
 * @author Oded Shimon  ( ods15 ods15 dyndns org )
 * @author Maxim Gavrilov ( maxim.gavrilov gmail com )
 */

/*
 * supported tools
 *
 * Support?             Name
 * N (code in SoC repo) gain control
 * Y                    block switching
 * Y                    window shapes - standard
 * N                    window shapes - Low Delay
 * Y                    filterbank - standard
 * N (code in SoC repo) filterbank - Scalable Sample Rate
 * Y                    Temporal Noise Shaping
 * Y                    Long Term Prediction
 * Y                    intensity stereo
 * Y                    channel coupling
 * Y                    frequency domain prediction
 * Y                    Perceptual Noise Substitution
 * Y                    Mid/Side stereo
 * N                    Scalable Inverse AAC Quantization
 * N                    Frequency Selective Switch
 * N                    upsampling filter
 * Y                    quantization & coding - AAC
 * N                    quantization & coding - TwinVQ
 * N                    quantization & coding - BSAC
 * N                    AAC Error Resilience tools
 * N                    Error Resilience payload syntax
 * N                    Error Protection tool
 * N                    CELP
 * N                    Silence Compression
 * N                    HVXC
 * N                    HVXC 4kbits/s VR
 * N                    Structured Audio tools
 * N                    Structured Audio Sample Bank Format
 * N                    MIDI
 * N                    Harmonic and Individual Lines plus Noise
 * N                    Text-To-Speech Interface
 * Y                    Spectral Band Replication
 * Y (not in this code) Layer-1
 * Y (not in this code) Layer-2
 * Y (not in this code) Layer-3
 * N                    SinuSoidal Coding (Transient, Sinusoid, Noise)
 * Y                    Parametric Stereo
 * N                    Direct Stream Transfer
 *
 * Note: - HE AAC v1 comprises LC AAC with Spectral Band Replication.
 *       - HE AAC v2 comprises LC AAC with Spectral Band Replication and
           Parametric Stereo.
 */

#include "libavutil/float_dsp.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "fft.h"
#include "fmtconvert.h"
#include "lpc.h"
#include "kbdwin.h"
#include "sinewin.h"

#include "aac.h"
#include "aactab.h"
#include "aacdectab.h"
#include "cbrt_tablegen.h"
#include "sbr.h"
#include "aacsbr.h"
#include "mpeg4audio.h"
#include "aacadtsdec.h"
#include "libavutil/intfloat.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#if ARCH_ARM
#   include "arm/aac.h"
#elif ARCH_MIPS
#   include "mips/aacdec_mips.h"
#endif

static VLC vlc_scalefactors;
static VLC vlc_spectral[11];

static int output_configure(AACContext *ac,
                            uint8_t layout_map[MAX_ELEM_ID*4][3], int tags,
                            enum OCStatus oc_type, int get_new_frame);

#define overread_err "Input buffer exhausted before END element found\n"

static int count_channels(uint8_t (*layout)[3], int tags)
{
    int i, sum = 0;
    for (i = 0; i < tags; i++) {
        int syn_ele = layout[i][0];
        int pos     = layout[i][2];
        sum += (1 + (syn_ele == TYPE_CPE)) *
               (pos != AAC_CHANNEL_OFF && pos != AAC_CHANNEL_CC);
    }
    return sum;
}

/**
 * Check for the channel element in the current channel position configuration.
 * If it exists, make sure the appropriate element is allocated and map the
 * channel order to match the internal FFmpeg channel layout.
 *
 * @param   che_pos current channel position configuration
 * @param   type channel element type
 * @param   id channel element id
 * @param   channels count of the number of channels in the configuration
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static av_cold int che_configure(AACContext *ac,
                                 enum ChannelPosition che_pos,
                                 int type, int id, int *channels)
{
    if (*channels >= MAX_CHANNELS)
        return AVERROR_INVALIDDATA;
    if (che_pos) {
        if (!ac->che[type][id]) {
            if (!(ac->che[type][id] = av_mallocz(sizeof(ChannelElement))))
                return AVERROR(ENOMEM);
            ff_aac_sbr_ctx_init(ac, &ac->che[type][id]->sbr);
        }
        if (type != TYPE_CCE) {
            if (*channels >= MAX_CHANNELS - (type == TYPE_CPE || (type == TYPE_SCE && ac->oc[1].m4ac.ps == 1))) {
                av_log(ac->avctx, AV_LOG_ERROR, "Too many channels\n");
                return AVERROR_INVALIDDATA;
            }
            ac->output_element[(*channels)++] = &ac->che[type][id]->ch[0];
            if (type == TYPE_CPE ||
                (type == TYPE_SCE && ac->oc[1].m4ac.ps == 1)) {
                ac->output_element[(*channels)++] = &ac->che[type][id]->ch[1];
            }
        }
    } else {
        if (ac->che[type][id])
            ff_aac_sbr_ctx_close(&ac->che[type][id]->sbr);
        av_freep(&ac->che[type][id]);
    }
    return 0;
}

static int frame_configure_elements(AVCodecContext *avctx)
{
    AACContext *ac = avctx->priv_data;
    int type, id, ch, ret;

    /* set channel pointers to internal buffers by default */
    for (type = 0; type < 4; type++) {
        for (id = 0; id < MAX_ELEM_ID; id++) {
            ChannelElement *che = ac->che[type][id];
            if (che) {
                che->ch[0].ret = che->ch[0].ret_buf;
                che->ch[1].ret = che->ch[1].ret_buf;
            }
        }
    }

    /* get output buffer */
    av_frame_unref(ac->frame);
    ac->frame->nb_samples = 2048;
    if ((ret = ff_get_buffer(avctx, ac->frame, 0)) < 0)
        return ret;

    /* map output channel pointers to AVFrame data */
    for (ch = 0; ch < avctx->channels; ch++) {
        if (ac->output_element[ch])
            ac->output_element[ch]->ret = (float *)ac->frame->extended_data[ch];
    }

    return 0;
}

struct elem_to_channel {
    uint64_t av_position;
    uint8_t syn_ele;
    uint8_t elem_id;
    uint8_t aac_position;
};

static int assign_pair(struct elem_to_channel e2c_vec[MAX_ELEM_ID],
                       uint8_t (*layout_map)[3], int offset, uint64_t left,
                       uint64_t right, int pos)
{
    if (layout_map[offset][0] == TYPE_CPE) {
        e2c_vec[offset] = (struct elem_to_channel) {
            .av_position  = left | right,
            .syn_ele      = TYPE_CPE,
            .elem_id      = layout_map[offset][1],
            .aac_position = pos
        };
        return 1;
    } else {
        e2c_vec[offset] = (struct elem_to_channel) {
            .av_position  = left,
            .syn_ele      = TYPE_SCE,
            .elem_id      = layout_map[offset][1],
            .aac_position = pos
        };
        e2c_vec[offset + 1] = (struct elem_to_channel) {
            .av_position  = right,
            .syn_ele      = TYPE_SCE,
            .elem_id      = layout_map[offset + 1][1],
            .aac_position = pos
        };
        return 2;
    }
}

static int count_paired_channels(uint8_t (*layout_map)[3], int tags, int pos,
                                 int *current)
{
    int num_pos_channels = 0;
    int first_cpe        = 0;
    int sce_parity       = 0;
    int i;
    for (i = *current; i < tags; i++) {
        if (layout_map[i][2] != pos)
            break;
        if (layout_map[i][0] == TYPE_CPE) {
            if (sce_parity) {
                if (pos == AAC_CHANNEL_FRONT && !first_cpe) {
                    sce_parity = 0;
                } else {
                    return -1;
                }
            }
            num_pos_channels += 2;
            first_cpe         = 1;
        } else {
            num_pos_channels++;
            sce_parity ^= 1;
        }
    }
    if (sce_parity &&
        ((pos == AAC_CHANNEL_FRONT && first_cpe) || pos == AAC_CHANNEL_SIDE))
        return -1;
    *current = i;
    return num_pos_channels;
}

static uint64_t sniff_channel_order(uint8_t (*layout_map)[3], int tags)
{
    int i, n, total_non_cc_elements;
    struct elem_to_channel e2c_vec[4 * MAX_ELEM_ID] = { { 0 } };
    int num_front_channels, num_side_channels, num_back_channels;
    uint64_t layout;

    if (FF_ARRAY_ELEMS(e2c_vec) < tags)
        return 0;

    i = 0;
    num_front_channels =
        count_paired_channels(layout_map, tags, AAC_CHANNEL_FRONT, &i);
    if (num_front_channels < 0)
        return 0;
    num_side_channels =
        count_paired_channels(layout_map, tags, AAC_CHANNEL_SIDE, &i);
    if (num_side_channels < 0)
        return 0;
    num_back_channels =
        count_paired_channels(layout_map, tags, AAC_CHANNEL_BACK, &i);
    if (num_back_channels < 0)
        return 0;

    i = 0;
    if (num_front_channels & 1) {
        e2c_vec[i] = (struct elem_to_channel) {
            .av_position  = AV_CH_FRONT_CENTER,
            .syn_ele      = TYPE_SCE,
            .elem_id      = layout_map[i][1],
            .aac_position = AAC_CHANNEL_FRONT
        };
        i++;
        num_front_channels--;
    }
    if (num_front_channels >= 4) {
        i += assign_pair(e2c_vec, layout_map, i,
                         AV_CH_FRONT_LEFT_OF_CENTER,
                         AV_CH_FRONT_RIGHT_OF_CENTER,
                         AAC_CHANNEL_FRONT);
        num_front_channels -= 2;
    }
    if (num_front_channels >= 2) {
        i += assign_pair(e2c_vec, layout_map, i,
                         AV_CH_FRONT_LEFT,
                         AV_CH_FRONT_RIGHT,
                         AAC_CHANNEL_FRONT);
        num_front_channels -= 2;
    }
    while (num_front_channels >= 2) {
        i += assign_pair(e2c_vec, layout_map, i,
                         UINT64_MAX,
                         UINT64_MAX,
                         AAC_CHANNEL_FRONT);
        num_front_channels -= 2;
    }

    if (num_side_channels >= 2) {
        i += assign_pair(e2c_vec, layout_map, i,
                         AV_CH_SIDE_LEFT,
                         AV_CH_SIDE_RIGHT,
                         AAC_CHANNEL_FRONT);
        num_side_channels -= 2;
    }
    while (num_side_channels >= 2) {
        i += assign_pair(e2c_vec, layout_map, i,
                         UINT64_MAX,
                         UINT64_MAX,
                         AAC_CHANNEL_SIDE);
        num_side_channels -= 2;
    }

    while (num_back_channels >= 4) {
        i += assign_pair(e2c_vec, layout_map, i,
                         UINT64_MAX,
                         UINT64_MAX,
                         AAC_CHANNEL_BACK);
        num_back_channels -= 2;
    }
    if (num_back_channels >= 2) {
        i += assign_pair(e2c_vec, layout_map, i,
                         AV_CH_BACK_LEFT,
                         AV_CH_BACK_RIGHT,
                         AAC_CHANNEL_BACK);
        num_back_channels -= 2;
    }
    if (num_back_channels) {
        e2c_vec[i] = (struct elem_to_channel) {
            .av_position  = AV_CH_BACK_CENTER,
            .syn_ele      = TYPE_SCE,
            .elem_id      = layout_map[i][1],
            .aac_position = AAC_CHANNEL_BACK
        };
        i++;
        num_back_channels--;
    }

    if (i < tags && layout_map[i][2] == AAC_CHANNEL_LFE) {
        e2c_vec[i] = (struct elem_to_channel) {
            .av_position  = AV_CH_LOW_FREQUENCY,
            .syn_ele      = TYPE_LFE,
            .elem_id      = layout_map[i][1],
            .aac_position = AAC_CHANNEL_LFE
        };
        i++;
    }
    while (i < tags && layout_map[i][2] == AAC_CHANNEL_LFE) {
        e2c_vec[i] = (struct elem_to_channel) {
            .av_position  = UINT64_MAX,
            .syn_ele      = TYPE_LFE,
            .elem_id      = layout_map[i][1],
            .aac_position = AAC_CHANNEL_LFE
        };
        i++;
    }

    // Must choose a stable sort
    total_non_cc_elements = n = i;
    do {
        int next_n = 0;
        for (i = 1; i < n; i++)
            if (e2c_vec[i - 1].av_position > e2c_vec[i].av_position) {
                FFSWAP(struct elem_to_channel, e2c_vec[i - 1], e2c_vec[i]);
                next_n = i;
            }
        n = next_n;
    } while (n > 0);

    layout = 0;
    for (i = 0; i < total_non_cc_elements; i++) {
        layout_map[i][0] = e2c_vec[i].syn_ele;
        layout_map[i][1] = e2c_vec[i].elem_id;
        layout_map[i][2] = e2c_vec[i].aac_position;
        if (e2c_vec[i].av_position != UINT64_MAX) {
            layout |= e2c_vec[i].av_position;
        }
    }

    return layout;
}

/**
 * Save current output configuration if and only if it has been locked.
 */
static void push_output_configuration(AACContext *ac) {
    if (ac->oc[1].status == OC_LOCKED) {
        ac->oc[0] = ac->oc[1];
    }
    ac->oc[1].status = OC_NONE;
}

/**
 * Restore the previous output configuration if and only if the current
 * configuration is unlocked.
 */
static void pop_output_configuration(AACContext *ac) {
    if (ac->oc[1].status != OC_LOCKED && ac->oc[0].status != OC_NONE) {
        ac->oc[1] = ac->oc[0];
        ac->avctx->channels = ac->oc[1].channels;
        ac->avctx->channel_layout = ac->oc[1].channel_layout;
        output_configure(ac, ac->oc[1].layout_map, ac->oc[1].layout_map_tags,
                         ac->oc[1].status, 0);
    }
}

/**
 * Configure output channel order based on the current program
 * configuration element.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int output_configure(AACContext *ac,
                            uint8_t layout_map[MAX_ELEM_ID * 4][3], int tags,
                            enum OCStatus oc_type, int get_new_frame)
{
    AVCodecContext *avctx = ac->avctx;
    int i, channels = 0, ret;
    uint64_t layout = 0;

    if (ac->oc[1].layout_map != layout_map) {
        memcpy(ac->oc[1].layout_map, layout_map, tags * sizeof(layout_map[0]));
        ac->oc[1].layout_map_tags = tags;
    }

    // Try to sniff a reasonable channel order, otherwise output the
    // channels in the order the PCE declared them.
    if (avctx->request_channel_layout != AV_CH_LAYOUT_NATIVE)
        layout = sniff_channel_order(layout_map, tags);
    for (i = 0; i < tags; i++) {
        int type =     layout_map[i][0];
        int id =       layout_map[i][1];
        int position = layout_map[i][2];
        // Allocate or free elements depending on if they are in the
        // current program configuration.
        ret = che_configure(ac, position, type, id, &channels);
        if (ret < 0)
            return ret;
    }
    if (ac->oc[1].m4ac.ps == 1 && channels == 2) {
        if (layout == AV_CH_FRONT_CENTER) {
            layout = AV_CH_FRONT_LEFT|AV_CH_FRONT_RIGHT;
        } else {
            layout = 0;
        }
    }

    memcpy(ac->tag_che_map, ac->che, 4 * MAX_ELEM_ID * sizeof(ac->che[0][0]));
    if (layout) avctx->channel_layout = layout;
                            ac->oc[1].channel_layout = layout;
    avctx->channels       = ac->oc[1].channels       = channels;
    ac->oc[1].status = oc_type;

    if (get_new_frame) {
        if ((ret = frame_configure_elements(ac->avctx)) < 0)
            return ret;
    }

    return 0;
}

static void flush(AVCodecContext *avctx)
{
    AACContext *ac= avctx->priv_data;
    int type, i, j;

    for (type = 3; type >= 0; type--) {
        for (i = 0; i < MAX_ELEM_ID; i++) {
            ChannelElement *che = ac->che[type][i];
            if (che) {
                for (j = 0; j <= 1; j++) {
                    memset(che->ch[j].saved, 0, sizeof(che->ch[j].saved));
                }
            }
        }
    }
}

/**
 * Set up channel positions based on a default channel configuration
 * as specified in table 1.17.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int set_default_channel_config(AVCodecContext *avctx,
                                      uint8_t (*layout_map)[3],
                                      int *tags,
                                      int channel_config)
{
    if (channel_config < 1 || channel_config > 7) {
        av_log(avctx, AV_LOG_ERROR,
               "invalid default channel configuration (%d)\n",
               channel_config);
        return AVERROR_INVALIDDATA;
    }
    *tags = tags_per_config[channel_config];
    memcpy(layout_map, aac_channel_layout_map[channel_config - 1],
           *tags * sizeof(*layout_map));
    return 0;
}

static ChannelElement *get_che(AACContext *ac, int type, int elem_id)
{
    /* For PCE based channel configurations map the channels solely based
     * on tags. */
    if (!ac->oc[1].m4ac.chan_config) {
        return ac->tag_che_map[type][elem_id];
    }
    // Allow single CPE stereo files to be signalled with mono configuration.
    if (!ac->tags_mapped && type == TYPE_CPE &&
        ac->oc[1].m4ac.chan_config == 1) {
        uint8_t layout_map[MAX_ELEM_ID*4][3];
        int layout_map_tags;
        push_output_configuration(ac);

        av_log(ac->avctx, AV_LOG_DEBUG, "mono with CPE\n");

        if (set_default_channel_config(ac->avctx, layout_map,
                                       &layout_map_tags, 2) < 0)
            return NULL;
        if (output_configure(ac, layout_map, layout_map_tags,
                             OC_TRIAL_FRAME, 1) < 0)
            return NULL;

        ac->oc[1].m4ac.chan_config = 2;
        ac->oc[1].m4ac.ps = 0;
    }
    // And vice-versa
    if (!ac->tags_mapped && type == TYPE_SCE &&
        ac->oc[1].m4ac.chan_config == 2) {
        uint8_t layout_map[MAX_ELEM_ID * 4][3];
        int layout_map_tags;
        push_output_configuration(ac);

        av_log(ac->avctx, AV_LOG_DEBUG, "stereo with SCE\n");

        if (set_default_channel_config(ac->avctx, layout_map,
                                       &layout_map_tags, 1) < 0)
            return NULL;
        if (output_configure(ac, layout_map, layout_map_tags,
                             OC_TRIAL_FRAME, 1) < 0)
            return NULL;

        ac->oc[1].m4ac.chan_config = 1;
        if (ac->oc[1].m4ac.sbr)
            ac->oc[1].m4ac.ps = -1;
    }
    /* For indexed channel configurations map the channels solely based
     * on position. */
    switch (ac->oc[1].m4ac.chan_config) {
    case 7:
        if (ac->tags_mapped == 3 && type == TYPE_CPE) {
            ac->tags_mapped++;
            return ac->tag_che_map[TYPE_CPE][elem_id] = ac->che[TYPE_CPE][2];
        }
    case 6:
        /* Some streams incorrectly code 5.1 audio as
         * SCE[0] CPE[0] CPE[1] SCE[1]
         * instead of
         * SCE[0] CPE[0] CPE[1] LFE[0].
         * If we seem to have encountered such a stream, transfer
         * the LFE[0] element to the SCE[1]'s mapping */
        if (ac->tags_mapped == tags_per_config[ac->oc[1].m4ac.chan_config] - 1 && (type == TYPE_LFE || type == TYPE_SCE)) {
            ac->tags_mapped++;
            return ac->tag_che_map[type][elem_id] = ac->che[TYPE_LFE][0];
        }
    case 5:
        if (ac->tags_mapped == 2 && type == TYPE_CPE) {
            ac->tags_mapped++;
            return ac->tag_che_map[TYPE_CPE][elem_id] = ac->che[TYPE_CPE][1];
        }
    case 4:
        if (ac->tags_mapped == 2 &&
            ac->oc[1].m4ac.chan_config == 4 &&
            type == TYPE_SCE) {
            ac->tags_mapped++;
            return ac->tag_che_map[TYPE_SCE][elem_id] = ac->che[TYPE_SCE][1];
        }
    case 3:
    case 2:
        if (ac->tags_mapped == (ac->oc[1].m4ac.chan_config != 2) &&
            type == TYPE_CPE) {
            ac->tags_mapped++;
            return ac->tag_che_map[TYPE_CPE][elem_id] = ac->che[TYPE_CPE][0];
        } else if (ac->oc[1].m4ac.chan_config == 2) {
            return NULL;
        }
    case 1:
        if (!ac->tags_mapped && type == TYPE_SCE) {
            ac->tags_mapped++;
            return ac->tag_che_map[TYPE_SCE][elem_id] = ac->che[TYPE_SCE][0];
        }
    default:
        return NULL;
    }
}

/**
 * Decode an array of 4 bit element IDs, optionally interleaved with a
 * stereo/mono switching bit.
 *
 * @param type speaker type/position for these channels
 */
static void decode_channel_map(uint8_t layout_map[][3],
                               enum ChannelPosition type,
                               GetBitContext *gb, int n)
{
    while (n--) {
        enum RawDataBlockType syn_ele;
        switch (type) {
        case AAC_CHANNEL_FRONT:
        case AAC_CHANNEL_BACK:
        case AAC_CHANNEL_SIDE:
            syn_ele = get_bits1(gb);
            break;
        case AAC_CHANNEL_CC:
            skip_bits1(gb);
            syn_ele = TYPE_CCE;
            break;
        case AAC_CHANNEL_LFE:
            syn_ele = TYPE_LFE;
            break;
        default:
            av_assert0(0);
        }
        layout_map[0][0] = syn_ele;
        layout_map[0][1] = get_bits(gb, 4);
        layout_map[0][2] = type;
        layout_map++;
    }
}

/**
 * Decode program configuration element; reference: table 4.2.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_pce(AVCodecContext *avctx, MPEG4AudioConfig *m4ac,
                      uint8_t (*layout_map)[3],
                      GetBitContext *gb)
{
    int num_front, num_side, num_back, num_lfe, num_assoc_data, num_cc;
    int sampling_index;
    int comment_len;
    int tags;

    skip_bits(gb, 2);  // object_type

    sampling_index = get_bits(gb, 4);
    if (m4ac->sampling_index != sampling_index)
        av_log(avctx, AV_LOG_WARNING,
               "Sample rate index in program config element does not "
               "match the sample rate index configured by the container.\n");

    num_front       = get_bits(gb, 4);
    num_side        = get_bits(gb, 4);
    num_back        = get_bits(gb, 4);
    num_lfe         = get_bits(gb, 2);
    num_assoc_data  = get_bits(gb, 3);
    num_cc          = get_bits(gb, 4);

    if (get_bits1(gb))
        skip_bits(gb, 4); // mono_mixdown_tag
    if (get_bits1(gb))
        skip_bits(gb, 4); // stereo_mixdown_tag

    if (get_bits1(gb))
        skip_bits(gb, 3); // mixdown_coeff_index and pseudo_surround

    if (get_bits_left(gb) < 4 * (num_front + num_side + num_back + num_lfe + num_assoc_data + num_cc)) {
        av_log(avctx, AV_LOG_ERROR, "decode_pce: " overread_err);
        return -1;
    }
    decode_channel_map(layout_map       , AAC_CHANNEL_FRONT, gb, num_front);
    tags = num_front;
    decode_channel_map(layout_map + tags, AAC_CHANNEL_SIDE,  gb, num_side);
    tags += num_side;
    decode_channel_map(layout_map + tags, AAC_CHANNEL_BACK,  gb, num_back);
    tags += num_back;
    decode_channel_map(layout_map + tags, AAC_CHANNEL_LFE,   gb, num_lfe);
    tags += num_lfe;

    skip_bits_long(gb, 4 * num_assoc_data);

    decode_channel_map(layout_map + tags, AAC_CHANNEL_CC,    gb, num_cc);
    tags += num_cc;

    align_get_bits(gb);

    /* comment field, first byte is length */
    comment_len = get_bits(gb, 8) * 8;
    if (get_bits_left(gb) < comment_len) {
        av_log(avctx, AV_LOG_ERROR, "decode_pce: " overread_err);
        return AVERROR_INVALIDDATA;
    }
    skip_bits_long(gb, comment_len);
    return tags;
}

/**
 * Decode GA "General Audio" specific configuration; reference: table 4.1.
 *
 * @param   ac          pointer to AACContext, may be null
 * @param   avctx       pointer to AVCCodecContext, used for logging
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_ga_specific_config(AACContext *ac, AVCodecContext *avctx,
                                     GetBitContext *gb,
                                     MPEG4AudioConfig *m4ac,
                                     int channel_config)
{
    int extension_flag, ret;
    uint8_t layout_map[MAX_ELEM_ID*4][3];
    int tags = 0;

    if (get_bits1(gb)) { // frameLengthFlag
        avpriv_request_sample(avctx, "960/120 MDCT window");
        return AVERROR_PATCHWELCOME;
    }

    if (get_bits1(gb))       // dependsOnCoreCoder
        skip_bits(gb, 14);   // coreCoderDelay
    extension_flag = get_bits1(gb);

    if (m4ac->object_type == AOT_AAC_SCALABLE ||
        m4ac->object_type == AOT_ER_AAC_SCALABLE)
        skip_bits(gb, 3);     // layerNr

    if (channel_config == 0) {
        skip_bits(gb, 4);  // element_instance_tag
        tags = decode_pce(avctx, m4ac, layout_map, gb);
        if (tags < 0)
            return tags;
    } else {
        if ((ret = set_default_channel_config(avctx, layout_map,
                                              &tags, channel_config)))
            return ret;
    }

    if (count_channels(layout_map, tags) > 1) {
        m4ac->ps = 0;
    } else if (m4ac->sbr == 1 && m4ac->ps == -1)
        m4ac->ps = 1;

    if (ac && (ret = output_configure(ac, layout_map, tags, OC_GLOBAL_HDR, 0)))
        return ret;

    if (extension_flag) {
        switch (m4ac->object_type) {
        case AOT_ER_BSAC:
            skip_bits(gb, 5);    // numOfSubFrame
            skip_bits(gb, 11);   // layer_length
            break;
        case AOT_ER_AAC_LC:
        case AOT_ER_AAC_LTP:
        case AOT_ER_AAC_SCALABLE:
        case AOT_ER_AAC_LD:
            skip_bits(gb, 3);      /* aacSectionDataResilienceFlag
                                    * aacScalefactorDataResilienceFlag
                                    * aacSpectralDataResilienceFlag
                                    */
            break;
        }
        skip_bits1(gb);    // extensionFlag3 (TBD in version 3)
    }
    return 0;
}

/**
 * Decode audio specific configuration; reference: table 1.13.
 *
 * @param   ac          pointer to AACContext, may be null
 * @param   avctx       pointer to AVCCodecContext, used for logging
 * @param   m4ac        pointer to MPEG4AudioConfig, used for parsing
 * @param   data        pointer to buffer holding an audio specific config
 * @param   bit_size    size of audio specific config or data in bits
 * @param   sync_extension look for an appended sync extension
 *
 * @return  Returns error status or number of consumed bits. <0 - error
 */
static int decode_audio_specific_config(AACContext *ac,
                                        AVCodecContext *avctx,
                                        MPEG4AudioConfig *m4ac,
                                        const uint8_t *data, int bit_size,
                                        int sync_extension)
{
    GetBitContext gb;
    int i, ret;

    av_dlog(avctx, "audio specific config size %d\n", bit_size >> 3);
    for (i = 0; i < bit_size >> 3; i++)
        av_dlog(avctx, "%02x ", data[i]);
    av_dlog(avctx, "\n");

    if ((ret = init_get_bits(&gb, data, bit_size)) < 0)
        return ret;

    if ((i = avpriv_mpeg4audio_get_config(m4ac, data, bit_size,
                                          sync_extension)) < 0)
        return AVERROR_INVALIDDATA;
    if (m4ac->sampling_index > 12) {
        av_log(avctx, AV_LOG_ERROR,
               "invalid sampling rate index %d\n",
               m4ac->sampling_index);
        return AVERROR_INVALIDDATA;
    }

    skip_bits_long(&gb, i);

    switch (m4ac->object_type) {
    case AOT_AAC_MAIN:
    case AOT_AAC_LC:
    case AOT_AAC_LTP:
        if ((ret = decode_ga_specific_config(ac, avctx, &gb,
                                            m4ac, m4ac->chan_config)) < 0)
            return ret;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR,
               "Audio object type %s%d is not supported.\n",
               m4ac->sbr == 1 ? "SBR+" : "",
               m4ac->object_type);
        return AVERROR(ENOSYS);
    }

    av_dlog(avctx,
            "AOT %d chan config %d sampling index %d (%d) SBR %d PS %d\n",
            m4ac->object_type, m4ac->chan_config, m4ac->sampling_index,
            m4ac->sample_rate, m4ac->sbr,
            m4ac->ps);

    return get_bits_count(&gb);
}

/**
 * linear congruential pseudorandom number generator
 *
 * @param   previous_val    pointer to the current state of the generator
 *
 * @return  Returns a 32-bit pseudorandom integer
 */
static av_always_inline int lcg_random(unsigned previous_val)
{
    union { unsigned u; int s; } v = { previous_val * 1664525u + 1013904223 };
    return v.s;
}

static av_always_inline void reset_predict_state(PredictorState *ps)
{
    ps->r0   = 0.0f;
    ps->r1   = 0.0f;
    ps->cor0 = 0.0f;
    ps->cor1 = 0.0f;
    ps->var0 = 1.0f;
    ps->var1 = 1.0f;
}

static void reset_all_predictors(PredictorState *ps)
{
    int i;
    for (i = 0; i < MAX_PREDICTORS; i++)
        reset_predict_state(&ps[i]);
}

static int sample_rate_idx (int rate)
{
         if (92017 <= rate) return 0;
    else if (75132 <= rate) return 1;
    else if (55426 <= rate) return 2;
    else if (46009 <= rate) return 3;
    else if (37566 <= rate) return 4;
    else if (27713 <= rate) return 5;
    else if (23004 <= rate) return 6;
    else if (18783 <= rate) return 7;
    else if (13856 <= rate) return 8;
    else if (11502 <= rate) return 9;
    else if (9391  <= rate) return 10;
    else                    return 11;
}

static void reset_predictor_group(PredictorState *ps, int group_num)
{
    int i;
    for (i = group_num - 1; i < MAX_PREDICTORS; i += 30)
        reset_predict_state(&ps[i]);
}

#define AAC_INIT_VLC_STATIC(num, size)                                     \
    INIT_VLC_STATIC(&vlc_spectral[num], 8, ff_aac_spectral_sizes[num],     \
         ff_aac_spectral_bits[num], sizeof(ff_aac_spectral_bits[num][0]),  \
                                    sizeof(ff_aac_spectral_bits[num][0]),  \
        ff_aac_spectral_codes[num], sizeof(ff_aac_spectral_codes[num][0]), \
                                    sizeof(ff_aac_spectral_codes[num][0]), \
        size);

static void aacdec_init(AACContext *ac);

static av_cold int aac_decode_init(AVCodecContext *avctx)
{
    AACContext *ac = avctx->priv_data;
    int ret;

    ac->avctx = avctx;
    ac->oc[1].m4ac.sample_rate = avctx->sample_rate;

    aacdec_init(ac);

    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;

    if (avctx->extradata_size > 0) {
        if ((ret = decode_audio_specific_config(ac, ac->avctx, &ac->oc[1].m4ac,
                                                avctx->extradata,
                                                avctx->extradata_size * 8,
                                                1)) < 0)
            return ret;
    } else {
        int sr, i;
        uint8_t layout_map[MAX_ELEM_ID*4][3];
        int layout_map_tags;

        sr = sample_rate_idx(avctx->sample_rate);
        ac->oc[1].m4ac.sampling_index = sr;
        ac->oc[1].m4ac.channels = avctx->channels;
        ac->oc[1].m4ac.sbr = -1;
        ac->oc[1].m4ac.ps = -1;

        for (i = 0; i < FF_ARRAY_ELEMS(ff_mpeg4audio_channels); i++)
            if (ff_mpeg4audio_channels[i] == avctx->channels)
                break;
        if (i == FF_ARRAY_ELEMS(ff_mpeg4audio_channels)) {
            i = 0;
        }
        ac->oc[1].m4ac.chan_config = i;

        if (ac->oc[1].m4ac.chan_config) {
            int ret = set_default_channel_config(avctx, layout_map,
                &layout_map_tags, ac->oc[1].m4ac.chan_config);
            if (!ret)
                output_configure(ac, layout_map, layout_map_tags,
                                 OC_GLOBAL_HDR, 0);
            else if (avctx->err_recognition & AV_EF_EXPLODE)
                return AVERROR_INVALIDDATA;
        }
    }

    if (avctx->channels > MAX_CHANNELS) {
        av_log(avctx, AV_LOG_ERROR, "Too many channels\n");
        return AVERROR_INVALIDDATA;
    }

    AAC_INIT_VLC_STATIC( 0, 304);
    AAC_INIT_VLC_STATIC( 1, 270);
    AAC_INIT_VLC_STATIC( 2, 550);
    AAC_INIT_VLC_STATIC( 3, 300);
    AAC_INIT_VLC_STATIC( 4, 328);
    AAC_INIT_VLC_STATIC( 5, 294);
    AAC_INIT_VLC_STATIC( 6, 306);
    AAC_INIT_VLC_STATIC( 7, 268);
    AAC_INIT_VLC_STATIC( 8, 510);
    AAC_INIT_VLC_STATIC( 9, 366);
    AAC_INIT_VLC_STATIC(10, 462);

    ff_aac_sbr_init();

    ff_fmt_convert_init(&ac->fmt_conv, avctx);
    avpriv_float_dsp_init(&ac->fdsp, avctx->flags & CODEC_FLAG_BITEXACT);

    ac->random_state = 0x1f2e3d4c;

    ff_aac_tableinit();

    INIT_VLC_STATIC(&vlc_scalefactors, 7,
                    FF_ARRAY_ELEMS(ff_aac_scalefactor_code),
                    ff_aac_scalefactor_bits,
                    sizeof(ff_aac_scalefactor_bits[0]),
                    sizeof(ff_aac_scalefactor_bits[0]),
                    ff_aac_scalefactor_code,
                    sizeof(ff_aac_scalefactor_code[0]),
                    sizeof(ff_aac_scalefactor_code[0]),
                    352);

    ff_mdct_init(&ac->mdct,       11, 1, 1.0 / (32768.0 * 1024.0));
    ff_mdct_init(&ac->mdct_small,  8, 1, 1.0 / (32768.0 * 128.0));
    ff_mdct_init(&ac->mdct_ltp,   11, 0, -2.0 * 32768.0);
    // window initialization
    ff_kbd_window_init(ff_aac_kbd_long_1024, 4.0, 1024);
    ff_kbd_window_init(ff_aac_kbd_short_128, 6.0, 128);
    ff_init_ff_sine_windows(10);
    ff_init_ff_sine_windows( 7);

    cbrt_tableinit();

    return 0;
}

/**
 * Skip data_stream_element; reference: table 4.10.
 */
static int skip_data_stream_element(AACContext *ac, GetBitContext *gb)
{
    int byte_align = get_bits1(gb);
    int count = get_bits(gb, 8);
    if (count == 255)
        count += get_bits(gb, 8);
    if (byte_align)
        align_get_bits(gb);

    if (get_bits_left(gb) < 8 * count) {
        av_log(ac->avctx, AV_LOG_ERROR, "skip_data_stream_element: "overread_err);
        return AVERROR_INVALIDDATA;
    }
    skip_bits_long(gb, 8 * count);
    return 0;
}

static int decode_prediction(AACContext *ac, IndividualChannelStream *ics,
                             GetBitContext *gb)
{
    int sfb;
    if (get_bits1(gb)) {
        ics->predictor_reset_group = get_bits(gb, 5);
        if (ics->predictor_reset_group == 0 ||
            ics->predictor_reset_group > 30) {
            av_log(ac->avctx, AV_LOG_ERROR,
                   "Invalid Predictor Reset Group.\n");
            return AVERROR_INVALIDDATA;
        }
    }
    for (sfb = 0; sfb < FFMIN(ics->max_sfb, ff_aac_pred_sfb_max[ac->oc[1].m4ac.sampling_index]); sfb++) {
        ics->prediction_used[sfb] = get_bits1(gb);
    }
    return 0;
}

/**
 * Decode Long Term Prediction data; reference: table 4.xx.
 */
static void decode_ltp(LongTermPrediction *ltp,
                       GetBitContext *gb, uint8_t max_sfb)
{
    int sfb;

    ltp->lag  = get_bits(gb, 11);
    ltp->coef = ltp_coef[get_bits(gb, 3)];
    for (sfb = 0; sfb < FFMIN(max_sfb, MAX_LTP_LONG_SFB); sfb++)
        ltp->used[sfb] = get_bits1(gb);
}

/**
 * Decode Individual Channel Stream info; reference: table 4.6.
 */
static int decode_ics_info(AACContext *ac, IndividualChannelStream *ics,
                           GetBitContext *gb)
{
    if (get_bits1(gb)) {
        av_log(ac->avctx, AV_LOG_ERROR, "Reserved bit set.\n");
        return AVERROR_INVALIDDATA;
    }
    ics->window_sequence[1] = ics->window_sequence[0];
    ics->window_sequence[0] = get_bits(gb, 2);
    ics->use_kb_window[1]   = ics->use_kb_window[0];
    ics->use_kb_window[0]   = get_bits1(gb);
    ics->num_window_groups  = 1;
    ics->group_len[0]       = 1;
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        int i;
        ics->max_sfb = get_bits(gb, 4);
        for (i = 0; i < 7; i++) {
            if (get_bits1(gb)) {
                ics->group_len[ics->num_window_groups - 1]++;
            } else {
                ics->num_window_groups++;
                ics->group_len[ics->num_window_groups - 1] = 1;
            }
        }
        ics->num_windows       = 8;
        ics->swb_offset        =    ff_swb_offset_128[ac->oc[1].m4ac.sampling_index];
        ics->num_swb           =   ff_aac_num_swb_128[ac->oc[1].m4ac.sampling_index];
        ics->tns_max_bands     = ff_tns_max_bands_128[ac->oc[1].m4ac.sampling_index];
        ics->predictor_present = 0;
    } else {
        ics->max_sfb               = get_bits(gb, 6);
        ics->num_windows           = 1;
        ics->swb_offset            =    ff_swb_offset_1024[ac->oc[1].m4ac.sampling_index];
        ics->num_swb               =   ff_aac_num_swb_1024[ac->oc[1].m4ac.sampling_index];
        ics->tns_max_bands         = ff_tns_max_bands_1024[ac->oc[1].m4ac.sampling_index];
        ics->predictor_present     = get_bits1(gb);
        ics->predictor_reset_group = 0;
        if (ics->predictor_present) {
            if (ac->oc[1].m4ac.object_type == AOT_AAC_MAIN) {
                if (decode_prediction(ac, ics, gb)) {
                    goto fail;
                }
            } else if (ac->oc[1].m4ac.object_type == AOT_AAC_LC) {
                av_log(ac->avctx, AV_LOG_ERROR,
                       "Prediction is not allowed in AAC-LC.\n");
                goto fail;
            } else {
                if ((ics->ltp.present = get_bits(gb, 1)))
                    decode_ltp(&ics->ltp, gb, ics->max_sfb);
            }
        }
    }

    if (ics->max_sfb > ics->num_swb) {
        av_log(ac->avctx, AV_LOG_ERROR,
               "Number of scalefactor bands in group (%d) "
               "exceeds limit (%d).\n",
               ics->max_sfb, ics->num_swb);
        goto fail;
    }

    return 0;
fail:
    ics->max_sfb = 0;
    return AVERROR_INVALIDDATA;
}

/**
 * Decode band types (section_data payload); reference: table 4.46.
 *
 * @param   band_type           array of the used band type
 * @param   band_type_run_end   array of the last scalefactor band of a band type run
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_band_types(AACContext *ac, enum BandType band_type[120],
                             int band_type_run_end[120], GetBitContext *gb,
                             IndividualChannelStream *ics)
{
    int g, idx = 0;
    const int bits = (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) ? 3 : 5;
    for (g = 0; g < ics->num_window_groups; g++) {
        int k = 0;
        while (k < ics->max_sfb) {
            uint8_t sect_end = k;
            int sect_len_incr;
            int sect_band_type = get_bits(gb, 4);
            if (sect_band_type == 12) {
                av_log(ac->avctx, AV_LOG_ERROR, "invalid band type\n");
                return AVERROR_INVALIDDATA;
            }
            do {
                sect_len_incr = get_bits(gb, bits);
                sect_end += sect_len_incr;
                if (get_bits_left(gb) < 0) {
                    av_log(ac->avctx, AV_LOG_ERROR, "decode_band_types: "overread_err);
                    return AVERROR_INVALIDDATA;
                }
                if (sect_end > ics->max_sfb) {
                    av_log(ac->avctx, AV_LOG_ERROR,
                           "Number of bands (%d) exceeds limit (%d).\n",
                           sect_end, ics->max_sfb);
                    return AVERROR_INVALIDDATA;
                }
            } while (sect_len_incr == (1 << bits) - 1);
            for (; k < sect_end; k++) {
                band_type        [idx]   = sect_band_type;
                band_type_run_end[idx++] = sect_end;
            }
        }
    }
    return 0;
}

/**
 * Decode scalefactors; reference: table 4.47.
 *
 * @param   global_gain         first scalefactor value as scalefactors are differentially coded
 * @param   band_type           array of the used band type
 * @param   band_type_run_end   array of the last scalefactor band of a band type run
 * @param   sf                  array of scalefactors or intensity stereo positions
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_scalefactors(AACContext *ac, float sf[120], GetBitContext *gb,
                               unsigned int global_gain,
                               IndividualChannelStream *ics,
                               enum BandType band_type[120],
                               int band_type_run_end[120])
{
    int g, i, idx = 0;
    int offset[3] = { global_gain, global_gain - 90, 0 };
    int clipped_offset;
    int noise_flag = 1;
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb;) {
            int run_end = band_type_run_end[idx];
            if (band_type[idx] == ZERO_BT) {
                for (; i < run_end; i++, idx++)
                    sf[idx] = 0.;
            } else if ((band_type[idx] == INTENSITY_BT) ||
                       (band_type[idx] == INTENSITY_BT2)) {
                for (; i < run_end; i++, idx++) {
                    offset[2] += get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60;
                    clipped_offset = av_clip(offset[2], -155, 100);
                    if (offset[2] != clipped_offset) {
                        avpriv_request_sample(ac->avctx,
                                              "If you heard an audible artifact, there may be a bug in the decoder. "
                                              "Clipped intensity stereo position (%d -> %d)",
                                              offset[2], clipped_offset);
                    }
                    sf[idx] = ff_aac_pow2sf_tab[-clipped_offset + POW_SF2_ZERO];
                }
            } else if (band_type[idx] == NOISE_BT) {
                for (; i < run_end; i++, idx++) {
                    if (noise_flag-- > 0)
                        offset[1] += get_bits(gb, 9) - 256;
                    else
                        offset[1] += get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60;
                    clipped_offset = av_clip(offset[1], -100, 155);
                    if (offset[1] != clipped_offset) {
                        avpriv_request_sample(ac->avctx,
                                              "If you heard an audible artifact, there may be a bug in the decoder. "
                                              "Clipped noise gain (%d -> %d)",
                                              offset[1], clipped_offset);
                    }
                    sf[idx] = -ff_aac_pow2sf_tab[clipped_offset + POW_SF2_ZERO];
                }
            } else {
                for (; i < run_end; i++, idx++) {
                    offset[0] += get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60;
                    if (offset[0] > 255U) {
                        av_log(ac->avctx, AV_LOG_ERROR,
                               "Scalefactor (%d) out of range.\n", offset[0]);
                        return AVERROR_INVALIDDATA;
                    }
                    sf[idx] = -ff_aac_pow2sf_tab[offset[0] - 100 + POW_SF2_ZERO];
                }
            }
        }
    }
    return 0;
}

/**
 * Decode pulse data; reference: table 4.7.
 */
static int decode_pulses(Pulse *pulse, GetBitContext *gb,
                         const uint16_t *swb_offset, int num_swb)
{
    int i, pulse_swb;
    pulse->num_pulse = get_bits(gb, 2) + 1;
    pulse_swb        = get_bits(gb, 6);
    if (pulse_swb >= num_swb)
        return -1;
    pulse->pos[0]    = swb_offset[pulse_swb];
    pulse->pos[0]   += get_bits(gb, 5);
    if (pulse->pos[0] > 1023)
        return -1;
    pulse->amp[0]    = get_bits(gb, 4);
    for (i = 1; i < pulse->num_pulse; i++) {
        pulse->pos[i] = get_bits(gb, 5) + pulse->pos[i - 1];
        if (pulse->pos[i] > 1023)
            return -1;
        pulse->amp[i] = get_bits(gb, 4);
    }
    return 0;
}

/**
 * Decode Temporal Noise Shaping data; reference: table 4.48.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_tns(AACContext *ac, TemporalNoiseShaping *tns,
                      GetBitContext *gb, const IndividualChannelStream *ics)
{
    int w, filt, i, coef_len, coef_res, coef_compress;
    const int is8 = ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE;
    const int tns_max_order = is8 ? 7 : ac->oc[1].m4ac.object_type == AOT_AAC_MAIN ? 20 : 12;
    for (w = 0; w < ics->num_windows; w++) {
        if ((tns->n_filt[w] = get_bits(gb, 2 - is8))) {
            coef_res = get_bits1(gb);

            for (filt = 0; filt < tns->n_filt[w]; filt++) {
                int tmp2_idx;
                tns->length[w][filt] = get_bits(gb, 6 - 2 * is8);

                if ((tns->order[w][filt] = get_bits(gb, 5 - 2 * is8)) > tns_max_order) {
                    av_log(ac->avctx, AV_LOG_ERROR,
                           "TNS filter order %d is greater than maximum %d.\n",
                           tns->order[w][filt], tns_max_order);
                    tns->order[w][filt] = 0;
                    return AVERROR_INVALIDDATA;
                }
                if (tns->order[w][filt]) {
                    tns->direction[w][filt] = get_bits1(gb);
                    coef_compress = get_bits1(gb);
                    coef_len = coef_res + 3 - coef_compress;
                    tmp2_idx = 2 * coef_compress + coef_res;

                    for (i = 0; i < tns->order[w][filt]; i++)
                        tns->coef[w][filt][i] = tns_tmp2_map[tmp2_idx][get_bits(gb, coef_len)];
                }
            }
        }
    }
    return 0;
}

/**
 * Decode Mid/Side data; reference: table 4.54.
 *
 * @param   ms_present  Indicates mid/side stereo presence. [0] mask is all 0s;
 *                      [1] mask is decoded from bitstream; [2] mask is all 1s;
 *                      [3] reserved for scalable AAC
 */
static void decode_mid_side_stereo(ChannelElement *cpe, GetBitContext *gb,
                                   int ms_present)
{
    int idx;
    if (ms_present == 1) {
        for (idx = 0;
             idx < cpe->ch[0].ics.num_window_groups * cpe->ch[0].ics.max_sfb;
             idx++)
            cpe->ms_mask[idx] = get_bits1(gb);
    } else if (ms_present == 2) {
        memset(cpe->ms_mask, 1,  sizeof(cpe->ms_mask[0]) * cpe->ch[0].ics.num_window_groups * cpe->ch[0].ics.max_sfb);
    }
}

#ifndef VMUL2
static inline float *VMUL2(float *dst, const float *v, unsigned idx,
                           const float *scale)
{
    float s = *scale;
    *dst++ = v[idx    & 15] * s;
    *dst++ = v[idx>>4 & 15] * s;
    return dst;
}
#endif

#ifndef VMUL4
static inline float *VMUL4(float *dst, const float *v, unsigned idx,
                           const float *scale)
{
    float s = *scale;
    *dst++ = v[idx    & 3] * s;
    *dst++ = v[idx>>2 & 3] * s;
    *dst++ = v[idx>>4 & 3] * s;
    *dst++ = v[idx>>6 & 3] * s;
    return dst;
}
#endif

#ifndef VMUL2S
static inline float *VMUL2S(float *dst, const float *v, unsigned idx,
                            unsigned sign, const float *scale)
{
    union av_intfloat32 s0, s1;

    s0.f = s1.f = *scale;
    s0.i ^= sign >> 1 << 31;
    s1.i ^= sign      << 31;

    *dst++ = v[idx    & 15] * s0.f;
    *dst++ = v[idx>>4 & 15] * s1.f;

    return dst;
}
#endif

#ifndef VMUL4S
static inline float *VMUL4S(float *dst, const float *v, unsigned idx,
                            unsigned sign, const float *scale)
{
    unsigned nz = idx >> 12;
    union av_intfloat32 s = { .f = *scale };
    union av_intfloat32 t;

    t.i = s.i ^ (sign & 1U<<31);
    *dst++ = v[idx    & 3] * t.f;

    sign <<= nz & 1; nz >>= 1;
    t.i = s.i ^ (sign & 1U<<31);
    *dst++ = v[idx>>2 & 3] * t.f;

    sign <<= nz & 1; nz >>= 1;
    t.i = s.i ^ (sign & 1U<<31);
    *dst++ = v[idx>>4 & 3] * t.f;

    sign <<= nz & 1;
    t.i = s.i ^ (sign & 1U<<31);
    *dst++ = v[idx>>6 & 3] * t.f;

    return dst;
}
#endif

/**
 * Decode spectral data; reference: table 4.50.
 * Dequantize and scale spectral data; reference: 4.6.3.3.
 *
 * @param   coef            array of dequantized, scaled spectral data
 * @param   sf              array of scalefactors or intensity stereo positions
 * @param   pulse_present   set if pulses are present
 * @param   pulse           pointer to pulse data struct
 * @param   band_type       array of the used band type
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_spectrum_and_dequant(AACContext *ac, float coef[1024],
                                       GetBitContext *gb, const float sf[120],
                                       int pulse_present, const Pulse *pulse,
                                       const IndividualChannelStream *ics,
                                       enum BandType band_type[120])
{
    int i, k, g, idx = 0;
    const int c = 1024 / ics->num_windows;
    const uint16_t *offsets = ics->swb_offset;
    float *coef_base = coef;

    for (g = 0; g < ics->num_windows; g++)
        memset(coef + g * 128 + offsets[ics->max_sfb], 0,
               sizeof(float) * (c - offsets[ics->max_sfb]));

    for (g = 0; g < ics->num_window_groups; g++) {
        unsigned g_len = ics->group_len[g];

        for (i = 0; i < ics->max_sfb; i++, idx++) {
            const unsigned cbt_m1 = band_type[idx] - 1;
            float *cfo = coef + offsets[i];
            int off_len = offsets[i + 1] - offsets[i];
            int group;

            if (cbt_m1 >= INTENSITY_BT2 - 1) {
                for (group = 0; group < g_len; group++, cfo+=128) {
                    memset(cfo, 0, off_len * sizeof(float));
                }
            } else if (cbt_m1 == NOISE_BT - 1) {
                for (group = 0; group < g_len; group++, cfo+=128) {
                    float scale;
                    float band_energy;

                    for (k = 0; k < off_len; k++) {
                        ac->random_state  = lcg_random(ac->random_state);
                        cfo[k] = ac->random_state;
                    }

                    band_energy = ac->fdsp.scalarproduct_float(cfo, cfo, off_len);
                    scale = sf[idx] / sqrtf(band_energy);
                    ac->fdsp.vector_fmul_scalar(cfo, cfo, scale, off_len);
                }
            } else {
                const float *vq = ff_aac_codebook_vector_vals[cbt_m1];
                const uint16_t *cb_vector_idx = ff_aac_codebook_vector_idx[cbt_m1];
                VLC_TYPE (*vlc_tab)[2] = vlc_spectral[cbt_m1].table;
                OPEN_READER(re, gb);

                switch (cbt_m1 >> 1) {
                case 0:
                    for (group = 0; group < g_len; group++, cfo+=128) {
                        float *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned cb_idx;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = cb_vector_idx[code];
                            cf = VMUL4(cf, vq, cb_idx, sf + idx);
                        } while (len -= 4);
                    }
                    break;

                case 1:
                    for (group = 0; group < g_len; group++, cfo+=128) {
                        float *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned nnz;
                            unsigned cb_idx;
                            uint32_t bits;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = cb_vector_idx[code];
                            nnz = cb_idx >> 8 & 15;
                            bits = nnz ? GET_CACHE(re, gb) : 0;
                            LAST_SKIP_BITS(re, gb, nnz);
                            cf = VMUL4S(cf, vq, cb_idx, bits, sf + idx);
                        } while (len -= 4);
                    }
                    break;

                case 2:
                    for (group = 0; group < g_len; group++, cfo+=128) {
                        float *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned cb_idx;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = cb_vector_idx[code];
                            cf = VMUL2(cf, vq, cb_idx, sf + idx);
                        } while (len -= 2);
                    }
                    break;

                case 3:
                case 4:
                    for (group = 0; group < g_len; group++, cfo+=128) {
                        float *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned nnz;
                            unsigned cb_idx;
                            unsigned sign;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = cb_vector_idx[code];
                            nnz = cb_idx >> 8 & 15;
                            sign = nnz ? SHOW_UBITS(re, gb, nnz) << (cb_idx >> 12) : 0;
                            LAST_SKIP_BITS(re, gb, nnz);
                            cf = VMUL2S(cf, vq, cb_idx, sign, sf + idx);
                        } while (len -= 2);
                    }
                    break;

                default:
                    for (group = 0; group < g_len; group++, cfo+=128) {
                        float *cf = cfo;
                        uint32_t *icf = (uint32_t *) cf;
                        int len = off_len;

                        do {
                            int code;
                            unsigned nzt, nnz;
                            unsigned cb_idx;
                            uint32_t bits;
                            int j;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);

                            if (!code) {
                                *icf++ = 0;
                                *icf++ = 0;
                                continue;
                            }

                            cb_idx = cb_vector_idx[code];
                            nnz = cb_idx >> 12;
                            nzt = cb_idx >> 8;
                            bits = SHOW_UBITS(re, gb, nnz) << (32-nnz);
                            LAST_SKIP_BITS(re, gb, nnz);

                            for (j = 0; j < 2; j++) {
                                if (nzt & 1<<j) {
                                    uint32_t b;
                                    int n;
                                    /* The total length of escape_sequence must be < 22 bits according
                                       to the specification (i.e. max is 111111110xxxxxxxxxxxx). */
                                    UPDATE_CACHE(re, gb);
                                    b = GET_CACHE(re, gb);
                                    b = 31 - av_log2(~b);

                                    if (b > 8) {
                                        av_log(ac->avctx, AV_LOG_ERROR, "error in spectral data, ESC overflow\n");
                                        return AVERROR_INVALIDDATA;
                                    }

                                    SKIP_BITS(re, gb, b + 1);
                                    b += 4;
                                    n = (1 << b) + SHOW_UBITS(re, gb, b);
                                    LAST_SKIP_BITS(re, gb, b);
                                    *icf++ = cbrt_tab[n] | (bits & 1U<<31);
                                    bits <<= 1;
                                } else {
                                    unsigned v = ((const uint32_t*)vq)[cb_idx & 15];
                                    *icf++ = (bits & 1U<<31) | v;
                                    bits <<= !!v;
                                }
                                cb_idx >>= 4;
                            }
                        } while (len -= 2);

                        ac->fdsp.vector_fmul_scalar(cfo, cfo, sf[idx], off_len);
                    }
                }

                CLOSE_READER(re, gb);
            }
        }
        coef += g_len << 7;
    }

    if (pulse_present) {
        idx = 0;
        for (i = 0; i < pulse->num_pulse; i++) {
            float co = coef_base[ pulse->pos[i] ];
            while (offsets[idx + 1] <= pulse->pos[i])
                idx++;
            if (band_type[idx] != NOISE_BT && sf[idx]) {
                float ico = -pulse->amp[i];
                if (co) {
                    co /= sf[idx];
                    ico = co / sqrtf(sqrtf(fabsf(co))) + (co > 0 ? -ico : ico);
                }
                coef_base[ pulse->pos[i] ] = cbrtf(fabsf(ico)) * ico * sf[idx];
            }
        }
    }
    return 0;
}

static av_always_inline float flt16_round(float pf)
{
    union av_intfloat32 tmp;
    tmp.f = pf;
    tmp.i = (tmp.i + 0x00008000U) & 0xFFFF0000U;
    return tmp.f;
}

static av_always_inline float flt16_even(float pf)
{
    union av_intfloat32 tmp;
    tmp.f = pf;
    tmp.i = (tmp.i + 0x00007FFFU + (tmp.i & 0x00010000U >> 16)) & 0xFFFF0000U;
    return tmp.f;
}

static av_always_inline float flt16_trunc(float pf)
{
    union av_intfloat32 pun;
    pun.f = pf;
    pun.i &= 0xFFFF0000U;
    return pun.f;
}

static av_always_inline void predict(PredictorState *ps, float *coef,
                                     int output_enable)
{
    const float a     = 0.953125; // 61.0 / 64
    const float alpha = 0.90625;  // 29.0 / 32
    float e0, e1;
    float pv;
    float k1, k2;
    float   r0 = ps->r0,     r1 = ps->r1;
    float cor0 = ps->cor0, cor1 = ps->cor1;
    float var0 = ps->var0, var1 = ps->var1;

    k1 = var0 > 1 ? cor0 * flt16_even(a / var0) : 0;
    k2 = var1 > 1 ? cor1 * flt16_even(a / var1) : 0;

    pv = flt16_round(k1 * r0 + k2 * r1);
    if (output_enable)
        *coef += pv;

    e0 = *coef;
    e1 = e0 - k1 * r0;

    ps->cor1 = flt16_trunc(alpha * cor1 + r1 * e1);
    ps->var1 = flt16_trunc(alpha * var1 + 0.5f * (r1 * r1 + e1 * e1));
    ps->cor0 = flt16_trunc(alpha * cor0 + r0 * e0);
    ps->var0 = flt16_trunc(alpha * var0 + 0.5f * (r0 * r0 + e0 * e0));

    ps->r1 = flt16_trunc(a * (r0 - k1 * e0));
    ps->r0 = flt16_trunc(a * e0);
}

/**
 * Apply AAC-Main style frequency domain prediction.
 */
static void apply_prediction(AACContext *ac, SingleChannelElement *sce)
{
    int sfb, k;

    if (!sce->ics.predictor_initialized) {
        reset_all_predictors(sce->predictor_state);
        sce->ics.predictor_initialized = 1;
    }

    if (sce->ics.window_sequence[0] != EIGHT_SHORT_SEQUENCE) {
        for (sfb = 0;
             sfb < ff_aac_pred_sfb_max[ac->oc[1].m4ac.sampling_index];
             sfb++) {
            for (k = sce->ics.swb_offset[sfb];
                 k < sce->ics.swb_offset[sfb + 1];
                 k++) {
                predict(&sce->predictor_state[k], &sce->coeffs[k],
                        sce->ics.predictor_present &&
                        sce->ics.prediction_used[sfb]);
            }
        }
        if (sce->ics.predictor_reset_group)
            reset_predictor_group(sce->predictor_state,
                                  sce->ics.predictor_reset_group);
    } else
        reset_all_predictors(sce->predictor_state);
}

/**
 * Decode an individual_channel_stream payload; reference: table 4.44.
 *
 * @param   common_window   Channels have independent [0], or shared [1], Individual Channel Stream information.
 * @param   scale_flag      scalable [1] or non-scalable [0] AAC (Unused until scalable AAC is implemented.)
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_ics(AACContext *ac, SingleChannelElement *sce,
                      GetBitContext *gb, int common_window, int scale_flag)
{
    Pulse pulse;
    TemporalNoiseShaping    *tns = &sce->tns;
    IndividualChannelStream *ics = &sce->ics;
    float *out = sce->coeffs;
    int global_gain, pulse_present = 0;
    int ret;

    /* This assignment is to silence a GCC warning about the variable being used
     * uninitialized when in fact it always is.
     */
    pulse.num_pulse = 0;

    global_gain = get_bits(gb, 8);

    if (!common_window && !scale_flag) {
        if (decode_ics_info(ac, ics, gb) < 0)
            return AVERROR_INVALIDDATA;
    }

    if ((ret = decode_band_types(ac, sce->band_type,
                                 sce->band_type_run_end, gb, ics)) < 0)
        return ret;
    if ((ret = decode_scalefactors(ac, sce->sf, gb, global_gain, ics,
                                  sce->band_type, sce->band_type_run_end)) < 0)
        return ret;

    pulse_present = 0;
    if (!scale_flag) {
        if ((pulse_present = get_bits1(gb))) {
            if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
                av_log(ac->avctx, AV_LOG_ERROR,
                       "Pulse tool not allowed in eight short sequence.\n");
                return AVERROR_INVALIDDATA;
            }
            if (decode_pulses(&pulse, gb, ics->swb_offset, ics->num_swb)) {
                av_log(ac->avctx, AV_LOG_ERROR,
                       "Pulse data corrupt or invalid.\n");
                return AVERROR_INVALIDDATA;
            }
        }
        if ((tns->present = get_bits1(gb)) && decode_tns(ac, tns, gb, ics))
            return AVERROR_INVALIDDATA;
        if (get_bits1(gb)) {
            avpriv_request_sample(ac->avctx, "SSR");
            return AVERROR_PATCHWELCOME;
        }
    }

    if (decode_spectrum_and_dequant(ac, out, gb, sce->sf, pulse_present,
                                    &pulse, ics, sce->band_type) < 0)
        return AVERROR_INVALIDDATA;

    if (ac->oc[1].m4ac.object_type == AOT_AAC_MAIN && !common_window)
        apply_prediction(ac, sce);

    return 0;
}

/**
 * Mid/Side stereo decoding; reference: 4.6.8.1.3.
 */
static void apply_mid_side_stereo(AACContext *ac, ChannelElement *cpe)
{
    const IndividualChannelStream *ics = &cpe->ch[0].ics;
    float *ch0 = cpe->ch[0].coeffs;
    float *ch1 = cpe->ch[1].coeffs;
    int g, i, group, idx = 0;
    const uint16_t *offsets = ics->swb_offset;
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb; i++, idx++) {
            if (cpe->ms_mask[idx] &&
                cpe->ch[0].band_type[idx] < NOISE_BT &&
                cpe->ch[1].band_type[idx] < NOISE_BT) {
                for (group = 0; group < ics->group_len[g]; group++) {
                    ac->fdsp.butterflies_float(ch0 + group * 128 + offsets[i],
                                               ch1 + group * 128 + offsets[i],
                                               offsets[i+1] - offsets[i]);
                }
            }
        }
        ch0 += ics->group_len[g] * 128;
        ch1 += ics->group_len[g] * 128;
    }
}

/**
 * intensity stereo decoding; reference: 4.6.8.2.3
 *
 * @param   ms_present  Indicates mid/side stereo presence. [0] mask is all 0s;
 *                      [1] mask is decoded from bitstream; [2] mask is all 1s;
 *                      [3] reserved for scalable AAC
 */
static void apply_intensity_stereo(AACContext *ac,
                                   ChannelElement *cpe, int ms_present)
{
    const IndividualChannelStream *ics = &cpe->ch[1].ics;
    SingleChannelElement         *sce1 = &cpe->ch[1];
    float *coef0 = cpe->ch[0].coeffs, *coef1 = cpe->ch[1].coeffs;
    const uint16_t *offsets = ics->swb_offset;
    int g, group, i, idx = 0;
    int c;
    float scale;
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb;) {
            if (sce1->band_type[idx] == INTENSITY_BT ||
                sce1->band_type[idx] == INTENSITY_BT2) {
                const int bt_run_end = sce1->band_type_run_end[idx];
                for (; i < bt_run_end; i++, idx++) {
                    c = -1 + 2 * (sce1->band_type[idx] - 14);
                    if (ms_present)
                        c *= 1 - 2 * cpe->ms_mask[idx];
                    scale = c * sce1->sf[idx];
                    for (group = 0; group < ics->group_len[g]; group++)
                        ac->fdsp.vector_fmul_scalar(coef1 + group * 128 + offsets[i],
                                                    coef0 + group * 128 + offsets[i],
                                                    scale,
                                                    offsets[i + 1] - offsets[i]);
                }
            } else {
                int bt_run_end = sce1->band_type_run_end[idx];
                idx += bt_run_end - i;
                i    = bt_run_end;
            }
        }
        coef0 += ics->group_len[g] * 128;
        coef1 += ics->group_len[g] * 128;
    }
}

/**
 * Decode a channel_pair_element; reference: table 4.4.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_cpe(AACContext *ac, GetBitContext *gb, ChannelElement *cpe)
{
    int i, ret, common_window, ms_present = 0;

    common_window = get_bits1(gb);
    if (common_window) {
        if (decode_ics_info(ac, &cpe->ch[0].ics, gb))
            return AVERROR_INVALIDDATA;
        i = cpe->ch[1].ics.use_kb_window[0];
        cpe->ch[1].ics = cpe->ch[0].ics;
        cpe->ch[1].ics.use_kb_window[1] = i;
        if (cpe->ch[1].ics.predictor_present &&
            (ac->oc[1].m4ac.object_type != AOT_AAC_MAIN))
            if ((cpe->ch[1].ics.ltp.present = get_bits(gb, 1)))
                decode_ltp(&cpe->ch[1].ics.ltp, gb, cpe->ch[1].ics.max_sfb);
        ms_present = get_bits(gb, 2);
        if (ms_present == 3) {
            av_log(ac->avctx, AV_LOG_ERROR, "ms_present = 3 is reserved.\n");
            return AVERROR_INVALIDDATA;
        } else if (ms_present)
            decode_mid_side_stereo(cpe, gb, ms_present);
    }
    if ((ret = decode_ics(ac, &cpe->ch[0], gb, common_window, 0)))
        return ret;
    if ((ret = decode_ics(ac, &cpe->ch[1], gb, common_window, 0)))
        return ret;

    if (common_window) {
        if (ms_present)
            apply_mid_side_stereo(ac, cpe);
        if (ac->oc[1].m4ac.object_type == AOT_AAC_MAIN) {
            apply_prediction(ac, &cpe->ch[0]);
            apply_prediction(ac, &cpe->ch[1]);
        }
    }

    apply_intensity_stereo(ac, cpe, ms_present);
    return 0;
}

static const float cce_scale[] = {
    1.09050773266525765921, //2^(1/8)
    1.18920711500272106672, //2^(1/4)
    M_SQRT2,
    2,
};

/**
 * Decode coupling_channel_element; reference: table 4.8.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_cce(AACContext *ac, GetBitContext *gb, ChannelElement *che)
{
    int num_gain = 0;
    int c, g, sfb, ret;
    int sign;
    float scale;
    SingleChannelElement *sce = &che->ch[0];
    ChannelCoupling     *coup = &che->coup;

    coup->coupling_point = 2 * get_bits1(gb);
    coup->num_coupled = get_bits(gb, 3);
    for (c = 0; c <= coup->num_coupled; c++) {
        num_gain++;
        coup->type[c] = get_bits1(gb) ? TYPE_CPE : TYPE_SCE;
        coup->id_select[c] = get_bits(gb, 4);
        if (coup->type[c] == TYPE_CPE) {
            coup->ch_select[c] = get_bits(gb, 2);
            if (coup->ch_select[c] == 3)
                num_gain++;
        } else
            coup->ch_select[c] = 2;
    }
    coup->coupling_point += get_bits1(gb) || (coup->coupling_point >> 1);

    sign  = get_bits(gb, 1);
    scale = cce_scale[get_bits(gb, 2)];

    if ((ret = decode_ics(ac, sce, gb, 0, 0)))
        return ret;

    for (c = 0; c < num_gain; c++) {
        int idx  = 0;
        int cge  = 1;
        int gain = 0;
        float gain_cache = 1.;
        if (c) {
            cge = coup->coupling_point == AFTER_IMDCT ? 1 : get_bits1(gb);
            gain = cge ? get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60: 0;
            gain_cache = powf(scale, -gain);
        }
        if (coup->coupling_point == AFTER_IMDCT) {
            coup->gain[c][0] = gain_cache;
        } else {
            for (g = 0; g < sce->ics.num_window_groups; g++) {
                for (sfb = 0; sfb < sce->ics.max_sfb; sfb++, idx++) {
                    if (sce->band_type[idx] != ZERO_BT) {
                        if (!cge) {
                            int t = get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60;
                            if (t) {
                                int s = 1;
                                t = gain += t;
                                if (sign) {
                                    s  -= 2 * (t & 0x1);
                                    t >>= 1;
                                }
                                gain_cache = powf(scale, -t) * s;
                            }
                        }
                        coup->gain[c][idx] = gain_cache;
                    }
                }
            }
        }
    }
    return 0;
}

/**
 * Parse whether channels are to be excluded from Dynamic Range Compression; reference: table 4.53.
 *
 * @return  Returns number of bytes consumed.
 */
static int decode_drc_channel_exclusions(DynamicRangeControl *che_drc,
                                         GetBitContext *gb)
{
    int i;
    int num_excl_chan = 0;

    do {
        for (i = 0; i < 7; i++)
            che_drc->exclude_mask[num_excl_chan++] = get_bits1(gb);
    } while (num_excl_chan < MAX_CHANNELS - 7 && get_bits1(gb));

    return num_excl_chan / 7;
}

/**
 * Decode dynamic range information; reference: table 4.52.
 *
 * @return  Returns number of bytes consumed.
 */
static int decode_dynamic_range(DynamicRangeControl *che_drc,
                                GetBitContext *gb)
{
    int n             = 1;
    int drc_num_bands = 1;
    int i;

    /* pce_tag_present? */
    if (get_bits1(gb)) {
        che_drc->pce_instance_tag  = get_bits(gb, 4);
        skip_bits(gb, 4); // tag_reserved_bits
        n++;
    }

    /* excluded_chns_present? */
    if (get_bits1(gb)) {
        n += decode_drc_channel_exclusions(che_drc, gb);
    }

    /* drc_bands_present? */
    if (get_bits1(gb)) {
        che_drc->band_incr            = get_bits(gb, 4);
        che_drc->interpolation_scheme = get_bits(gb, 4);
        n++;
        drc_num_bands += che_drc->band_incr;
        for (i = 0; i < drc_num_bands; i++) {
            che_drc->band_top[i] = get_bits(gb, 8);
            n++;
        }
    }

    /* prog_ref_level_present? */
    if (get_bits1(gb)) {
        che_drc->prog_ref_level = get_bits(gb, 7);
        skip_bits1(gb); // prog_ref_level_reserved_bits
        n++;
    }

    for (i = 0; i < drc_num_bands; i++) {
        che_drc->dyn_rng_sgn[i] = get_bits1(gb);
        che_drc->dyn_rng_ctl[i] = get_bits(gb, 7);
        n++;
    }

    return n;
}

static int decode_fill(AACContext *ac, GetBitContext *gb, int len) {
    uint8_t buf[256];
    int i, major, minor;

    if (len < 13+7*8)
        goto unknown;

    get_bits(gb, 13); len -= 13;

    for(i=0; i+1<sizeof(buf) && len>=8; i++, len-=8)
        buf[i] = get_bits(gb, 8);

    buf[i] = 0;
    if (ac->avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(ac->avctx, AV_LOG_DEBUG, "FILL:%s\n", buf);

    if (sscanf(buf, "libfaac %d.%d", &major, &minor) == 2){
        ac->avctx->internal->skip_samples = 1024;
    }

unknown:
    skip_bits_long(gb, len);

    return 0;
}

/**
 * Decode extension data (incomplete); reference: table 4.51.
 *
 * @param   cnt length of TYPE_FIL syntactic element in bytes
 *
 * @return Returns number of bytes consumed
 */
static int decode_extension_payload(AACContext *ac, GetBitContext *gb, int cnt,
                                    ChannelElement *che, enum RawDataBlockType elem_type)
{
    int crc_flag = 0;
    int res = cnt;
    switch (get_bits(gb, 4)) { // extension type
    case EXT_SBR_DATA_CRC:
        crc_flag++;
    case EXT_SBR_DATA:
        if (!che) {
            av_log(ac->avctx, AV_LOG_ERROR, "SBR was found before the first channel element.\n");
            return res;
        } else if (!ac->oc[1].m4ac.sbr) {
            av_log(ac->avctx, AV_LOG_ERROR, "SBR signaled to be not-present but was found in the bitstream.\n");
            skip_bits_long(gb, 8 * cnt - 4);
            return res;
        } else if (ac->oc[1].m4ac.sbr == -1 && ac->oc[1].status == OC_LOCKED) {
            av_log(ac->avctx, AV_LOG_ERROR, "Implicit SBR was found with a first occurrence after the first frame.\n");
            skip_bits_long(gb, 8 * cnt - 4);
            return res;
        } else if (ac->oc[1].m4ac.ps == -1 && ac->oc[1].status < OC_LOCKED && ac->avctx->channels == 1) {
            ac->oc[1].m4ac.sbr = 1;
            ac->oc[1].m4ac.ps = 1;
            output_configure(ac, ac->oc[1].layout_map, ac->oc[1].layout_map_tags,
                             ac->oc[1].status, 1);
        } else {
            ac->oc[1].m4ac.sbr = 1;
        }
        res = ff_decode_sbr_extension(ac, &che->sbr, gb, crc_flag, cnt, elem_type);
        break;
    case EXT_DYNAMIC_RANGE:
        res = decode_dynamic_range(&ac->che_drc, gb);
        break;
    case EXT_FILL:
        decode_fill(ac, gb, 8 * cnt - 4);
        break;
    case EXT_FILL_DATA:
    case EXT_DATA_ELEMENT:
    default:
        skip_bits_long(gb, 8 * cnt - 4);
        break;
    };
    return res;
}

/**
 * Decode Temporal Noise Shaping filter coefficients and apply all-pole filters; reference: 4.6.9.3.
 *
 * @param   decode  1 if tool is used normally, 0 if tool is used in LTP.
 * @param   coef    spectral coefficients
 */
static void apply_tns(float coef[1024], TemporalNoiseShaping *tns,
                      IndividualChannelStream *ics, int decode)
{
    const int mmm = FFMIN(ics->tns_max_bands, ics->max_sfb);
    int w, filt, m, i;
    int bottom, top, order, start, end, size, inc;
    float lpc[TNS_MAX_ORDER];
    float tmp[TNS_MAX_ORDER+1];

    for (w = 0; w < ics->num_windows; w++) {
        bottom = ics->num_swb;
        for (filt = 0; filt < tns->n_filt[w]; filt++) {
            top    = bottom;
            bottom = FFMAX(0, top - tns->length[w][filt]);
            order  = tns->order[w][filt];
            if (order == 0)
                continue;

            // tns_decode_coef
            compute_lpc_coefs(tns->coef[w][filt], order, lpc, 0, 0, 0);

            start = ics->swb_offset[FFMIN(bottom, mmm)];
            end   = ics->swb_offset[FFMIN(   top, mmm)];
            if ((size = end - start) <= 0)
                continue;
            if (tns->direction[w][filt]) {
                inc = -1;
                start = end - 1;
            } else {
                inc = 1;
            }
            start += w * 128;

            if (decode) {
                // ar filter
                for (m = 0; m < size; m++, start += inc)
                    for (i = 1; i <= FFMIN(m, order); i++)
                        coef[start] -= coef[start - i * inc] * lpc[i - 1];
            } else {
                // ma filter
                for (m = 0; m < size; m++, start += inc) {
                    tmp[0] = coef[start];
                    for (i = 1; i <= FFMIN(m, order); i++)
                        coef[start] += tmp[i] * lpc[i - 1];
                    for (i = order; i > 0; i--)
                        tmp[i] = tmp[i - 1];
                }
            }
        }
    }
}

/**
 *  Apply windowing and MDCT to obtain the spectral
 *  coefficient from the predicted sample by LTP.
 */
static void windowing_and_mdct_ltp(AACContext *ac, float *out,
                                   float *in, IndividualChannelStream *ics)
{
    const float *lwindow      = ics->use_kb_window[0] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float *swindow      = ics->use_kb_window[0] ? ff_aac_kbd_short_128 : ff_sine_128;
    const float *lwindow_prev = ics->use_kb_window[1] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float *swindow_prev = ics->use_kb_window[1] ? ff_aac_kbd_short_128 : ff_sine_128;

    if (ics->window_sequence[0] != LONG_STOP_SEQUENCE) {
        ac->fdsp.vector_fmul(in, in, lwindow_prev, 1024);
    } else {
        memset(in, 0, 448 * sizeof(float));
        ac->fdsp.vector_fmul(in + 448, in + 448, swindow_prev, 128);
    }
    if (ics->window_sequence[0] != LONG_START_SEQUENCE) {
        ac->fdsp.vector_fmul_reverse(in + 1024, in + 1024, lwindow, 1024);
    } else {
        ac->fdsp.vector_fmul_reverse(in + 1024 + 448, in + 1024 + 448, swindow, 128);
        memset(in + 1024 + 576, 0, 448 * sizeof(float));
    }
    ac->mdct_ltp.mdct_calc(&ac->mdct_ltp, out, in);
}

/**
 * Apply the long term prediction
 */
static void apply_ltp(AACContext *ac, SingleChannelElement *sce)
{
    const LongTermPrediction *ltp = &sce->ics.ltp;
    const uint16_t *offsets = sce->ics.swb_offset;
    int i, sfb;

    if (sce->ics.window_sequence[0] != EIGHT_SHORT_SEQUENCE) {
        float *predTime = sce->ret;
        float *predFreq = ac->buf_mdct;
        int16_t num_samples = 2048;

        if (ltp->lag < 1024)
            num_samples = ltp->lag + 1024;
        for (i = 0; i < num_samples; i++)
            predTime[i] = sce->ltp_state[i + 2048 - ltp->lag] * ltp->coef;
        memset(&predTime[i], 0, (2048 - i) * sizeof(float));

        ac->windowing_and_mdct_ltp(ac, predFreq, predTime, &sce->ics);

        if (sce->tns.present)
            ac->apply_tns(predFreq, &sce->tns, &sce->ics, 0);

        for (sfb = 0; sfb < FFMIN(sce->ics.max_sfb, MAX_LTP_LONG_SFB); sfb++)
            if (ltp->used[sfb])
                for (i = offsets[sfb]; i < offsets[sfb + 1]; i++)
                    sce->coeffs[i] += predFreq[i];
    }
}

/**
 * Update the LTP buffer for next frame
 */
static void update_ltp(AACContext *ac, SingleChannelElement *sce)
{
    IndividualChannelStream *ics = &sce->ics;
    float *saved     = sce->saved;
    float *saved_ltp = sce->coeffs;
    const float *lwindow = ics->use_kb_window[0] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float *swindow = ics->use_kb_window[0] ? ff_aac_kbd_short_128 : ff_sine_128;
    int i;

    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        memcpy(saved_ltp,       saved, 512 * sizeof(float));
        memset(saved_ltp + 576, 0,     448 * sizeof(float));
        ac->fdsp.vector_fmul_reverse(saved_ltp + 448, ac->buf_mdct + 960,     &swindow[64],      64);
        for (i = 0; i < 64; i++)
            saved_ltp[i + 512] = ac->buf_mdct[1023 - i] * swindow[63 - i];
    } else if (ics->window_sequence[0] == LONG_START_SEQUENCE) {
        memcpy(saved_ltp,       ac->buf_mdct + 512, 448 * sizeof(float));
        memset(saved_ltp + 576, 0,                  448 * sizeof(float));
        ac->fdsp.vector_fmul_reverse(saved_ltp + 448, ac->buf_mdct + 960,     &swindow[64],      64);
        for (i = 0; i < 64; i++)
            saved_ltp[i + 512] = ac->buf_mdct[1023 - i] * swindow[63 - i];
    } else { // LONG_STOP or ONLY_LONG
        ac->fdsp.vector_fmul_reverse(saved_ltp,       ac->buf_mdct + 512,     &lwindow[512],     512);
        for (i = 0; i < 512; i++)
            saved_ltp[i + 512] = ac->buf_mdct[1023 - i] * lwindow[511 - i];
    }

    memcpy(sce->ltp_state,      sce->ltp_state+1024, 1024 * sizeof(*sce->ltp_state));
    memcpy(sce->ltp_state+1024, sce->ret,            1024 * sizeof(*sce->ltp_state));
    memcpy(sce->ltp_state+2048, saved_ltp,           1024 * sizeof(*sce->ltp_state));
}

/**
 * Conduct IMDCT and windowing.
 */
static void imdct_and_windowing(AACContext *ac, SingleChannelElement *sce)
{
    IndividualChannelStream *ics = &sce->ics;
    float *in    = sce->coeffs;
    float *out   = sce->ret;
    float *saved = sce->saved;
    const float *swindow      = ics->use_kb_window[0] ? ff_aac_kbd_short_128 : ff_sine_128;
    const float *lwindow_prev = ics->use_kb_window[1] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float *swindow_prev = ics->use_kb_window[1] ? ff_aac_kbd_short_128 : ff_sine_128;
    float *buf  = ac->buf_mdct;
    float *temp = ac->temp;
    int i;

    // imdct
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        for (i = 0; i < 1024; i += 128)
            ac->mdct_small.imdct_half(&ac->mdct_small, buf + i, in + i);
    } else
        ac->mdct.imdct_half(&ac->mdct, buf, in);

    /* window overlapping
     * NOTE: To simplify the overlapping code, all 'meaningless' short to long
     * and long to short transitions are considered to be short to short
     * transitions. This leaves just two cases (long to long and short to short)
     * with a little special sauce for EIGHT_SHORT_SEQUENCE.
     */
    if ((ics->window_sequence[1] == ONLY_LONG_SEQUENCE || ics->window_sequence[1] == LONG_STOP_SEQUENCE) &&
            (ics->window_sequence[0] == ONLY_LONG_SEQUENCE || ics->window_sequence[0] == LONG_START_SEQUENCE)) {
        ac->fdsp.vector_fmul_window(    out,               saved,            buf,         lwindow_prev, 512);
    } else {
        memcpy(                         out,               saved,            448 * sizeof(float));

        if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
            ac->fdsp.vector_fmul_window(out + 448 + 0*128, saved + 448,      buf + 0*128, swindow_prev, 64);
            ac->fdsp.vector_fmul_window(out + 448 + 1*128, buf + 0*128 + 64, buf + 1*128, swindow,      64);
            ac->fdsp.vector_fmul_window(out + 448 + 2*128, buf + 1*128 + 64, buf + 2*128, swindow,      64);
            ac->fdsp.vector_fmul_window(out + 448 + 3*128, buf + 2*128 + 64, buf + 3*128, swindow,      64);
            ac->fdsp.vector_fmul_window(temp,              buf + 3*128 + 64, buf + 4*128, swindow,      64);
            memcpy(                     out + 448 + 4*128, temp, 64 * sizeof(float));
        } else {
            ac->fdsp.vector_fmul_window(out + 448,         saved + 448,      buf,         swindow_prev, 64);
            memcpy(                     out + 576,         buf + 64,         448 * sizeof(float));
        }
    }

    // buffer update
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        memcpy(                     saved,       temp + 64,         64 * sizeof(float));
        ac->fdsp.vector_fmul_window(saved + 64,  buf + 4*128 + 64, buf + 5*128, swindow, 64);
        ac->fdsp.vector_fmul_window(saved + 192, buf + 5*128 + 64, buf + 6*128, swindow, 64);
        ac->fdsp.vector_fmul_window(saved + 320, buf + 6*128 + 64, buf + 7*128, swindow, 64);
        memcpy(                     saved + 448, buf + 7*128 + 64,  64 * sizeof(float));
    } else if (ics->window_sequence[0] == LONG_START_SEQUENCE) {
        memcpy(                     saved,       buf + 512,        448 * sizeof(float));
        memcpy(                     saved + 448, buf + 7*128 + 64,  64 * sizeof(float));
    } else { // LONG_STOP or ONLY_LONG
        memcpy(                     saved,       buf + 512,        512 * sizeof(float));
    }
}

/**
 * Apply dependent channel coupling (applied before IMDCT).
 *
 * @param   index   index into coupling gain array
 */
static void apply_dependent_coupling(AACContext *ac,
                                     SingleChannelElement *target,
                                     ChannelElement *cce, int index)
{
    IndividualChannelStream *ics = &cce->ch[0].ics;
    const uint16_t *offsets = ics->swb_offset;
    float *dest = target->coeffs;
    const float *src = cce->ch[0].coeffs;
    int g, i, group, k, idx = 0;
    if (ac->oc[1].m4ac.object_type == AOT_AAC_LTP) {
        av_log(ac->avctx, AV_LOG_ERROR,
               "Dependent coupling is not supported together with LTP\n");
        return;
    }
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb; i++, idx++) {
            if (cce->ch[0].band_type[idx] != ZERO_BT) {
                const float gain = cce->coup.gain[index][idx];
                for (group = 0; group < ics->group_len[g]; group++) {
                    for (k = offsets[i]; k < offsets[i + 1]; k++) {
                        // XXX dsputil-ize
                        dest[group * 128 + k] += gain * src[group * 128 + k];
                    }
                }
            }
        }
        dest += ics->group_len[g] * 128;
        src  += ics->group_len[g] * 128;
    }
}

/**
 * Apply independent channel coupling (applied after IMDCT).
 *
 * @param   index   index into coupling gain array
 */
static void apply_independent_coupling(AACContext *ac,
                                       SingleChannelElement *target,
                                       ChannelElement *cce, int index)
{
    int i;
    const float gain = cce->coup.gain[index][0];
    const float *src = cce->ch[0].ret;
    float *dest = target->ret;
    const int len = 1024 << (ac->oc[1].m4ac.sbr == 1);

    for (i = 0; i < len; i++)
        dest[i] += gain * src[i];
}

/**
 * channel coupling transformation interface
 *
 * @param   apply_coupling_method   pointer to (in)dependent coupling function
 */
static void apply_channel_coupling(AACContext *ac, ChannelElement *cc,
                                   enum RawDataBlockType type, int elem_id,
                                   enum CouplingPoint coupling_point,
                                   void (*apply_coupling_method)(AACContext *ac, SingleChannelElement *target, ChannelElement *cce, int index))
{
    int i, c;

    for (i = 0; i < MAX_ELEM_ID; i++) {
        ChannelElement *cce = ac->che[TYPE_CCE][i];
        int index = 0;

        if (cce && cce->coup.coupling_point == coupling_point) {
            ChannelCoupling *coup = &cce->coup;

            for (c = 0; c <= coup->num_coupled; c++) {
                if (coup->type[c] == type && coup->id_select[c] == elem_id) {
                    if (coup->ch_select[c] != 1) {
                        apply_coupling_method(ac, &cc->ch[0], cce, index);
                        if (coup->ch_select[c] != 0)
                            index++;
                    }
                    if (coup->ch_select[c] != 2)
                        apply_coupling_method(ac, &cc->ch[1], cce, index++);
                } else
                    index += 1 + (coup->ch_select[c] == 3);
            }
        }
    }
}

/**
 * Convert spectral data to float samples, applying all supported tools as appropriate.
 */
static void spectral_to_sample(AACContext *ac)
{
    int i, type;
    for (type = 3; type >= 0; type--) {
        for (i = 0; i < MAX_ELEM_ID; i++) {
            ChannelElement *che = ac->che[type][i];
            if (che) {
                if (type <= TYPE_CPE)
                    apply_channel_coupling(ac, che, type, i, BEFORE_TNS, apply_dependent_coupling);
                if (ac->oc[1].m4ac.object_type == AOT_AAC_LTP) {
                    if (che->ch[0].ics.predictor_present) {
                        if (che->ch[0].ics.ltp.present)
                            ac->apply_ltp(ac, &che->ch[0]);
                        if (che->ch[1].ics.ltp.present && type == TYPE_CPE)
                            ac->apply_ltp(ac, &che->ch[1]);
                    }
                }
                if (che->ch[0].tns.present)
                    ac->apply_tns(che->ch[0].coeffs, &che->ch[0].tns, &che->ch[0].ics, 1);
                if (che->ch[1].tns.present)
                    ac->apply_tns(che->ch[1].coeffs, &che->ch[1].tns, &che->ch[1].ics, 1);
                if (type <= TYPE_CPE)
                    apply_channel_coupling(ac, che, type, i, BETWEEN_TNS_AND_IMDCT, apply_dependent_coupling);
                if (type != TYPE_CCE || che->coup.coupling_point == AFTER_IMDCT) {
                    ac->imdct_and_windowing(ac, &che->ch[0]);
                    if (ac->oc[1].m4ac.object_type == AOT_AAC_LTP)
                        ac->update_ltp(ac, &che->ch[0]);
                    if (type == TYPE_CPE) {
                        ac->imdct_and_windowing(ac, &che->ch[1]);
                        if (ac->oc[1].m4ac.object_type == AOT_AAC_LTP)
                            ac->update_ltp(ac, &che->ch[1]);
                    }
                    if (ac->oc[1].m4ac.sbr > 0) {
                        ff_sbr_apply(ac, &che->sbr, type, che->ch[0].ret, che->ch[1].ret);
                    }
                }
                if (type <= TYPE_CCE)
                    apply_channel_coupling(ac, che, type, i, AFTER_IMDCT, apply_independent_coupling);
            }
        }
    }
}

static int parse_adts_frame_header(AACContext *ac, GetBitContext *gb)
{
    int size;
    AACADTSHeaderInfo hdr_info;
    uint8_t layout_map[MAX_ELEM_ID*4][3];
    int layout_map_tags;

    size = avpriv_aac_parse_header(gb, &hdr_info);
    if (size > 0) {
        if (!ac->warned_num_aac_frames && hdr_info.num_aac_frames != 1) {
            // This is 2 for "VLB " audio in NSV files.
            // See samples/nsv/vlb_audio.
            avpriv_report_missing_feature(ac->avctx,
                                          "More than one AAC RDB per ADTS frame");
            ac->warned_num_aac_frames = 1;
        }
        push_output_configuration(ac);
        if (hdr_info.chan_config) {
            ac->oc[1].m4ac.chan_config = hdr_info.chan_config;
            if (set_default_channel_config(ac->avctx, layout_map,
                    &layout_map_tags, hdr_info.chan_config))
                return -7;
            if (output_configure(ac, layout_map, layout_map_tags,
                                 FFMAX(ac->oc[1].status, OC_TRIAL_FRAME), 0))
                return -7;
        } else {
            ac->oc[1].m4ac.chan_config = 0;
            /**
             * dual mono frames in Japanese DTV can have chan_config 0
             * WITHOUT specifying PCE.
             *  thus, set dual mono as default.
             */
            if (ac->dmono_mode && ac->oc[0].status == OC_NONE) {
                layout_map_tags = 2;
                layout_map[0][0] = layout_map[1][0] = TYPE_SCE;
                layout_map[0][2] = layout_map[1][2] = AAC_CHANNEL_FRONT;
                layout_map[0][1] = 0;
                layout_map[1][1] = 1;
                if (output_configure(ac, layout_map, layout_map_tags,
                                     OC_TRIAL_FRAME, 0))
                    return -7;
            }
        }
        ac->oc[1].m4ac.sample_rate     = hdr_info.sample_rate;
        ac->oc[1].m4ac.sampling_index  = hdr_info.sampling_index;
        ac->oc[1].m4ac.object_type     = hdr_info.object_type;
        if (ac->oc[0].status != OC_LOCKED ||
            ac->oc[0].m4ac.chan_config != hdr_info.chan_config ||
            ac->oc[0].m4ac.sample_rate != hdr_info.sample_rate) {
            ac->oc[1].m4ac.sbr = -1;
            ac->oc[1].m4ac.ps  = -1;
        }
        if (!hdr_info.crc_absent)
            skip_bits(gb, 16);
    }
    return size;
}

static int aac_decode_frame_int(AVCodecContext *avctx, void *data,
                                int *got_frame_ptr, GetBitContext *gb, AVPacket *avpkt)
{
    AACContext *ac = avctx->priv_data;
    ChannelElement *che = NULL, *che_prev = NULL;
    enum RawDataBlockType elem_type, elem_type_prev = TYPE_END;
    int err, elem_id;
    int samples = 0, multiplier, audio_found = 0, pce_found = 0;
    int is_dmono, sce_count = 0;

    ac->frame = data;

    if (show_bits(gb, 12) == 0xfff) {
        if (parse_adts_frame_header(ac, gb) < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error decoding AAC frame header.\n");
            err = -1;
            goto fail;
        }
        if (ac->oc[1].m4ac.sampling_index > 12) {
            av_log(ac->avctx, AV_LOG_ERROR, "invalid sampling rate index %d\n", ac->oc[1].m4ac.sampling_index);
            err = -1;
            goto fail;
        }
    }

    if (frame_configure_elements(avctx) < 0) {
        err = -1;
        goto fail;
    }

    ac->tags_mapped = 0;
    // parse
    while ((elem_type = get_bits(gb, 3)) != TYPE_END) {
        elem_id = get_bits(gb, 4);

        if (elem_type < TYPE_DSE) {
            if (!(che=get_che(ac, elem_type, elem_id))) {
                av_log(ac->avctx, AV_LOG_ERROR, "channel element %d.%d is not allocated\n",
                       elem_type, elem_id);
                err = -1;
                goto fail;
            }
            samples = 1024;
        }

        switch (elem_type) {

        case TYPE_SCE:
            err = decode_ics(ac, &che->ch[0], gb, 0, 0);
            audio_found = 1;
            sce_count++;
            break;

        case TYPE_CPE:
            err = decode_cpe(ac, gb, che);
            audio_found = 1;
            break;

        case TYPE_CCE:
            err = decode_cce(ac, gb, che);
            break;

        case TYPE_LFE:
            err = decode_ics(ac, &che->ch[0], gb, 0, 0);
            audio_found = 1;
            break;

        case TYPE_DSE:
            err = skip_data_stream_element(ac, gb);
            break;

        case TYPE_PCE: {
            uint8_t layout_map[MAX_ELEM_ID*4][3];
            int tags;
            push_output_configuration(ac);
            tags = decode_pce(avctx, &ac->oc[1].m4ac, layout_map, gb);
            if (tags < 0) {
                err = tags;
                break;
            }
            if (pce_found) {
                av_log(avctx, AV_LOG_ERROR,
                       "Not evaluating a further program_config_element as this construct is dubious at best.\n");
            } else {
                err = output_configure(ac, layout_map, tags, OC_TRIAL_PCE, 1);
                if (!err)
                    ac->oc[1].m4ac.chan_config = 0;
                pce_found = 1;
            }
            break;
        }

        case TYPE_FIL:
            if (elem_id == 15)
                elem_id += get_bits(gb, 8) - 1;
            if (get_bits_left(gb) < 8 * elem_id) {
                    av_log(avctx, AV_LOG_ERROR, "TYPE_FIL: "overread_err);
                    err = -1;
                    goto fail;
            }
            while (elem_id > 0)
                elem_id -= decode_extension_payload(ac, gb, elem_id, che_prev, elem_type_prev);
            err = 0; /* FIXME */
            break;

        default:
            err = -1; /* should not happen, but keeps compiler happy */
            break;
        }

        che_prev       = che;
        elem_type_prev = elem_type;

        if (err)
            goto fail;

        if (get_bits_left(gb) < 3) {
            av_log(avctx, AV_LOG_ERROR, overread_err);
            err = -1;
            goto fail;
        }
    }

    spectral_to_sample(ac);

    multiplier = (ac->oc[1].m4ac.sbr == 1) ? ac->oc[1].m4ac.ext_sample_rate > ac->oc[1].m4ac.sample_rate : 0;
    samples <<= multiplier;
    /* for dual-mono audio (SCE + SCE) */
    is_dmono = ac->dmono_mode && sce_count == 2 &&
               ac->oc[1].channel_layout == (AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT);

    if (samples)
        ac->frame->nb_samples = samples;
    else
        av_frame_unref(ac->frame);
    *got_frame_ptr = !!samples;

    if (is_dmono) {
        if (ac->dmono_mode == 1)
            ((AVFrame *)data)->data[1] =((AVFrame *)data)->data[0];
        else if (ac->dmono_mode == 2)
            ((AVFrame *)data)->data[0] =((AVFrame *)data)->data[1];
    }

    if (ac->oc[1].status && audio_found) {
        avctx->sample_rate = ac->oc[1].m4ac.sample_rate << multiplier;
        avctx->frame_size = samples;
        ac->oc[1].status = OC_LOCKED;
    }

    if (multiplier) {
        int side_size;
        const uint8_t *side = av_packet_get_side_data(avpkt, AV_PKT_DATA_SKIP_SAMPLES, &side_size);
        if (side && side_size>=4)
            AV_WL32(side, 2*AV_RL32(side));
    }
    return 0;
fail:
    pop_output_configuration(ac);
    return err;
}

static int aac_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    AACContext *ac = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    GetBitContext gb;
    int buf_consumed;
    int buf_offset;
    int err;
    int new_extradata_size;
    const uint8_t *new_extradata = av_packet_get_side_data(avpkt,
                                       AV_PKT_DATA_NEW_EXTRADATA,
                                       &new_extradata_size);
    int jp_dualmono_size;
    const uint8_t *jp_dualmono   = av_packet_get_side_data(avpkt,
                                       AV_PKT_DATA_JP_DUALMONO,
                                       &jp_dualmono_size);

    if (new_extradata && 0) {
        av_free(avctx->extradata);
        avctx->extradata = av_mallocz(new_extradata_size +
                                      FF_INPUT_BUFFER_PADDING_SIZE);
        if (!avctx->extradata)
            return AVERROR(ENOMEM);
        avctx->extradata_size = new_extradata_size;
        memcpy(avctx->extradata, new_extradata, new_extradata_size);
        push_output_configuration(ac);
        if (decode_audio_specific_config(ac, ac->avctx, &ac->oc[1].m4ac,
                                         avctx->extradata,
                                         avctx->extradata_size*8, 1) < 0) {
            pop_output_configuration(ac);
            return AVERROR_INVALIDDATA;
        }
    }

    ac->dmono_mode = 0;
    if (jp_dualmono && jp_dualmono_size > 0)
        ac->dmono_mode =  1 + *jp_dualmono;
    if (ac->force_dmono_mode >= 0)
        ac->dmono_mode = ac->force_dmono_mode;

    if (INT_MAX / 8 <= buf_size)
        return AVERROR_INVALIDDATA;

    init_get_bits(&gb, buf, buf_size * 8);

    if ((err = aac_decode_frame_int(avctx, data, got_frame_ptr, &gb, avpkt)) < 0)
        return err;

    buf_consumed = (get_bits_count(&gb) + 7) >> 3;
    for (buf_offset = buf_consumed; buf_offset < buf_size; buf_offset++)
        if (buf[buf_offset])
            break;

    return buf_size > buf_offset ? buf_consumed : buf_size;
}

static av_cold int aac_decode_close(AVCodecContext *avctx)
{
    AACContext *ac = avctx->priv_data;
    int i, type;

    for (i = 0; i < MAX_ELEM_ID; i++) {
        for (type = 0; type < 4; type++) {
            if (ac->che[type][i])
                ff_aac_sbr_ctx_close(&ac->che[type][i]->sbr);
            av_freep(&ac->che[type][i]);
        }
    }

    ff_mdct_end(&ac->mdct);
    ff_mdct_end(&ac->mdct_small);
    ff_mdct_end(&ac->mdct_ltp);
    return 0;
}


#define LOAS_SYNC_WORD   0x2b7       ///< 11 bits LOAS sync word

struct LATMContext {
    AACContext aac_ctx;     ///< containing AACContext
    int initialized;        ///< initialized after a valid extradata was seen

    // parser data
    int audio_mux_version_A; ///< LATM syntax version
    int frame_length_type;   ///< 0/1 variable/fixed frame length
    int frame_length;        ///< frame length for fixed frame length
};

static inline uint32_t latm_get_value(GetBitContext *b)
{
    int length = get_bits(b, 2);

    return get_bits_long(b, (length+1)*8);
}

static int latm_decode_audio_specific_config(struct LATMContext *latmctx,
                                             GetBitContext *gb, int asclen)
{
    AACContext *ac        = &latmctx->aac_ctx;
    AVCodecContext *avctx = ac->avctx;
    MPEG4AudioConfig m4ac = { 0 };
    int config_start_bit  = get_bits_count(gb);
    int sync_extension    = 0;
    int bits_consumed, esize;

    if (asclen) {
        sync_extension = 1;
        asclen         = FFMIN(asclen, get_bits_left(gb));
    } else
        asclen         = get_bits_left(gb);

    if (config_start_bit % 8) {
        avpriv_request_sample(latmctx->aac_ctx.avctx,
                              "Non-byte-aligned audio-specific config");
        return AVERROR_PATCHWELCOME;
    }
    if (asclen <= 0)
        return AVERROR_INVALIDDATA;
    bits_consumed = decode_audio_specific_config(NULL, avctx, &m4ac,
                                         gb->buffer + (config_start_bit / 8),
                                         asclen, sync_extension);

    if (bits_consumed < 0)
        return AVERROR_INVALIDDATA;

    if (!latmctx->initialized ||
        ac->oc[1].m4ac.sample_rate != m4ac.sample_rate ||
        ac->oc[1].m4ac.chan_config != m4ac.chan_config) {

        if(latmctx->initialized) {
            av_log(avctx, AV_LOG_INFO, "audio config changed\n");
        } else {
            av_log(avctx, AV_LOG_DEBUG, "initializing latmctx\n");
        }
        latmctx->initialized = 0;

        esize = (bits_consumed+7) / 8;

        if (avctx->extradata_size < esize) {
            av_free(avctx->extradata);
            avctx->extradata = av_malloc(esize + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!avctx->extradata)
                return AVERROR(ENOMEM);
        }

        avctx->extradata_size = esize;
        memcpy(avctx->extradata, gb->buffer + (config_start_bit/8), esize);
        memset(avctx->extradata+esize, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    }
    skip_bits_long(gb, bits_consumed);

    return bits_consumed;
}

static int read_stream_mux_config(struct LATMContext *latmctx,
                                  GetBitContext *gb)
{
    int ret, audio_mux_version = get_bits(gb, 1);

    latmctx->audio_mux_version_A = 0;
    if (audio_mux_version)
        latmctx->audio_mux_version_A = get_bits(gb, 1);

    if (!latmctx->audio_mux_version_A) {

        if (audio_mux_version)
            latm_get_value(gb);                 // taraFullness

        skip_bits(gb, 1);                       // allStreamSameTimeFraming
        skip_bits(gb, 6);                       // numSubFrames
        // numPrograms
        if (get_bits(gb, 4)) {                  // numPrograms
            avpriv_request_sample(latmctx->aac_ctx.avctx, "Multiple programs");
            return AVERROR_PATCHWELCOME;
        }

        // for each program (which there is only one in DVB)

        // for each layer (which there is only one in DVB)
        if (get_bits(gb, 3)) {                   // numLayer
            avpriv_request_sample(latmctx->aac_ctx.avctx, "Multiple layers");
            return AVERROR_PATCHWELCOME;
        }

        // for all but first stream: use_same_config = get_bits(gb, 1);
        if (!audio_mux_version) {
            if ((ret = latm_decode_audio_specific_config(latmctx, gb, 0)) < 0)
                return ret;
        } else {
            int ascLen = latm_get_value(gb);
            if ((ret = latm_decode_audio_specific_config(latmctx, gb, ascLen)) < 0)
                return ret;
            ascLen -= ret;
            skip_bits_long(gb, ascLen);
        }

        latmctx->frame_length_type = get_bits(gb, 3);
        switch (latmctx->frame_length_type) {
        case 0:
            skip_bits(gb, 8);       // latmBufferFullness
            break;
        case 1:
            latmctx->frame_length = get_bits(gb, 9);
            break;
        case 3:
        case 4:
        case 5:
            skip_bits(gb, 6);       // CELP frame length table index
            break;
        case 6:
        case 7:
            skip_bits(gb, 1);       // HVXC frame length table index
            break;
        }

        if (get_bits(gb, 1)) {                  // other data
            if (audio_mux_version) {
                latm_get_value(gb);             // other_data_bits
            } else {
                int esc;
                do {
                    esc = get_bits(gb, 1);
                    skip_bits(gb, 8);
                } while (esc);
            }
        }

        if (get_bits(gb, 1))                     // crc present
            skip_bits(gb, 8);                    // config_crc
    }

    return 0;
}

static int read_payload_length_info(struct LATMContext *ctx, GetBitContext *gb)
{
    uint8_t tmp;

    if (ctx->frame_length_type == 0) {
        int mux_slot_length = 0;
        do {
            tmp = get_bits(gb, 8);
            mux_slot_length += tmp;
        } while (tmp == 255);
        return mux_slot_length;
    } else if (ctx->frame_length_type == 1) {
        return ctx->frame_length;
    } else if (ctx->frame_length_type == 3 ||
               ctx->frame_length_type == 5 ||
               ctx->frame_length_type == 7) {
        skip_bits(gb, 2);          // mux_slot_length_coded
    }
    return 0;
}

static int read_audio_mux_element(struct LATMContext *latmctx,
                                  GetBitContext *gb)
{
    int err;
    uint8_t use_same_mux = get_bits(gb, 1);
    if (!use_same_mux) {
        if ((err = read_stream_mux_config(latmctx, gb)) < 0)
            return err;
    } else if (!latmctx->aac_ctx.avctx->extradata) {
        av_log(latmctx->aac_ctx.avctx, AV_LOG_DEBUG,
               "no decoder config found\n");
        return AVERROR(EAGAIN);
    }
    if (latmctx->audio_mux_version_A == 0) {
        int mux_slot_length_bytes = read_payload_length_info(latmctx, gb);
        if (mux_slot_length_bytes * 8 > get_bits_left(gb)) {
            av_log(latmctx->aac_ctx.avctx, AV_LOG_ERROR, "incomplete frame\n");
            return AVERROR_INVALIDDATA;
        } else if (mux_slot_length_bytes * 8 + 256 < get_bits_left(gb)) {
            av_log(latmctx->aac_ctx.avctx, AV_LOG_ERROR,
                   "frame length mismatch %d << %d\n",
                   mux_slot_length_bytes * 8, get_bits_left(gb));
            return AVERROR_INVALIDDATA;
        }
    }
    return 0;
}


static int latm_decode_frame(AVCodecContext *avctx, void *out,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    struct LATMContext *latmctx = avctx->priv_data;
    int                 muxlength, err;
    GetBitContext       gb;

    if ((err = init_get_bits8(&gb, avpkt->data, avpkt->size)) < 0)
        return err;

    // check for LOAS sync word
    if (get_bits(&gb, 11) != LOAS_SYNC_WORD)
        return AVERROR_INVALIDDATA;

    muxlength = get_bits(&gb, 13) + 3;
    // not enough data, the parser should have sorted this out
    if (muxlength > avpkt->size)
        return AVERROR_INVALIDDATA;

    if ((err = read_audio_mux_element(latmctx, &gb)) < 0)
        return err;

    if (!latmctx->initialized) {
        if (!avctx->extradata) {
            *got_frame_ptr = 0;
            return avpkt->size;
        } else {
            push_output_configuration(&latmctx->aac_ctx);
            if ((err = decode_audio_specific_config(
                    &latmctx->aac_ctx, avctx, &latmctx->aac_ctx.oc[1].m4ac,
                    avctx->extradata, avctx->extradata_size*8, 1)) < 0) {
                pop_output_configuration(&latmctx->aac_ctx);
                return err;
            }
            latmctx->initialized = 1;
        }
    }

    if (show_bits(&gb, 12) == 0xfff) {
        av_log(latmctx->aac_ctx.avctx, AV_LOG_ERROR,
               "ADTS header detected, probably as result of configuration "
               "misparsing\n");
        return AVERROR_INVALIDDATA;
    }

    if ((err = aac_decode_frame_int(avctx, out, got_frame_ptr, &gb, avpkt)) < 0)
        return err;

    return muxlength;
}

static av_cold int latm_decode_init(AVCodecContext *avctx)
{
    struct LATMContext *latmctx = avctx->priv_data;
    int ret = aac_decode_init(avctx);

    if (avctx->extradata_size > 0)
        latmctx->initialized = !ret;

    return ret;
}

static void aacdec_init(AACContext *c)
{
    c->imdct_and_windowing                      = imdct_and_windowing;
    c->apply_ltp                                = apply_ltp;
    c->apply_tns                                = apply_tns;
    c->windowing_and_mdct_ltp                   = windowing_and_mdct_ltp;
    c->update_ltp                               = update_ltp;

    if(ARCH_MIPS)
        ff_aacdec_init_mips(c);
}
/**
 * AVOptions for Japanese DTV specific extensions (ADTS only)
 */
#define AACDEC_FLAGS AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM
static const AVOption options[] = {
    {"dual_mono_mode", "Select the channel to decode for dual mono",
     offsetof(AACContext, force_dmono_mode), AV_OPT_TYPE_INT, {.i64=-1}, -1, 2,
     AACDEC_FLAGS, "dual_mono_mode"},

    {"auto", "autoselection",            0, AV_OPT_TYPE_CONST, {.i64=-1}, INT_MIN, INT_MAX, AACDEC_FLAGS, "dual_mono_mode"},
    {"main", "Select Main/Left channel", 0, AV_OPT_TYPE_CONST, {.i64= 1}, INT_MIN, INT_MAX, AACDEC_FLAGS, "dual_mono_mode"},
    {"sub" , "Select Sub/Right channel", 0, AV_OPT_TYPE_CONST, {.i64= 2}, INT_MIN, INT_MAX, AACDEC_FLAGS, "dual_mono_mode"},
    {"both", "Select both channels",     0, AV_OPT_TYPE_CONST, {.i64= 0}, INT_MIN, INT_MAX, AACDEC_FLAGS, "dual_mono_mode"},

    {NULL},
};

static const AVClass aac_decoder_class = {
    .class_name = "AAC decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_aac_decoder = {
    .name            = "aac",
    .type            = AVMEDIA_TYPE_AUDIO,
    .id              = AV_CODEC_ID_AAC,
    .priv_data_size  = sizeof(AACContext),
    .init            = aac_decode_init,
    .close           = aac_decode_close,
    .decode          = aac_decode_frame,
    .long_name       = NULL_IF_CONFIG_SMALL("AAC (Advanced Audio Coding)"),
    .sample_fmts     = (const enum AVSampleFormat[]) {
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE
    },
    .capabilities    = CODEC_CAP_CHANNEL_CONF | CODEC_CAP_DR1,
    .channel_layouts = aac_channel_layout,
    .flush = flush,
    .priv_class      = &aac_decoder_class,
};

/*
    Note: This decoder filter is intended to decode LATM streams transferred
    in MPEG transport streams which only contain one program.
    To do a more complex LATM demuxing a separate LATM demuxer should be used.
*/
AVCodec ff_aac_latm_decoder = {
    .name            = "aac_latm",
    .type            = AVMEDIA_TYPE_AUDIO,
    .id              = AV_CODEC_ID_AAC_LATM,
    .priv_data_size  = sizeof(struct LATMContext),
    .init            = latm_decode_init,
    .close           = aac_decode_close,
    .decode          = latm_decode_frame,
    .long_name       = NULL_IF_CONFIG_SMALL("AAC LATM (Advanced Audio Coding LATM syntax)"),
    .sample_fmts     = (const enum AVSampleFormat[]) {
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE
    },
    .capabilities    = CODEC_CAP_CHANNEL_CONF | CODEC_CAP_DR1,
    .channel_layouts = aac_channel_layout,
    .flush = flush,
};
