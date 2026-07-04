/*
 * LPCM codec for PCM formats found in DVD-Audio streams
 * Copyright (c) 2026 Kacper Michajłow
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

#include <assert.h>
#include <stddef.h>

#include "libavutil/channel_layout.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"

/*
 * Header of a linear PCM audio packet (A_PKT) of a DVD-Audio AOB. It
 * follows the 0xa0 sub_stream_id byte of the MPEG private_stream_1 packet
 * and is itself followed by 0 to 7 stuffing bytes and the audio data.
 * Layout and semantics from US 6,580,671 (FIGS. 27 and 29, cols. 19-21).
 */
typedef struct PCMDVDAHeader {
    /* reserved (4 bits), ISRC number (4 bits): position, from 1 to 12, of
     * the isrc_data byte within the track's 12-character International
     * Standard Recording Code, which is spread over consecutive packets */
    uint8_t isrc_number;
    /* the ISRC character designated by isrc_number */
    uint8_t isrc_data;
    /* number of header bytes following this field, including the trailing
     * stuffing bytes; the audio data starts right after */
    uint8_t private_header_length;
    /* big-endian byte offset, counted from the end of this field, of the
     * first audio frame (access unit) to be presented at the packet's PTS */
    uint8_t first_access_unit_pointer[2];
    /* audio emphasis flag (1 bit): high-frequency emphasis is applied,
     * never set for a group sampled at 96 or 88.2 kHz; reserved (3 bits);
     * downmix code (4 bits): selects which of the 16 downmix coefficient
     * tables of the title set (ATS_DM_COEFT #0..#15 in the ATSI_MAT) to
     * use for multichannel to 2-channel output */
    uint8_t emphasis_downmix;
    /* quantization word length of channel group 1 (4 bits) and group 2
     * (4 bits): 0 = 16, 1 = 20, 2 = 24 bits per sample;
     * 0xf in group 2 when the group does not exist */
    uint8_t quantization;
    /* audio sampling frequency of channel group 1 (4 bits) and group 2
     * (4 bits): 0 = 48 kHz, 1 = 96 kHz, 2 = 192 kHz, 8 = 44.1 kHz,
     * 9 = 88.2 kHz, 0xa = 176.4 kHz; 0xf in group 2 when the group does
     * not exist */
    uint8_t sampling_frequency;
    /* reserved (4 bits), multichannel type (4 bits): 0 = type 1, the only
     * defined sample structure; other values reserved */
    uint8_t multichannel_type;
    /* reserved (3 bits), channel assignment (5 bits): selects one of the
     * 21 channel-to-group allocations, see channel_assignments[] */
    uint8_t channel_assignment;
    /* dynamic range control: X (3 bits), Y (5 bits); playback may scale
     * the audio by 2^(4 - X - Y/30) with 0 <= X <= 7, 0 <= Y <= 29, so
     * 0x80 is unity gain; not applied, as with the other PCM decoders */
    uint8_t dynamic_range_control;
} PCMDVDAHeader;

static_assert(sizeof(PCMDVDAHeader) == 11, "unexpected header padding");

typedef struct PCMDVDAContext {
    uint32_t last_header;    // Cached header to avoid reparsing
    int block_size;          // Size of a set of 2 samples over all channels
    int channels;
    int group_channels[2];   // Channels in group 1 / group 2 (0 = unused)
    int group_bits[2];       // Quantization of group 1 / group 2
    uint8_t group_map[2][6]; // Stream channel -> native layout position
} PCMDVDAContext;

#define CH_C   AV_CH_FRONT_CENTER
#define CH_L   AV_CH_FRONT_LEFT
#define CH_R   AV_CH_FRONT_RIGHT
#define CH_LFE AV_CH_LOW_FREQUENCY
#define CH_S   AV_CH_BACK_CENTER
#define CH_LS  AV_CH_BACK_LEFT
#define CH_RS  AV_CH_BACK_RIGHT

/*
 * Channel allocation table (US 6,580,671 FIG. 26 and cols. 18-19): the
 * speaker fed by each audio channel, in stream order (ACH0..ACH5, first
 * channel group first), and how many channels each group holds. Beware
 * that the counts column of FIG. 26 misprints the group 1 size of
 * assignments 13 (3, not 2) and 18 (4, not 3). The group boundary drawn
 * in the figure and the enumeration in cols. 18-19 agree on the values
 * used here.
 */
static const struct {
    uint8_t group1_channels;
    uint8_t group2_channels;
    uint64_t channels[6];
} channel_assignments[21] = {
    [ 0] = { 1, 0, { CH_C } },
    [ 1] = { 2, 0, { CH_L, CH_R } },
    [ 2] = { 2, 1, { CH_L, CH_R, CH_S } },
    [ 3] = { 2, 2, { CH_L, CH_R, CH_LS, CH_RS } },
    [ 4] = { 2, 1, { CH_L, CH_R, CH_LFE } },
    [ 5] = { 2, 2, { CH_L, CH_R, CH_LFE, CH_S } },
    [ 6] = { 2, 3, { CH_L, CH_R, CH_LFE, CH_LS, CH_RS } },
    [ 7] = { 2, 1, { CH_L, CH_R, CH_C } },
    [ 8] = { 2, 2, { CH_L, CH_R, CH_C, CH_S } },
    [ 9] = { 2, 3, { CH_L, CH_R, CH_C, CH_LS, CH_RS } },
    [10] = { 2, 2, { CH_L, CH_R, CH_C, CH_LFE } },
    [11] = { 2, 3, { CH_L, CH_R, CH_C, CH_LFE, CH_S } },
    [12] = { 2, 4, { CH_L, CH_R, CH_C, CH_LFE, CH_LS, CH_RS } },
    [13] = { 3, 1, { CH_L, CH_R, CH_C, CH_S } },
    [14] = { 3, 2, { CH_L, CH_R, CH_C, CH_LS, CH_RS } },
    [15] = { 3, 1, { CH_L, CH_R, CH_C, CH_LFE } },
    [16] = { 3, 2, { CH_L, CH_R, CH_C, CH_LFE, CH_S } },
    [17] = { 3, 3, { CH_L, CH_R, CH_C, CH_LFE, CH_LS, CH_RS } },
    [18] = { 4, 1, { CH_L, CH_R, CH_LS, CH_RS, CH_LFE } },
    [19] = { 4, 1, { CH_L, CH_R, CH_LS, CH_RS, CH_C } },
    [20] = { 4, 2, { CH_L, CH_R, CH_LS, CH_RS, CH_C, CH_LFE } },
};

static av_cold int pcm_dvda_decode_init(AVCodecContext *avctx)
{
    PCMDVDAContext *s = avctx->priv_data;

    /* Invalid header to force parsing of the first header */
    s->last_header = -1;

    return 0;
}

static int pcm_dvda_parse_header(AVCodecContext *avctx,
                                 const PCMDVDAHeader *header)
{
    PCMDVDAContext *s = avctx->priv_data;
    uint32_t header_int = header->quantization |
                          header->sampling_frequency << 8 |
                          header->channel_assignment << 16;
    int assignment = header->channel_assignment & 0x1f;
    int bits[2], rate[2];
    uint64_t mask = 0;

    /* early exit if the header didn't change */
    if (s->last_header == header_int)
        return 0;
    s->last_header = -1;

    if (avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(avctx, AV_LOG_DEBUG, "pcm_dvda_parse_header: header = %02x%02x%02x\n",
               header->quantization, header->sampling_frequency,
               header->channel_assignment);

    if (assignment > 20) {
        av_log(avctx, AV_LOG_ERROR, "invalid channel group assignment %d\n",
               assignment);
        return AVERROR_INVALIDDATA;
    }

    s->group_channels[0] = channel_assignments[assignment].group1_channels;
    s->group_channels[1] = channel_assignments[assignment].group2_channels;

    for (int i = 0; i < 2; i++) {
        int quant = i ? header->quantization       & 0xf : header->quantization       >> 4;
        int freq  = i ? header->sampling_frequency & 0xf : header->sampling_frequency >> 4;

        /* 0xf marks an absent channel group */
        if (i && (quant == 0xf || freq == 0xf))
            s->group_channels[1] = 0;
        if (!s->group_channels[i]) {
            bits[i] = rate[i] = 0;
            continue;
        }

        if (quant > 2 || (freq & 7) > 2) {
            av_log(avctx, AV_LOG_ERROR,
                   "invalid group %d quantization %#x or sample rate %#x\n",
                   i + 1, quant, freq);
            return AVERROR_INVALIDDATA;
        }
        bits[i] = 16 + 4 * quant;
        rate[i] = (freq & 8 ? 44100 : 48000) << (freq & 7);

        if (bits[i] == 20) {
            avpriv_request_sample(avctx, "20-bit group %d quantization", i + 1);
            return AVERROR_PATCHWELCOME;
        }
    }

    if (s->group_channels[1] && rate[1] != rate[0]) {
        avpriv_request_sample(avctx, "Mixed group sample rates (%d, %d)",
                              rate[0], rate[1]);
        return AVERROR_PATCHWELCOME;
    }

    s->group_bits[0] = bits[0];
    s->group_bits[1] = bits[1];
    s->channels      = s->group_channels[0] + s->group_channels[1];
    s->block_size    = 2 * (s->group_channels[0] * bits[0] +
                            s->group_channels[1] * bits[1]) / 8;

    /* map the stream channel order to the native layout order */
    for (int i = 0; i < s->channels; i++)
        mask |= channel_assignments[assignment].channels[i];
    for (int i = 0; i < s->channels; i++) {
        uint64_t ch = channel_assignments[assignment].channels[i];
        int pos = av_popcount64(mask & (ch - 1));
        if (i < s->group_channels[0])
            s->group_map[0][i] = pos;
        else
            s->group_map[1][i - s->group_channels[0]] = pos;
    }

    avctx->sample_fmt = FFMAX(bits[0], bits[1]) == 16 ? AV_SAMPLE_FMT_S16
                                                      : AV_SAMPLE_FMT_S32;
    avctx->bits_per_raw_sample = FFMAX(bits[0], bits[1]);
    avctx->sample_rate = rate[0];
    av_channel_layout_uninit(&avctx->ch_layout);
    av_channel_layout_from_mask(&avctx->ch_layout, mask);
    avctx->bit_rate = (int64_t)s->block_size * rate[0] * 8 / 2;

    if (avctx->debug & FF_DEBUG_PICT_INFO)
        ff_dlog(avctx,
                "pcm_dvda_parse_header: %d channels, %d+%d bits per sample, "
                "%d Hz, %"PRId64" bit/s\n",
                s->channels, bits[0], bits[1], avctx->sample_rate,
                avctx->bit_rate);

    s->last_header = header_int;

    return 0;
}

/*
 * The audio data is arranged in sets of 2 samples per channel ("two-pair
 * samples", US 6,580,671 FIG. 1B): each channel group contributes the
 * 16-bit main words of all its channels for both samples, then, for 24-bit
 * quantization, one low-order extra byte per channel and sample in the
 * same order (FIG. 4B).
 */
static void pcm_dvda_decode_samples(AVCodecContext *avctx, GetByteContext *gb,
                                    void *dst, int blocks)
{
    PCMDVDAContext *s = avctx->priv_data;
    int16_t *dst16    = dst;
    int32_t *dst32    = dst;

    while (blocks--) {
        /* the patent leaves the order of the groups within a set open
         * (col. 16); discs store the second channel group first */
        for (int g = 1; g >= 0; g--) {
            const int ch          = s->group_channels[g];
            const int bits        = s->group_bits[g];
            const uint8_t *map    = s->group_map[g];

            if (!ch)
                continue;

            for (int n = 0; n < 2; n++) {
                for (int j = 0; j < ch; j++) {
                    unsigned v = bytestream2_get_be16u(gb);
                    if (avctx->sample_fmt == AV_SAMPLE_FMT_S16)
                        dst16[n * s->channels + map[j]] = v;
                    else
                        dst32[n * s->channels + map[j]] = v << 16;
                }
            }
            if (bits == 24) {
                for (int n = 0; n < 2; n++)
                    for (int j = 0; j < ch; j++)
                        dst32[n * s->channels + map[j]] |=
                            bytestream2_get_byteu(gb) << 8;
            }
        }
        dst16 += 2 * s->channels;
        dst32 += 2 * s->channels;
    }
}

static int pcm_dvda_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                                 int *got_frame_ptr, AVPacket *avpkt)
{
    const PCMDVDAHeader *header = (const PCMDVDAHeader *)avpkt->data;
    PCMDVDAContext *s  = avctx->priv_data;
    int buf_size       = avpkt->size;
    GetByteContext gb;
    int header_size;
    int retval;
    int blocks;

    if (buf_size < sizeof(*header)) {
        av_log(avctx, AV_LOG_ERROR, "PCM packet too small\n");
        return AVERROR_INVALIDDATA;
    }

    /* the private header length counts the bytes following its own field,
     * including the stuffing bytes that precede the audio data */
    header_size = offsetof(PCMDVDAHeader, first_access_unit_pointer) +
                  header->private_header_length;
    if (header_size < sizeof(*header) || header_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "invalid PCM header size %d\n",
               header_size);
        return AVERROR_INVALIDDATA;
    }

    if ((retval = pcm_dvda_parse_header(avctx, header)))
        return retval;

    buf_size -= header_size;
    blocks    = buf_size / s->block_size;
    if (buf_size % s->block_size)
        av_log(avctx, AV_LOG_DEBUG, "ignoring %d leftover bytes\n",
               buf_size % s->block_size);
    if (!blocks) {
        *got_frame_ptr = 0;
        return avpkt->size;
    }

    /* get output buffer */
    frame->nb_samples = blocks * 2;
    if ((retval = ff_get_buffer(avctx, frame, 0)) < 0)
        return retval;

    bytestream2_init(&gb, avpkt->data + header_size, blocks * s->block_size);
    pcm_dvda_decode_samples(avctx, &gb, frame->data[0], blocks);

    *got_frame_ptr = 1;

    return avpkt->size;
}

const FFCodec ff_pcm_dvda_decoder = {
    .p.name         = "pcm_dvda",
    CODEC_LONG_NAME("PCM signed 16|20|24-bit big-endian for DVD-Audio media"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_PCM_DVDA,
    .priv_data_size = sizeof(PCMDVDAContext),
    .init           = pcm_dvda_decode_init,
    FF_CODEC_DECODE_CB(pcm_dvda_decode_frame),
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
};
