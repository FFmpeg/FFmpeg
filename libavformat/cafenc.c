/*
 * Core Audio Format muxer
 * Copyright (c) 2011 Carl Eugen Hoyos
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

#include "avformat.h"
#include "caf.h"
#include "riff.h"
#include "isom.h"
#include "avio_internal.h"

typedef struct {
    int64_t data;
} CAFContext;

static uint32_t codec_flags(enum CodecID codec_id) {
    switch (codec_id) {
    case CODEC_ID_PCM_F32BE:
    case CODEC_ID_PCM_F64BE:
        return 1; //< kCAFLinearPCMFormatFlagIsFloat
    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_S24LE:
    case CODEC_ID_PCM_S32LE:
        return 2; //< kCAFLinearPCMFormatFlagIsLittleEndian
    case CODEC_ID_PCM_F32LE:
    case CODEC_ID_PCM_F64LE:
        return 3; //< kCAFLinearPCMFormatFlagIsFloat | kCAFLinearPCMFormatFlagIsLittleEndian
    default:
        return 0;
    }
}

static uint32_t samples_per_packet(enum CodecID codec_id) {
    switch (codec_id) {
    case CODEC_ID_PCM_S8:
    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_S24LE:
    case CODEC_ID_PCM_S24BE:
    case CODEC_ID_PCM_S32LE:
    case CODEC_ID_PCM_S32BE:
    case CODEC_ID_PCM_F32LE:
    case CODEC_ID_PCM_F32BE:
    case CODEC_ID_PCM_F64LE:
    case CODEC_ID_PCM_F64BE:
    case CODEC_ID_PCM_ALAW:
    case CODEC_ID_PCM_MULAW:
        return 1;
    case CODEC_ID_MACE3:
    case CODEC_ID_MACE6:
        return 6;
    case CODEC_ID_ADPCM_IMA_QT:
        return 64;
    case CODEC_ID_AMR_NB:
    case CODEC_ID_GSM:
    case CODEC_ID_QCELP:
        return 160;
    case CODEC_ID_MP1:
        return 384;
    case CODEC_ID_MP2:
    case CODEC_ID_MP3:
        return 1152;
    case CODEC_ID_AC3:
        return 1536;
    case CODEC_ID_ALAC:
    case CODEC_ID_QDM2:
        return 4096;
    default:
        return 0;
    }
}

static int caf_write_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVCodecContext *enc = s->streams[0]->codec;
    CAFContext *caf = s->priv_data;
    unsigned int codec_tag = ff_codec_get_tag(ff_codec_caf_tags, enc->codec_id);

    switch (enc->codec_id) {
    case CODEC_ID_PCM_S8:
    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_S24LE:
    case CODEC_ID_PCM_S24BE:
    case CODEC_ID_PCM_S32LE:
    case CODEC_ID_PCM_S32BE:
    case CODEC_ID_PCM_F32LE:
    case CODEC_ID_PCM_F32BE:
    case CODEC_ID_PCM_F64LE:
    case CODEC_ID_PCM_F64BE:
    case CODEC_ID_PCM_ALAW:
    case CODEC_ID_PCM_MULAW:
        codec_tag = MKBETAG('l','p','c','m');
    }

    if (!codec_tag) {
        av_log(s, AV_LOG_ERROR, "unsupported codec\n");
        return AVERROR_INVALIDDATA;
    }

    if (!enc->block_align) {
        av_log(s, AV_LOG_ERROR, "muxing with unknown or variable packet size not yet supported\n");
        return AVERROR_PATCHWELCOME;
    }

    ffio_wfourcc(pb, "caff"); //< mFileType
    avio_wb16(pb, 1);         //< mFileVersion
    avio_wb16(pb, 0);         //< mFileFlags

    ffio_wfourcc(pb, "desc");                         //< Audio Description chunk
    avio_wb64(pb, 32);                                //< mChunkSize
    avio_wb64(pb, av_dbl2int(enc->sample_rate));      //< mSampleRate
    avio_wb32(pb, codec_tag);                         //< mFormatID
    avio_wb32(pb, codec_flags(enc->codec_id));        //< mFormatFlags
    avio_wb32(pb, enc->block_align);                  //< mBytesPerPacket
    avio_wb32(pb, samples_per_packet(enc->codec_id)); //< mFramesPerPacket
    avio_wb32(pb, enc->channels);                     //< mChannelsPerFrame
    avio_wb32(pb, enc->bits_per_coded_sample);        //< mBitsPerChannel

    if (enc->channel_layout) {
        ffio_wfourcc(pb, "chan");
        avio_wb64(pb, 12);
        ff_mov_write_chan(pb, enc->channel_layout);
    }

    ffio_wfourcc(pb, "data"); //< Audio Data chunk
    caf->data = avio_tell(pb);
    avio_wb64(pb, -1);        //< mChunkSize
    avio_wb32(pb, 0);         //< mEditCount

    avio_flush(pb);
    return 0;
}

static int caf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    avio_write(s->pb, pkt->data, pkt->size);
    return 0;
}

static int caf_write_trailer(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;

    if (pb->seekable) {
        CAFContext *caf = s->priv_data;
        int64_t file_size = avio_tell(pb);

        avio_seek(pb, caf->data, SEEK_SET);
        avio_wb64(pb, file_size - caf->data - 8);
        avio_seek(pb, file_size, SEEK_SET);
        avio_flush(pb);
    }
    return 0;
}

AVOutputFormat ff_caf_muxer = {
    "caf",
    NULL_IF_CONFIG_SMALL("Apple Core Audio Format"),
    "audio/x-caf",
    "caf",
    sizeof(CAFContext),
    CODEC_ID_PCM_S16BE,
    CODEC_ID_NONE,
    caf_write_header,
    caf_write_packet,
    caf_write_trailer,
    .codec_tag= (const AVCodecTag* const []){ff_codec_caf_tags, 0},
};
