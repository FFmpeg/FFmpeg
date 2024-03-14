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
#include "libavcodec/packet_internal.h"
#include "avformat.h"
#include "internal.h"
#include "aiff.h"
#include "avio_internal.h"
#include "isom.h"
#include "id3v2.h"
#include "mux.h"

typedef struct AIFFOutputContext {
    const AVClass *class;
    int64_t form;
    int64_t frames;
    int64_t ssnd;
    int audio_stream_idx;
    PacketList pict_list;
    int write_id3v2;
    int id3v2_version;
} AIFFOutputContext;

static int put_id3v2_tags(AVFormatContext *s, AIFFOutputContext *aiff)
{
    int ret;
    uint64_t pos, end, size;
    ID3v2EncContext id3v2 = { 0 };
    AVIOContext *pb = s->pb;
    PacketListEntry *list_entry = aiff->pict_list.head;

    if (!s->metadata && !s->nb_chapters && !list_entry)
        return 0;

    avio_wb32(pb, MKBETAG('I', 'D', '3', ' '));
    avio_wb32(pb, 0);
    pos = avio_tell(pb);

    ff_id3v2_start(&id3v2, pb, aiff->id3v2_version, ID3v2_DEFAULT_MAGIC);
    ff_id3v2_write_metadata(s, &id3v2);
    while (list_entry) {
        if ((ret = ff_id3v2_write_apic(s, &id3v2, &list_entry->pkt)) < 0)
            return ret;
        list_entry = list_entry->next;
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
        size_t size = strlen(tag->value);

        // AIFF tags are zero-padded to an even length.
        // So simply copy the terminating \0 if the length is odd.
        size = FFALIGN(size, 2);

        avio_wb32(pb, id);
        avio_wb32(pb, size);
        avio_write(pb, tag->value, size);
    }
}

static int aiff_write_header(AVFormatContext *s)
{
    AIFFOutputContext *aiff = s->priv_data;
    AVIOContext *pb = s->pb;
    AVCodecParameters *par;
    uint64_t sample_rate;
    int i, aifc = 0;

    aiff->audio_stream_idx = -1;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        if (aiff->audio_stream_idx < 0 && st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            aiff->audio_stream_idx = i;
        } else if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
            av_log(s, AV_LOG_ERROR, "AIFF allows only one audio stream and a picture.\n");
            return AVERROR(EINVAL);
        }
    }
    if (aiff->audio_stream_idx < 0) {
        av_log(s, AV_LOG_ERROR, "No audio stream present.\n");
        return AVERROR(EINVAL);
    }

    par = s->streams[aiff->audio_stream_idx]->codecpar;

    /* First verify if format is ok */
    if (!par->codec_tag)
        return AVERROR(EINVAL);
    if (par->codec_tag != MKTAG('N','O','N','E'))
        aifc = 1;

    /* FORM AIFF header */
    ffio_wfourcc(pb, "FORM");
    aiff->form = avio_tell(pb);
    avio_wb32(pb, 0);                    /* file length */
    ffio_wfourcc(pb, aifc ? "AIFC" : "AIFF");

    if (aifc) { // compressed audio
        if (!par->block_align) {
            av_log(s, AV_LOG_ERROR, "block align not set\n");
            return AVERROR(EINVAL);
        }
        /* Version chunk */
        ffio_wfourcc(pb, "FVER");
        avio_wb32(pb, 4);
        avio_wb32(pb, 0xA2805140);
    }

    if (par->ch_layout.order == AV_CHANNEL_ORDER_NATIVE && par->ch_layout.nb_channels > 2) {
        ffio_wfourcc(pb, "CHAN");
        avio_wb32(pb, 12);
        ff_mov_write_chan(pb, par->ch_layout.u.mask);
    }

    put_meta(s, "title",     MKBETAG('N', 'A', 'M', 'E'));
    put_meta(s, "author",    MKBETAG('A', 'U', 'T', 'H'));
    put_meta(s, "copyright", MKBETAG('(', 'c', ')', ' '));
    put_meta(s, "comment",   MKBETAG('A', 'N', 'N', 'O'));

    /* Common chunk */
    ffio_wfourcc(pb, "COMM");
    avio_wb32(pb, aifc ? 24 : 18); /* size */
    avio_wb16(pb, par->ch_layout.nb_channels);  /* Number of channels */

    aiff->frames = avio_tell(pb);
    avio_wb32(pb, 0);              /* Number of frames */

    if (!par->bits_per_coded_sample)
        par->bits_per_coded_sample = av_get_bits_per_sample(par->codec_id);
    if (!par->bits_per_coded_sample) {
        av_log(s, AV_LOG_ERROR, "could not compute bits per sample\n");
        return AVERROR(EINVAL);
    }
    if (!par->block_align)
        par->block_align = (par->bits_per_coded_sample * par->ch_layout.nb_channels) >> 3;

    avio_wb16(pb, par->bits_per_coded_sample); /* Sample size */

    sample_rate = av_double2int(par->sample_rate);
    avio_wb16(pb, (sample_rate >> 52) + (16383 - 1023));
    avio_wb64(pb, UINT64_C(1) << 63 | sample_rate << 11);

    if (aifc) {
        avio_wl32(pb, par->codec_tag);
        avio_wb16(pb, 0);
    }

    if (  (par->codec_tag == MKTAG('Q','D','M','2')
        || par->codec_tag == MKTAG('Q','c','l','p')) && par->extradata_size) {
        ffio_wfourcc(pb, "wave");
        avio_wb32(pb, par->extradata_size);
        avio_write(pb, par->extradata, par->extradata_size);
    }

    /* Sound data chunk */
    ffio_wfourcc(pb, "SSND");
    aiff->ssnd = avio_tell(pb);         /* Sound chunk size */
    avio_wb32(pb, 0);                    /* Sound samples data size */
    avio_wb32(pb, 0);                    /* Data offset */
    avio_wb32(pb, 0);                    /* Block-size (block align) */

    avpriv_set_pts_info(s->streams[aiff->audio_stream_idx], 64, 1,
                        s->streams[aiff->audio_stream_idx]->codecpar->sample_rate);

    return 0;
}

static int aiff_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AIFFOutputContext *aiff = s->priv_data;
    AVIOContext *pb = s->pb;
    if (pkt->stream_index == aiff->audio_stream_idx)
        avio_write(pb, pkt->data, pkt->size);
    else {
        /* warn only once for each stream */
        if (s->streams[pkt->stream_index]->nb_frames == 1) {
            av_log(s, AV_LOG_WARNING, "Got more than one picture in stream %d,"
                   " ignoring.\n", pkt->stream_index);
        }
        if (s->streams[pkt->stream_index]->nb_frames >= 1)
            return 0;

        return avpriv_packet_list_put(&aiff->pict_list, pkt, NULL, 0);
    }

    return 0;
}

static int aiff_write_trailer(AVFormatContext *s)
{
    int ret = 0;
    AVIOContext *pb = s->pb;
    AIFFOutputContext *aiff = s->priv_data;
    AVCodecParameters *par = s->streams[aiff->audio_stream_idx]->codecpar;

    /* Chunks sizes must be even */
    int64_t file_size, data_size;
    data_size = avio_tell(pb);
    if (data_size & 1)
        avio_w8(pb, 0);

    if (s->pb->seekable & AVIO_SEEKABLE_NORMAL) {
        /* Write ID3 tags */
        if (aiff->write_id3v2)
            if ((ret = put_id3v2_tags(s, aiff)) < 0)
                return ret;

        /* File length */
        file_size = avio_tell(pb);
        avio_seek(pb, aiff->form, SEEK_SET);
        avio_wb32(pb, file_size - aiff->form - 4);

        /* Number of sample frames */
        avio_seek(pb, aiff->frames, SEEK_SET);
        avio_wb32(pb, (data_size - aiff->ssnd - 12) / par->block_align);

        /* Sound Data chunk size */
        avio_seek(pb, aiff->ssnd, SEEK_SET);
        avio_wb32(pb, data_size - aiff->ssnd - 4);
    }

    return ret;
}

static void aiff_deinit(AVFormatContext *s)
{
    AIFFOutputContext *aiff = s->priv_data;

    avpriv_packet_list_free(&aiff->pict_list);
}

#define OFFSET(x) offsetof(AIFFOutputContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "write_id3v2", "Enable ID3 tags writing.",
      OFFSET(write_id3v2), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, ENC },
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

const FFOutputFormat ff_aiff_muxer = {
    .p.name            = "aiff",
    .p.long_name       = NULL_IF_CONFIG_SMALL("Audio IFF"),
    .p.mime_type       = "audio/aiff",
    .p.extensions      = "aif,aiff,afc,aifc",
    .priv_data_size    = sizeof(AIFFOutputContext),
    .p.audio_codec     = AV_CODEC_ID_PCM_S16BE,
    .p.video_codec     = AV_CODEC_ID_PNG,
    .write_header      = aiff_write_header,
    .write_packet      = aiff_write_packet,
    .write_trailer     = aiff_write_trailer,
    .deinit            = aiff_deinit,
    .p.codec_tag       = ff_aiff_codec_tags_list,
    .p.priv_class      = &aiff_muxer_class,
};
