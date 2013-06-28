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
#include "isom.h"
#include "avio_internal.h"
#include "libavutil/intfloat.h"
#include "libavutil/dict.h"

typedef struct {
    int64_t data;
    uint8_t *pkt_sizes;
    int size_buffer_size;
    int size_entries_used;
    int packets;
} CAFContext;

static uint32_t codec_flags(enum AVCodecID codec_id) {
    switch (codec_id) {
    case AV_CODEC_ID_PCM_F32BE:
    case AV_CODEC_ID_PCM_F64BE:
        return 1; //< kCAFLinearPCMFormatFlagIsFloat
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S24LE:
    case AV_CODEC_ID_PCM_S32LE:
        return 2; //< kCAFLinearPCMFormatFlagIsLittleEndian
    case AV_CODEC_ID_PCM_F32LE:
    case AV_CODEC_ID_PCM_F64LE:
        return 3; //< kCAFLinearPCMFormatFlagIsFloat | kCAFLinearPCMFormatFlagIsLittleEndian
    default:
        return 0;
    }
}

static uint32_t samples_per_packet(enum AVCodecID codec_id, int channels) {
    switch (codec_id) {
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_S24LE:
    case AV_CODEC_ID_PCM_S24BE:
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_S32BE:
    case AV_CODEC_ID_PCM_F32LE:
    case AV_CODEC_ID_PCM_F32BE:
    case AV_CODEC_ID_PCM_F64LE:
    case AV_CODEC_ID_PCM_F64BE:
    case AV_CODEC_ID_PCM_ALAW:
    case AV_CODEC_ID_PCM_MULAW:
        return 1;
    case AV_CODEC_ID_MACE3:
    case AV_CODEC_ID_MACE6:
        return 6;
    case AV_CODEC_ID_ADPCM_IMA_QT:
        return 64;
    case AV_CODEC_ID_AMR_NB:
    case AV_CODEC_ID_GSM:
    case AV_CODEC_ID_ILBC:
    case AV_CODEC_ID_QCELP:
        return 160;
    case AV_CODEC_ID_GSM_MS:
        return 320;
    case AV_CODEC_ID_MP1:
        return 384;
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
        return 1152;
    case AV_CODEC_ID_AC3:
        return 1536;
    case AV_CODEC_ID_QDM2:
        return 2048 * channels;
    case AV_CODEC_ID_ALAC:
        return 4096;
    case AV_CODEC_ID_ADPCM_IMA_WAV:
        return (1024 - 4 * channels) * 8 / (4 * channels) + 1;
    case AV_CODEC_ID_ADPCM_MS:
        return (1024 - 7 * channels) * 2 / channels + 2;
    default:
        return 0;
    }
}

static int caf_write_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVCodecContext *enc = s->streams[0]->codec;
    CAFContext *caf = s->priv_data;
    AVDictionaryEntry *t = NULL;
    unsigned int codec_tag = ff_codec_get_tag(ff_codec_caf_tags, enc->codec_id);
    int64_t chunk_size = 0;

    switch (enc->codec_id) {
    case AV_CODEC_ID_AAC:
    case AV_CODEC_ID_AC3:
        av_log(s, AV_LOG_ERROR, "muxing codec currently unsupported\n");
        return AVERROR_PATCHWELCOME;
    }

    switch (enc->codec_id) {
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_S24LE:
    case AV_CODEC_ID_PCM_S24BE:
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_S32BE:
    case AV_CODEC_ID_PCM_F32LE:
    case AV_CODEC_ID_PCM_F32BE:
    case AV_CODEC_ID_PCM_F64LE:
    case AV_CODEC_ID_PCM_F64BE:
    case AV_CODEC_ID_PCM_ALAW:
    case AV_CODEC_ID_PCM_MULAW:
        codec_tag = MKTAG('l','p','c','m');
    }

    if (!codec_tag) {
        av_log(s, AV_LOG_ERROR, "unsupported codec\n");
        return AVERROR_INVALIDDATA;
    }

    if (!enc->block_align && !pb->seekable) {
        av_log(s, AV_LOG_ERROR, "Muxing variable packet size not supported on non seekable output\n");
        return AVERROR_INVALIDDATA;
    }

    ffio_wfourcc(pb, "caff"); //< mFileType
    avio_wb16(pb, 1);         //< mFileVersion
    avio_wb16(pb, 0);         //< mFileFlags

    ffio_wfourcc(pb, "desc");                         //< Audio Description chunk
    avio_wb64(pb, 32);                                //< mChunkSize
    avio_wb64(pb, av_double2int(enc->sample_rate));   //< mSampleRate
    avio_wl32(pb, codec_tag);                         //< mFormatID
    avio_wb32(pb, codec_flags(enc->codec_id));        //< mFormatFlags
    avio_wb32(pb, enc->block_align);                  //< mBytesPerPacket
    avio_wb32(pb, samples_per_packet(enc->codec_id, enc->channels)); //< mFramesPerPacket
    avio_wb32(pb, enc->channels);                     //< mChannelsPerFrame
    avio_wb32(pb, av_get_bits_per_sample(enc->codec_id)); //< mBitsPerChannel

    if (enc->channel_layout) {
        ffio_wfourcc(pb, "chan");
        avio_wb64(pb, 12);
        ff_mov_write_chan(pb, enc->channel_layout);
    }

    if (enc->codec_id == AV_CODEC_ID_ALAC) {
        ffio_wfourcc(pb, "kuki");
        avio_wb64(pb, 12 + enc->extradata_size);
        avio_write(pb, "\0\0\0\14frmaalac", 12);
        avio_write(pb, enc->extradata, enc->extradata_size);
    } else if (enc->codec_id == AV_CODEC_ID_AMR_NB) {
        ffio_wfourcc(pb, "kuki");
        avio_wb64(pb, 29);
        avio_write(pb, "\0\0\0\14frmasamr", 12);
        avio_wb32(pb, 0x11); /* size */
        avio_write(pb, "samrFFMP", 8);
        avio_w8(pb, 0); /* decoder version */

        avio_wb16(pb, 0x81FF); /* Mode set (all modes for AMR_NB) */
        avio_w8(pb, 0x00); /* Mode change period (no restriction) */
        avio_w8(pb, 0x01); /* Frames per sample */
    } else if (enc->codec_id == AV_CODEC_ID_QDM2) {
        ffio_wfourcc(pb, "kuki");
        avio_wb64(pb, enc->extradata_size);
        avio_write(pb, enc->extradata, enc->extradata_size);
    }

    if (av_dict_count(s->metadata)) {
        ffio_wfourcc(pb, "info"); //< Information chunk
        while ((t = av_dict_get(s->metadata, "", t, AV_DICT_IGNORE_SUFFIX))) {
            chunk_size += strlen(t->key) + strlen(t->value) + 2;
        }
        avio_wb64(pb, chunk_size + 4);
        avio_wb32(pb, av_dict_count(s->metadata));
        t = NULL;
        while ((t = av_dict_get(s->metadata, "", t, AV_DICT_IGNORE_SUFFIX))) {
            avio_put_str(pb, t->key);
            avio_put_str(pb, t->value);
        }
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
    CAFContext *caf = s->priv_data;

    avio_write(s->pb, pkt->data, pkt->size);
    if (!s->streams[0]->codec->block_align) {
        void *pkt_sizes = caf->pkt_sizes;
        int i, alloc_size = caf->size_entries_used + 5;
        if (alloc_size < 0) {
            caf->pkt_sizes = NULL;
        } else {
            caf->pkt_sizes = av_fast_realloc(caf->pkt_sizes,
                                             &caf->size_buffer_size,
                                             alloc_size);
        }
        if (!caf->pkt_sizes) {
            av_free(pkt_sizes);
            return AVERROR(ENOMEM);
        }
        for (i = 4; i > 0; i--) {
            unsigned top = pkt->size >> i * 7;
            if (top)
                caf->pkt_sizes[caf->size_entries_used++] = 128 | top;
        }
        caf->pkt_sizes[caf->size_entries_used++] = pkt->size & 127;
        caf->packets++;
    }
    return 0;
}

static int caf_write_trailer(AVFormatContext *s)
{
    CAFContext *caf = s->priv_data;
    AVIOContext *pb = s->pb;
    AVCodecContext *enc = s->streams[0]->codec;

    if (pb->seekable) {
        int64_t file_size = avio_tell(pb);

        avio_seek(pb, caf->data, SEEK_SET);
        avio_wb64(pb, file_size - caf->data - 8);
        avio_seek(pb, file_size, SEEK_SET);
        if (!enc->block_align) {
            ffio_wfourcc(pb, "pakt");
            avio_wb64(pb, caf->size_entries_used + 24);
            avio_wb64(pb, caf->packets); ///< mNumberPackets
            avio_wb64(pb, caf->packets * samples_per_packet(enc->codec_id, enc->channels)); ///< mNumberValidFrames
            avio_wb32(pb, 0); ///< mPrimingFrames
            avio_wb32(pb, 0); ///< mRemainderFrames
            avio_write(pb, caf->pkt_sizes, caf->size_entries_used);
            caf->size_buffer_size = 0;
        }
        avio_flush(pb);
    }
    av_freep(&caf->pkt_sizes);
    return 0;
}

AVOutputFormat ff_caf_muxer = {
    .name           = "caf",
    .long_name      = NULL_IF_CONFIG_SMALL("Apple CAF (Core Audio Format)"),
    .mime_type      = "audio/x-caf",
    .extensions     = "caf",
    .priv_data_size = sizeof(CAFContext),
    .audio_codec    = AV_CODEC_ID_PCM_S16BE,
    .video_codec    = AV_CODEC_ID_NONE,
    .write_header   = caf_write_header,
    .write_packet   = caf_write_packet,
    .write_trailer  = caf_write_trailer,
    .codec_tag      = (const AVCodecTag* const []){ff_codec_caf_tags, 0},
};
