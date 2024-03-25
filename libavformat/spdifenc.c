/*
 * IEC 61937 muxer
 * Copyright (c) 2009 Bartlomiej Wolowiec
 * Copyright (c) 2010, 2020 Anssi Hannula
 * Copyright (c) 2010 Carl Eugen Hoyos
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
 * IEC-61937 encapsulation of various formats, used by S/PDIF
 * @author Bartlomiej Wolowiec
 * @author Anssi Hannula
 * @author Carl Eugen Hoyos
 */

/*
 * Terminology used in specification:
 * data-burst - IEC61937 frame, contains header and encapsuled frame
 * burst-preamble - IEC61937 frame header, contains 16-bit words named Pa, Pb, Pc and Pd
 * burst-payload - encapsuled frame
 * Pa, Pb - syncword - 0xF872, 0x4E1F
 * Pc - burst-info, contains data-type (bits 0-6), error flag (bit 7), data-type-dependent info (bits 8-12)
 *      and bitstream number (bits 13-15)
 * data-type - determines type of encapsuled frames
 * Pd - length code (number of bits or bytes of encapsuled frame - according to data_type)
 *
 * IEC 61937 frames at normal usage start every specific count of bytes,
 *      dependent from data-type (spaces between packets are filled by zeros)
 */

#include <inttypes.h>

#include "avformat.h"
#include "avio_internal.h"
#include "mux.h"
#include "spdif.h"
#include "libavcodec/ac3defs.h"
#include "libavcodec/adts_parser.h"
#include "libavcodec/dca.h"
#include "libavcodec/dca_syncwords.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

typedef struct IEC61937Context {
    const AVClass *av_class;
    enum IEC61937DataType data_type;///< burst info - reference to type of payload of the data-burst
    int length_code;                ///< length code in bits or bytes, depending on data type
    int pkt_offset;                 ///< data burst repetition period in bytes
    uint8_t *buffer;                ///< allocated buffer, used for swap bytes
    int buffer_size;                ///< size of allocated buffer

    const uint8_t *out_buf;         ///< pointer to the outgoing data before byte-swapping
    int out_bytes;                  ///< amount of outgoing bytes

    int use_preamble;               ///< preamble enabled (disabled for exactly pre-padded DTS)
    int extra_bswap;                ///< extra bswap for payload (for LE DTS => standard BE DTS)

    uint8_t *hd_buf[2];             ///< allocated buffers to concatenate hd audio frames
    int hd_buf_size;                ///< size of the hd audio buffer (eac3, dts4)
    int hd_buf_count;               ///< number of frames in the hd audio buffer (eac3)
    int hd_buf_filled;              ///< amount of bytes in the hd audio buffer (eac3, truehd)
    int hd_buf_idx;                 ///< active hd buffer index (truehd)

    int dtshd_skip;                 ///< counter used for skipping DTS-HD frames

    uint16_t truehd_prev_time;      ///< input_timing from the last frame
    int truehd_prev_size;           ///< previous frame size in bytes, including any MAT codes
    int truehd_samples_per_frame;   ///< samples per frame for padding calculation

    /* AVOptions: */
    int dtshd_rate;
    int dtshd_fallback;
#define SPDIF_FLAG_BIGENDIAN    0x01
    int spdif_flags;

    /// function, which generates codec dependent header information.
    /// Sets data_type and pkt_offset, and length_code, out_bytes, out_buf if necessary
    int (*header_info) (AVFormatContext *s, AVPacket *pkt);
} IEC61937Context;

static const AVOption options[] = {
{ "spdif_flags", "IEC 61937 encapsulation flags", offsetof(IEC61937Context, spdif_flags), AV_OPT_TYPE_FLAGS, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "spdif_flags" },
{ "be", "output in big-endian format (for use as s16be)", 0, AV_OPT_TYPE_CONST, {.i64 = SPDIF_FLAG_BIGENDIAN},  0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "spdif_flags" },
{ "dtshd_rate", "mux complete DTS frames in HD mode at the specified IEC958 rate (in Hz, default 0=disabled)", offsetof(IEC61937Context, dtshd_rate), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 768000, AV_OPT_FLAG_ENCODING_PARAM },
{ "dtshd_fallback_time", "min secs to strip HD for after an overflow (-1: till the end, default 60)", offsetof(IEC61937Context, dtshd_fallback), AV_OPT_TYPE_INT, {.i64 = 60}, -1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
{ NULL },
};

static const AVClass spdif_class = {
    .class_name     = "spdif",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

static int spdif_header_ac3(AVFormatContext *s, AVPacket *pkt)
{
    IEC61937Context *ctx = s->priv_data;
    int bitstream_mode = pkt->data[5] & 0x7;

    ctx->data_type  = IEC61937_AC3 | (bitstream_mode << 8);
    ctx->pkt_offset = AC3_FRAME_SIZE << 2;
    return 0;
}

static int spdif_header_eac3(AVFormatContext *s, AVPacket *pkt)
{
    IEC61937Context *ctx = s->priv_data;
    static const uint8_t eac3_repeat[4] = {6, 3, 2, 1};
    int repeat = 1;
    uint8_t *tmp;

    int bsid = pkt->data[5] >> 3;
    if (bsid > 10 && (pkt->data[4] & 0xc0) != 0xc0) /* fscod */
        repeat = eac3_repeat[(pkt->data[4] & 0x30) >> 4]; /* numblkscod */

    tmp = av_fast_realloc(ctx->hd_buf[0], &ctx->hd_buf_size, ctx->hd_buf_filled + pkt->size);
    if (!tmp)
        return AVERROR(ENOMEM);
    ctx->hd_buf[0] = tmp;

    memcpy(&ctx->hd_buf[0][ctx->hd_buf_filled], pkt->data, pkt->size);

    ctx->hd_buf_filled += pkt->size;
    if (++ctx->hd_buf_count < repeat){
        ctx->pkt_offset = 0;
        return 0;
    }
    ctx->data_type   = IEC61937_EAC3;
    ctx->pkt_offset  = 24576;
    ctx->out_buf     = ctx->hd_buf[0];
    ctx->out_bytes   = ctx->hd_buf_filled;
    ctx->length_code = ctx->hd_buf_filled;

    ctx->hd_buf_count  = 0;
    ctx->hd_buf_filled = 0;
    return 0;
}

/*
 * DTS type IV (DTS-HD) can be transmitted with various frame repetition
 * periods; longer repetition periods allow for longer packets and therefore
 * higher bitrate. Longer repetition periods mean that the constant bitrate of
 * the output IEC 61937 stream is higher.
 * The repetition period is measured in IEC 60958 frames (4 bytes).
 */
static int spdif_dts4_subtype(int period)
{
    switch (period) {
    case 512:   return 0x0;
    case 1024:  return 0x1;
    case 2048:  return 0x2;
    case 4096:  return 0x3;
    case 8192:  return 0x4;
    case 16384: return 0x5;
    }
    return -1;
}

static int spdif_header_dts4(AVFormatContext *s, AVPacket *pkt, int core_size,
                             int sample_rate, int blocks)
{
    IEC61937Context *ctx = s->priv_data;
    static const char dtshd_start_code[10] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xfe };
    int pkt_size = pkt->size;
    int period;
    int subtype;

    if (!core_size) {
        av_log(s, AV_LOG_ERROR, "HD mode not supported for this format\n");
        return AVERROR(EINVAL);
    }

    if (!sample_rate) {
        av_log(s, AV_LOG_ERROR, "Unknown DTS sample rate for HD\n");
        return AVERROR_INVALIDDATA;
    }

    period = ctx->dtshd_rate * (blocks << 5) / sample_rate;
    subtype = spdif_dts4_subtype(period);

    if (subtype < 0) {
        av_log(s, AV_LOG_ERROR, "Specified HD rate of %d Hz would require an "
               "impossible repetition period of %d for the current DTS stream"
               " (blocks = %d, sample rate = %d)\n", ctx->dtshd_rate, period,
               blocks << 5, sample_rate);
        return AVERROR(EINVAL);
    }

    /* set pkt_offset and DTS IV subtype according to the requested output
     * rate */
    ctx->pkt_offset = period * 4;
    ctx->data_type = IEC61937_DTSHD | subtype << 8;

    /* If the bitrate is too high for transmitting at the selected
     * repetition period setting, strip DTS-HD until a good amount
     * of consecutive non-overflowing HD frames have been observed.
     * This generally only happens if the caller is cramming a Master
     * Audio stream into 192kHz IEC 60958 (which may or may not fit). */
    if (sizeof(dtshd_start_code) + 2 + pkt_size
            > ctx->pkt_offset - BURST_HEADER_SIZE && core_size) {
        if (!ctx->dtshd_skip)
            av_log(s, AV_LOG_WARNING, "DTS-HD bitrate too high, "
                                      "temporarily sending core only\n");
        if (ctx->dtshd_fallback > 0)
            ctx->dtshd_skip = sample_rate * ctx->dtshd_fallback / (blocks << 5);
        else
            /* skip permanently (dtshd_fallback == -1) or just once
             * (dtshd_fallback == 0) */
            ctx->dtshd_skip = 1;
    }
    if (ctx->dtshd_skip && core_size) {
        pkt_size = core_size;
        if (ctx->dtshd_fallback >= 0)
            --ctx->dtshd_skip;
    }

    ctx->out_bytes   = sizeof(dtshd_start_code) + 2 + pkt_size;

    /* Align so that (length_code & 0xf) == 0x8. This is reportedly needed
     * with some receivers, but the exact requirement is unconfirmed. */
    ctx->length_code = FFALIGN(ctx->out_bytes + 0x8, 0x10) - 0x8;

    av_fast_malloc(&ctx->hd_buf[0], &ctx->hd_buf_size, ctx->out_bytes);
    if (!ctx->hd_buf[0])
        return AVERROR(ENOMEM);

    ctx->out_buf = ctx->hd_buf[0];

    memcpy(ctx->hd_buf[0], dtshd_start_code, sizeof(dtshd_start_code));
    AV_WB16(ctx->hd_buf[0] + sizeof(dtshd_start_code), pkt_size);
    memcpy(ctx->hd_buf[0] + sizeof(dtshd_start_code) + 2, pkt->data, pkt_size);

    return 0;
}

static int spdif_header_dts(AVFormatContext *s, AVPacket *pkt)
{
    IEC61937Context *ctx = s->priv_data;
    uint32_t syncword_dts = AV_RB32(pkt->data);
    int blocks;
    int sample_rate = 0;
    int core_size = 0;

    if (pkt->size < 9)
        return AVERROR_INVALIDDATA;

    switch (syncword_dts) {
    case DCA_SYNCWORD_CORE_BE:
        blocks = (AV_RB16(pkt->data + 4) >> 2) & 0x7f;
        core_size = ((AV_RB24(pkt->data + 5) >> 4) & 0x3fff) + 1;
        sample_rate = ff_dca_sample_rates[(pkt->data[8] >> 2) & 0x0f];
        break;
    case DCA_SYNCWORD_CORE_LE:
        blocks = (AV_RL16(pkt->data + 4) >> 2) & 0x7f;
        ctx->extra_bswap = 1;
        break;
    case DCA_SYNCWORD_CORE_14B_BE:
        blocks =
            (((pkt->data[5] & 0x07) << 4) | ((pkt->data[6] & 0x3f) >> 2));
        break;
    case DCA_SYNCWORD_CORE_14B_LE:
        blocks =
            (((pkt->data[4] & 0x07) << 4) | ((pkt->data[7] & 0x3f) >> 2));
        ctx->extra_bswap = 1;
        break;
    case DCA_SYNCWORD_SUBSTREAM:
        /* We only handle HD frames that are paired with core. However,
           sometimes DTS-HD streams with core have a stray HD frame without
           core in the beginning of the stream. */
        av_log(s, AV_LOG_ERROR, "stray DTS-HD frame\n");
        return AVERROR_INVALIDDATA;
    default:
        av_log(s, AV_LOG_ERROR, "bad DTS syncword 0x%"PRIx32"\n", syncword_dts);
        return AVERROR_INVALIDDATA;
    }
    blocks++;

    if (ctx->dtshd_rate)
        /* DTS type IV output requested */
        return spdif_header_dts4(s, pkt, core_size, sample_rate, blocks);

    switch (blocks) {
    case  512 >> 5: ctx->data_type = IEC61937_DTS1; break;
    case 1024 >> 5: ctx->data_type = IEC61937_DTS2; break;
    case 2048 >> 5: ctx->data_type = IEC61937_DTS3; break;
    default:
        av_log(s, AV_LOG_ERROR, "%i samples in DTS frame not supported\n",
               blocks << 5);
        return AVERROR(ENOSYS);
    }

    /* discard extraneous data by default */
    if (core_size && core_size < pkt->size) {
        ctx->out_bytes = core_size;
        ctx->length_code = core_size << 3;
    }

    ctx->pkt_offset = blocks << 7;

    if (ctx->out_bytes == ctx->pkt_offset) {
        /* The DTS stream fits exactly into the output stream, so skip the
         * preamble as it would not fit in there. This is the case for dts
         * discs and dts-in-wav. */
        ctx->use_preamble = 0;
    } else if (ctx->out_bytes > ctx->pkt_offset - BURST_HEADER_SIZE) {
        avpriv_request_sample(s, "Unrecognized large DTS frame");
        /* This will fail with a "bitrate too high" in the caller */
    }

    return 0;
}

static const enum IEC61937DataType mpeg_data_type[2][3] = {
    //     LAYER1                      LAYER2                  LAYER3
    { IEC61937_MPEG2_LAYER1_LSF, IEC61937_MPEG2_LAYER2_LSF, IEC61937_MPEG2_LAYER3_LSF }, // MPEG-2 LSF
    { IEC61937_MPEG1_LAYER1,     IEC61937_MPEG1_LAYER23,    IEC61937_MPEG1_LAYER23 },    // MPEG-1
};

static int spdif_header_mpeg(AVFormatContext *s, AVPacket *pkt)
{
    IEC61937Context *ctx = s->priv_data;
    int version =      (pkt->data[1] >> 3) & 3;
    int layer   = 3 - ((pkt->data[1] >> 1) & 3);
    int extension = pkt->data[2] & 1;

    if (layer == 3 || version == 1) {
        av_log(s, AV_LOG_ERROR, "Wrong MPEG file format\n");
        return AVERROR_INVALIDDATA;
    }
    av_log(s, AV_LOG_DEBUG, "version: %i layer: %i extension: %i\n", version, layer, extension);
    if (version == 2 && extension) {
        ctx->data_type  = IEC61937_MPEG2_EXT;
        ctx->pkt_offset = 4608;
    } else {
        ctx->data_type  = mpeg_data_type [version & 1][layer];
        ctx->pkt_offset = spdif_mpeg_pkt_offset[version & 1][layer];
    }
    // TODO Data type dependent info (normal/karaoke, dynamic range control)
    return 0;
}

static int spdif_header_aac(AVFormatContext *s, AVPacket *pkt)
{
    IEC61937Context *ctx = s->priv_data;
    uint32_t samples;
    uint8_t frames;
    int ret;

    ret = av_adts_header_parse(pkt->data, &samples, &frames);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Wrong AAC file format\n");
        return ret;
    }

    ctx->pkt_offset = samples << 2;
    switch (frames) {
    case 1:
        ctx->data_type = IEC61937_MPEG2_AAC;
        break;
    case 2:
        ctx->data_type = IEC61937_MPEG2_AAC_LSF_2048;
        break;
    case 4:
        ctx->data_type = IEC61937_MPEG2_AAC_LSF_4096;
        break;
    default:
        av_log(s, AV_LOG_ERROR,
               "%"PRIu32" samples in AAC frame not supported\n", samples);
        return AVERROR(EINVAL);
    }
    //TODO Data type dependent info (LC profile/SBR)
    return 0;
}


/*
 * It seems Dolby TrueHD frames have to be encapsulated in MAT frames before
 * they can be encapsulated in IEC 61937.
 */
#define MAT_PKT_OFFSET          61440
#define MAT_FRAME_SIZE          61424

static const uint8_t mat_start_code[20] = {
    0x07, 0x9E, 0x00, 0x03, 0x84, 0x01, 0x01, 0x01, 0x80, 0x00, 0x56, 0xA5, 0x3B, 0xF4, 0x81, 0x83,
    0x49, 0x80, 0x77, 0xE0,
};
static const uint8_t mat_middle_code[12] = {
    0xC3, 0xC1, 0x42, 0x49, 0x3B, 0xFA, 0x82, 0x83, 0x49, 0x80, 0x77, 0xE0,
};
static const uint8_t mat_end_code[16] = {
    0xC3, 0xC2, 0xC0, 0xC4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x97, 0x11,
};

#define MAT_CODE(position, data) { .pos = position, .code = data, .len = sizeof(data) }

static const struct {
    unsigned int pos;
    unsigned int len;
    const uint8_t *code;
} mat_codes[] = {
    MAT_CODE(0, mat_start_code),
    MAT_CODE(30708, mat_middle_code),
    MAT_CODE(MAT_FRAME_SIZE - sizeof(mat_end_code), mat_end_code),
};

static int spdif_header_truehd(AVFormatContext *s, AVPacket *pkt)
{
    IEC61937Context *ctx = s->priv_data;
    uint8_t *hd_buf = ctx->hd_buf[ctx->hd_buf_idx];
    int ratebits;
    int padding_remaining = 0;
    uint16_t input_timing;
    int total_frame_size = pkt->size;
    const uint8_t *dataptr = pkt->data;
    int data_remaining = pkt->size;
    int have_pkt = 0;
    int next_code_idx;

    if (pkt->size < 10)
        return AVERROR_INVALIDDATA;

    if (AV_RB24(pkt->data + 4) == 0xf8726f) {
        /* major sync unit, fetch sample rate */
        if (pkt->data[7] == 0xba)
            ratebits = pkt->data[8] >> 4;
        else if (pkt->data[7] == 0xbb)
            ratebits = pkt->data[9] >> 4;
        else
            return AVERROR_INVALIDDATA;

        ctx->truehd_samples_per_frame = 40 << (ratebits & 3);
        av_log(s, AV_LOG_TRACE, "TrueHD samples per frame: %d\n",
               ctx->truehd_samples_per_frame);
    }

    if (!ctx->truehd_samples_per_frame)
        return AVERROR_INVALIDDATA;

    input_timing = AV_RB16(pkt->data + 2);
    if (ctx->truehd_prev_size) {
        uint16_t delta_samples = input_timing - ctx->truehd_prev_time;
        /*
         * One multiple-of-48kHz frame is 1/1200 sec and the IEC 61937 rate
         * is 768kHz = 768000*4 bytes/sec.
         * The nominal space per frame is therefore
         * (768000*4 bytes/sec) * (1/1200 sec) = 2560 bytes.
         * For multiple-of-44.1kHz frames: 1/1102.5 sec, 705.6kHz, 2560 bytes.
         *
         * 2560 is divisible by truehd_samples_per_frame.
         */
        int delta_bytes = delta_samples * 2560 / ctx->truehd_samples_per_frame;

        /* padding needed before this frame */
        padding_remaining = delta_bytes - ctx->truehd_prev_size;

        av_log(s, AV_LOG_TRACE, "delta_samples: %"PRIu16", delta_bytes: %d\n",
               delta_samples, delta_bytes);

        /* sanity check */
        if (padding_remaining < 0 || padding_remaining >= MAT_FRAME_SIZE / 2) {
            avpriv_request_sample(s, "Unusual frame timing: %"PRIu16" => %"PRIu16", %d samples/frame",
                                  ctx->truehd_prev_time, input_timing, ctx->truehd_samples_per_frame);
            padding_remaining = 0;
        }
    }

    for (next_code_idx = 0; next_code_idx < FF_ARRAY_ELEMS(mat_codes); next_code_idx++)
        if (ctx->hd_buf_filled <= mat_codes[next_code_idx].pos)
            break;

    if (next_code_idx >= FF_ARRAY_ELEMS(mat_codes))
        return AVERROR_BUG;

    while (padding_remaining || data_remaining ||
           mat_codes[next_code_idx].pos == ctx->hd_buf_filled) {

        if (mat_codes[next_code_idx].pos == ctx->hd_buf_filled) {
            /* time to insert MAT code */
            int code_len = mat_codes[next_code_idx].len;
            int code_len_remaining = code_len;
            memcpy(hd_buf + mat_codes[next_code_idx].pos,
                   mat_codes[next_code_idx].code, code_len);
            ctx->hd_buf_filled += code_len;

            next_code_idx++;
            if (next_code_idx == FF_ARRAY_ELEMS(mat_codes)) {
                next_code_idx = 0;

                /* this was the last code, move to the next MAT frame */
                have_pkt = 1;
                ctx->out_buf = hd_buf;
                ctx->hd_buf_idx ^= 1;
                hd_buf = ctx->hd_buf[ctx->hd_buf_idx];
                ctx->hd_buf_filled = 0;

                /* inter-frame gap has to be counted as well, add it */
                code_len_remaining += MAT_PKT_OFFSET - MAT_FRAME_SIZE;
            }

            if (padding_remaining) {
                /* consider the MAT code as padding */
                int counted_as_padding = FFMIN(padding_remaining,
                                               code_len_remaining);
                padding_remaining -= counted_as_padding;
                code_len_remaining -= counted_as_padding;
            }
            /* count the remainder of the code as part of frame size */
            if (code_len_remaining)
                total_frame_size += code_len_remaining;
        }

        if (padding_remaining) {
            int padding_to_insert = FFMIN(mat_codes[next_code_idx].pos - ctx->hd_buf_filled,
                                          padding_remaining);

            memset(hd_buf + ctx->hd_buf_filled, 0, padding_to_insert);
            ctx->hd_buf_filled += padding_to_insert;
            padding_remaining -= padding_to_insert;

            if (padding_remaining)
                continue; /* time to insert MAT code */
        }

        if (data_remaining) {
            int data_to_insert = FFMIN(mat_codes[next_code_idx].pos - ctx->hd_buf_filled,
                                       data_remaining);

            memcpy(hd_buf + ctx->hd_buf_filled, dataptr, data_to_insert);
            ctx->hd_buf_filled += data_to_insert;
            dataptr += data_to_insert;
            data_remaining -= data_to_insert;
        }
    }

    ctx->truehd_prev_size = total_frame_size;
    ctx->truehd_prev_time = input_timing;

    av_log(s, AV_LOG_TRACE, "TrueHD frame inserted, total size %d, buffer position %d\n",
           total_frame_size, ctx->hd_buf_filled);

    if (!have_pkt) {
        ctx->pkt_offset = 0;
        return 0;
    }

    ctx->data_type   = IEC61937_TRUEHD;
    ctx->pkt_offset  = MAT_PKT_OFFSET;
    ctx->out_bytes   = MAT_FRAME_SIZE;
    ctx->length_code = MAT_FRAME_SIZE;
    return 0;
}

static int spdif_write_header(AVFormatContext *s)
{
    IEC61937Context *ctx = s->priv_data;

    switch (s->streams[0]->codecpar->codec_id) {
    case AV_CODEC_ID_AC3:
        ctx->header_info = spdif_header_ac3;
        break;
    case AV_CODEC_ID_EAC3:
        ctx->header_info = spdif_header_eac3;
        break;
    case AV_CODEC_ID_MP1:
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
        ctx->header_info = spdif_header_mpeg;
        break;
    case AV_CODEC_ID_DTS:
        ctx->header_info = spdif_header_dts;
        break;
    case AV_CODEC_ID_AAC:
        ctx->header_info = spdif_header_aac;
        break;
    case AV_CODEC_ID_TRUEHD:
    case AV_CODEC_ID_MLP:
        ctx->header_info = spdif_header_truehd;
        for (int i = 0; i < FF_ARRAY_ELEMS(ctx->hd_buf); i++) {
            ctx->hd_buf[i] = av_malloc(MAT_FRAME_SIZE);
            if (!ctx->hd_buf[i])
                return AVERROR(ENOMEM);
        }
        break;
    default:
        avpriv_report_missing_feature(s, "Codec %d",
                                      s->streams[0]->codecpar->codec_id);
        return AVERROR_PATCHWELCOME;
    }
    return 0;
}

static void spdif_deinit(AVFormatContext *s)
{
    IEC61937Context *ctx = s->priv_data;
    av_freep(&ctx->buffer);
    for (int i = 0; i < FF_ARRAY_ELEMS(ctx->hd_buf); i++)
        av_freep(&ctx->hd_buf[i]);
}

static av_always_inline void spdif_put_16(IEC61937Context *ctx,
                                          AVIOContext *pb, unsigned int val)
{
    if (ctx->spdif_flags & SPDIF_FLAG_BIGENDIAN)
        avio_wb16(pb, val);
    else
        avio_wl16(pb, val);
}

static int spdif_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    IEC61937Context *ctx = s->priv_data;
    int ret, padding;

    ctx->out_buf = pkt->data;
    ctx->out_bytes = pkt->size;
    ctx->length_code = FFALIGN(pkt->size, 2) << 3;
    ctx->use_preamble = 1;
    ctx->extra_bswap = 0;

    ret = ctx->header_info(s, pkt);
    if (ret < 0)
        return ret;
    if (!ctx->pkt_offset)
        return 0;

    padding = (ctx->pkt_offset - ctx->use_preamble * BURST_HEADER_SIZE - ctx->out_bytes) & ~1;
    if (padding < 0) {
        av_log(s, AV_LOG_ERROR, "bitrate is too high\n");
        return AVERROR(EINVAL);
    }

    if (ctx->use_preamble) {
        spdif_put_16(ctx, s->pb, SYNCWORD1);       //Pa
        spdif_put_16(ctx, s->pb, SYNCWORD2);       //Pb
        spdif_put_16(ctx, s->pb, ctx->data_type);  //Pc
        spdif_put_16(ctx, s->pb, ctx->length_code);//Pd
    }

    if (ctx->extra_bswap ^ (ctx->spdif_flags & SPDIF_FLAG_BIGENDIAN)) {
        avio_write(s->pb, ctx->out_buf, ctx->out_bytes & ~1);
    } else {
        av_fast_malloc(&ctx->buffer, &ctx->buffer_size, ctx->out_bytes + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!ctx->buffer)
            return AVERROR(ENOMEM);
        ff_spdif_bswap_buf16((uint16_t *)ctx->buffer, (const uint16_t *)ctx->out_buf, ctx->out_bytes >> 1);
        avio_write(s->pb, ctx->buffer, ctx->out_bytes & ~1);
    }

    /* a final lone byte has to be MSB aligned */
    if (ctx->out_bytes & 1)
        spdif_put_16(ctx, s->pb, ctx->out_buf[ctx->out_bytes - 1] << 8);

    ffio_fill(s->pb, 0, padding);

    av_log(s, AV_LOG_DEBUG, "type=%x len=%i pkt_offset=%i\n",
           ctx->data_type, ctx->out_bytes, ctx->pkt_offset);

    return 0;
}

const FFOutputFormat ff_spdif_muxer = {
    .p.name            = "spdif",
    .p.long_name       = NULL_IF_CONFIG_SMALL("IEC 61937 (used on S/PDIF - IEC958)"),
    .p.extensions      = "spdif",
    .priv_data_size    = sizeof(IEC61937Context),
    .p.audio_codec     = AV_CODEC_ID_AC3,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .p.subtitle_codec  = AV_CODEC_ID_NONE,
    .write_header      = spdif_write_header,
    .write_packet      = spdif_write_packet,
    .deinit            = spdif_deinit,
    .p.flags           = AVFMT_NOTIMESTAMPS,
    .p.priv_class      = &spdif_class,
    .flags_internal    = FF_OFMT_FLAG_MAX_ONE_OF_EACH,
};
