/*
 * Ogg muxer
 * Copyright (c) 2007 Baptiste Coudurier <baptiste dot coudurier at free dot fr>
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

#include "libavutil/crc.h"
#include "libavutil/opt.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/random_seed.h"
#include "libavcodec/xiph.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/flac.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "vorbiscomment.h"

#define MAX_PAGE_SIZE 65025

typedef struct {
    int64_t start_granule;
    int64_t granule;
    int stream_index;
    uint8_t flags;
    uint8_t segments_count;
    uint8_t segments[255];
    uint8_t data[MAX_PAGE_SIZE];
    uint16_t size;
} OGGPage;

typedef struct {
    unsigned page_counter;
    uint8_t *header[3];
    int header_len[3];
    /** for theora granule */
    int kfgshift;
    int64_t last_kf_pts;
    int vrev;
    int eos;
    unsigned page_count; ///< number of page buffered
    OGGPage page; ///< current page
    unsigned serial_num; ///< serial number
    int64_t last_granule; ///< last packet granule
} OGGStreamContext;

typedef struct OGGPageList {
    OGGPage page;
    struct OGGPageList *next;
} OGGPageList;

typedef struct {
    const AVClass *class;
    OGGPageList *page_list;
    int pref_size; ///< preferred page size (0 => fill all segments)
    int64_t pref_duration;      ///< preferred page duration (0 => fill all segments)
} OGGContext;

#define OFFSET(x) offsetof(OGGContext, x)
#define PARAM AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { "oggpagesize", "Set preferred Ogg page size.",
      offsetof(OGGContext, pref_size), AV_OPT_TYPE_INT, {.i64 = 0}, 0, MAX_PAGE_SIZE, AV_OPT_FLAG_ENCODING_PARAM},
    { "pagesize", "preferred page size in bytes (deprecated)",
        OFFSET(pref_size), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, MAX_PAGE_SIZE, PARAM },
    { "page_duration", "preferred page duration, in microseconds",
        OFFSET(pref_duration), AV_OPT_TYPE_INT64, { .i64 = 1000000 }, 0, INT64_MAX, PARAM },
    { NULL },
};

static const AVClass ogg_muxer_class = {
    .class_name = "Ogg muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};


static void ogg_update_checksum(AVFormatContext *s, AVIOContext *pb, int64_t crc_offset)
{
    int64_t pos = avio_tell(pb);
    uint32_t checksum = ffio_get_checksum(pb);
    avio_seek(pb, crc_offset, SEEK_SET);
    avio_wb32(pb, checksum);
    avio_seek(pb, pos, SEEK_SET);
}

static int ogg_write_page(AVFormatContext *s, OGGPage *page, int extra_flags)
{
    OGGStreamContext *oggstream = s->streams[page->stream_index]->priv_data;
    AVIOContext *pb;
    int64_t crc_offset;
    int ret, size;
    uint8_t *buf;

    ret = avio_open_dyn_buf(&pb);
    if (ret < 0)
        return ret;
    ffio_init_checksum(pb, ff_crc04C11DB7_update, 0);
    ffio_wfourcc(pb, "OggS");
    avio_w8(pb, 0);
    avio_w8(pb, page->flags | extra_flags);
    avio_wl64(pb, page->granule);
    avio_wl32(pb, oggstream->serial_num);
    avio_wl32(pb, oggstream->page_counter++);
    crc_offset = avio_tell(pb);
    avio_wl32(pb, 0); // crc
    avio_w8(pb, page->segments_count);
    avio_write(pb, page->segments, page->segments_count);
    avio_write(pb, page->data, page->size);

    ogg_update_checksum(s, pb, crc_offset);
    avio_flush(pb);

    size = avio_close_dyn_buf(pb, &buf);
    if (size < 0)
        return size;

    avio_write(s->pb, buf, size);
    avio_flush(s->pb);
    av_free(buf);
    oggstream->page_count--;
    return 0;
}

static int ogg_key_granule(OGGStreamContext *oggstream, int64_t granule)
{
    return oggstream->kfgshift && !(granule & ((1<<oggstream->kfgshift)-1));
}

static int64_t ogg_granule_to_timestamp(OGGStreamContext *oggstream, int64_t granule)
{
    if (oggstream->kfgshift)
        return (granule>>oggstream->kfgshift) +
            (granule & ((1<<oggstream->kfgshift)-1));
    else
        return granule;
}

static int ogg_compare_granule(AVFormatContext *s, OGGPage *next, OGGPage *page)
{
    AVStream *st2 = s->streams[next->stream_index];
    AVStream *st  = s->streams[page->stream_index];
    int64_t next_granule, cur_granule;

    if (next->granule == -1 || page->granule == -1)
        return 0;

    next_granule = av_rescale_q(ogg_granule_to_timestamp(st2->priv_data, next->granule),
                                st2->time_base, AV_TIME_BASE_Q);
    cur_granule  = av_rescale_q(ogg_granule_to_timestamp(st->priv_data, page->granule),
                                st ->time_base, AV_TIME_BASE_Q);
    return next_granule > cur_granule;
}

static int ogg_reset_cur_page(OGGStreamContext *oggstream)
{
    oggstream->page.granule = -1;
    oggstream->page.flags = 0;
    oggstream->page.segments_count = 0;
    oggstream->page.size = 0;
    return 0;
}

static int ogg_buffer_page(AVFormatContext *s, OGGStreamContext *oggstream)
{
    OGGContext *ogg = s->priv_data;
    OGGPageList **p = &ogg->page_list;
    OGGPageList *l = av_mallocz(sizeof(*l));

    if (!l)
        return AVERROR(ENOMEM);
    l->page = oggstream->page;

    oggstream->page.start_granule = oggstream->page.granule;
    oggstream->page_count++;
    ogg_reset_cur_page(oggstream);

    while (*p) {
        if (ogg_compare_granule(s, &(*p)->page, &l->page))
            break;
        p = &(*p)->next;
    }
    l->next = *p;
    *p = l;

    return 0;
}

static int ogg_buffer_data(AVFormatContext *s, AVStream *st,
                           uint8_t *data, unsigned size, int64_t granule,
                           int header)
{
    OGGStreamContext *oggstream = st->priv_data;
    OGGContext *ogg = s->priv_data;
    int total_segments = size / 255 + 1;
    uint8_t *p = data;
    int i, segments, len, flush = 0;

    // Handles VFR by flushing page because this frame needs to have a timestamp
    // For theora, keyframes also need to have a timestamp to correctly mark
    // them as such, otherwise seeking will not work correctly at the very
    // least with old libogg versions.
    // Do not try to flush header packets though, that will create broken files.
    if (st->codec->codec_id == AV_CODEC_ID_THEORA && !header &&
        (ogg_granule_to_timestamp(oggstream, granule) >
         ogg_granule_to_timestamp(oggstream, oggstream->last_granule) + 1 ||
         ogg_key_granule(oggstream, granule))) {
        if (oggstream->page.granule != -1)
            ogg_buffer_page(s, oggstream);
        flush = 1;
    }

    // avoid a continued page
    if (!header && oggstream->page.size > 0 &&
        MAX_PAGE_SIZE - oggstream->page.size < size) {
        ogg_buffer_page(s, oggstream);
    }

    for (i = 0; i < total_segments; ) {
        OGGPage *page = &oggstream->page;

        segments = FFMIN(total_segments - i, 255 - page->segments_count);

        if (i && !page->segments_count)
            page->flags |= 1; // continued packet

        memset(page->segments+page->segments_count, 255, segments - 1);
        page->segments_count += segments - 1;

        len = FFMIN(size, segments*255);
        page->segments[page->segments_count++] = len - (segments-1)*255;
        memcpy(page->data+page->size, p, len);
        p += len;
        size -= len;
        i += segments;
        page->size += len;

        if (i == total_segments)
            page->granule = granule;

        if (!header) {
            AVStream *st = s->streams[page->stream_index];

            int64_t start = av_rescale_q(page->start_granule, st->time_base,
                                         AV_TIME_BASE_Q);
            int64_t next  = av_rescale_q(page->granule, st->time_base,
                                         AV_TIME_BASE_Q);

            if (page->segments_count == 255 ||
                (ogg->pref_size     > 0 && page->size   >= ogg->pref_size) ||
                (ogg->pref_duration > 0 && next - start >= ogg->pref_duration)) {
                ogg_buffer_page(s, oggstream);
            }
        }
    }

    if (flush && oggstream->page.granule != -1)
        ogg_buffer_page(s, oggstream);

    return 0;
}

static uint8_t *ogg_write_vorbiscomment(int offset, int bitexact,
                                        int *header_len, AVDictionary **m, int framing_bit)
{
    const char *vendor = bitexact ? "ffmpeg" : LIBAVFORMAT_IDENT;
    int size;
    uint8_t *p, *p0;
    unsigned int count;

    ff_metadata_conv(m, ff_vorbiscomment_metadata_conv, NULL);

    size = offset + ff_vorbiscomment_length(*m, vendor, &count) + framing_bit;
    p = av_mallocz(size);
    if (!p)
        return NULL;
    p0 = p;

    p += offset;
    ff_vorbiscomment_write(&p, m, vendor, count);
    if (framing_bit)
        bytestream_put_byte(&p, 1);

    *header_len = size;
    return p0;
}

static int ogg_build_flac_headers(AVCodecContext *avctx,
                                  OGGStreamContext *oggstream, int bitexact,
                                  AVDictionary **m)
{
    enum FLACExtradataFormat format;
    uint8_t *streaminfo;
    uint8_t *p;

    if (!avpriv_flac_is_extradata_valid(avctx, &format, &streaminfo))
        return -1;

    // first packet: STREAMINFO
    oggstream->header_len[0] = 51;
    oggstream->header[0] = av_mallocz(51); // per ogg flac specs
    p = oggstream->header[0];
    if (!p)
        return AVERROR(ENOMEM);
    bytestream_put_byte(&p, 0x7F);
    bytestream_put_buffer(&p, "FLAC", 4);
    bytestream_put_byte(&p, 1); // major version
    bytestream_put_byte(&p, 0); // minor version
    bytestream_put_be16(&p, 1); // headers packets without this one
    bytestream_put_buffer(&p, "fLaC", 4);
    bytestream_put_byte(&p, 0x00); // streaminfo
    bytestream_put_be24(&p, 34);
    bytestream_put_buffer(&p, streaminfo, FLAC_STREAMINFO_SIZE);

    // second packet: VorbisComment
    p = ogg_write_vorbiscomment(4, bitexact, &oggstream->header_len[1], m, 0);
    if (!p)
        return AVERROR(ENOMEM);
    oggstream->header[1] = p;
    bytestream_put_byte(&p, 0x84); // last metadata block and vorbis comment
    bytestream_put_be24(&p, oggstream->header_len[1] - 4);

    return 0;
}

#define SPEEX_HEADER_SIZE 80

static int ogg_build_speex_headers(AVCodecContext *avctx,
                                   OGGStreamContext *oggstream, int bitexact,
                                   AVDictionary **m)
{
    uint8_t *p;

    if (avctx->extradata_size < SPEEX_HEADER_SIZE)
        return -1;

    // first packet: Speex header
    p = av_mallocz(SPEEX_HEADER_SIZE);
    if (!p)
        return AVERROR(ENOMEM);
    oggstream->header[0] = p;
    oggstream->header_len[0] = SPEEX_HEADER_SIZE;
    bytestream_put_buffer(&p, avctx->extradata, SPEEX_HEADER_SIZE);
    AV_WL32(&oggstream->header[0][68], 0);  // set extra_headers to 0

    // second packet: VorbisComment
    p = ogg_write_vorbiscomment(0, bitexact, &oggstream->header_len[1], m, 0);
    if (!p)
        return AVERROR(ENOMEM);
    oggstream->header[1] = p;

    return 0;
}

#define OPUS_HEADER_SIZE 19

static int ogg_build_opus_headers(AVCodecContext *avctx,
                                  OGGStreamContext *oggstream, int bitexact,
                                  AVDictionary **m)
{
    uint8_t *p;

    if (avctx->extradata_size < OPUS_HEADER_SIZE)
        return -1;

    /* first packet: Opus header */
    p = av_mallocz(avctx->extradata_size);
    if (!p)
        return AVERROR(ENOMEM);
    oggstream->header[0] = p;
    oggstream->header_len[0] = avctx->extradata_size;
    bytestream_put_buffer(&p, avctx->extradata, avctx->extradata_size);

    /* second packet: VorbisComment */
    p = ogg_write_vorbiscomment(8, bitexact, &oggstream->header_len[1], m, 0);
    if (!p)
        return AVERROR(ENOMEM);
    oggstream->header[1] = p;
    bytestream_put_buffer(&p, "OpusTags", 8);

    return 0;
}

static int ogg_write_header(AVFormatContext *s)
{
    OGGContext *ogg = s->priv_data;
    OGGStreamContext *oggstream = NULL;
    int i, j;

    if (ogg->pref_size)
        av_log(s, AV_LOG_WARNING, "The pagesize option is deprecated\n");

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        unsigned serial_num = i;

        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (st->codec->codec_id == AV_CODEC_ID_OPUS)
                /* Opus requires a fixed 48kHz clock */
                avpriv_set_pts_info(st, 64, 1, 48000);
            else
                avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);
        } else if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            avpriv_set_pts_info(st, 64, st->codec->time_base.num, st->codec->time_base.den);
        if (st->codec->codec_id != AV_CODEC_ID_VORBIS &&
            st->codec->codec_id != AV_CODEC_ID_THEORA &&
            st->codec->codec_id != AV_CODEC_ID_SPEEX  &&
            st->codec->codec_id != AV_CODEC_ID_FLAC   &&
            st->codec->codec_id != AV_CODEC_ID_OPUS) {
            av_log(s, AV_LOG_ERROR, "Unsupported codec id in stream %d\n", i);
            return -1;
        }

        if (!st->codec->extradata || !st->codec->extradata_size) {
            av_log(s, AV_LOG_ERROR, "No extradata present\n");
            return -1;
        }
        oggstream = av_mallocz(sizeof(*oggstream));
        oggstream->page.stream_index = i;

        if (!(st->codec->flags & CODEC_FLAG_BITEXACT))
            do {
                serial_num = av_get_random_seed();
                for (j = 0; j < i; j++) {
                    OGGStreamContext *sc = s->streams[j]->priv_data;
                    if (serial_num == sc->serial_num)
                        break;
                }
            } while (j < i);
        oggstream->serial_num = serial_num;

        st->priv_data = oggstream;
        if (st->codec->codec_id == AV_CODEC_ID_FLAC) {
            int err = ogg_build_flac_headers(st->codec, oggstream,
                                             st->codec->flags & CODEC_FLAG_BITEXACT,
                                             &s->metadata);
            if (err) {
                av_log(s, AV_LOG_ERROR, "Error writing FLAC headers\n");
                av_freep(&st->priv_data);
                return err;
            }
        } else if (st->codec->codec_id == AV_CODEC_ID_SPEEX) {
            int err = ogg_build_speex_headers(st->codec, oggstream,
                                              st->codec->flags & CODEC_FLAG_BITEXACT,
                                              &s->metadata);
            if (err) {
                av_log(s, AV_LOG_ERROR, "Error writing Speex headers\n");
                av_freep(&st->priv_data);
                return err;
            }
        } else if (st->codec->codec_id == AV_CODEC_ID_OPUS) {
            int err = ogg_build_opus_headers(st->codec, oggstream,
                                             st->codec->flags & CODEC_FLAG_BITEXACT,
                                             &s->metadata);
            if (err) {
                av_log(s, AV_LOG_ERROR, "Error writing Opus headers\n");
                av_freep(&st->priv_data);
                return err;
            }
        } else {
            uint8_t *p;
            const char *cstr = st->codec->codec_id == AV_CODEC_ID_VORBIS ? "vorbis" : "theora";
            int header_type = st->codec->codec_id == AV_CODEC_ID_VORBIS ? 3 : 0x81;
            int framing_bit = st->codec->codec_id == AV_CODEC_ID_VORBIS ? 1 : 0;

            if (avpriv_split_xiph_headers(st->codec->extradata, st->codec->extradata_size,
                                      st->codec->codec_id == AV_CODEC_ID_VORBIS ? 30 : 42,
                                      oggstream->header, oggstream->header_len) < 0) {
                av_log(s, AV_LOG_ERROR, "Extradata corrupted\n");
                av_freep(&st->priv_data);
                return -1;
            }

            p = ogg_write_vorbiscomment(7, st->codec->flags & CODEC_FLAG_BITEXACT,
                                        &oggstream->header_len[1], &s->metadata,
                                        framing_bit);
            oggstream->header[1] = p;
            if (!p)
                return AVERROR(ENOMEM);

            bytestream_put_byte(&p, header_type);
            bytestream_put_buffer(&p, cstr, 6);

            if (st->codec->codec_id == AV_CODEC_ID_THEORA) {
                /** KFGSHIFT is the width of the less significant section of the granule position
                    The less significant section is the frame count since the last keyframe */
                oggstream->kfgshift = ((oggstream->header[0][40]&3)<<3)|(oggstream->header[0][41]>>5);
                oggstream->vrev = oggstream->header[0][9];
                av_log(s, AV_LOG_DEBUG, "theora kfgshift %d, vrev %d\n",
                       oggstream->kfgshift, oggstream->vrev);
            }
        }
    }

    for (j = 0; j < s->nb_streams; j++) {
        OGGStreamContext *oggstream = s->streams[j]->priv_data;
        ogg_buffer_data(s, s->streams[j], oggstream->header[0],
                        oggstream->header_len[0], 0, 1);
        oggstream->page.flags |= 2; // bos
        ogg_buffer_page(s, oggstream);
    }
    for (j = 0; j < s->nb_streams; j++) {
        AVStream *st = s->streams[j];
        OGGStreamContext *oggstream = st->priv_data;
        for (i = 1; i < 3; i++) {
            if (oggstream->header_len[i])
                ogg_buffer_data(s, st, oggstream->header[i],
                                oggstream->header_len[i], 0, 1);
        }
        ogg_buffer_page(s, oggstream);
    }

    oggstream->page.start_granule = AV_NOPTS_VALUE;

    return 0;
}

static void ogg_write_pages(AVFormatContext *s, int flush)
{
    OGGContext *ogg = s->priv_data;
    OGGPageList *next, *p;

    if (!ogg->page_list)
        return;

    for (p = ogg->page_list; p; ) {
        OGGStreamContext *oggstream =
            s->streams[p->page.stream_index]->priv_data;
        if (oggstream->page_count < 2 && !flush)
            break;
        ogg_write_page(s, &p->page,
                       flush && oggstream->page_count == 1 ? 4 : 0); // eos
        next = p->next;
        av_freep(&p);
        p = next;
    }
    ogg->page_list = p;
}

static int ogg_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[pkt->stream_index];
    OGGStreamContext *oggstream = st->priv_data;
    int ret;
    int64_t granule;

    if (st->codec->codec_id == AV_CODEC_ID_THEORA) {
        int64_t pts = oggstream->vrev < 1 ? pkt->pts : pkt->pts + pkt->duration;
        int pframe_count;
        if (pkt->flags & AV_PKT_FLAG_KEY)
            oggstream->last_kf_pts = pts;
        pframe_count = pts - oggstream->last_kf_pts;
        // prevent frame count from overflow if key frame flag is not set
        if (pframe_count >= (1<<oggstream->kfgshift)) {
            oggstream->last_kf_pts += pframe_count;
            pframe_count = 0;
        }
        granule = (oggstream->last_kf_pts<<oggstream->kfgshift) | pframe_count;
    } else if (st->codec->codec_id == AV_CODEC_ID_OPUS)
        granule = pkt->pts + pkt->duration + av_rescale_q(st->codec->delay, (AVRational){ 1, st->codec->sample_rate }, st->time_base);
    else
        granule = pkt->pts + pkt->duration;

    if (oggstream->page.start_granule == AV_NOPTS_VALUE)
        oggstream->page.start_granule = pkt->pts;

    ret = ogg_buffer_data(s, st, pkt->data, pkt->size, granule, 0);
    if (ret < 0)
        return ret;

    ogg_write_pages(s, 0);

    oggstream->last_granule = granule;

    return 0;
}

static int ogg_write_trailer(AVFormatContext *s)
{
    int i;

    /* flush current page if needed */
    for (i = 0; i < s->nb_streams; i++) {
        OGGStreamContext *oggstream = s->streams[i]->priv_data;

        if (oggstream->page.size > 0)
            ogg_buffer_page(s, oggstream);
    }

    ogg_write_pages(s, 1);

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        OGGStreamContext *oggstream = st->priv_data;
        if (st->codec->codec_id == AV_CODEC_ID_FLAC ||
            st->codec->codec_id == AV_CODEC_ID_SPEEX ||
            st->codec->codec_id == AV_CODEC_ID_OPUS) {
            av_freep(&oggstream->header[0]);
        }
        av_freep(&oggstream->header[1]);
        av_freep(&st->priv_data);
    }
    return 0;
}

AVOutputFormat ff_ogg_muxer = {
    .name              = "ogg",
    .long_name         = NULL_IF_CONFIG_SMALL("Ogg"),
    .mime_type         = "application/ogg",
    .extensions        = "ogg,ogv,spx,opus",
    .priv_data_size    = sizeof(OGGContext),
    .audio_codec       = AV_CODEC_ID_FLAC,
    .video_codec       = AV_CODEC_ID_THEORA,
    .write_header      = ogg_write_header,
    .write_packet      = ogg_write_packet,
    .write_trailer     = ogg_write_trailer,
    .flags             = AVFMT_TS_NEGATIVE,
    .priv_class        = &ogg_muxer_class,
};
