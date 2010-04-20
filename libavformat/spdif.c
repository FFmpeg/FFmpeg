/*
 * IEC958 muxer
 * Copyright (c) 2009 Bartlomiej Wolowiec
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
 */

/*
 * Terminology used in specification:
 * data-burst - IEC958 frame, contains header and encapsuled frame
 * burst-preambule - IEC958 frame header, contains 16-bits words named Pa, Pb, Pc and Pd
 * burst-payload - encapsuled frame
 * Pa, Pb - syncword - 0xF872, 0x4E1F
 * Pc - burst-info, contains data-type (bits 0-6), error flag (bit 7), data-type-dependent info (bits 8-12)
 *      and bitstream number (bits 13-15)
 * data-type - determines type of encapsuled frames
 * Pd - length code (number of bits or bytes of encapsuled frame - according to data_type)
 *
 * IEC958 frames at normal usage start every specific count of bytes,
 *      dependent from data-type (spaces between packets are filled by zeros)
 */

#include "avformat.h"
#include "libavcodec/ac3.h"
#include "libavcodec/dca.h"
#include "libavcodec/aac_parser.h"

#define SYNCWORD1 0xF872
#define SYNCWORD2 0x4E1F
#define BURST_HEADER_SIZE 0x8

enum IEC958DataType {
    IEC958_AC3                = 0x01,          ///< AC-3 data
    IEC958_MPEG1_LAYER1       = 0x04,          ///< MPEG-1 layer 1
    IEC958_MPEG1_LAYER23      = 0x05,          ///< MPEG-1 layer 2 or 3 data or MPEG-2 without extension
    IEC958_MPEG2_EXT          = 0x06,          ///< MPEG-2 data with extension
    IEC958_MPEG2_AAC          = 0x07,          ///< MPEG-2 AAC ADTS
    IEC958_MPEG2_LAYER1_LSF   = 0x08,          ///< MPEG-2, layer-1 low sampling frequency
    IEC958_MPEG2_LAYER2_LSF   = 0x09,          ///< MPEG-2, layer-2 low sampling frequency
    IEC958_MPEG2_LAYER3_LSF   = 0x0A,          ///< MPEG-2, layer-3 low sampling frequency
    IEC958_DTS1               = 0x0B,          ///< DTS type I   (512 samples)
    IEC958_DTS2               = 0x0C,          ///< DTS type II  (1024 samples)
    IEC958_DTS3               = 0x0D,          ///< DTS type III (2048 samples)
    IEC958_MPEG2_AAC_LSF_2048 = 0x13,          ///< MPEG-2 AAC ADTS half-rate low sampling frequency
    IEC958_MPEG2_AAC_LSF_4096 = 0x13 | 0x20,   ///< MPEG-2 AAC ADTS quarter-rate low sampling frequency
};

typedef struct IEC958Context {
    enum IEC958DataType data_type;  ///< burst info - reference to type of payload of the data-burst
    int pkt_size;                   ///< length code in bits
    int pkt_offset;                 ///< data burst repetition period in bytes
    uint8_t *buffer;                ///< allocated buffer, used for swap bytes
    int buffer_size;                ///< size of allocated buffer

    /// function, which generates codec dependent header information.
    /// Sets data_type and data_offset
    int (*header_info) (AVFormatContext *s, AVPacket *pkt);
} IEC958Context;

//TODO move to DSP
static void bswap_buf16(uint16_t *dst, const uint16_t *src, int w)
{
    int i;

    for (i = 0; i + 8 <= w; i += 8) {
        dst[i + 0] = bswap_16(src[i + 0]);
        dst[i + 1] = bswap_16(src[i + 1]);
        dst[i + 2] = bswap_16(src[i + 2]);
        dst[i + 3] = bswap_16(src[i + 3]);
        dst[i + 4] = bswap_16(src[i + 4]);
        dst[i + 5] = bswap_16(src[i + 5]);
        dst[i + 6] = bswap_16(src[i + 6]);
        dst[i + 7] = bswap_16(src[i + 7]);
    }
    for (; i < w; i++)
        dst[i + 0] = bswap_16(src[i + 0]);
}

static int spdif_header_ac3(AVFormatContext *s, AVPacket *pkt)
{
    IEC958Context *ctx = s->priv_data;
    int bitstream_mode = pkt->data[6] & 0x7;

    ctx->data_type  = IEC958_AC3 | (bitstream_mode << 8);
    ctx->pkt_offset = AC3_FRAME_SIZE << 2;
    return 0;
}

static int spdif_header_dts(AVFormatContext *s, AVPacket *pkt)
{
    IEC958Context *ctx = s->priv_data;
    uint32_t syncword_dts = AV_RB32(pkt->data);
    int blocks;

    switch (syncword_dts) {
    case DCA_MARKER_RAW_BE:
        blocks = (AV_RB16(pkt->data + 4) >> 2) & 0x7f;
        break;
    case DCA_MARKER_RAW_LE:
        blocks = (AV_RL16(pkt->data + 4) >> 2) & 0x7f;
        break;
    case DCA_MARKER_14B_BE:
        blocks =
            (((pkt->data[5] & 0x07) << 4) | ((pkt->data[6] & 0x3f) >> 2));
        break;
    case DCA_MARKER_14B_LE:
        blocks =
            (((pkt->data[4] & 0x07) << 4) | ((pkt->data[7] & 0x3f) >> 2));
        break;
    default:
        av_log(s, AV_LOG_ERROR, "bad DTS syncword 0x%x\n", syncword_dts);
        return -1;
    }
    blocks++;
    switch (blocks) {
    case  512 >> 5: ctx->data_type = IEC958_DTS1; break;
    case 1024 >> 5: ctx->data_type = IEC958_DTS2; break;
    case 2048 >> 5: ctx->data_type = IEC958_DTS3; break;
    default:
        av_log(s, AV_LOG_ERROR, "%i samples in DTS frame not supported\n",
               blocks << 5);
        return -1;
    }
    ctx->pkt_offset = blocks << 7;

    return 0;
}

static const enum IEC958DataType mpeg_data_type[2][3] = {
    //     LAYER1                      LAYER2                  LAYER3
    { IEC958_MPEG2_LAYER1_LSF, IEC958_MPEG2_LAYER2_LSF, IEC958_MPEG2_LAYER3_LSF },  //MPEG2 LSF
    { IEC958_MPEG1_LAYER1,     IEC958_MPEG1_LAYER23,    IEC958_MPEG1_LAYER23 },     //MPEG1
};

static const uint16_t mpeg_pkt_offset[2][3] = {
    //LAYER1  LAYER2  LAYER3
    { 3072,    9216,   4608 }, // MPEG2 LSF
    { 1536,    4608,   4608 }, // MPEG1
};

static int spdif_header_mpeg(AVFormatContext *s, AVPacket *pkt)
{
    IEC958Context *ctx = s->priv_data;
    int version =      (pkt->data[1] >> 3) & 3;
    int layer   = 3 - ((pkt->data[1] >> 1) & 3);
    int extension = pkt->data[2] & 1;

    if (layer == 3 || version == 1) {
        av_log(s, AV_LOG_ERROR, "Wrong MPEG file format\n");
        return -1;
    }
    av_log(s, AV_LOG_DEBUG, "version: %i layer: %i extension: %i\n", version, layer, extension);
    if (version == 2 && extension) {
        ctx->data_type  = IEC958_MPEG2_EXT;
        ctx->pkt_offset = 4608;
    } else {
        ctx->data_type  = mpeg_data_type [version & 1][layer];
        ctx->pkt_offset = mpeg_pkt_offset[version & 1][layer];
    }
    // TODO Data type dependant info (normal/karaoke, dynamic range control)
    return 0;
}

static int spdif_header_aac(AVFormatContext *s, AVPacket *pkt)
{
    IEC958Context *ctx = s->priv_data;
    AACADTSHeaderInfo hdr;
    GetBitContext gbc;
    int ret;

    init_get_bits(&gbc, pkt->data, AAC_ADTS_HEADER_SIZE * 8);
    ret = ff_aac_parse_header(&gbc, &hdr);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Wrong AAC file format\n");
        return -1;
    }

    ctx->pkt_offset = hdr.samples << 2;
    switch (hdr.num_aac_frames) {
    case 1:
        ctx->data_type = IEC958_MPEG2_AAC;
        break;
    case 2:
        ctx->data_type = IEC958_MPEG2_AAC_LSF_2048;
        break;
    case 4:
        ctx->data_type = IEC958_MPEG2_AAC_LSF_4096;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "%i samples in AAC frame not supported\n",
               hdr.samples);
        return -1;
    }
    //TODO Data type dependent info (LC profile/SBR)
    return 0;
}

static int spdif_write_header(AVFormatContext *s)
{
    IEC958Context *ctx = s->priv_data;

    switch (s->streams[0]->codec->codec_id) {
    case CODEC_ID_AC3:
        ctx->header_info = spdif_header_ac3;
        break;
    case CODEC_ID_MP1:
    case CODEC_ID_MP2:
    case CODEC_ID_MP3:
        ctx->header_info = spdif_header_mpeg;
        break;
    case CODEC_ID_DTS:
        ctx->header_info = spdif_header_dts;
        break;
    case CODEC_ID_AAC:
        ctx->header_info = spdif_header_aac;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "codec not supported\n");
        return -1;
    }
    return 0;
}

static int spdif_write_trailer(AVFormatContext *s)
{
    IEC958Context *ctx = s->priv_data;
    av_freep(&ctx->buffer);
    return 0;
}

static int spdif_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    IEC958Context *ctx = s->priv_data;
    int ret, padding;

    ctx->pkt_size = FFALIGN(pkt->size, 2) << 3;
    ret = ctx->header_info(s, pkt);
    if (ret < 0)
        return -1;

    padding = (ctx->pkt_offset - BURST_HEADER_SIZE - pkt->size) >> 1;
    if (padding < 0) {
        av_log(s, AV_LOG_ERROR, "bitrate is too high\n");
        return -1;
    }

    put_le16(s->pb, SYNCWORD1);      //Pa
    put_le16(s->pb, SYNCWORD2);      //Pb
    put_le16(s->pb, ctx->data_type); //Pc
    put_le16(s->pb, ctx->pkt_size);  //Pd

#if HAVE_BIGENDIAN
    put_buffer(s->pb, pkt->data, pkt->size & ~1);
#else
    av_fast_malloc(&ctx->buffer, &ctx->buffer_size, pkt->size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!ctx->buffer)
        return AVERROR(ENOMEM);
    bswap_buf16((uint16_t *)ctx->buffer, (uint16_t *)pkt->data, pkt->size >> 1);
    put_buffer(s->pb, ctx->buffer, pkt->size & ~1);
#endif

    if (pkt->size & 1)
        put_be16(s->pb, pkt->data[pkt->size - 1]);

    for (; padding > 0; padding--)
        put_be16(s->pb, 0);

    av_log(s, AV_LOG_DEBUG, "type=%x len=%i pkt_offset=%i\n",
           ctx->data_type, pkt->size, ctx->pkt_offset);

    put_flush_packet(s->pb);
    return 0;
}

AVOutputFormat spdif_muxer = {
    "spdif",
    NULL_IF_CONFIG_SMALL("IEC958 - S/PDIF (IEC-61937)"),
    NULL,
    "spdif",
    sizeof(IEC958Context),
    CODEC_ID_AC3,
    CODEC_ID_NONE,
    spdif_write_header,
    spdif_write_packet,
    spdif_write_trailer,
};
