/*
 * HLS ID3 timestamped MPEG audio elementary stream
 * Copyright (c) 2014 Anssi Hannula
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

#include "libavutil/bswap.h"
#include "avformat.h"
#include "avio_internal.h"
#include "id3v2.h"
#include "internal.h"
#include "rawdec.h"

typedef struct HLSID3AudioContext {
    const AVClass *av_class;

    int64_t segment_end;

    /* read by read_header() */
    int64_t first_offset;
    int64_t first_dts;

    int metadata_changed;

    AVDictionary *first_id3_metadata;
} HLSID3AudioContext;

#define OFFSET(x) offsetof(HLSID3AudioContext, x)
static const AVOption options[] = {
/* This parameter has to be continuously updated during playback in order
 * for the demuxer to correctly catch all the timestamps in the stream.
 * This is normally done by the HLS demuxer. Without this parameter only the
 * first timestamp in the beginning of the stream will be properly parsed. */
{ "segment_end", "offset of the end of the current HLS segment in the input data stream", OFFSET(segment_end), AV_OPT_TYPE_INT64, {.i64 = -1}, INT64_MIN, INT64_MAX, AV_OPT_FLAG_DECODING_PARAM },
{ NULL },
};

static const char id3_priv_owner_ts[] = "com.apple.streaming.transportStreamTimestamp";

static int parse_id3(AVFormatContext *s, AVIOContext *pb,
                     AVDictionary **metadata_param, int64_t *dts,
                     ID3v2ExtraMetaAPIC **apic, ID3v2ExtraMeta **extrameta_list)
{
    AVDictionary *metadata_tmp = NULL;
    AVDictionary **metadata = metadata_param ? metadata_param : &metadata_tmp;
    ID3v2ExtraMeta *extra_meta = NULL;
    ID3v2ExtraMeta *meta;
    int ret = AVERROR_INVALIDDATA;

    ff_id3v2_read_dict(pb, metadata, ID3v2_DEFAULT_MAGIC, &extra_meta);
    for (meta = extra_meta; meta; meta = meta->next) {
        if (!strcmp(meta->tag, "PRIV")) {
            ID3v2ExtraMetaPRIV *priv = meta->data;
            if (priv->datasize == 8 && !strcmp(priv->owner, id3_priv_owner_ts)) {
                /* 33-bit MPEG timestamp */
                if (dts) {
                    *dts = av_be2ne64(*(uint64_t *)priv->data);
                    av_log(s, AV_LOG_DEBUG, "HLS ID3 audio timestamp %"PRId64"\n", *dts);
                }
                ret = 0;
            }
        } else if (!strcmp(meta->tag, "APIC") && apic)
            *apic = meta->data;
    }

    if (extrameta_list)
        *extrameta_list = extra_meta;
    else
        ff_id3v2_free_extra_meta(&extra_meta);

    av_dict_free(&metadata_tmp);

    return ret;
}

static int has_changed_values(AVFormatContext *s, AVDictionary *metadata,
                              ID3v2ExtraMetaAPIC *apic)
{
    HLSID3AudioContext *ctx = s->priv_data;
    AVDictionaryEntry *entry = NULL;
    AVDictionaryEntry *oldentry;
    /* check that no keys have changed values */
    while ((entry = av_dict_get(metadata, "", entry, AV_DICT_IGNORE_SUFFIX))) {
        oldentry = av_dict_get(ctx->first_id3_metadata, entry->key, NULL, AV_DICT_MATCH_CASE);
        if (!oldentry || strcmp(oldentry->value, entry->value) != 0)
            return 1;
    }

    /* check if apic appeared */
    if (apic && (s->nb_streams != 2 || !s->streams[1]->attached_pic.data))
        return 1;

    if (apic) {
        int size = s->streams[1]->attached_pic.size;
        if (size != apic->buf->size - FF_INPUT_BUFFER_PADDING_SIZE)
            return 1;

        if (memcmp(apic->buf->data, s->streams[1]->attached_pic.data, size) != 0)
            return 1;
    }

    return 0;
}

static int handle_id3(AVFormatContext *s, int64_t *dts, int is_beginning)
{
    HLSID3AudioContext *ctx = s->priv_data;
    AVDictionary *metadata = NULL;
    ID3v2ExtraMetaAPIC *apic = NULL;
    ID3v2ExtraMeta *extra_meta = NULL;
    int ret;

    ret = parse_id3(s, s->pb, &metadata, dts, &apic, &extra_meta);

    if (is_beginning) {
        /* get picture attachment and set text metadata */
        ff_id3v2_parse_apic(s, &extra_meta);
        av_dict_copy(&s->metadata, metadata, 0);
        ctx->first_id3_metadata = metadata;

    } else {
        if (!ctx->metadata_changed && has_changed_values(s, metadata, apic)) {
            avpriv_report_missing_feature(s, "Changing ID3 metadata in HLS audio elementary stream");
            ctx->metadata_changed = 1;
        }
        av_dict_free(&metadata);
    }

    ff_id3v2_free_extra_meta(&extra_meta);

    if (ret != 0)
        av_log(s, is_beginning ? AV_LOG_ERROR : AV_LOG_WARNING,
               "Missing ID3 timestamp in HLS audio elementary stream\n");

    return ret;
}

#if 1
#define MAX_RAW_PACKET_SIZE (1024*1024)
#else
#define MAX_RAW_PACKET_SIZE 1024
#endif

static int hls_id3_audio_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    HLSID3AudioContext *ctx = s->priv_data;
    int64_t cur_pos = avio_tell(s->pb);
    int64_t dts = AV_NOPTS_VALUE;
    int pkt_size = MAX_RAW_PACKET_SIZE;
    int ret;

    if (cur_pos == ctx->segment_end) {
        handle_id3(s, &dts, 0);
        cur_pos = avio_tell(s->pb);
    } else if (cur_pos == ctx->first_offset)
        dts = ctx->first_dts;
    
    if (cur_pos < ctx->segment_end)
        pkt_size = FFMIN(pkt_size, ctx->segment_end - cur_pos);

    ret = av_get_packet(s->pb, pkt, pkt_size);

    if (ret < 0)
        return ret;

    if (dts != AV_NOPTS_VALUE)
        pkt->dts = dts;

    return 0;
}

static int hls_id3_audio_close(AVFormatContext *s)
{
    HLSID3AudioContext *ctx = s->priv_data;

    av_dict_free(&ctx->first_id3_metadata);
    return 0;
}

static int hls_id3_audio_read_header(AVFormatContext *s)
{
    HLSID3AudioContext *ctx = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);
    int ret = 0;

    if (!st)
        return AVERROR(ENOMEM);

    st->need_parsing = AVSTREAM_PARSE_FULL;
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->request_probe = 1;

    avpriv_set_pts_info(st, 33, 1, 90000);

    ret = handle_id3(s, &ctx->first_dts, 1);
    ctx->first_offset = avio_tell(s->pb);

    if (ret)
        goto fail;


    return 0;

fail:
    hls_id3_audio_close(s);
    return ret;
}

#if 1

static int hls_id3_audio_probe(AVProbeData *p)
{
    if (ff_id3v2_match(p->buf, ID3v2_DEFAULT_MAGIC)) {
        AVIOContext id3ioctx;
        int taglen = ff_id3v2_tag_len(p->buf);

        /* alternatively we could search for string
         * "com.apple.streaming.transportStreamTimestamp" here, but that
         * would make the assumption that the ID3 tag is uncompressed,
         * which is not specified by the HLS specification */

        ffio_init_context(&id3ioctx, p->buf, FFMIN(taglen, p->buf_size),
                          0, NULL, NULL, NULL, NULL);

        if (parse_id3(NULL, &id3ioctx, NULL, NULL, NULL, NULL) == 0)
            /* HLS audio elementary stream timestamp found.
             * Still a relatively low score since it is just an id3 tag,
             * but this is high enough to prefer this demuxer over raw
             * demuxers. */
            return AVPROBE_SCORE_MAX * 3 / 4;

        if (taglen > p->buf_size)
            /* maybe just a too small buffer, so we do not know for sure yet */
            return AVPROBE_SCORE_MAX / 8;
    }

    return 0;
}

#else

static int hls_id3_audio_probe(AVProbeData *p)
{
    if (ff_id3v2_match(p->buf, ID3v2_DEFAULT_MAGIC)) {
        int taglen = ff_id3v2_tag_len(p->buf);
        int search_end = FFMIN(taglen, p->buf_size) - sizeof(id3_priv_owner_ts) + 1;
        const uint8_t *ptr = p->buf;
        
        /* This assumes that the ID3 tag is uncompressed... */
        while (ptr - p->buf < search_end) {
            ptr = memchr(ptr, id3_priv_owner_ts[0], search_end - (ptr - p->buf));
            if (!ptr)
                break;

            if (!memcmp(ptr, id3_priv_owner_ts, sizeof(id3_priv_owner_ts))) {
                return AVPROBE_SCORE_EXTENSION + 1;
            }
            else
                ptr++;
        }

        if (taglen > p->buf_size) {
            /* maybe just a too small buffer, so we do not know for sure yet */
            return AVPROBE_SCORE_MAX / 8;
        }
    }

    return 0;
}

#endif

static const AVClass hls_id3_audio_class = {
    .class_name  = "hls_id3_audio",
    .item_name   = av_default_item_name,
    .option      = options,
    .version     = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_hls_id3_audio_demuxer = {
    .name           = "hls_id3_audio",
    .long_name      = NULL_IF_CONFIG_SMALL("HLS ID3 timestamped MPEG audio elementary stream"),
    .priv_data_size = sizeof(HLSID3AudioContext),
    .read_probe     = hls_id3_audio_probe,
    .read_header    = hls_id3_audio_read_header,
    .read_packet    = hls_id3_audio_read_packet,
    .read_close     = hls_id3_audio_close,
    .flags          = AVFMT_GENERIC_INDEX | AVFMT_RAW_ID3,
    .priv_class     = &hls_id3_audio_class,
};
