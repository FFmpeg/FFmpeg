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

#include "config_components.h"

#include <stdint.h>

#include "libavutil/crc.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/random_seed.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/xiph.h"
#include "libavformat/avformat.h"
#include "libavformat/avio_internal.h"
#include "libavformat/internal.h"
#include "libavformat/metadata.h"
#include "libavformat/mux.h"
#include "libavformat/version.h"
#include "libavformat/vorbis/comment.h"
#include "muxer.h"

typedef struct OGGPageList {
    OGGPage page;
    struct OGGPageList *next;
} OGGPageList;

typedef struct OGGContext {
    const AVClass *class;
    OGGPageList *page_list;
    int64_t pref_duration;      ///< preferred page duration (0 => fill all segments)
    int serial_offset;
    int failed; // if true all packet submission will fail.
    int ended;  ///< a new link could not be started, the packets that follow are dropped
} OGGContext;

#define OFFSET(x) offsetof(OGGContext, x)
#define PARAM AV_OPT_FLAG_ENCODING_PARAM

static int ogg_write_trailer(AVFormatContext *s);

static const AVOption options[] = {
    { "serial_offset", "serial number offset, -1 to derive it from the stream headers",
        OFFSET(serial_offset), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, PARAM },
    { "page_duration", "preferred page duration, in microseconds",
        OFFSET(pref_duration), AV_OPT_TYPE_INT64, { .i64 = 1000000 }, 0, INT64_MAX, PARAM },
    { NULL },
};

static const AVClass ogg_muxer_class = {
    .class_name = "Ogg (audio/video/Speex/Opus) muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static void ogg_cleanup_stream(AVStream *st)
{
    OGGStreamContext *oggstream = st->priv_data;

    for (int i = 0; i < 3; i++)
        av_freep(&oggstream->header[i]);
    av_freep(&oggstream->priv_data);
}

static void ogg_write_page(AVFormatContext *s, OGGPage *page, int extra_flags)
{
    OGGStreamContext *oggstream = s->streams[page->stream_index]->priv_data;
    uint8_t buf[4 + 1 + 1 + 8 + 4 + 4 + 4 + 1 + 255], *ptr = buf, *crc_pos;
    const AVCRC *crc_table = av_crc_get_table(AV_CRC_32_IEEE);
    uint32_t crc;

    bytestream_put_le32(&ptr, MKTAG('O', 'g', 'g', 'S'));
    bytestream_put_byte(&ptr, 0);
    bytestream_put_byte(&ptr, page->flags | extra_flags);
    bytestream_put_le64(&ptr, page->granule);
    bytestream_put_le32(&ptr, oggstream->serial_num);
    bytestream_put_le32(&ptr, oggstream->page_counter++);
    crc_pos = ptr;
    bytestream_put_le32(&ptr, 0);
    bytestream_put_byte(&ptr, page->segments_count);
    bytestream_put_buffer(&ptr, page->segments, page->segments_count);

    crc = av_crc(crc_table, 0, buf, ptr - buf);
    crc = av_crc(crc_table, crc, page->data, page->size);
    bytestream_put_be32(&crc_pos, crc);

    avio_write(s->pb, buf, ptr - buf);
    avio_write(s->pb, page->data, page->size);
    avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_FLUSH_POINT);
    oggstream->page_count--;
}

static int ogg_key_granule(const OGGStreamContext *oggstream, int64_t granule)
{
    return oggstream->granule_key_mask && !(granule & oggstream->granule_key_mask);
}

static int64_t ogg_granule_to_timestamp(const OGGStreamContext *oggstream, int64_t granule)
{
    return (granule >> oggstream->granule_shift) + (granule & oggstream->granule_delta_mask);
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

    if (!l) {
        ogg_reset_cur_page(oggstream);
        return AVERROR(ENOMEM);
    }
    l->page = oggstream->page;

    oggstream->page.start_granule = ogg_granule_to_timestamp(oggstream, oggstream->page.granule);
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
                           const uint8_t *data, unsigned size, int64_t granule,
                           int header)
{
    OGGStreamContext *oggstream = st->priv_data;
    OGGContext *ogg = s->priv_data;
    int total_segments = size / 255 + 1;
    const uint8_t *p = data;
    int i, segments, len, ret, flush = 0;

    // Handles VFR by flushing page because this frame needs to have a timestamp
    // For theora and VP8, keyframes also need to have a timestamp to correctly mark
    // them as such, otherwise seeking will not work correctly at the very
    // least with old libogg versions.
    // Do not try to flush header packets though, that will create broken files.
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !header &&
        (ogg_granule_to_timestamp(oggstream, granule) >
         ogg_granule_to_timestamp(oggstream, oggstream->last_granule) + 1 ||
         ogg_key_granule(oggstream, granule))) {
        if (oggstream->page.granule != -1 &&
            (ret = ogg_buffer_page(s, oggstream)) < 0)
            return ret;
        flush = 1;
    }

    // avoid a continued page
    if (!header && oggstream->page.size > 0 &&
        MAX_PAGE_SIZE - oggstream->page.size < size) {
        if ((ret = ogg_buffer_page(s, oggstream)) < 0)
            return ret;
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
        if (len)
            memcpy(page->data+page->size, p, len);
        p += len;
        size -= len;
        i += segments;
        page->size += len;

        if (i == total_segments)
            page->granule = granule;

        {
            AVRational time_base = s->streams[page->stream_index]->time_base;

            int64_t start = av_rescale_q(page->start_granule, time_base,
                                         AV_TIME_BASE_Q);
            int64_t next  = av_rescale_q(ogg_granule_to_timestamp(oggstream, page->granule),
                                         time_base, AV_TIME_BASE_Q);

            if (page->segments_count == 255) {
                if ((ret = ogg_buffer_page(s, oggstream)) < 0)
                    return ret;
            } else if (!header) {
                if (ogg->pref_duration > 0 && next - start >= ogg->pref_duration) {
                    if ((ret = ogg_buffer_page(s, oggstream)) < 0)
                        return ret;
                }
            }
        }
    }

    if (flush && oggstream->page.granule != -1 &&
        (ret = ogg_buffer_page(s, oggstream)) < 0)
        return ret;

    return 0;
}

uint8_t *ff_ogg_write_vorbiscomment(int64_t offset, int bitexact,
                                    int *header_len, AVDictionary **m, int framing_bit,
                                    AVChapter **chapters, unsigned int nb_chapters)
{
    const char *vendor = bitexact ? "ffmpeg" : LIBAVFORMAT_IDENT;
    FFIOContext pb;
    int64_t size;
    uint8_t *p;

    ff_metadata_conv(m, ff_vorbis_comment_metadata_conv, NULL);

    size = ff_vorbis_comment_length(*m, vendor, chapters, nb_chapters);
    if (size < 0)
        return NULL;
    size += offset + framing_bit;
    if (size > INT_MAX)
        return NULL;
    p = av_mallocz(size);
    if (!p)
        return NULL;

    ffio_init_write_context(&pb, p + offset, size - offset);
    ff_vorbis_comment_write(&pb.pub, *m, vendor, chapters, nb_chapters);
    if (framing_bit)
        avio_w8(&pb.pub, 1);

    *header_len = size;
    return p;
}

int ff_ogg_build_xiph_headers(AVFormatContext *s, AVStream *st,
                              int first_header_size, int comment_type,
                              const char *name, int framing_bit)
{
    OGGStreamContext *oggstream = st->priv_data;
    const uint8_t *header[3];
    uint8_t *p;

    if (avpriv_split_xiph_headers(st->codecpar->extradata, st->codecpar->extradata_size,
                                  first_header_size, header, oggstream->header_len) < 0 ||
        oggstream->header_len[0] < first_header_size) {
        av_log(s, AV_LOG_ERROR, "Extradata corrupted\n");
        return AVERROR_INVALIDDATA;
    }

    oggstream->header[0] = av_memdup(header[0], oggstream->header_len[0]);
    oggstream->header[2] = av_memdup(header[2], oggstream->header_len[2]);
    if (!oggstream->header[0] || !oggstream->header[2])
        return AVERROR(ENOMEM);

    p = ff_ogg_write_vorbiscomment(7, s->flags & AVFMT_FLAG_BITEXACT,
                                   &oggstream->header_len[1], &st->metadata,
                                   framing_bit, NULL, 0);
    oggstream->header[1] = p;
    if (!p)
        return AVERROR(ENOMEM);

    bytestream_put_byte(&p, comment_type);
    bytestream_put_buffer(&p, name, 6);

    return 0;
}

static const OGGMuxerMapping *const ogg_mappings[] = {
    &ff_ogg_flac_muxer_mapping,
    &ff_ogg_opus_muxer_mapping,
    &ff_ogg_speex_muxer_mapping,
    &ff_ogg_theora_muxer_mapping,
    &ff_ogg_vorbis_muxer_mapping,
    &ff_ogg_vp8_muxer_mapping,
};

static const OGGMuxerMapping *ogg_find_mapping(enum AVCodecID codec_id)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(ogg_mappings); i++)
        if (ogg_mappings[i]->codec_id == codec_id)
            return ogg_mappings[i];
    return NULL;
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
                       flush == 1 && oggstream->page_count == 1 ? 4 : 0); // eos
        next = p->next;
        av_freep(&p);
        p = next;
    }
    ogg->page_list = p;
}

/**
 * Derive a bitexact serial number from the stream's header packets, which
 * makes links muxed apart unlikely to share one, though not certain not to.
 */
static unsigned ogg_bitexact_serial(const OGGStreamContext *oggstream, unsigned seed)
{
    const AVCRC *table = av_crc_get_table(AV_CRC_32_IEEE);
    uint32_t crc = seed;

    for (int i = 0; i < 3; i++)
        if (oggstream->header_len[i])
            crc = av_crc(table, crc, oggstream->header[i], oggstream->header_len[i]);

    return crc;
}

static int ogg_serial_in_use(AVFormatContext *s, int nb_streams, unsigned serial_num)
{
    for (int i = 0; i < nb_streams; i++) {
        const OGGStreamContext *oggstream = s->streams[i]->priv_data;
        if (oggstream->serial_num == serial_num)
            return 1;
    }
    return 0;
}

// This function can be used on an initialized context to reinitialize the
// streams.
static int ogg_init(AVFormatContext *s)
{
    OGGContext *ogg = s->priv_data;
    OGGStreamContext *oggstream = NULL;
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        unsigned serial_num;

        const OGGMuxerMapping *mapping = ogg_find_mapping(st->codecpar->codec_id);
        int err;

        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

        if (!mapping) {
            av_log(s, AV_LOG_ERROR, "Unsupported codec id in stream %d\n", i);
            return AVERROR(EINVAL);
        }

        if ((!st->codecpar->extradata || !st->codecpar->extradata_size) &&
            !mapping->optional_extradata) {
            av_log(s, AV_LOG_ERROR, "No extradata present\n");
            return AVERROR_INVALIDDATA;
        }
        oggstream = st->priv_data;

        if (!oggstream) {
            oggstream = av_mallocz(sizeof(*oggstream));
            if (!oggstream)
                return AVERROR(ENOMEM);
            st->priv_data = oggstream;
        } else {
            ogg_cleanup_stream(st);
            memset(oggstream, 0, sizeof(*oggstream));
        }

        oggstream->page.stream_index = i;
        oggstream->mapping = mapping;
        if (mapping->priv_data_size) {
            oggstream->priv_data = av_mallocz(mapping->priv_data_size);
            if (!oggstream->priv_data)
                return AVERROR(ENOMEM);
        }

        av_dict_copy(&st->metadata, s->metadata, AV_DICT_DONT_OVERWRITE);

        err = mapping->init(s, st);
        if (err < 0)
            return err;

        if (s->flags & AVFMT_FLAG_BITEXACT && ogg->serial_offset >= 0)
            serial_num = i + ogg->serial_offset;
        else if (s->flags & AVFMT_FLAG_BITEXACT)
            serial_num = ogg_bitexact_serial(oggstream, i);
        else
            serial_num = av_get_random_seed();
        while (ogg_serial_in_use(s, i, serial_num))
            serial_num = s->flags & AVFMT_FLAG_BITEXACT ? serial_num + 1
                                                        : av_get_random_seed();
        oggstream->serial_num = serial_num;
    }

    return 0;
}

static int ogg_write_header(AVFormatContext *s)
{
    OGGStreamContext *oggstream = NULL;
    int i, j, ret;

    for (j = 0; j < s->nb_streams; j++) {
        oggstream = s->streams[j]->priv_data;
        ret = ogg_buffer_data(s, s->streams[j], oggstream->header[0],
                              oggstream->header_len[0], 0, 1);
        if (ret < 0)
            return ret;
        oggstream->page.flags |= 2; // bos
        ret = ogg_buffer_page(s, oggstream);
        if (ret < 0)
            return ret;
    }
    for (j = 0; j < s->nb_streams; j++) {
        AVStream *st = s->streams[j];
        oggstream = st->priv_data;
        for (i = 1; i < 3; i++) {
            if (oggstream->header_len[i]) {
                ret = ogg_buffer_data(s, st, oggstream->header[i],
                                      oggstream->header_len[i], 0, 1);
                if (ret < 0)
                    return ret;
            }
        }
        ret = ogg_buffer_page(s, oggstream);
        if (ret < 0)
            return ret;
    }

    oggstream->page.start_granule = AV_NOPTS_VALUE;

    ogg_write_pages(s, 2);

    return 0;
}

/**
 * Keep the current link when a new one cannot be started. New metadata is
 * then dropped, while new extradata ends the output, as the packets that
 * follow need its headers.
 */
static int ogg_skip_new_link(AVFormatContext *s, const uint8_t *new_extradata,
                             const char *reason)
{
    OGGContext *ogg = s->priv_data;

    if (!new_extradata) {
        av_log(s, AV_LOG_WARNING, "%s: new metadata ignored\n", reason);
        return 0;
    }

    av_log(s, AV_LOG_WARNING, "%s: ending the output at the current link\n", reason);
    ogg->ended = 1;
    return 0;
}

static int ogg_check_new_link(AVFormatContext *s, AVPacket *pkt)
{
    int ret = 0;
    const char *refused;
    size_t metadata_size, extradata_size;
    OGGContext *oggcontext = s->priv_data;
    AVStream *st = s->streams[pkt->stream_index];
    AVCodecParameters *par = st->codecpar;
    OGGStreamContext *oggstream = st->priv_data;
    const uint8_t *side_metadata = av_packet_get_side_data(pkt, AV_PKT_DATA_STRINGS_METADATA, &metadata_size);
    const uint8_t *new_extradata = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, &extradata_size);

    if (new_extradata && extradata_size == par->extradata_size &&
        !memcmp(new_extradata, par->extradata, extradata_size))
        new_extradata = NULL;

    if (!side_metadata && !new_extradata)
        return 0;

    /* The headers of the first link are written from codecpar before any packet. */
    if (!oggstream->packet_seen) {
        if (new_extradata)
            av_log(s, AV_LOG_WARNING, "New extradata on the first packet ignored: "
                   "the stream headers are already written!\n");
        return 0;
    }

    if (s->nb_streams > 1)
        return ogg_skip_new_link(s, new_extradata, "Multiple streams present");

    if (!oggstream->mapping->new_links)
        return ogg_skip_new_link(s, new_extradata,
                                 "This codec does not support starting a new Ogg link");

    if (oggstream->mapping->new_link_refused &&
        (refused = oggstream->mapping->new_link_refused(st, pkt, new_extradata, extradata_size)))
        return ogg_skip_new_link(s, new_extradata, refused);

    ret = ogg_write_trailer(s);
    if (ret < 0)
        goto end;

    if (side_metadata) {
        av_dict_free(&st->metadata);
        ret = av_packet_unpack_dictionary(side_metadata, metadata_size, &st->metadata);
        if (ret < 0)
            goto end;
    }

    if (new_extradata) {
        if (extradata_size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE) {
            ret = AVERROR_INVALIDDATA;
            goto end;
        }
        ret = ff_alloc_extradata(par, extradata_size);
        if (ret < 0)
            goto end;
        memcpy(par->extradata, new_extradata, extradata_size);
    }

    ret = ogg_init(s);
    if (ret < 0)
        goto end;

    ret = ogg_write_header(s);

end:
    oggcontext->failed = ret < 0;
    return ret;
}

static int ogg_write_packet_internal(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[pkt->stream_index];
    OGGContext *oggcontext = s->priv_data;
    OGGStreamContext *oggstream = st->priv_data;
    int ret;
    int64_t granule;

    if (oggcontext->failed)
        return AVERROR_INVALIDDATA;
    if (oggcontext->ended)
        return 0;

    ret = ogg_check_new_link(s, pkt);
    if (ret < 0 || oggcontext->ended)
        return ret;

    if (oggstream->mapping->packet_granule)
        granule = oggstream->mapping->packet_granule(st, pkt);
    else
        granule = pkt->pts + pkt->duration + oggstream->packet_granule_offset;

    if (oggstream->page.start_granule == AV_NOPTS_VALUE)
        oggstream->page.start_granule = pkt->pts;

    ret = ogg_buffer_data(s, st, pkt->data, pkt->size, granule, 0);
    if (ret < 0)
        return ret;

    ogg_write_pages(s, 0);

    oggstream->last_granule = granule;
    oggstream->packet_seen = 1;

    return 0;
}

static int ogg_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    int i, ret;

    if (pkt)
        return pkt->size || !pkt->side_data_elems ? ogg_write_packet_internal(s, pkt) : 0;

    for (i = 0; i < s->nb_streams; i++) {
        OGGStreamContext *oggstream = s->streams[i]->priv_data;
        if (oggstream->page.segments_count &&
            (ret = ogg_buffer_page(s, oggstream)) < 0)
            return ret;
    }

    ogg_write_pages(s, 2);
    return 1;
}

static int ogg_write_trailer(AVFormatContext *s)
{
    int i, ret;

    /* flush current page if needed */
    for (i = 0; i < s->nb_streams; i++) {
        OGGStreamContext *oggstream = s->streams[i]->priv_data;

        if (oggstream->page.segments_count &&
            (ret = ogg_buffer_page(s, oggstream)) < 0)
            return ret;
    }

    ogg_write_pages(s, 1);

    return 0;
}

static void ogg_free(AVFormatContext *s)
{
    OGGContext *ogg = s->priv_data;
    OGGPageList *p = ogg->page_list;
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        if (st->priv_data)
            ogg_cleanup_stream(st);
    }

    while (p) {
        OGGPageList *next = p->next;
        av_free(p);
        p = next;
    }
    ogg->page_list = NULL;
}

#if CONFIG_OGG_MUXER
const FFOutputFormat ff_ogg_muxer = {
    .p.name            = "ogg",
    .p.long_name       = NULL_IF_CONFIG_SMALL("Ogg"),
    .p.mime_type       = "application/ogg",
    .p.extensions      = "ogg"
#if !CONFIG_OGV_MUXER
                         ",ogv"
#endif
#if !CONFIG_SPX_MUXER
                         ",spx"
#endif
#if !CONFIG_OPUS_MUXER
                         ",opus"
#endif
                         ,
    .priv_data_size    = sizeof(OGGContext),
    .p.audio_codec     = CONFIG_LIBVORBIS_ENCODER ?
                         AV_CODEC_ID_VORBIS : AV_CODEC_ID_FLAC,
    .p.video_codec     = AV_CODEC_ID_THEORA,
    .init              = ogg_init,
    .write_header      = ogg_write_header,
    .write_packet      = ogg_write_packet,
    .write_trailer     = ogg_write_trailer,
    .deinit            = ogg_free,
    .p.flags           = AVFMT_TS_NEGATIVE | AVFMT_TS_NONSTRICT,
    .p.priv_class      = &ogg_muxer_class,
    .flags_internal    = FF_OFMT_FLAG_ALLOW_FLUSH,
};
#endif

#if CONFIG_OGA_MUXER
const FFOutputFormat ff_oga_muxer = {
    .p.name            = "oga",
    .p.long_name       = NULL_IF_CONFIG_SMALL("Ogg Audio"),
    .p.mime_type       = "audio/ogg",
    .p.extensions      = "oga",
    .priv_data_size    = sizeof(OGGContext),
    .p.audio_codec     = AV_CODEC_ID_FLAC,
    .init              = ogg_init,
    .write_header      = ogg_write_header,
    .write_packet      = ogg_write_packet,
    .write_trailer     = ogg_write_trailer,
    .deinit            = ogg_free,
    .p.flags           = AVFMT_TS_NEGATIVE,
    .p.priv_class      = &ogg_muxer_class,
    .flags_internal    = FF_OFMT_FLAG_ALLOW_FLUSH,
};
#endif

#if CONFIG_OGV_MUXER
const FFOutputFormat ff_ogv_muxer = {
    .p.name            = "ogv",
    .p.long_name       = NULL_IF_CONFIG_SMALL("Ogg Video"),
    .p.mime_type       = "video/ogg",
    .p.extensions      = "ogv",
    .priv_data_size    = sizeof(OGGContext),
    .p.audio_codec     = CONFIG_LIBVORBIS_ENCODER ?
                         AV_CODEC_ID_VORBIS : AV_CODEC_ID_FLAC,
    .p.video_codec     = CONFIG_LIBTHEORA_ENCODER ?
                         AV_CODEC_ID_THEORA : AV_CODEC_ID_VP8,
    .init              = ogg_init,
    .write_header      = ogg_write_header,
    .write_packet      = ogg_write_packet,
    .write_trailer     = ogg_write_trailer,
    .deinit            = ogg_free,
    .p.flags           = AVFMT_TS_NEGATIVE | AVFMT_TS_NONSTRICT,
    .p.priv_class      = &ogg_muxer_class,
    .flags_internal    = FF_OFMT_FLAG_ALLOW_FLUSH,
};
#endif

#if CONFIG_SPX_MUXER
const FFOutputFormat ff_spx_muxer = {
    .p.name            = "spx",
    .p.long_name       = NULL_IF_CONFIG_SMALL("Ogg Speex"),
    .p.mime_type       = "audio/ogg",
    .p.extensions      = "spx",
    .priv_data_size    = sizeof(OGGContext),
    .p.audio_codec     = AV_CODEC_ID_SPEEX,
    .init              = ogg_init,
    .write_header      = ogg_write_header,
    .write_packet      = ogg_write_packet,
    .write_trailer     = ogg_write_trailer,
    .deinit            = ogg_free,
    .p.flags           = AVFMT_TS_NEGATIVE,
    .p.priv_class      = &ogg_muxer_class,
    .flags_internal    = FF_OFMT_FLAG_ALLOW_FLUSH,
};
#endif

#if CONFIG_OPUS_MUXER
const FFOutputFormat ff_opus_muxer = {
    .p.name            = "opus",
    .p.long_name       = NULL_IF_CONFIG_SMALL("Ogg Opus"),
    .p.mime_type       = "audio/ogg",
    .p.extensions      = "opus",
    .priv_data_size    = sizeof(OGGContext),
    .p.audio_codec     = AV_CODEC_ID_OPUS,
    .init              = ogg_init,
    .write_header      = ogg_write_header,
    .write_packet      = ogg_write_packet,
    .write_trailer     = ogg_write_trailer,
    .deinit            = ogg_free,
    .p.flags           = AVFMT_TS_NEGATIVE,
    .p.priv_class      = &ogg_muxer_class,
    .flags_internal    = FF_OFMT_FLAG_ALLOW_FLUSH,
};
#endif
