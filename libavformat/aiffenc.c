/*
 * AIFF/AIFF-C muxer
 * Copyright (c) 2006  Patrick Guimond
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

#include <stdint.h>

#include "libavutil/intfloat.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "internal.h"
#include "aiff.h"
#include "avio_internal.h"
#include "isom.h"
#include "id3v2.h"

typedef struct {
    const AVClass *class;
    int64_t form;
    int64_t frames;
    int64_t ssnd;
    int audio_stream_idx;
    AVPacketList *pict_list;
    int write_id3v2;
    int id3v2_version;
} AIFFOutputContext;

static int put_id3v2_tags(AVFormatContext *s, AIFFOutputContext *aiff)
{
    int ret;
    uint64_t pos, end, size;
    ID3v2EncContext id3v2 = { 0 };
    AVIOContext *pb = s->pb;
    AVPacketList *pict_list = aiff->pict_list;

    if (!pb->seekable)
        return 0;

    if (!s->metadata && !aiff->pict_list)
        return 0;

    avio_wl32(pb, MKTAG('I', 'D', '3', ' '));
    avio_wb32(pb, 0);
    pos = avio_tell(pb);

    ff_id3v2_start(&id3v2, pb, aiff->id3v2_version, ID3v2_DEFAULT_MAGIC);
    ff_id3v2_write_metadata(s, &id3v2);
    while (pict_list) {
        if ((ret = ff_id3v2_write_apic(s, &id3v2, &pict_list->pkt)) < 0)
            return ret;
        pict_list = pict_list->next;
    }
    ff_id3v2_finish(&id3v2, pb, s->metadata_header_padding);

    end = avio_tell(pb);
    size = end - pos;

    /* Update chunk size */
    avio_seek(pb, pos - 4, SEEK_SET);
    avio_wb32(pb, size);
    avio_seek(pb, end, SEEK_SET);

    if (size & 1)
        avio_w8(pb, 0);

    return 0;
}

static void put_meta(AVFormatContext *s, const char *key, uint32_t id)
{
    AVDictionaryEntry *tag;
    AVIOContext *pb = s->pb;

    if (tag = av_dict_get(s->metadata, key, NULL, 0)) {
        int size = strlen(tag->value);

        avio_wl32(pb, id);
        avio_wb32(pb, FFALIGN(size, 2));
        avio_write(pb, tag->value, size);
        if (size & 1)
            avio_w8(pb, 0);
    }
}

static int aiff_write_header(AVFormatContext *s)
{
    AIFFOutputContext *aiff = s->priv_data;
    AVIOContext *pb = s->pb;
    AVCodecContext *enc;
    uint64_t sample_rate;
    int i, aifc = 0;

    aiff->audio_stream_idx = -1;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        if (aiff->audio_stream_idx < 0 && st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            aiff->audio_stream_idx = i;
        } else if (st->codec->codec_type != AVMEDIA_TYPE_VIDEO) {
            av_log(s, AV_LOG_ERROR, "Only audio streams and pictures are allowed in AIFF.\n");
            return AVERROR(EINVAL);
        }
    }
    if (aiff->audio_stream_idx < 0) {
        av_log(s, AV_LOG_ERROR, "No audio stream present.\n");
        return AVERROR(EINVAL);
    }

    enc = s->streams[aiff->audio_stream_idx]->codec;

    /* First verify if format is ok */
    if (!enc->codec_tag)
        return -1;
    if (enc->codec_tag != MKTAG('N','O','N','E'))
        aifc = 1;

    /* FORM AIFF header */
    ffio_wfourcc(pb, "FORM");
    aiff->form = avio_tell(pb);
    avio_wb32(pb, 0);                    /* file length */
    ffio_wfourcc(pb, aifc ? "AIFC" : "AIFF");

    if (aifc) { // compressed audio
        if (!enc->block_align) {
            av_log(s, AV_LOG_ERROR, "block align not set\n");
            return -1;
        }
        /* Version chunk */
        ffio_wfourcc(pb, "FVER");
        avio_wb32(pb, 4);
        avio_wb32(pb, 0xA2805140);
    }

    if (enc->channels > 2 && enc->channel_layout) {
        ffio_wfourcc(pb, "CHAN");
        avio_wb32(pb, 12);
        ff_mov_write_chan(pb, enc->channel_layout);
    }

    put_meta(s, "title",     MKTAG('N', 'A', 'M', 'E'));
    put_meta(s, "author",    MKTAG('A', 'U', 'T', 'H'));
    put_meta(s, "copyright", MKTAG('(', 'c', ')', ' '));
    put_meta(s, "comment",   MKTAG('A', 'N', 'N', 'O'));

    /* Common chunk */
    ffio_wfourcc(pb, "COMM");
    avio_wb32(pb, aifc ? 24 : 18); /* size */
    avio_wb16(pb, enc->channels);  /* Number of channels */

    aiff->frames = avio_tell(pb);
    avio_wb32(pb, 0);              /* Number of frames */

    if (!enc->bits_per_coded_sample)
        enc->bits_per_coded_sample = av_get_bits_per_sample(enc->codec_id);
    if (!enc->bits_per_coded_sample) {
        av_log(s, AV_LOG_ERROR, "could not compute bits per sample\n");
        return -1;
    }
    if (!enc->block_align)
        enc->block_align = (enc->bits_per_coded_sample * enc->channels) >> 3;

    avio_wb16(pb, enc->bits_per_coded_sample); /* Sample size */

    sample_rate = av_double2int(enc->sample_rate);
    avio_wb16(pb, (sample_rate >> 52) + (16383 - 1023));
    avio_wb64(pb, UINT64_C(1) << 63 | sample_rate << 11);

    if (aifc) {
        avio_wl32(pb, enc->codec_tag);
        avio_wb16(pb, 0);
    }

    if (enc->codec_tag == MKTAG('Q','D','M','2') && enc->extradata_size) {
        ffio_wfourcc(pb, "wave");
        avio_wb32(pb, enc->extradata_size);
        avio_write(pb, enc->extradata, enc->extradata_size);
    }

    /* Sound data chunk */
    ffio_wfourcc(pb, "SSND");
    aiff->ssnd = avio_tell(pb);         /* Sound chunk size */
    avio_wb32(pb, 0);                    /* Sound samples data size */
    avio_wb32(pb, 0);                    /* Data offset */
    avio_wb32(pb, 0);                    /* Block-size (block align) */

    avpriv_set_pts_info(s->streams[aiff->audio_stream_idx], 64, 1,
                        s->streams[aiff->audio_stream_idx]->codec->sample_rate);

    /* Data is starting here */
    avio_flush(pb);

    return 0;
}

static int aiff_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AIFFOutputContext *aiff = s->priv_data;
    AVIOContext *pb = s->pb;
    if (pkt->stream_index == aiff->audio_stream_idx)
        avio_write(pb, pkt->data, pkt->size);
    else {
        int ret;
        AVPacketList *pict_list, *last;

        if (s->streams[pkt->stream_index]->codec->codec_type != AVMEDIA_TYPE_VIDEO)
            return 0;

        /* warn only once for each stream */
        if (s->streams[pkt->stream_index]->nb_frames == 1) {
            av_log(s, AV_LOG_WARNING, "Got more than one picture in stream %d,"
                   " ignoring.\n", pkt->stream_index);
        }
        if (s->streams[pkt->stream_index]->nb_frames >= 1)
            return 0;

        pict_list = av_mallocz(sizeof(AVPacketList));
        if (!pict_list)
            return AVERROR(ENOMEM);

        if ((ret = av_copy_packet(&pict_list->pkt, pkt)) < 0) {
            av_freep(&pict_list);
            return ret;
        }

        if (!aiff->pict_list)
            aiff->pict_list = pict_list;
        else {
            last = aiff->pict_list;
            while (last->next)
                last = last->next;
            last->next = pict_list;
        }
    }

    return 0;
}

static int aiff_write_trailer(AVFormatContext *s)
{
    int ret;
    AVIOContext *pb = s->pb;
    AIFFOutputContext *aiff = s->priv_data;
    AVPacketList *pict_list = aiff->pict_list;
    AVCodecContext *enc = s->streams[aiff->audio_stream_idx]->codec;

    /* Chunks sizes must be even */
    int64_t file_size, end_size;
    end_size = file_size = avio_tell(pb);
    if (file_size & 1) {
        avio_w8(pb, 0);
        end_size++;
    }

    if (s->pb->seekable) {
        /* Number of sample frames */
        avio_seek(pb, aiff->frames, SEEK_SET);
        avio_wb32(pb, (file_size-aiff->ssnd-12)/enc->block_align);

        /* Sound Data chunk size */
        avio_seek(pb, aiff->ssnd, SEEK_SET);
        avio_wb32(pb, file_size - aiff->ssnd - 4);

        /* return to the end */
        avio_seek(pb, end_size, SEEK_SET);

        /* Write ID3 tags */
        if (aiff->write_id3v2)
            if ((ret = put_id3v2_tags(s, aiff)) < 0)
                return ret;

        /* File length */
        file_size = avio_tell(pb);
        avio_seek(pb, aiff->form, SEEK_SET);
        avio_wb32(pb, file_size - aiff->form - 4);

        avio_flush(pb);
    }

    while (pict_list) {
        AVPacketList *next = pict_list->next;
        av_free_packet(&pict_list->pkt);
        av_freep(&pict_list);
        pict_list = next;
    }

    return 0;
}

#define OFFSET(x) offsetof(AIFFOutputContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "write_id3v2", "Enable ID3 tags writing.",
      OFFSET(write_id3v2), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, ENC },
    { "id3v2_version", "Select ID3v2 version to write. Currently 3 and 4 are supported.",
      OFFSET(id3v2_version), AV_OPT_TYPE_INT, {.i64 = 4}, 3, 4, ENC },
    { NULL },
};

static const AVClass aiff_muxer_class = {
    .class_name     = "AIFF muxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_aiff_muxer = {
    .name              = "aiff",
    .long_name         = NULL_IF_CONFIG_SMALL("Audio IFF"),
    .mime_type         = "audio/aiff",
    .extensions        = "aif,aiff,afc,aifc",
    .priv_data_size    = sizeof(AIFFOutputContext),
    .audio_codec       = AV_CODEC_ID_PCM_S16BE,
    .video_codec       = AV_CODEC_ID_PNG,
    .write_header      = aiff_write_header,
    .write_packet      = aiff_write_packet,
    .write_trailer     = aiff_write_trailer,
    .codec_tag         = (const AVCodecTag* const []){ ff_codec_aiff_tags, 0 },
    .priv_class        = &aiff_muxer_class,
};
