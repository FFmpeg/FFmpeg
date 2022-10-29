/*
 * AAC decoder
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
 * Copyright (c) 2008-2013 Alex Converse <alex.converse@gmail.com>
 *
 * AAC LATM decoder
 * Copyright (c) 2008-2010 Paul Kendall <paul@kcbbs.gen.nz>
 * Copyright (c) 2010      Janne Grunau <janne-libav@jannau.net>
 *
 * AAC decoder fixed-point implementation
 * Copyright (c) 2013
 *      MIPS Technologies, Inc., California.
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
 *
 * AAC decoder fixed-point implementation
 * @author Stanislav Ocovaj ( stanislav.ocovaj imgtec com )
 * @author Nedeljko Babic ( nedeljko.babic imgtec com )
 */

/*
 * supported tools
 *
 * Support?                     Name
 * N (code in SoC repo)         gain control
 * Y                            block switching
 * Y                            window shapes - standard
 * N                            window shapes - Low Delay
 * Y                            filterbank - standard
 * N (code in SoC repo)         filterbank - Scalable Sample Rate
 * Y                            Temporal Noise Shaping
 * Y                            Long Term Prediction
 * Y                            intensity stereo
 * Y                            channel coupling
 * Y                            frequency domain prediction
 * Y                            Perceptual Noise Substitution
 * Y                            Mid/Side stereo
 * N                            Scalable Inverse AAC Quantization
 * N                            Frequency Selective Switch
 * N                            upsampling filter
 * Y                            quantization & coding - AAC
 * N                            quantization & coding - TwinVQ
 * N                            quantization & coding - BSAC
 * N                            AAC Error Resilience tools
 * N                            Error Resilience payload syntax
 * N                            Error Protection tool
 * N                            CELP
 * N                            Silence Compression
 * N                            HVXC
 * N                            HVXC 4kbits/s VR
 * N                            Structured Audio tools
 * N                            Structured Audio Sample Bank Format
 * N                            MIDI
 * N                            Harmonic and Individual Lines plus Noise
 * N                            Text-To-Speech Interface
 * Y                            Spectral Band Replication
 * Y (not in this code)         Layer-1
 * Y (not in this code)         Layer-2
 * Y (not in this code)         Layer-3
 * N                            SinuSoidal Coding (Transient, Sinusoid, Noise)
 * Y                            Parametric Stereo
 * N                            Direct Stream Transfer
 * Y  (not in fixed point code) Enhanced AAC Low Delay (ER AAC ELD)
 *
 * Note: - HE AAC v1 comprises LC AAC with Spectral Band Replication.
 *       - HE AAC v2 comprises LC AAC with Spectral Band Replication and
           Parametric Stereo.
 */

#include "libavutil/channel_layout.h"
#include "libavutil/thread.h"
#include "decode.h"
#include "internal.h"

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
            int ret;
            if (!(ac->che[type][id] = av_mallocz(sizeof(ChannelElement))))
                return AVERROR(ENOMEM);
            ret = AAC_RENAME(ff_aac_sbr_ctx_init)(ac, &ac->che[type][id]->sbr, type);
            if (ret < 0)
                return ret;
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
            AAC_RENAME(ff_aac_sbr_ctx_close)(&ac->che[type][id]->sbr);
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
    if (!avctx->ch_layout.nb_channels)
        return 1;

    ac->frame->nb_samples = 2048;
    if ((ret = ff_get_buffer(avctx, ac->frame, 0)) < 0)
        return ret;

    /* map output channel pointers to AVFrame data */
    for (ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
        if (ac->output_element[ch])
            ac->output_element[ch]->ret = (INTFLOAT *)ac->frame->extended_data[ch];
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
                       uint64_t right, int pos, uint64_t *layout)
{
    if (layout_map[offset][0] == TYPE_CPE) {
        e2c_vec[offset] = (struct elem_to_channel) {
            .av_position  = left | right,
            .syn_ele      = TYPE_CPE,
            .elem_id      = layout_map[offset][1],
            .aac_position = pos
        };
        if (e2c_vec[offset].av_position != UINT64_MAX)
            *layout |= e2c_vec[offset].av_position;

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
        if (left != UINT64_MAX)
            *layout |= left;

        if (right != UINT64_MAX)
            *layout |= right;

        return 2;
    }
}

static int count_paired_channels(uint8_t (*layout_map)[3], int tags, int pos,
                                 int current)
{
    int num_pos_channels = 0;
    int first_cpe        = 0;
    int sce_parity       = 0;
    int i;
    for (i = current; i < tags; i++) {
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
            sce_parity ^= (pos != AAC_CHANNEL_LFE);
        }
    }
    if (sce_parity &&
        (pos == AAC_CHANNEL_FRONT && first_cpe))
        return -1;

    return num_pos_channels;
}

static int assign_channels(struct elem_to_channel e2c_vec[MAX_ELEM_ID], uint8_t (*layout_map)[3],
                           uint64_t *layout, int tags, int layer, int pos, int *current)
{
    int i = *current, j = 0;
    int nb_channels = count_paired_channels(layout_map, tags, pos, i);

    if (nb_channels < 0 || nb_channels > 5)
        return 0;

    if (pos == AAC_CHANNEL_LFE) {
        while (nb_channels) {
            if (aac_channel_map[layer][pos - 1][j] == AV_CHAN_NONE)
                return -1;
            e2c_vec[i] = (struct elem_to_channel) {
                .av_position  = 1ULL << aac_channel_map[layer][pos - 1][j],
                .syn_ele      = layout_map[i][0],
                .elem_id      = layout_map[i][1],
                .aac_position = pos
            };
            *layout |= e2c_vec[i].av_position;
            i++;
            j++;
            nb_channels--;
        }
        *current = i;

        return 0;
    }

    while (nb_channels & 1) {
        if (aac_channel_map[layer][pos - 1][0] == AV_CHAN_NONE)
            return -1;
        if (aac_channel_map[layer][pos - 1][0] == AV_CHAN_UNUSED)
            break;
        e2c_vec[i] = (struct elem_to_channel) {
            .av_position  = 1ULL << aac_channel_map[layer][pos - 1][0],
            .syn_ele      = layout_map[i][0],
            .elem_id      = layout_map[i][1],
            .aac_position = pos
        };
        *layout |= e2c_vec[i].av_position;
        i++;
        nb_channels--;
    }

    j = (pos != AAC_CHANNEL_SIDE) && nb_channels <= 3 ? 3 : 1;
    while (nb_channels >= 2) {
        if (aac_channel_map[layer][pos - 1][j]   == AV_CHAN_NONE ||
            aac_channel_map[layer][pos - 1][j+1] == AV_CHAN_NONE)
            return -1;
        i += assign_pair(e2c_vec, layout_map, i,
                         1ULL << aac_channel_map[layer][pos - 1][j],
                         1ULL << aac_channel_map[layer][pos - 1][j+1],
                         pos, layout);
        j += 2;
        nb_channels -= 2;
    }
    while (nb_channels & 1) {
        if (aac_channel_map[layer][pos - 1][5] == AV_CHAN_NONE)
            return -1;
        e2c_vec[i] = (struct elem_to_channel) {
            .av_position  = 1ULL << aac_channel_map[layer][pos - 1][5],
            .syn_ele      = layout_map[i][0],
            .elem_id      = layout_map[i][1],
            .aac_position = pos
        };
        *layout |= e2c_vec[i].av_position;
        i++;
        nb_channels--;
    }
    if (nb_channels)
        return -1;

    *current = i;

    return 0;
}

static uint64_t sniff_channel_order(uint8_t (*layout_map)[3], int tags)
{
    int i, n, total_non_cc_elements;
    struct elem_to_channel e2c_vec[4 * MAX_ELEM_ID] = { { 0 } };
    uint64_t layout = 0;

    if (FF_ARRAY_ELEMS(e2c_vec) < tags)
        return 0;

    for (n = 0, i = 0; n < 3 && i < tags; n++) {
        int ret = assign_channels(e2c_vec, layout_map, &layout, tags, n, AAC_CHANNEL_FRONT, &i);
        if (ret < 0)
            return 0;
        ret = assign_channels(e2c_vec, layout_map, &layout, tags, n, AAC_CHANNEL_SIDE, &i);
        if (ret < 0)
            return 0;
        ret = assign_channels(e2c_vec, layout_map, &layout, tags, n, AAC_CHANNEL_BACK, &i);
        if (ret < 0)
            return 0;
        ret = assign_channels(e2c_vec, layout_map, &layout, tags, n, AAC_CHANNEL_LFE, &i);
        if (ret < 0)
            return 0;
    }

    total_non_cc_elements = n = i;

    if (layout == AV_CH_LAYOUT_22POINT2) {
        // For 22.2 reorder the result as needed
        FFSWAP(struct elem_to_channel, e2c_vec[2], e2c_vec[0]);   // FL & FR first (final), FC third
        FFSWAP(struct elem_to_channel, e2c_vec[2], e2c_vec[1]);   // FC second (final), FLc & FRc third
        FFSWAP(struct elem_to_channel, e2c_vec[6], e2c_vec[2]);   // LFE1 third (final), FLc & FRc seventh
        FFSWAP(struct elem_to_channel, e2c_vec[4], e2c_vec[3]);   // BL & BR fourth (final), SiL & SiR fifth
        FFSWAP(struct elem_to_channel, e2c_vec[6], e2c_vec[4]);   // FLc & FRc fifth (final), SiL & SiR seventh
        FFSWAP(struct elem_to_channel, e2c_vec[7], e2c_vec[6]);   // LFE2 seventh (final), SiL & SiR eight (final)
        FFSWAP(struct elem_to_channel, e2c_vec[9], e2c_vec[8]);   // TpFL & TpFR ninth (final), TFC tenth (final)
        FFSWAP(struct elem_to_channel, e2c_vec[11], e2c_vec[10]); // TC eleventh (final), TpSiL & TpSiR twelth
        FFSWAP(struct elem_to_channel, e2c_vec[12], e2c_vec[11]); // TpBL & TpBR twelth (final), TpSiL & TpSiR thirteenth (final)
    } else {
        // For everything else, utilize the AV channel position define as a
        // stable sort.
        do {
            int next_n = 0;
            for (i = 1; i < n; i++)
                if (e2c_vec[i - 1].av_position > e2c_vec[i].av_position) {
                    FFSWAP(struct elem_to_channel, e2c_vec[i - 1], e2c_vec[i]);
                    next_n = i;
                }
            n = next_n;
        } while (n > 0);

    }

    for (i = 0; i < total_non_cc_elements; i++) {
        layout_map[i][0] = e2c_vec[i].syn_ele;
        layout_map[i][1] = e2c_vec[i].elem_id;
        layout_map[i][2] = e2c_vec[i].aac_position;
    }

    return layout;
}

/**
 * Save current output configuration if and only if it has been locked.
 */
static int push_output_configuration(AACContext *ac) {
    int pushed = 0;

    if (ac->oc[1].status == OC_LOCKED || ac->oc[0].status == OC_NONE) {
        ac->oc[0] = ac->oc[1];
        pushed = 1;
    }
    ac->oc[1].status = OC_NONE;
    return pushed;
}

/**
 * Restore the previous output configuration if and only if the current
 * configuration is unlocked.
 */
static void pop_output_configuration(AACContext *ac) {
    if (ac->oc[1].status != OC_LOCKED && ac->oc[0].status != OC_NONE) {
        ac->oc[1] = ac->oc[0];
        ac->avctx->ch_layout = ac->oc[1].ch_layout;
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
    uint8_t id_map[TYPE_END][MAX_ELEM_ID] = {{ 0 }};
    uint8_t type_counts[TYPE_END] = { 0 };

    if (ac->oc[1].layout_map != layout_map) {
        memcpy(ac->oc[1].layout_map, layout_map, tags * sizeof(layout_map[0]));
        ac->oc[1].layout_map_tags = tags;
    }
    for (i = 0; i < tags; i++) {
        int type =         layout_map[i][0];
        int id =           layout_map[i][1];
        id_map[type][id] = type_counts[type]++;
        if (id_map[type][id] >= MAX_ELEM_ID) {
            avpriv_request_sample(ac->avctx, "Too large remapped id");
            return AVERROR_PATCHWELCOME;
        }
    }
    // Try to sniff a reasonable channel order, otherwise output the
    // channels in the order the PCE declared them.
#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->request_channel_layout == AV_CH_LAYOUT_NATIVE)
        ac->output_channel_order = CHANNEL_ORDER_CODED;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    if (ac->output_channel_order == CHANNEL_ORDER_DEFAULT)
        layout = sniff_channel_order(layout_map, tags);
    for (i = 0; i < tags; i++) {
        int type =     layout_map[i][0];
        int id =       layout_map[i][1];
        int iid =      id_map[type][id];
        int position = layout_map[i][2];
        // Allocate or free elements depending on if they are in the
        // current program configuration.
        ret = che_configure(ac, position, type, iid, &channels);
        if (ret < 0)
            return ret;
        ac->tag_che_map[type][id] = ac->che[type][iid];
    }
    if (ac->oc[1].m4ac.ps == 1 && channels == 2) {
        if (layout == AV_CH_FRONT_CENTER) {
            layout = AV_CH_FRONT_LEFT|AV_CH_FRONT_RIGHT;
        } else {
            layout = 0;
        }
    }

    av_channel_layout_uninit(&ac->oc[1].ch_layout);
    if (layout)
        av_channel_layout_from_mask(&ac->oc[1].ch_layout, layout);
    else {
        ac->oc[1].ch_layout.order       = AV_CHANNEL_ORDER_UNSPEC;
        ac->oc[1].ch_layout.nb_channels = channels;
    }

    av_channel_layout_copy(&avctx->ch_layout, &ac->oc[1].ch_layout);
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
static int set_default_channel_config(AACContext *ac, AVCodecContext *avctx,
                                      uint8_t (*layout_map)[3],
                                      int *tags,
                                      int channel_config)
{
    if (channel_config < 1 || (channel_config > 7 && channel_config < 11) ||
        channel_config > 14) {
        av_log(avctx, AV_LOG_ERROR,
               "invalid default channel configuration (%d)\n",
               channel_config);
        return AVERROR_INVALIDDATA;
    }
    *tags = tags_per_config[channel_config];
    memcpy(layout_map, aac_channel_layout_map[channel_config - 1],
           *tags * sizeof(*layout_map));

    /*
     * AAC specification has 7.1(wide) as a default layout for 8-channel streams.
     * However, at least Nero AAC encoder encodes 7.1 streams using the default
     * channel config 7, mapping the side channels of the original audio stream
     * to the second AAC_CHANNEL_FRONT pair in the AAC stream. Similarly, e.g. FAAD
     * decodes the second AAC_CHANNEL_FRONT pair as side channels, therefore decoding
     * the incorrect streams as if they were correct (and as the encoder intended).
     *
     * As actual intended 7.1(wide) streams are very rare, default to assuming a
     * 7.1 layout was intended.
     */
    if (channel_config == 7 && avctx->strict_std_compliance < FF_COMPLIANCE_STRICT) {
        layout_map[2][2] = AAC_CHANNEL_BACK;

        if (!ac || !ac->warned_71_wide++) {
            av_log(avctx, AV_LOG_INFO, "Assuming an incorrectly encoded 7.1 channel layout"
                   " instead of a spec-compliant 7.1(wide) layout, use -strict %d to decode"
                   " according to the specification instead.\n", FF_COMPLIANCE_STRICT);
        }
    }

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

        if (set_default_channel_config(ac, ac->avctx, layout_map,
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

        layout_map_tags = 2;
        layout_map[0][0] = layout_map[1][0] = TYPE_SCE;
        layout_map[0][2] = layout_map[1][2] = AAC_CHANNEL_FRONT;
        layout_map[0][1] = 0;
        layout_map[1][1] = 1;
        if (output_configure(ac, layout_map, layout_map_tags,
                             OC_TRIAL_FRAME, 1) < 0)
            return NULL;

        if (ac->oc[1].m4ac.sbr)
            ac->oc[1].m4ac.ps = -1;
    }
    /* For indexed channel configurations map the channels solely based
     * on position. */
    switch (ac->oc[1].m4ac.chan_config) {
    case 14:
        if (ac->tags_mapped > 2 && ((type == TYPE_CPE && elem_id < 3) ||
                                    (type == TYPE_LFE && elem_id < 1))) {
            ac->tags_mapped++;
            return ac->tag_che_map[type][elem_id] = ac->che[type][elem_id];
        }
    case 13:
        if (ac->tags_mapped > 3 && ((type == TYPE_CPE && elem_id < 8) ||
                                    (type == TYPE_SCE && elem_id < 6) ||
                                    (type == TYPE_LFE && elem_id < 2))) {
            ac->tags_mapped++;
            return ac->tag_che_map[type][elem_id] = ac->che[type][elem_id];
        }
    case 12:
    case 7:
        if (ac->tags_mapped == 3 && type == TYPE_CPE) {
            ac->tags_mapped++;
            return ac->tag_che_map[TYPE_CPE][elem_id] = ac->che[TYPE_CPE][2];
        }
    case 11:
        if (ac->tags_mapped == 3 && type == TYPE_SCE) {
            ac->tags_mapped++;
            return ac->tag_che_map[TYPE_SCE][elem_id] = ac->che[TYPE_SCE][1];
        }
    case 6:
        /* Some streams incorrectly code 5.1 audio as
         * SCE[0] CPE[0] CPE[1] SCE[1]
         * instead of
         * SCE[0] CPE[0] CPE[1] LFE[0].
         * If we seem to have encountered such a stream, transfer
         * the LFE[0] element to the SCE[1]'s mapping */
        if (ac->tags_mapped == tags_per_config[ac->oc[1].m4ac.chan_config] - 1 && (type == TYPE_LFE || type == TYPE_SCE)) {
            if (!ac->warned_remapping_once && (type != TYPE_LFE || elem_id != 0)) {
                av_log(ac->avctx, AV_LOG_WARNING,
                   "This stream seems to incorrectly report its last channel as %s[%d], mapping to LFE[0]\n",
                   type == TYPE_SCE ? "SCE" : "LFE", elem_id);
                ac->warned_remapping_once++;
            }
            ac->tags_mapped++;
            return ac->tag_che_map[type][elem_id] = ac->che[TYPE_LFE][0];
        }
    case 5:
        if (ac->tags_mapped == 2 && type == TYPE_CPE) {
            ac->tags_mapped++;
            return ac->tag_che_map[TYPE_CPE][elem_id] = ac->che[TYPE_CPE][1];
        }
    case 4:
        /* Some streams incorrectly code 4.0 audio as
         * SCE[0] CPE[0] LFE[0]
         * instead of
         * SCE[0] CPE[0] SCE[1].
         * If we seem to have encountered such a stream, transfer
         * the SCE[1] element to the LFE[0]'s mapping */
        if (ac->tags_mapped == tags_per_config[ac->oc[1].m4ac.chan_config] - 1 && (type == TYPE_LFE || type == TYPE_SCE)) {
            if (!ac->warned_remapping_once && (type != TYPE_SCE || elem_id != 1)) {
                av_log(ac->avctx, AV_LOG_WARNING,
                   "This stream seems to incorrectly report its last channel as %s[%d], mapping to SCE[1]\n",
                   type == TYPE_SCE ? "SCE" : "LFE", elem_id);
                ac->warned_remapping_once++;
            }
            ac->tags_mapped++;
            return ac->tag_che_map[type][elem_id] = ac->che[TYPE_SCE][1];
        }
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
        } else if (ac->tags_mapped == 1 && ac->oc[1].m4ac.chan_config == 2 &&
            type == TYPE_SCE) {
            ac->tags_mapped++;
            return ac->tag_che_map[TYPE_SCE][elem_id] = ac->che[TYPE_SCE][1];
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
            // AAC_CHANNEL_OFF has no channel map
            av_assert0(0);
        }
        layout_map[0][0] = syn_ele;
        layout_map[0][1] = get_bits(gb, 4);
        layout_map[0][2] = type;
        layout_map++;
    }
}

static inline void relative_align_get_bits(GetBitContext *gb,
                                           int reference_position) {
    int n = (reference_position - get_bits_count(gb) & 7);
    if (n)
        skip_bits(gb, n);
}

/**
 * Decode program configuration element; reference: table 4.2.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_pce(AVCodecContext *avctx, MPEG4AudioConfig *m4ac,
                      uint8_t (*layout_map)[3],
                      GetBitContext *gb, int byte_align_ref)
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

    if (get_bits_left(gb) < 5 * (num_front + num_side + num_back + num_cc) + 4 *(num_lfe + num_assoc_data + num_cc)) {
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

    relative_align_get_bits(gb, byte_align_ref);

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
                                     int get_bit_alignment,
                                     MPEG4AudioConfig *m4ac,
                                     int channel_config)
{
    int extension_flag, ret, ep_config, res_flags;
    uint8_t layout_map[MAX_ELEM_ID*4][3];
    int tags = 0;

    m4ac->frame_length_short = get_bits1(gb);
    if (m4ac->frame_length_short && m4ac->sbr == 1) {
      avpriv_report_missing_feature(avctx, "SBR with 960 frame length");
      if (ac) ac->warned_960_sbr = 1;
      m4ac->sbr = 0;
      m4ac->ps = 0;
    }

    if (get_bits1(gb))       // dependsOnCoreCoder
        skip_bits(gb, 14);   // coreCoderDelay
    extension_flag = get_bits1(gb);

    if (m4ac->object_type == AOT_AAC_SCALABLE ||
        m4ac->object_type == AOT_ER_AAC_SCALABLE)
        skip_bits(gb, 3);     // layerNr

    if (channel_config == 0) {
        skip_bits(gb, 4);  // element_instance_tag
        tags = decode_pce(avctx, m4ac, layout_map, gb, get_bit_alignment);
        if (tags < 0)
            return tags;
    } else {
        if ((ret = set_default_channel_config(ac, avctx, layout_map,
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
            res_flags = get_bits(gb, 3);
            if (res_flags) {
                avpriv_report_missing_feature(avctx,
                                              "AAC data resilience (flags %x)",
                                              res_flags);
                return AVERROR_PATCHWELCOME;
            }
            break;
        }
        skip_bits1(gb);    // extensionFlag3 (TBD in version 3)
    }
    switch (m4ac->object_type) {
    case AOT_ER_AAC_LC:
    case AOT_ER_AAC_LTP:
    case AOT_ER_AAC_SCALABLE:
    case AOT_ER_AAC_LD:
        ep_config = get_bits(gb, 2);
        if (ep_config) {
            avpriv_report_missing_feature(avctx,
                                          "epConfig %d", ep_config);
            return AVERROR_PATCHWELCOME;
        }
    }
    return 0;
}

static int decode_eld_specific_config(AACContext *ac, AVCodecContext *avctx,
                                     GetBitContext *gb,
                                     MPEG4AudioConfig *m4ac,
                                     int channel_config)
{
    int ret, ep_config, res_flags;
    uint8_t layout_map[MAX_ELEM_ID*4][3];
    int tags = 0;
    const int ELDEXT_TERM = 0;

    m4ac->ps  = 0;
    m4ac->sbr = 0;
    m4ac->frame_length_short = get_bits1(gb);

    res_flags = get_bits(gb, 3);
    if (res_flags) {
        avpriv_report_missing_feature(avctx,
                                      "AAC data resilience (flags %x)",
                                      res_flags);
        return AVERROR_PATCHWELCOME;
    }

    if (get_bits1(gb)) { // ldSbrPresentFlag
        avpriv_report_missing_feature(avctx,
                                      "Low Delay SBR");
        return AVERROR_PATCHWELCOME;
    }

    while (get_bits(gb, 4) != ELDEXT_TERM) {
        int len = get_bits(gb, 4);
        if (len == 15)
            len += get_bits(gb, 8);
        if (len == 15 + 255)
            len += get_bits(gb, 16);
        if (get_bits_left(gb) < len * 8 + 4) {
            av_log(avctx, AV_LOG_ERROR, overread_err);
            return AVERROR_INVALIDDATA;
        }
        skip_bits_long(gb, 8 * len);
    }

    if ((ret = set_default_channel_config(ac, avctx, layout_map,
                                          &tags, channel_config)))
        return ret;

    if (ac && (ret = output_configure(ac, layout_map, tags, OC_GLOBAL_HDR, 0)))
        return ret;

    ep_config = get_bits(gb, 2);
    if (ep_config) {
        avpriv_report_missing_feature(avctx,
                                      "epConfig %d", ep_config);
        return AVERROR_PATCHWELCOME;
    }
    return 0;
}

/**
 * Decode audio specific configuration; reference: table 1.13.
 *
 * @param   ac          pointer to AACContext, may be null
 * @param   avctx       pointer to AVCCodecContext, used for logging
 * @param   m4ac        pointer to MPEG4AudioConfig, used for parsing
 * @param   gb          buffer holding an audio specific config
 * @param   get_bit_alignment relative alignment for byte align operations
 * @param   sync_extension look for an appended sync extension
 *
 * @return  Returns error status or number of consumed bits. <0 - error
 */
static int decode_audio_specific_config_gb(AACContext *ac,
                                           AVCodecContext *avctx,
                                           MPEG4AudioConfig *m4ac,
                                           GetBitContext *gb,
                                           int get_bit_alignment,
                                           int sync_extension)
{
    int i, ret;
    GetBitContext gbc = *gb;
    MPEG4AudioConfig m4ac_bak = *m4ac;

    if ((i = ff_mpeg4audio_get_config_gb(m4ac, &gbc, sync_extension, avctx)) < 0) {
        *m4ac = m4ac_bak;
        return AVERROR_INVALIDDATA;
    }

    if (m4ac->sampling_index > 12) {
        av_log(avctx, AV_LOG_ERROR,
               "invalid sampling rate index %d\n",
               m4ac->sampling_index);
        *m4ac = m4ac_bak;
        return AVERROR_INVALIDDATA;
    }
    if (m4ac->object_type == AOT_ER_AAC_LD &&
        (m4ac->sampling_index < 3 || m4ac->sampling_index > 7)) {
        av_log(avctx, AV_LOG_ERROR,
               "invalid low delay sampling rate index %d\n",
               m4ac->sampling_index);
        *m4ac = m4ac_bak;
        return AVERROR_INVALIDDATA;
    }

    skip_bits_long(gb, i);

    switch (m4ac->object_type) {
    case AOT_AAC_MAIN:
    case AOT_AAC_LC:
    case AOT_AAC_SSR:
    case AOT_AAC_LTP:
    case AOT_ER_AAC_LC:
    case AOT_ER_AAC_LD:
        if ((ret = decode_ga_specific_config(ac, avctx, gb, get_bit_alignment,
                                            m4ac, m4ac->chan_config)) < 0)
            return ret;
        break;
    case AOT_ER_AAC_ELD:
        if ((ret = decode_eld_specific_config(ac, avctx, gb,
                                              m4ac, m4ac->chan_config)) < 0)
            return ret;
        break;
    default:
        avpriv_report_missing_feature(avctx,
                                      "Audio object type %s%d",
                                      m4ac->sbr == 1 ? "SBR+" : "",
                                      m4ac->object_type);
        return AVERROR(ENOSYS);
    }

    ff_dlog(avctx,
            "AOT %d chan config %d sampling index %d (%d) SBR %d PS %d\n",
            m4ac->object_type, m4ac->chan_config, m4ac->sampling_index,
            m4ac->sample_rate, m4ac->sbr,
            m4ac->ps);

    return get_bits_count(gb);
}

static int decode_audio_specific_config(AACContext *ac,
                                        AVCodecContext *avctx,
                                        MPEG4AudioConfig *m4ac,
                                        const uint8_t *data, int64_t bit_size,
                                        int sync_extension)
{
    int i, ret;
    GetBitContext gb;

    if (bit_size < 0 || bit_size > INT_MAX) {
        av_log(avctx, AV_LOG_ERROR, "Audio specific config size is invalid\n");
        return AVERROR_INVALIDDATA;
    }

    ff_dlog(avctx, "audio specific config size %d\n", (int)bit_size >> 3);
    for (i = 0; i < bit_size >> 3; i++)
        ff_dlog(avctx, "%02x ", data[i]);
    ff_dlog(avctx, "\n");

    if ((ret = init_get_bits(&gb, data, bit_size)) < 0)
        return ret;

    return decode_audio_specific_config_gb(ac, avctx, m4ac, &gb, 0,
                                           sync_extension);
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

static void aacdec_init(AACContext *ac);

static av_cold void aac_static_table_init(void)
{
    static VLCElem vlc_buf[304 + 270 + 550 + 300 + 328 +
                           294 + 306 + 268 + 510 + 366 + 462];
    for (unsigned i = 0, offset = 0; i < 11; i++) {
        vlc_spectral[i].table           = &vlc_buf[offset];
        vlc_spectral[i].table_allocated = FF_ARRAY_ELEMS(vlc_buf) - offset;
        ff_init_vlc_sparse(&vlc_spectral[i], 8, ff_aac_spectral_sizes[i],
                           ff_aac_spectral_bits[i],       sizeof(ff_aac_spectral_bits[i][0]),
                                                          sizeof(ff_aac_spectral_bits[i][0]),
                           ff_aac_spectral_codes[i],      sizeof(ff_aac_spectral_codes[i][0]),
                                                          sizeof(ff_aac_spectral_codes[i][0]),
                           ff_aac_codebook_vector_idx[i], sizeof(ff_aac_codebook_vector_idx[i][0]),
                                                          sizeof(ff_aac_codebook_vector_idx[i][0]),
                 INIT_VLC_STATIC_OVERLONG);
        offset += vlc_spectral[i].table_size;
    }

    AAC_RENAME(ff_aac_sbr_init)();

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

    // window initialization
    AAC_RENAME(ff_kbd_window_init)(AAC_RENAME(aac_kbd_long_960), 4.0, 960);
    AAC_RENAME(ff_kbd_window_init)(AAC_RENAME(aac_kbd_short_120), 6.0, 120);

#if !USE_FIXED
    AAC_RENAME(ff_sine_window_init)(AAC_RENAME(sine_960), 960);
    AAC_RENAME(ff_sine_window_init)(AAC_RENAME(sine_120), 120);
    AAC_RENAME(ff_init_ff_sine_windows)(9);
    ff_aac_float_common_init();
#else
    AAC_RENAME(ff_kbd_window_init)(AAC_RENAME2(aac_kbd_long_1024), 4.0, 1024);
    AAC_RENAME(ff_kbd_window_init)(AAC_RENAME2(aac_kbd_short_128), 6.0, 128);
    init_sine_windows_fixed();
#endif

    AAC_RENAME(ff_cbrt_tableinit)();
}

static AVOnce aac_table_init = AV_ONCE_INIT;

static av_cold int aac_decode_init(AVCodecContext *avctx)
{
    float scale;
    AACContext *ac = avctx->priv_data;
    int ret;

    if (avctx->sample_rate > 96000)
        return AVERROR_INVALIDDATA;

    ret = ff_thread_once(&aac_table_init, &aac_static_table_init);
    if (ret != 0)
        return AVERROR_UNKNOWN;

    ac->avctx = avctx;
    ac->oc[1].m4ac.sample_rate = avctx->sample_rate;

    aacdec_init(ac);
#if USE_FIXED
    avctx->sample_fmt = AV_SAMPLE_FMT_S32P;
#else
    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
#endif /* USE_FIXED */

    if (avctx->extradata_size > 0) {
        if ((ret = decode_audio_specific_config(ac, ac->avctx, &ac->oc[1].m4ac,
                                                avctx->extradata,
                                                avctx->extradata_size * 8LL,
                                                1)) < 0)
            return ret;
    } else {
        int sr, i;
        uint8_t layout_map[MAX_ELEM_ID*4][3];
        int layout_map_tags;

        sr = sample_rate_idx(avctx->sample_rate);
        ac->oc[1].m4ac.sampling_index = sr;
        ac->oc[1].m4ac.channels = avctx->ch_layout.nb_channels;
        ac->oc[1].m4ac.sbr = -1;
        ac->oc[1].m4ac.ps = -1;

        for (i = 0; i < FF_ARRAY_ELEMS(ff_mpeg4audio_channels); i++)
            if (ff_mpeg4audio_channels[i] == avctx->ch_layout.nb_channels)
                break;
        if (i == FF_ARRAY_ELEMS(ff_mpeg4audio_channels)) {
            i = 0;
        }
        ac->oc[1].m4ac.chan_config = i;

        if (ac->oc[1].m4ac.chan_config) {
            int ret = set_default_channel_config(ac, avctx, layout_map,
                &layout_map_tags, ac->oc[1].m4ac.chan_config);
            if (!ret)
                output_configure(ac, layout_map, layout_map_tags,
                                 OC_GLOBAL_HDR, 0);
            else if (avctx->err_recognition & AV_EF_EXPLODE)
                return AVERROR_INVALIDDATA;
        }
    }

    if (avctx->ch_layout.nb_channels > MAX_CHANNELS) {
        av_log(avctx, AV_LOG_ERROR, "Too many channels\n");
        return AVERROR_INVALIDDATA;
    }

#if USE_FIXED
    ac->fdsp = avpriv_alloc_fixed_dsp(avctx->flags & AV_CODEC_FLAG_BITEXACT);
#else
    ac->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
#endif /* USE_FIXED */
    if (!ac->fdsp) {
        return AVERROR(ENOMEM);
    }

    ac->random_state = 0x1f2e3d4c;

#define MDCT_INIT(s, fn, len, sval)                                            \
    scale = sval;                                                              \
    ret = av_tx_init(&s, &fn, TX_TYPE, 1, len, &scale, 0);                     \
    if (ret < 0)                                                               \
        return ret;

    MDCT_INIT(ac->mdct120,  ac->mdct120_fn,   120, TX_SCALE(1.0/120))
    MDCT_INIT(ac->mdct128,  ac->mdct128_fn,   128, TX_SCALE(1.0/128))
    MDCT_INIT(ac->mdct480,  ac->mdct480_fn,   480, TX_SCALE(1.0/480))
    MDCT_INIT(ac->mdct512,  ac->mdct512_fn,   512, TX_SCALE(1.0/512))
    MDCT_INIT(ac->mdct960,  ac->mdct960_fn,   960, TX_SCALE(1.0/960))
    MDCT_INIT(ac->mdct1024, ac->mdct1024_fn, 1024, TX_SCALE(1.0/1024))
#undef MDCT_INIT

    /* LTP forward MDCT */
    scale = USE_FIXED ? -1.0 : -32786.0*2 + 36;
    ret = av_tx_init(&ac->mdct_ltp, &ac->mdct_ltp_fn, TX_TYPE, 0, 1024, &scale, 0);
    if (ret < 0)
        return ret;

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
    const MPEG4AudioConfig *const m4ac = &ac->oc[1].m4ac;
    const int aot = m4ac->object_type;
    const int sampling_index = m4ac->sampling_index;
    int ret_fail = AVERROR_INVALIDDATA;

    if (aot != AOT_ER_AAC_ELD) {
        if (get_bits1(gb)) {
            av_log(ac->avctx, AV_LOG_ERROR, "Reserved bit set.\n");
            if (ac->avctx->err_recognition & AV_EF_BITSTREAM)
                return AVERROR_INVALIDDATA;
        }
        ics->window_sequence[1] = ics->window_sequence[0];
        ics->window_sequence[0] = get_bits(gb, 2);
        if (aot == AOT_ER_AAC_LD &&
            ics->window_sequence[0] != ONLY_LONG_SEQUENCE) {
            av_log(ac->avctx, AV_LOG_ERROR,
                   "AAC LD is only defined for ONLY_LONG_SEQUENCE but "
                   "window sequence %d found.\n", ics->window_sequence[0]);
            ics->window_sequence[0] = ONLY_LONG_SEQUENCE;
            return AVERROR_INVALIDDATA;
        }
        ics->use_kb_window[1]   = ics->use_kb_window[0];
        ics->use_kb_window[0]   = get_bits1(gb);
    }
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
        if (m4ac->frame_length_short) {
            ics->swb_offset    =  ff_swb_offset_120[sampling_index];
            ics->num_swb       = ff_aac_num_swb_120[sampling_index];
        } else {
            ics->swb_offset    =  ff_swb_offset_128[sampling_index];
            ics->num_swb       = ff_aac_num_swb_128[sampling_index];
        }
        ics->tns_max_bands     = ff_tns_max_bands_128[sampling_index];
        ics->predictor_present = 0;
    } else {
        ics->max_sfb           = get_bits(gb, 6);
        ics->num_windows       = 1;
        if (aot == AOT_ER_AAC_LD || aot == AOT_ER_AAC_ELD) {
            if (m4ac->frame_length_short) {
                ics->swb_offset    =     ff_swb_offset_480[sampling_index];
                ics->num_swb       =    ff_aac_num_swb_480[sampling_index];
                ics->tns_max_bands =  ff_tns_max_bands_480[sampling_index];
            } else {
                ics->swb_offset    =     ff_swb_offset_512[sampling_index];
                ics->num_swb       =    ff_aac_num_swb_512[sampling_index];
                ics->tns_max_bands =  ff_tns_max_bands_512[sampling_index];
            }
            if (!ics->num_swb || !ics->swb_offset) {
                ret_fail = AVERROR_BUG;
                goto fail;
            }
        } else {
            if (m4ac->frame_length_short) {
                ics->num_swb    = ff_aac_num_swb_960[sampling_index];
                ics->swb_offset = ff_swb_offset_960[sampling_index];
            } else {
                ics->num_swb    = ff_aac_num_swb_1024[sampling_index];
                ics->swb_offset = ff_swb_offset_1024[sampling_index];
            }
            ics->tns_max_bands = ff_tns_max_bands_1024[sampling_index];
        }
        if (aot != AOT_ER_AAC_ELD) {
            ics->predictor_present     = get_bits1(gb);
            ics->predictor_reset_group = 0;
        }
        if (ics->predictor_present) {
            if (aot == AOT_AAC_MAIN) {
                if (decode_prediction(ac, ics, gb)) {
                    goto fail;
                }
            } else if (aot == AOT_AAC_LC ||
                       aot == AOT_ER_AAC_LC) {
                av_log(ac->avctx, AV_LOG_ERROR,
                       "Prediction is not allowed in AAC-LC.\n");
                goto fail;
            } else {
                if (aot == AOT_ER_AAC_LD) {
                    av_log(ac->avctx, AV_LOG_ERROR,
                           "LTP in ER AAC LD not yet implemented.\n");
                    ret_fail = AVERROR_PATCHWELCOME;
                    goto fail;
                }
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
    return ret_fail;
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
static int decode_scalefactors(AACContext *ac, INTFLOAT sf[120], GetBitContext *gb,
                               unsigned int global_gain,
                               IndividualChannelStream *ics,
                               enum BandType band_type[120],
                               int band_type_run_end[120])
{
    int g, i, idx = 0;
    int offset[3] = { global_gain, global_gain - NOISE_OFFSET, 0 };
    int clipped_offset;
    int noise_flag = 1;
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb;) {
            int run_end = band_type_run_end[idx];
            if (band_type[idx] == ZERO_BT) {
                for (; i < run_end; i++, idx++)
                    sf[idx] = FIXR(0.);
            } else if ((band_type[idx] == INTENSITY_BT) ||
                       (band_type[idx] == INTENSITY_BT2)) {
                for (; i < run_end; i++, idx++) {
                    offset[2] += get_vlc2(gb, vlc_scalefactors.table, 7, 3) - SCALE_DIFF_ZERO;
                    clipped_offset = av_clip(offset[2], -155, 100);
                    if (offset[2] != clipped_offset) {
                        avpriv_request_sample(ac->avctx,
                                              "If you heard an audible artifact, there may be a bug in the decoder. "
                                              "Clipped intensity stereo position (%d -> %d)",
                                              offset[2], clipped_offset);
                    }
#if USE_FIXED
                    sf[idx] = 100 - clipped_offset;
#else
                    sf[idx] = ff_aac_pow2sf_tab[-clipped_offset + POW_SF2_ZERO];
#endif /* USE_FIXED */
                }
            } else if (band_type[idx] == NOISE_BT) {
                for (; i < run_end; i++, idx++) {
                    if (noise_flag-- > 0)
                        offset[1] += get_bits(gb, NOISE_PRE_BITS) - NOISE_PRE;
                    else
                        offset[1] += get_vlc2(gb, vlc_scalefactors.table, 7, 3) - SCALE_DIFF_ZERO;
                    clipped_offset = av_clip(offset[1], -100, 155);
                    if (offset[1] != clipped_offset) {
                        avpriv_request_sample(ac->avctx,
                                              "If you heard an audible artifact, there may be a bug in the decoder. "
                                              "Clipped noise gain (%d -> %d)",
                                              offset[1], clipped_offset);
                    }
#if USE_FIXED
                    sf[idx] = -(100 + clipped_offset);
#else
                    sf[idx] = -ff_aac_pow2sf_tab[clipped_offset + POW_SF2_ZERO];
#endif /* USE_FIXED */
                }
            } else {
                for (; i < run_end; i++, idx++) {
                    offset[0] += get_vlc2(gb, vlc_scalefactors.table, 7, 3) - SCALE_DIFF_ZERO;
                    if (offset[0] > 255U) {
                        av_log(ac->avctx, AV_LOG_ERROR,
                               "Scalefactor (%d) out of range.\n", offset[0]);
                        return AVERROR_INVALIDDATA;
                    }
#if USE_FIXED
                    sf[idx] = -offset[0];
#else
                    sf[idx] = -ff_aac_pow2sf_tab[offset[0] - 100 + POW_SF2_ZERO];
#endif /* USE_FIXED */
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
    if (pulse->pos[0] >= swb_offset[num_swb])
        return -1;
    pulse->amp[0]    = get_bits(gb, 4);
    for (i = 1; i < pulse->num_pulse; i++) {
        pulse->pos[i] = get_bits(gb, 5) + pulse->pos[i - 1];
        if (pulse->pos[i] >= swb_offset[num_swb])
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
    int max_idx = cpe->ch[0].ics.num_window_groups * cpe->ch[0].ics.max_sfb;
    if (ms_present == 1) {
        for (idx = 0; idx < max_idx; idx++)
            cpe->ms_mask[idx] = get_bits1(gb);
    } else if (ms_present == 2) {
        memset(cpe->ms_mask, 1, max_idx * sizeof(cpe->ms_mask[0]));
    }
}

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
static int decode_spectrum_and_dequant(AACContext *ac, INTFLOAT coef[1024],
                                       GetBitContext *gb, const INTFLOAT sf[120],
                                       int pulse_present, const Pulse *pulse,
                                       const IndividualChannelStream *ics,
                                       enum BandType band_type[120])
{
    int i, k, g, idx = 0;
    const int c = 1024 / ics->num_windows;
    const uint16_t *offsets = ics->swb_offset;
    INTFLOAT *coef_base = coef;

    for (g = 0; g < ics->num_windows; g++)
        memset(coef + g * 128 + offsets[ics->max_sfb], 0,
               sizeof(INTFLOAT) * (c - offsets[ics->max_sfb]));

    for (g = 0; g < ics->num_window_groups; g++) {
        unsigned g_len = ics->group_len[g];

        for (i = 0; i < ics->max_sfb; i++, idx++) {
            const unsigned cbt_m1 = band_type[idx] - 1;
            INTFLOAT *cfo = coef + offsets[i];
            int off_len = offsets[i + 1] - offsets[i];
            int group;

            if (cbt_m1 >= INTENSITY_BT2 - 1) {
                for (group = 0; group < (AAC_SIGNE)g_len; group++, cfo+=128) {
                    memset(cfo, 0, off_len * sizeof(*cfo));
                }
            } else if (cbt_m1 == NOISE_BT - 1) {
                for (group = 0; group < (AAC_SIGNE)g_len; group++, cfo+=128) {
                    INTFLOAT band_energy;
#if USE_FIXED
                    for (k = 0; k < off_len; k++) {
                        ac->random_state  = lcg_random(ac->random_state);
                        cfo[k] = ac->random_state >> 3;
                    }

                    band_energy = ac->fdsp->scalarproduct_fixed(cfo, cfo, off_len);
                    band_energy = fixed_sqrt(band_energy, 31);
                    noise_scale(cfo, sf[idx], band_energy, off_len);
#else
                    float scale;

                    for (k = 0; k < off_len; k++) {
                        ac->random_state  = lcg_random(ac->random_state);
                        cfo[k] = ac->random_state;
                    }

                    band_energy = ac->fdsp->scalarproduct_float(cfo, cfo, off_len);
                    scale = sf[idx] / sqrtf(band_energy);
                    ac->fdsp->vector_fmul_scalar(cfo, cfo, scale, off_len);
#endif /* USE_FIXED */
                }
            } else {
#if !USE_FIXED
                const float *vq = ff_aac_codebook_vector_vals[cbt_m1];
#endif /* !USE_FIXED */
                const VLCElem *vlc_tab = vlc_spectral[cbt_m1].table;
                OPEN_READER(re, gb);

                switch (cbt_m1 >> 1) {
                case 0:
                    for (group = 0; group < (AAC_SIGNE)g_len; group++, cfo+=128) {
                        INTFLOAT *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned cb_idx;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = code;
#if USE_FIXED
                            cf = DEC_SQUAD(cf, cb_idx);
#else
                            cf = VMUL4(cf, vq, cb_idx, sf + idx);
#endif /* USE_FIXED */
                        } while (len -= 4);
                    }
                    break;

                case 1:
                    for (group = 0; group < (AAC_SIGNE)g_len; group++, cfo+=128) {
                        INTFLOAT *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned nnz;
                            unsigned cb_idx;
                            uint32_t bits;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = code;
                            nnz = cb_idx >> 8 & 15;
                            bits = nnz ? GET_CACHE(re, gb) : 0;
                            LAST_SKIP_BITS(re, gb, nnz);
#if USE_FIXED
                            cf = DEC_UQUAD(cf, cb_idx, bits);
#else
                            cf = VMUL4S(cf, vq, cb_idx, bits, sf + idx);
#endif /* USE_FIXED */
                        } while (len -= 4);
                    }
                    break;

                case 2:
                    for (group = 0; group < (AAC_SIGNE)g_len; group++, cfo+=128) {
                        INTFLOAT *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned cb_idx;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = code;
#if USE_FIXED
                            cf = DEC_SPAIR(cf, cb_idx);
#else
                            cf = VMUL2(cf, vq, cb_idx, sf + idx);
#endif /* USE_FIXED */
                        } while (len -= 2);
                    }
                    break;

                case 3:
                case 4:
                    for (group = 0; group < (AAC_SIGNE)g_len; group++, cfo+=128) {
                        INTFLOAT *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned nnz;
                            unsigned cb_idx;
                            unsigned sign;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = code;
                            nnz = cb_idx >> 8 & 15;
                            sign = nnz ? SHOW_UBITS(re, gb, nnz) << (cb_idx >> 12) : 0;
                            LAST_SKIP_BITS(re, gb, nnz);
#if USE_FIXED
                            cf = DEC_UPAIR(cf, cb_idx, sign);
#else
                            cf = VMUL2S(cf, vq, cb_idx, sign, sf + idx);
#endif /* USE_FIXED */
                        } while (len -= 2);
                    }
                    break;

                default:
                    for (group = 0; group < (AAC_SIGNE)g_len; group++, cfo+=128) {
#if USE_FIXED
                        int *icf = cfo;
                        int v;
#else
                        float *cf = cfo;
                        uint32_t *icf = (uint32_t *) cf;
#endif /* USE_FIXED */
                        int len = off_len;

                        do {
                            int code;
                            unsigned nzt, nnz;
                            unsigned cb_idx;
                            uint32_t bits;
                            int j;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = code;

                            if (cb_idx == 0x0000) {
                                *icf++ = 0;
                                *icf++ = 0;
                                continue;
                            }

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
#if USE_FIXED
                                    v = n;
                                    if (bits & 1U<<31)
                                        v = -v;
                                    *icf++ = v;
#else
                                    *icf++ = ff_cbrt_tab[n] | (bits & 1U<<31);
#endif /* USE_FIXED */
                                    bits <<= 1;
                                } else {
#if USE_FIXED
                                    v = cb_idx & 15;
                                    if (bits & 1U<<31)
                                        v = -v;
                                    *icf++ = v;
#else
                                    unsigned v = ((const uint32_t*)vq)[cb_idx & 15];
                                    *icf++ = (bits & 1U<<31) | v;
#endif /* USE_FIXED */
                                    bits <<= !!v;
                                }
                                cb_idx >>= 4;
                            }
                        } while (len -= 2);
#if !USE_FIXED
                        ac->fdsp->vector_fmul_scalar(cfo, cfo, sf[idx], off_len);
#endif /* !USE_FIXED */
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
            INTFLOAT co = coef_base[ pulse->pos[i] ];
            while (offsets[idx + 1] <= pulse->pos[i])
                idx++;
            if (band_type[idx] != NOISE_BT && sf[idx]) {
                INTFLOAT ico = -pulse->amp[i];
#if USE_FIXED
                if (co) {
                    ico = co + (co > 0 ? -ico : ico);
                }
                coef_base[ pulse->pos[i] ] = ico;
#else
                if (co) {
                    co /= sf[idx];
                    ico = co / sqrtf(sqrtf(fabsf(co))) + (co > 0 ? -ico : ico);
                }
                coef_base[ pulse->pos[i] ] = cbrtf(fabsf(ico)) * ico * sf[idx];
#endif /* USE_FIXED */
            }
        }
    }
#if USE_FIXED
    coef = coef_base;
    idx = 0;
    for (g = 0; g < ics->num_window_groups; g++) {
        unsigned g_len = ics->group_len[g];

        for (i = 0; i < ics->max_sfb; i++, idx++) {
            const unsigned cbt_m1 = band_type[idx] - 1;
            int *cfo = coef + offsets[i];
            int off_len = offsets[i + 1] - offsets[i];
            int group;

            if (cbt_m1 < NOISE_BT - 1) {
                for (group = 0; group < (int)g_len; group++, cfo+=128) {
                    ac->vector_pow43(cfo, off_len);
                    ac->subband_scale(cfo, cfo, sf[idx], 34, off_len, ac->avctx);
                }
            }
        }
        coef += g_len << 7;
    }
#endif /* USE_FIXED */
    return 0;
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

static void decode_gain_control(SingleChannelElement * sce, GetBitContext * gb)
{
    // wd_num, wd_test, aloc_size
    static const uint8_t gain_mode[4][3] = {
        {1, 0, 5},  // ONLY_LONG_SEQUENCE = 0,
        {2, 1, 2},  // LONG_START_SEQUENCE,
        {8, 0, 2},  // EIGHT_SHORT_SEQUENCE,
        {2, 1, 5},  // LONG_STOP_SEQUENCE
    };

    const int mode = sce->ics.window_sequence[0];
    uint8_t bd, wd, ad;

    // FIXME: Store the gain control data on |sce| and do something with it.
    uint8_t max_band = get_bits(gb, 2);
    for (bd = 0; bd < max_band; bd++) {
        for (wd = 0; wd < gain_mode[mode][0]; wd++) {
            uint8_t adjust_num = get_bits(gb, 3);
            for (ad = 0; ad < adjust_num; ad++) {
                skip_bits(gb, 4 + ((wd == 0 && gain_mode[mode][1])
                                     ? 4
                                     : gain_mode[mode][2]));
            }
        }
    }
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
    INTFLOAT *out = sce->coeffs;
    int global_gain, eld_syntax, er_syntax, pulse_present = 0;
    int ret;

    eld_syntax = ac->oc[1].m4ac.object_type == AOT_ER_AAC_ELD;
    er_syntax  = ac->oc[1].m4ac.object_type == AOT_ER_AAC_LC ||
                 ac->oc[1].m4ac.object_type == AOT_ER_AAC_LTP ||
                 ac->oc[1].m4ac.object_type == AOT_ER_AAC_LD ||
                 ac->oc[1].m4ac.object_type == AOT_ER_AAC_ELD;

    /* This assignment is to silence a GCC warning about the variable being used
     * uninitialized when in fact it always is.
     */
    pulse.num_pulse = 0;

    global_gain = get_bits(gb, 8);

    if (!common_window && !scale_flag) {
        ret = decode_ics_info(ac, ics, gb);
        if (ret < 0)
            goto fail;
    }

    if ((ret = decode_band_types(ac, sce->band_type,
                                 sce->band_type_run_end, gb, ics)) < 0)
        goto fail;
    if ((ret = decode_scalefactors(ac, sce->sf, gb, global_gain, ics,
                                  sce->band_type, sce->band_type_run_end)) < 0)
        goto fail;

    pulse_present = 0;
    if (!scale_flag) {
        if (!eld_syntax && (pulse_present = get_bits1(gb))) {
            if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
                av_log(ac->avctx, AV_LOG_ERROR,
                       "Pulse tool not allowed in eight short sequence.\n");
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            if (decode_pulses(&pulse, gb, ics->swb_offset, ics->num_swb)) {
                av_log(ac->avctx, AV_LOG_ERROR,
                       "Pulse data corrupt or invalid.\n");
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
        }
        tns->present = get_bits1(gb);
        if (tns->present && !er_syntax) {
            ret = decode_tns(ac, tns, gb, ics);
            if (ret < 0)
                goto fail;
        }
        if (!eld_syntax && get_bits1(gb)) {
            decode_gain_control(sce, gb);
            if (!ac->warned_gain_control) {
                avpriv_report_missing_feature(ac->avctx, "Gain control");
                ac->warned_gain_control = 1;
            }
        }
        // I see no textual basis in the spec for this occurring after SSR gain
        // control, but this is what both reference and real implmentations do
        if (tns->present && er_syntax) {
            ret = decode_tns(ac, tns, gb, ics);
            if (ret < 0)
                goto fail;
        }
    }

    ret = decode_spectrum_and_dequant(ac, out, gb, sce->sf, pulse_present,
                                    &pulse, ics, sce->band_type);
    if (ret < 0)
        goto fail;

    if (ac->oc[1].m4ac.object_type == AOT_AAC_MAIN && !common_window)
        apply_prediction(ac, sce);

    return 0;
fail:
    tns->present = 0;
    return ret;
}

/**
 * Mid/Side stereo decoding; reference: 4.6.8.1.3.
 */
static void apply_mid_side_stereo(AACContext *ac, ChannelElement *cpe)
{
    const IndividualChannelStream *ics = &cpe->ch[0].ics;
    INTFLOAT *ch0 = cpe->ch[0].coeffs;
    INTFLOAT *ch1 = cpe->ch[1].coeffs;
    int g, i, group, idx = 0;
    const uint16_t *offsets = ics->swb_offset;
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb; i++, idx++) {
            if (cpe->ms_mask[idx] &&
                cpe->ch[0].band_type[idx] < NOISE_BT &&
                cpe->ch[1].band_type[idx] < NOISE_BT) {
#if USE_FIXED
                for (group = 0; group < ics->group_len[g]; group++) {
                    ac->fdsp->butterflies_fixed(ch0 + group * 128 + offsets[i],
                                                ch1 + group * 128 + offsets[i],
                                                offsets[i+1] - offsets[i]);
#else
                for (group = 0; group < ics->group_len[g]; group++) {
                    ac->fdsp->butterflies_float(ch0 + group * 128 + offsets[i],
                                               ch1 + group * 128 + offsets[i],
                                               offsets[i+1] - offsets[i]);
#endif /* USE_FIXED */
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
    INTFLOAT *coef0 = cpe->ch[0].coeffs, *coef1 = cpe->ch[1].coeffs;
    const uint16_t *offsets = ics->swb_offset;
    int g, group, i, idx = 0;
    int c;
    INTFLOAT scale;
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
#if USE_FIXED
                        ac->subband_scale(coef1 + group * 128 + offsets[i],
                                      coef0 + group * 128 + offsets[i],
                                      scale,
                                      23,
                                      offsets[i + 1] - offsets[i] ,ac->avctx);
#else
                        ac->fdsp->vector_fmul_scalar(coef1 + group * 128 + offsets[i],
                                                    coef0 + group * 128 + offsets[i],
                                                    scale,
                                                    offsets[i + 1] - offsets[i]);
#endif /* USE_FIXED */
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
    int eld_syntax = ac->oc[1].m4ac.object_type == AOT_ER_AAC_ELD;

    common_window = eld_syntax || get_bits1(gb);
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
    INTFLOAT scale;
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
#if USE_FIXED
    scale = get_bits(gb, 2);
#else
    scale = cce_scale[get_bits(gb, 2)];
#endif

    if ((ret = decode_ics(ac, sce, gb, 0, 0)))
        return ret;

    for (c = 0; c < num_gain; c++) {
        int idx  = 0;
        int cge  = 1;
        int gain = 0;
        INTFLOAT gain_cache = FIXR10(1.);
        if (c) {
            cge = coup->coupling_point == AFTER_IMDCT ? 1 : get_bits1(gb);
            gain = cge ? get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60: 0;
            gain_cache = GET_GAIN(scale, gain);
#if USE_FIXED
            if ((abs(gain_cache)-1024) >> 3 > 30)
                return AVERROR(ERANGE);
#endif
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
                                gain_cache = GET_GAIN(scale, t) * s;
#if USE_FIXED
                                if ((abs(gain_cache)-1024) >> 3 > 30)
                                    return AVERROR(ERANGE);
#endif
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
    int type = get_bits(gb, 4);

    if (ac->avctx->debug & FF_DEBUG_STARTCODE)
        av_log(ac->avctx, AV_LOG_DEBUG, "extension type: %d len:%d\n", type, cnt);

    switch (type) { // extension type
    case EXT_SBR_DATA_CRC:
        crc_flag++;
    case EXT_SBR_DATA:
        if (!che) {
            av_log(ac->avctx, AV_LOG_ERROR, "SBR was found before the first channel element.\n");
            return res;
        } else if (ac->oc[1].m4ac.frame_length_short) {
            if (!ac->warned_960_sbr)
              avpriv_report_missing_feature(ac->avctx,
                                            "SBR with 960 frame length");
            ac->warned_960_sbr = 1;
            skip_bits_long(gb, 8 * cnt - 4);
            return res;
        } else if (!ac->oc[1].m4ac.sbr) {
            av_log(ac->avctx, AV_LOG_ERROR, "SBR signaled to be not-present but was found in the bitstream.\n");
            skip_bits_long(gb, 8 * cnt - 4);
            return res;
        } else if (ac->oc[1].m4ac.sbr == -1 && ac->oc[1].status == OC_LOCKED) {
            av_log(ac->avctx, AV_LOG_ERROR, "Implicit SBR was found with a first occurrence after the first frame.\n");
            skip_bits_long(gb, 8 * cnt - 4);
            return res;
        } else if (ac->oc[1].m4ac.ps == -1 && ac->oc[1].status < OC_LOCKED &&
                   ac->avctx->ch_layout.nb_channels == 1) {
            ac->oc[1].m4ac.sbr = 1;
            ac->oc[1].m4ac.ps = 1;
            ac->avctx->profile = FF_PROFILE_AAC_HE_V2;
            output_configure(ac, ac->oc[1].layout_map, ac->oc[1].layout_map_tags,
                             ac->oc[1].status, 1);
        } else {
            ac->oc[1].m4ac.sbr = 1;
            ac->avctx->profile = FF_PROFILE_AAC_HE;
        }
        res = AAC_RENAME(ff_decode_sbr_extension)(ac, &che->sbr, gb, crc_flag, cnt, elem_type);
        if (ac->oc[1].m4ac.ps == 1 && !ac->warned_he_aac_mono) {
            av_log(ac->avctx, AV_LOG_VERBOSE, "Treating HE-AAC mono as stereo.\n");
            ac->warned_he_aac_mono = 1;
        }
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
static void apply_tns(INTFLOAT coef_param[1024], TemporalNoiseShaping *tns,
                      IndividualChannelStream *ics, int decode)
{
    const int mmm = FFMIN(ics->tns_max_bands, ics->max_sfb);
    int w, filt, m, i;
    int bottom, top, order, start, end, size, inc;
    INTFLOAT lpc[TNS_MAX_ORDER];
    INTFLOAT tmp[TNS_MAX_ORDER+1];
    UINTFLOAT *coef = coef_param;

    if(!mmm)
        return;

    for (w = 0; w < ics->num_windows; w++) {
        bottom = ics->num_swb;
        for (filt = 0; filt < tns->n_filt[w]; filt++) {
            top    = bottom;
            bottom = FFMAX(0, top - tns->length[w][filt]);
            order  = tns->order[w][filt];
            if (order == 0)
                continue;

            // tns_decode_coef
            AAC_RENAME(compute_lpc_coefs)(tns->coef[w][filt], order, lpc, 0, 0, 0);

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
                        coef[start] -= AAC_MUL26((INTFLOAT)coef[start - i * inc], lpc[i - 1]);
            } else {
                // ma filter
                for (m = 0; m < size; m++, start += inc) {
                    tmp[0] = coef[start];
                    for (i = 1; i <= FFMIN(m, order); i++)
                        coef[start] += AAC_MUL26(tmp[i], lpc[i - 1]);
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
static void windowing_and_mdct_ltp(AACContext *ac, INTFLOAT *out,
                                   INTFLOAT *in, IndividualChannelStream *ics)
{
    const INTFLOAT *lwindow      = ics->use_kb_window[0] ? AAC_RENAME2(aac_kbd_long_1024) : AAC_RENAME2(sine_1024);
    const INTFLOAT *swindow      = ics->use_kb_window[0] ? AAC_RENAME2(aac_kbd_short_128) : AAC_RENAME2(sine_128);
    const INTFLOAT *lwindow_prev = ics->use_kb_window[1] ? AAC_RENAME2(aac_kbd_long_1024) : AAC_RENAME2(sine_1024);
    const INTFLOAT *swindow_prev = ics->use_kb_window[1] ? AAC_RENAME2(aac_kbd_short_128) : AAC_RENAME2(sine_128);

    if (ics->window_sequence[0] != LONG_STOP_SEQUENCE) {
        ac->fdsp->vector_fmul(in, in, lwindow_prev, 1024);
    } else {
        memset(in, 0, 448 * sizeof(*in));
        ac->fdsp->vector_fmul(in + 448, in + 448, swindow_prev, 128);
    }
    if (ics->window_sequence[0] != LONG_START_SEQUENCE) {
        ac->fdsp->vector_fmul_reverse(in + 1024, in + 1024, lwindow, 1024);
    } else {
        ac->fdsp->vector_fmul_reverse(in + 1024 + 448, in + 1024 + 448, swindow, 128);
        memset(in + 1024 + 576, 0, 448 * sizeof(*in));
    }
    ac->mdct_ltp_fn(ac->mdct_ltp, out, in, sizeof(INTFLOAT));
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
        INTFLOAT *predTime = sce->ret;
        INTFLOAT *predFreq = ac->buf_mdct;
        int16_t num_samples = 2048;

        if (ltp->lag < 1024)
            num_samples = ltp->lag + 1024;
        for (i = 0; i < num_samples; i++)
            predTime[i] = AAC_MUL30(sce->ltp_state[i + 2048 - ltp->lag], ltp->coef);
        memset(&predTime[i], 0, (2048 - i) * sizeof(*predTime));

        ac->windowing_and_mdct_ltp(ac, predFreq, predTime, &sce->ics);

        if (sce->tns.present)
            ac->apply_tns(predFreq, &sce->tns, &sce->ics, 0);

        for (sfb = 0; sfb < FFMIN(sce->ics.max_sfb, MAX_LTP_LONG_SFB); sfb++)
            if (ltp->used[sfb])
                for (i = offsets[sfb]; i < offsets[sfb + 1]; i++)
                    sce->coeffs[i] += (UINTFLOAT)predFreq[i];
    }
}

/**
 * Update the LTP buffer for next frame
 */
static void update_ltp(AACContext *ac, SingleChannelElement *sce)
{
    IndividualChannelStream *ics = &sce->ics;
    INTFLOAT *saved     = sce->saved;
    INTFLOAT *saved_ltp = sce->coeffs;
    const INTFLOAT *lwindow = ics->use_kb_window[0] ? AAC_RENAME2(aac_kbd_long_1024) : AAC_RENAME2(sine_1024);
    const INTFLOAT *swindow = ics->use_kb_window[0] ? AAC_RENAME2(aac_kbd_short_128) : AAC_RENAME2(sine_128);
    int i;

    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        memcpy(saved_ltp,       saved, 512 * sizeof(*saved_ltp));
        memset(saved_ltp + 576, 0,     448 * sizeof(*saved_ltp));
        ac->fdsp->vector_fmul_reverse(saved_ltp + 448, ac->buf_mdct + 960,     &swindow[64],      64);

        for (i = 0; i < 64; i++)
            saved_ltp[i + 512] = AAC_MUL31(ac->buf_mdct[1023 - i], swindow[63 - i]);
    } else if (ics->window_sequence[0] == LONG_START_SEQUENCE) {
        memcpy(saved_ltp,       ac->buf_mdct + 512, 448 * sizeof(*saved_ltp));
        memset(saved_ltp + 576, 0,                  448 * sizeof(*saved_ltp));
        ac->fdsp->vector_fmul_reverse(saved_ltp + 448, ac->buf_mdct + 960,     &swindow[64],      64);

        for (i = 0; i < 64; i++)
            saved_ltp[i + 512] = AAC_MUL31(ac->buf_mdct[1023 - i], swindow[63 - i]);
    } else { // LONG_STOP or ONLY_LONG
        ac->fdsp->vector_fmul_reverse(saved_ltp,       ac->buf_mdct + 512,     &lwindow[512],     512);

        for (i = 0; i < 512; i++)
            saved_ltp[i + 512] = AAC_MUL31(ac->buf_mdct[1023 - i], lwindow[511 - i]);
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
    INTFLOAT *in    = sce->coeffs;
    INTFLOAT *out   = sce->ret;
    INTFLOAT *saved = sce->saved;
    const INTFLOAT *swindow      = ics->use_kb_window[0] ? AAC_RENAME2(aac_kbd_short_128) : AAC_RENAME2(sine_128);
    const INTFLOAT *lwindow_prev = ics->use_kb_window[1] ? AAC_RENAME2(aac_kbd_long_1024) : AAC_RENAME2(sine_1024);
    const INTFLOAT *swindow_prev = ics->use_kb_window[1] ? AAC_RENAME2(aac_kbd_short_128) : AAC_RENAME2(sine_128);
    INTFLOAT *buf  = ac->buf_mdct;
    INTFLOAT *temp = ac->temp;
    int i;

    // imdct
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        for (i = 0; i < 1024; i += 128)
            ac->mdct128_fn(ac->mdct128, buf + i, in + i, sizeof(INTFLOAT));
    } else {
        ac->mdct1024_fn(ac->mdct1024, buf, in, sizeof(INTFLOAT));
    }

    /* window overlapping
     * NOTE: To simplify the overlapping code, all 'meaningless' short to long
     * and long to short transitions are considered to be short to short
     * transitions. This leaves just two cases (long to long and short to short)
     * with a little special sauce for EIGHT_SHORT_SEQUENCE.
     */
    if ((ics->window_sequence[1] == ONLY_LONG_SEQUENCE || ics->window_sequence[1] == LONG_STOP_SEQUENCE) &&
            (ics->window_sequence[0] == ONLY_LONG_SEQUENCE || ics->window_sequence[0] == LONG_START_SEQUENCE)) {
        ac->fdsp->vector_fmul_window(    out,               saved,            buf,         lwindow_prev, 512);
    } else {
        memcpy(                         out,               saved,            448 * sizeof(*out));

        if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
            ac->fdsp->vector_fmul_window(out + 448 + 0*128, saved + 448,      buf + 0*128, swindow_prev, 64);
            ac->fdsp->vector_fmul_window(out + 448 + 1*128, buf + 0*128 + 64, buf + 1*128, swindow,      64);
            ac->fdsp->vector_fmul_window(out + 448 + 2*128, buf + 1*128 + 64, buf + 2*128, swindow,      64);
            ac->fdsp->vector_fmul_window(out + 448 + 3*128, buf + 2*128 + 64, buf + 3*128, swindow,      64);
            ac->fdsp->vector_fmul_window(temp,              buf + 3*128 + 64, buf + 4*128, swindow,      64);
            memcpy(                     out + 448 + 4*128, temp, 64 * sizeof(*out));
        } else {
            ac->fdsp->vector_fmul_window(out + 448,         saved + 448,      buf,         swindow_prev, 64);
            memcpy(                     out + 576,         buf + 64,         448 * sizeof(*out));
        }
    }

    // buffer update
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        memcpy(                     saved,       temp + 64,         64 * sizeof(*saved));
        ac->fdsp->vector_fmul_window(saved + 64,  buf + 4*128 + 64, buf + 5*128, swindow, 64);
        ac->fdsp->vector_fmul_window(saved + 192, buf + 5*128 + 64, buf + 6*128, swindow, 64);
        ac->fdsp->vector_fmul_window(saved + 320, buf + 6*128 + 64, buf + 7*128, swindow, 64);
        memcpy(                     saved + 448, buf + 7*128 + 64,  64 * sizeof(*saved));
    } else if (ics->window_sequence[0] == LONG_START_SEQUENCE) {
        memcpy(                     saved,       buf + 512,        448 * sizeof(*saved));
        memcpy(                     saved + 448, buf + 7*128 + 64,  64 * sizeof(*saved));
    } else { // LONG_STOP or ONLY_LONG
        memcpy(                     saved,       buf + 512,        512 * sizeof(*saved));
    }
}

/**
 * Conduct IMDCT and windowing.
 */
static void imdct_and_windowing_960(AACContext *ac, SingleChannelElement *sce)
{
    IndividualChannelStream *ics = &sce->ics;
    INTFLOAT *in    = sce->coeffs;
    INTFLOAT *out   = sce->ret;
    INTFLOAT *saved = sce->saved;
    const INTFLOAT *swindow      = ics->use_kb_window[0] ? AAC_RENAME(aac_kbd_short_120) : AAC_RENAME(sine_120);
    const INTFLOAT *lwindow_prev = ics->use_kb_window[1] ? AAC_RENAME(aac_kbd_long_960) : AAC_RENAME(sine_960);
    const INTFLOAT *swindow_prev = ics->use_kb_window[1] ? AAC_RENAME(aac_kbd_short_120) : AAC_RENAME(sine_120);
    INTFLOAT *buf  = ac->buf_mdct;
    INTFLOAT *temp = ac->temp;
    int i;

    // imdct
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        for (i = 0; i < 8; i++)
            ac->mdct120_fn(ac->mdct120, buf + i * 120, in + i * 128, sizeof(INTFLOAT));
    } else {
        ac->mdct960_fn(ac->mdct960, buf, in, sizeof(INTFLOAT));
    }

    /* window overlapping
     * NOTE: To simplify the overlapping code, all 'meaningless' short to long
     * and long to short transitions are considered to be short to short
     * transitions. This leaves just two cases (long to long and short to short)
     * with a little special sauce for EIGHT_SHORT_SEQUENCE.
     */

    if ((ics->window_sequence[1] == ONLY_LONG_SEQUENCE || ics->window_sequence[1] == LONG_STOP_SEQUENCE) &&
        (ics->window_sequence[0] == ONLY_LONG_SEQUENCE || ics->window_sequence[0] == LONG_START_SEQUENCE)) {
        ac->fdsp->vector_fmul_window(    out,               saved,            buf,         lwindow_prev, 480);
    } else {
        memcpy(                          out,               saved,            420 * sizeof(*out));

        if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
            ac->fdsp->vector_fmul_window(out + 420 + 0*120, saved + 420,      buf + 0*120, swindow_prev, 60);
            ac->fdsp->vector_fmul_window(out + 420 + 1*120, buf + 0*120 + 60, buf + 1*120, swindow,      60);
            ac->fdsp->vector_fmul_window(out + 420 + 2*120, buf + 1*120 + 60, buf + 2*120, swindow,      60);
            ac->fdsp->vector_fmul_window(out + 420 + 3*120, buf + 2*120 + 60, buf + 3*120, swindow,      60);
            ac->fdsp->vector_fmul_window(temp,              buf + 3*120 + 60, buf + 4*120, swindow,      60);
            memcpy(                      out + 420 + 4*120, temp, 60 * sizeof(*out));
        } else {
            ac->fdsp->vector_fmul_window(out + 420,         saved + 420,      buf,         swindow_prev, 60);
            memcpy(                      out + 540,         buf + 60,         420 * sizeof(*out));
        }
    }

    // buffer update
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        memcpy(                      saved,       temp + 60,         60 * sizeof(*saved));
        ac->fdsp->vector_fmul_window(saved + 60,  buf + 4*120 + 60, buf + 5*120, swindow, 60);
        ac->fdsp->vector_fmul_window(saved + 180, buf + 5*120 + 60, buf + 6*120, swindow, 60);
        ac->fdsp->vector_fmul_window(saved + 300, buf + 6*120 + 60, buf + 7*120, swindow, 60);
        memcpy(                      saved + 420, buf + 7*120 + 60,  60 * sizeof(*saved));
    } else if (ics->window_sequence[0] == LONG_START_SEQUENCE) {
        memcpy(                      saved,       buf + 480,        420 * sizeof(*saved));
        memcpy(                      saved + 420, buf + 7*120 + 60,  60 * sizeof(*saved));
    } else { // LONG_STOP or ONLY_LONG
        memcpy(                      saved,       buf + 480,        480 * sizeof(*saved));
    }
}
static void imdct_and_windowing_ld(AACContext *ac, SingleChannelElement *sce)
{
    IndividualChannelStream *ics = &sce->ics;
    INTFLOAT *in    = sce->coeffs;
    INTFLOAT *out   = sce->ret;
    INTFLOAT *saved = sce->saved;
    INTFLOAT *buf  = ac->buf_mdct;

    // imdct
    ac->mdct512_fn(ac->mdct512, buf, in, sizeof(INTFLOAT));

    // window overlapping
    if (ics->use_kb_window[1]) {
        // AAC LD uses a low overlap sine window instead of a KBD window
        memcpy(out, saved, 192 * sizeof(*out));
        ac->fdsp->vector_fmul_window(out + 192, saved + 192, buf, AAC_RENAME2(sine_128), 64);
        memcpy(                     out + 320, buf + 64, 192 * sizeof(*out));
    } else {
        ac->fdsp->vector_fmul_window(out, saved, buf, AAC_RENAME2(sine_512), 256);
    }

    // buffer update
    memcpy(saved, buf + 256, 256 * sizeof(*saved));
}

static void imdct_and_windowing_eld(AACContext *ac, SingleChannelElement *sce)
{
    UINTFLOAT *in   = sce->coeffs;
    INTFLOAT *out   = sce->ret;
    INTFLOAT *saved = sce->saved;
    INTFLOAT *buf  = ac->buf_mdct;
    int i;
    const int n  = ac->oc[1].m4ac.frame_length_short ? 480 : 512;
    const int n2 = n >> 1;
    const int n4 = n >> 2;
    const INTFLOAT *const window = n == 480 ? AAC_RENAME(ff_aac_eld_window_480) :
                                           AAC_RENAME(ff_aac_eld_window_512);

    // Inverse transform, mapped to the conventional IMDCT by
    // Chivukula, R.K.; Reznik, Y.A.; Devarajan, V.,
    // "Efficient algorithms for MPEG-4 AAC-ELD, AAC-LD and AAC-LC filterbanks,"
    // International Conference on Audio, Language and Image Processing, ICALIP 2008.
    // URL: http://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=4590245&isnumber=4589950
    for (i = 0; i < n2; i+=2) {
        INTFLOAT temp;
        temp =  in[i    ]; in[i    ] = -in[n - 1 - i]; in[n - 1 - i] = temp;
        temp = -in[i + 1]; in[i + 1] =  in[n - 2 - i]; in[n - 2 - i] = temp;
    }

    if (n == 480)
        ac->mdct480_fn(ac->mdct480, buf, in, sizeof(INTFLOAT));
    else
        ac->mdct512_fn(ac->mdct512, buf, in, sizeof(INTFLOAT));

    for (i = 0; i < n; i+=2) {
        buf[i + 0] = -(USE_FIXED + 1)*buf[i + 0];
        buf[i + 1] =  (USE_FIXED + 1)*buf[i + 1];
    }
    // Like with the regular IMDCT at this point we still have the middle half
    // of a transform but with even symmetry on the left and odd symmetry on
    // the right

    // window overlapping
    // The spec says to use samples [0..511] but the reference decoder uses
    // samples [128..639].
    for (i = n4; i < n2; i ++) {
        out[i - n4] = AAC_MUL31(   buf[    n2 - 1 - i] , window[i       - n4]) +
                      AAC_MUL31( saved[        i + n2] , window[i +   n - n4]) +
                      AAC_MUL31(-saved[n + n2 - 1 - i] , window[i + 2*n - n4]) +
                      AAC_MUL31(-saved[  2*n + n2 + i] , window[i + 3*n - n4]);
    }
    for (i = 0; i < n2; i ++) {
        out[n4 + i] = AAC_MUL31(   buf[              i] , window[i + n2       - n4]) +
                      AAC_MUL31(-saved[      n - 1 - i] , window[i + n2 +   n - n4]) +
                      AAC_MUL31(-saved[          n + i] , window[i + n2 + 2*n - n4]) +
                      AAC_MUL31( saved[2*n + n - 1 - i] , window[i + n2 + 3*n - n4]);
    }
    for (i = 0; i < n4; i ++) {
        out[n2 + n4 + i] = AAC_MUL31(   buf[    i + n2] , window[i +   n - n4]) +
                           AAC_MUL31(-saved[n2 - 1 - i] , window[i + 2*n - n4]) +
                           AAC_MUL31(-saved[n + n2 + i] , window[i + 3*n - n4]);
    }

    // buffer update
    memmove(saved + n, saved, 2 * n * sizeof(*saved));
    memcpy( saved,       buf,     n * sizeof(*saved));
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
 * Convert spectral data to samples, applying all supported tools as appropriate.
 */
static void spectral_to_sample(AACContext *ac, int samples)
{
    int i, type;
    void (*imdct_and_window)(AACContext *ac, SingleChannelElement *sce);
    switch (ac->oc[1].m4ac.object_type) {
    case AOT_ER_AAC_LD:
        imdct_and_window = imdct_and_windowing_ld;
        break;
    case AOT_ER_AAC_ELD:
        imdct_and_window = imdct_and_windowing_eld;
        break;
    default:
        if (ac->oc[1].m4ac.frame_length_short)
            imdct_and_window = imdct_and_windowing_960;
        else
            imdct_and_window = ac->imdct_and_windowing;
    }
    for (type = 3; type >= 0; type--) {
        for (i = 0; i < MAX_ELEM_ID; i++) {
            ChannelElement *che = ac->che[type][i];
            if (che && che->present) {
                if (type <= TYPE_CPE)
                    apply_channel_coupling(ac, che, type, i, BEFORE_TNS, AAC_RENAME(apply_dependent_coupling));
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
                    apply_channel_coupling(ac, che, type, i, BETWEEN_TNS_AND_IMDCT, AAC_RENAME(apply_dependent_coupling));
                if (type != TYPE_CCE || che->coup.coupling_point == AFTER_IMDCT) {
                    imdct_and_window(ac, &che->ch[0]);
                    if (ac->oc[1].m4ac.object_type == AOT_AAC_LTP)
                        ac->update_ltp(ac, &che->ch[0]);
                    if (type == TYPE_CPE) {
                        imdct_and_window(ac, &che->ch[1]);
                        if (ac->oc[1].m4ac.object_type == AOT_AAC_LTP)
                            ac->update_ltp(ac, &che->ch[1]);
                    }
                    if (ac->oc[1].m4ac.sbr > 0) {
                        AAC_RENAME(ff_sbr_apply)(ac, &che->sbr, type, che->ch[0].ret, che->ch[1].ret);
                    }
                }
                if (type <= TYPE_CCE)
                    apply_channel_coupling(ac, che, type, i, AFTER_IMDCT, AAC_RENAME(apply_independent_coupling));

#if USE_FIXED
                {
                    int j;
                    /* preparation for resampler */
                    for(j = 0; j<samples; j++){
                        che->ch[0].ret[j] = (int32_t)av_clip64((int64_t)che->ch[0].ret[j]*128, INT32_MIN, INT32_MAX-0x8000)+0x8000;
                        if (type == TYPE_CPE || (type == TYPE_SCE && ac->oc[1].m4ac.ps == 1))
                            che->ch[1].ret[j] = (int32_t)av_clip64((int64_t)che->ch[1].ret[j]*128, INT32_MIN, INT32_MAX-0x8000)+0x8000;
                    }
                }
#endif /* USE_FIXED */
                che->present = 0;
            } else if (che) {
                av_log(ac->avctx, AV_LOG_VERBOSE, "ChannelElement %d.%d missing \n", type, i);
            }
        }
    }
}

static int parse_adts_frame_header(AACContext *ac, GetBitContext *gb)
{
    int size;
    AACADTSHeaderInfo hdr_info;
    uint8_t layout_map[MAX_ELEM_ID*4][3];
    int layout_map_tags, ret;

    size = ff_adts_header_parse(gb, &hdr_info);
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
            if ((ret = set_default_channel_config(ac, ac->avctx,
                                                  layout_map,
                                                  &layout_map_tags,
                                                  hdr_info.chan_config)) < 0)
                return ret;
            if ((ret = output_configure(ac, layout_map, layout_map_tags,
                                        FFMAX(ac->oc[1].status,
                                              OC_TRIAL_FRAME), 0)) < 0)
                return ret;
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
        ac->oc[1].m4ac.frame_length_short = 0;
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

static int aac_decode_er_frame(AVCodecContext *avctx, void *data,
                               int *got_frame_ptr, GetBitContext *gb)
{
    AACContext *ac = avctx->priv_data;
    const MPEG4AudioConfig *const m4ac = &ac->oc[1].m4ac;
    ChannelElement *che;
    int err, i;
    int samples = m4ac->frame_length_short ? 960 : 1024;
    int chan_config = m4ac->chan_config;
    int aot = m4ac->object_type;

    if (aot == AOT_ER_AAC_LD || aot == AOT_ER_AAC_ELD)
        samples >>= 1;

    ac->frame = data;

    if ((err = frame_configure_elements(avctx)) < 0)
        return err;

    // The FF_PROFILE_AAC_* defines are all object_type - 1
    // This may lead to an undefined profile being signaled
    ac->avctx->profile = aot - 1;

    ac->tags_mapped = 0;

    if (chan_config < 0 || (chan_config >= 8 && chan_config < 11) || chan_config >= 13) {
        avpriv_request_sample(avctx, "Unknown ER channel configuration %d",
                              chan_config);
        return AVERROR_INVALIDDATA;
    }
    for (i = 0; i < tags_per_config[chan_config]; i++) {
        const int elem_type = aac_channel_layout_map[chan_config-1][i][0];
        const int elem_id   = aac_channel_layout_map[chan_config-1][i][1];
        if (!(che=get_che(ac, elem_type, elem_id))) {
            av_log(ac->avctx, AV_LOG_ERROR,
                   "channel element %d.%d is not allocated\n",
                   elem_type, elem_id);
            return AVERROR_INVALIDDATA;
        }
        che->present = 1;
        if (aot != AOT_ER_AAC_ELD)
            skip_bits(gb, 4);
        switch (elem_type) {
        case TYPE_SCE:
            err = decode_ics(ac, &che->ch[0], gb, 0, 0);
            break;
        case TYPE_CPE:
            err = decode_cpe(ac, gb, che);
            break;
        case TYPE_LFE:
            err = decode_ics(ac, &che->ch[0], gb, 0, 0);
            break;
        }
        if (err < 0)
            return err;
    }

    spectral_to_sample(ac, samples);

    if (!ac->frame->data[0] && samples) {
        av_log(avctx, AV_LOG_ERROR, "no frame data found\n");
        return AVERROR_INVALIDDATA;
    }

    ac->frame->nb_samples = samples;
    ac->frame->sample_rate = avctx->sample_rate;
    *got_frame_ptr = 1;

    skip_bits_long(gb, get_bits_left(gb));
    return 0;
}

static int aac_decode_frame_int(AVCodecContext *avctx, AVFrame *frame,
                                int *got_frame_ptr, GetBitContext *gb,
                                const AVPacket *avpkt)
{
    AACContext *ac = avctx->priv_data;
    ChannelElement *che = NULL, *che_prev = NULL;
    enum RawDataBlockType elem_type, che_prev_type = TYPE_END;
    int err, elem_id;
    int samples = 0, multiplier, audio_found = 0, pce_found = 0;
    int is_dmono, sce_count = 0;
    int payload_alignment;
    uint8_t che_presence[4][MAX_ELEM_ID] = {{0}};

    ac->frame = frame;

    if (show_bits(gb, 12) == 0xfff) {
        if ((err = parse_adts_frame_header(ac, gb)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error decoding AAC frame header.\n");
            goto fail;
        }
        if (ac->oc[1].m4ac.sampling_index > 12) {
            av_log(ac->avctx, AV_LOG_ERROR, "invalid sampling rate index %d\n", ac->oc[1].m4ac.sampling_index);
            err = AVERROR_INVALIDDATA;
            goto fail;
        }
    }

    if ((err = frame_configure_elements(avctx)) < 0)
        goto fail;

    // The FF_PROFILE_AAC_* defines are all object_type - 1
    // This may lead to an undefined profile being signaled
    ac->avctx->profile = ac->oc[1].m4ac.object_type - 1;

    payload_alignment = get_bits_count(gb);
    ac->tags_mapped = 0;
    // parse
    while ((elem_type = get_bits(gb, 3)) != TYPE_END) {
        elem_id = get_bits(gb, 4);

        if (avctx->debug & FF_DEBUG_STARTCODE)
            av_log(avctx, AV_LOG_DEBUG, "Elem type:%x id:%x\n", elem_type, elem_id);

        if (!avctx->ch_layout.nb_channels && elem_type != TYPE_PCE) {
            err = AVERROR_INVALIDDATA;
            goto fail;
        }

        if (elem_type < TYPE_DSE) {
            if (che_presence[elem_type][elem_id]) {
                int error = che_presence[elem_type][elem_id] > 1;
                av_log(ac->avctx, error ? AV_LOG_ERROR : AV_LOG_DEBUG, "channel element %d.%d duplicate\n",
                       elem_type, elem_id);
                if (error) {
                    err = AVERROR_INVALIDDATA;
                    goto fail;
                }
            }
            che_presence[elem_type][elem_id]++;

            if (!(che=get_che(ac, elem_type, elem_id))) {
                av_log(ac->avctx, AV_LOG_ERROR, "channel element %d.%d is not allocated\n",
                       elem_type, elem_id);
                err = AVERROR_INVALIDDATA;
                goto fail;
            }
            samples = ac->oc[1].m4ac.frame_length_short ? 960 : 1024;
            che->present = 1;
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
            uint8_t layout_map[MAX_ELEM_ID*4][3] = {{0}};
            int tags;

            int pushed = push_output_configuration(ac);
            if (pce_found && !pushed) {
                err = AVERROR_INVALIDDATA;
                goto fail;
            }

            tags = decode_pce(avctx, &ac->oc[1].m4ac, layout_map, gb,
                              payload_alignment);
            if (tags < 0) {
                err = tags;
                break;
            }
            if (pce_found) {
                av_log(avctx, AV_LOG_ERROR,
                       "Not evaluating a further program_config_element as this construct is dubious at best.\n");
                pop_output_configuration(ac);
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
                    err = AVERROR_INVALIDDATA;
                    goto fail;
            }
            err = 0;
            while (elem_id > 0) {
                int ret = decode_extension_payload(ac, gb, elem_id, che_prev, che_prev_type);
                if (ret < 0) {
                    err = ret;
                    break;
                }
                elem_id -= ret;
            }
            break;

        default:
            err = AVERROR_BUG; /* should not happen, but keeps compiler happy */
            break;
        }

        if (elem_type < TYPE_DSE) {
            che_prev      = che;
            che_prev_type = elem_type;
        }

        if (err)
            goto fail;

        if (get_bits_left(gb) < 3) {
            av_log(avctx, AV_LOG_ERROR, overread_err);
            err = AVERROR_INVALIDDATA;
            goto fail;
        }
    }

    if (!avctx->ch_layout.nb_channels) {
        *got_frame_ptr = 0;
        return 0;
    }

    multiplier = (ac->oc[1].m4ac.sbr == 1) ? ac->oc[1].m4ac.ext_sample_rate > ac->oc[1].m4ac.sample_rate : 0;
    samples <<= multiplier;

    spectral_to_sample(ac, samples);

    if (ac->oc[1].status && audio_found) {
        avctx->sample_rate = ac->oc[1].m4ac.sample_rate << multiplier;
        avctx->frame_size = samples;
        ac->oc[1].status = OC_LOCKED;
    }

    if (!ac->frame->data[0] && samples) {
        av_log(avctx, AV_LOG_ERROR, "no frame data found\n");
        err = AVERROR_INVALIDDATA;
        goto fail;
    }

    if (samples) {
        ac->frame->nb_samples = samples;
        ac->frame->sample_rate = avctx->sample_rate;
    } else
        av_frame_unref(ac->frame);
    *got_frame_ptr = !!samples;

    /* for dual-mono audio (SCE + SCE) */
    is_dmono = ac->dmono_mode && sce_count == 2 &&
               !av_channel_layout_compare(&ac->oc[1].ch_layout,
                                          &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO);
    if (is_dmono) {
        if (ac->dmono_mode == 1)
            frame->data[1] = frame->data[0];
        else if (ac->dmono_mode == 2)
            frame->data[0] = frame->data[1];
    }

    return 0;
fail:
    pop_output_configuration(ac);
    return err;
}

static int aac_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    AACContext *ac = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    GetBitContext gb;
    int buf_consumed;
    int buf_offset;
    int err;
    size_t new_extradata_size;
    const uint8_t *new_extradata = av_packet_get_side_data(avpkt,
                                       AV_PKT_DATA_NEW_EXTRADATA,
                                       &new_extradata_size);
    size_t jp_dualmono_size;
    const uint8_t *jp_dualmono   = av_packet_get_side_data(avpkt,
                                       AV_PKT_DATA_JP_DUALMONO,
                                       &jp_dualmono_size);

    if (new_extradata) {
        /* discard previous configuration */
        ac->oc[1].status = OC_NONE;
        err = decode_audio_specific_config(ac, ac->avctx, &ac->oc[1].m4ac,
                                           new_extradata,
                                           new_extradata_size * 8LL, 1);
        if (err < 0) {
            return err;
        }
    }

    ac->dmono_mode = 0;
    if (jp_dualmono && jp_dualmono_size > 0)
        ac->dmono_mode =  1 + *jp_dualmono;
    if (ac->force_dmono_mode >= 0)
        ac->dmono_mode = ac->force_dmono_mode;

    if (INT_MAX / 8 <= buf_size)
        return AVERROR_INVALIDDATA;

    if ((err = init_get_bits8(&gb, buf, buf_size)) < 0)
        return err;

    switch (ac->oc[1].m4ac.object_type) {
    case AOT_ER_AAC_LC:
    case AOT_ER_AAC_LTP:
    case AOT_ER_AAC_LD:
    case AOT_ER_AAC_ELD:
        err = aac_decode_er_frame(avctx, frame, got_frame_ptr, &gb);
        break;
    default:
        err = aac_decode_frame_int(avctx, frame, got_frame_ptr, &gb, avpkt);
    }
    if (err < 0)
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
                AAC_RENAME(ff_aac_sbr_ctx_close)(&ac->che[type][i]->sbr);
            av_freep(&ac->che[type][i]);
        }
    }

    av_tx_uninit(&ac->mdct120);
    av_tx_uninit(&ac->mdct128);
    av_tx_uninit(&ac->mdct480);
    av_tx_uninit(&ac->mdct512);
    av_tx_uninit(&ac->mdct960);
    av_tx_uninit(&ac->mdct1024);
    av_tx_uninit(&ac->mdct_ltp);

    av_freep(&ac->fdsp);
    return 0;
}

static void aacdec_init(AACContext *c)
{
    c->imdct_and_windowing                      = imdct_and_windowing;
    c->apply_ltp                                = apply_ltp;
    c->apply_tns                                = apply_tns;
    c->windowing_and_mdct_ltp                   = windowing_and_mdct_ltp;
    c->update_ltp                               = update_ltp;
#if USE_FIXED
    c->vector_pow43                             = vector_pow43;
    c->subband_scale                            = subband_scale;
#endif

#if !USE_FIXED
#if ARCH_MIPS
    ff_aacdec_init_mips(c);
#endif
#endif /* !USE_FIXED */
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

    { "channel_order", "Order in which the channels are to be exported",
        offsetof(AACContext, output_channel_order), AV_OPT_TYPE_INT,
        { .i64 = CHANNEL_ORDER_DEFAULT }, 0, 1, AACDEC_FLAGS, "channel_order" },
      { "default", "normal libavcodec channel order", 0, AV_OPT_TYPE_CONST,
        { .i64 = CHANNEL_ORDER_DEFAULT }, .flags = AACDEC_FLAGS, "channel_order" },
      { "coded",    "order in which the channels are coded in the bitstream",
        0, AV_OPT_TYPE_CONST, { .i64 = CHANNEL_ORDER_CODED }, .flags = AACDEC_FLAGS, "channel_order" },

    {NULL},
};

static const AVClass aac_decoder_class = {
    .class_name = "AAC decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};
