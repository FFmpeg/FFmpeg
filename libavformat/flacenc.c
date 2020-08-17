/*
 * raw FLAC muxer
 * Copyright (c) 2006-2009 Justin Ruggles
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

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/flac.h"
#include "libavcodec/packet_internal.h"
#include "avformat.h"
#include "avio_internal.h"
#include "flacenc.h"
#include "id3v2.h"
#include "internal.h"
#include "vorbiscomment.h"


typedef struct FlacMuxerContext {
    const AVClass *class;
    int write_header;

    int audio_stream_idx;
    int waiting_pics;
    /* audio packets are queued here until we get all the attached pictures */
    AVPacketList *queue, *queue_end;

    /* updated streaminfo sent by the encoder at the end */
    uint8_t streaminfo[FLAC_STREAMINFO_SIZE];
    int updated_streaminfo;

    unsigned attached_types;
} FlacMuxerContext;

static int flac_write_block_padding(AVIOContext *pb, unsigned int n_padding_bytes,
                                    int last_block)
{
    avio_w8(pb, last_block ? 0x81 : 0x01);
    avio_wb24(pb, n_padding_bytes);
    ffio_fill(pb, 0, n_padding_bytes);
    return 0;
}

static int flac_write_block_comment(AVIOContext *pb, AVDictionary **m,
                                    int last_block, int bitexact)
{
    const char *vendor = bitexact ? "ffmpeg" : LIBAVFORMAT_IDENT;
    int64_t len;

    ff_metadata_conv(m, ff_vorbiscomment_metadata_conv, NULL);

    len = ff_vorbiscomment_length(*m, vendor, NULL, 0);
    if (len >= ((1<<24) - 4))
        return AVERROR(EINVAL);

    avio_w8(pb, last_block ? 0x84 : 0x04);
    avio_wb24(pb, len);
    ff_vorbiscomment_write(pb, *m, vendor, NULL, 0);

    return 0;
}

static int flac_write_picture(struct AVFormatContext *s, AVPacket *pkt)
{
    FlacMuxerContext *c = s->priv_data;
    AVIOContext *pb = s->pb;
    const AVPixFmtDescriptor *pixdesc;
    const CodecMime *mime = ff_id3v2_mime_tags;
    AVDictionaryEntry *e;
    const char *mimetype = NULL, *desc = "";
    const AVStream *st = s->streams[pkt->stream_index];
    int i, mimelen, desclen, type = 0, blocklen;

    if (!pkt->data)
        return 0;

    while (mime->id != AV_CODEC_ID_NONE) {
        if (mime->id == st->codecpar->codec_id) {
            mimetype = mime->str;
            break;
        }
        mime++;
    }
    if (!mimetype) {
        av_log(s, AV_LOG_ERROR, "No mimetype is known for stream %d, cannot "
               "write an attached picture.\n", st->index);
        return AVERROR(EINVAL);
    }
    mimelen = strlen(mimetype);

    /* get the picture type */
    e = av_dict_get(st->metadata, "comment", NULL, 0);
    for (i = 0; e && i < FF_ARRAY_ELEMS(ff_id3v2_picture_types); i++) {
        if (!av_strcasecmp(e->value, ff_id3v2_picture_types[i])) {
            type = i;
            break;
        }
    }

    if ((c->attached_types & (1 << type)) & 0x6) {
        av_log(s, AV_LOG_ERROR, "Duplicate attachment for type '%s'\n", ff_id3v2_picture_types[type]);
        return AVERROR(EINVAL);
    }

    if (type == 1 && (st->codecpar->codec_id != AV_CODEC_ID_PNG ||
                      st->codecpar->width != 32 ||
                      st->codecpar->height != 32)) {
        av_log(s, AV_LOG_ERROR, "File icon attachment must be a 32x32 PNG");
        return AVERROR(EINVAL);
    }

    c->attached_types |= (1 << type);

    /* get the description */
    if ((e = av_dict_get(st->metadata, "title", NULL, 0)))
        desc = e->value;
    desclen = strlen(desc);

    blocklen = 4 + 4 + mimelen + 4 + desclen + 4 + 4 + 4 + 4 + 4 + pkt->size;
    if (blocklen >= 1<<24) {
        av_log(s, AV_LOG_ERROR, "Picture block too big %d >= %d\n", blocklen, 1<<24);
        return AVERROR(EINVAL);
    }

    avio_w8(pb, 0x06);
    avio_wb24(pb, blocklen);

    avio_wb32(pb, type);

    avio_wb32(pb, mimelen);
    avio_write(pb, mimetype, mimelen);

    avio_wb32(pb, desclen);
    avio_write(pb, desc, desclen);

    avio_wb32(pb, st->codecpar->width);
    avio_wb32(pb, st->codecpar->height);
    if ((pixdesc = av_pix_fmt_desc_get(st->codecpar->format)))
        avio_wb32(pb, av_get_bits_per_pixel(pixdesc));
    else
        avio_wb32(pb, 0);
    avio_wb32(pb, 0);

    avio_wb32(pb, pkt->size);
    avio_write(pb, pkt->data, pkt->size);
    return 0;
}

static int flac_finish_header(struct AVFormatContext *s)
{
    int i, ret, padding = s->metadata_header_padding;
    if (padding < 0)
        padding = 8192;
    /* The FLAC specification states that 24 bits are used to represent the
     * size of a metadata block so we must clip this value to 2^24-1. */
    padding = av_clip_uintp2(padding, 24);

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        AVPacket *pkt = st->priv_data;
        if (!pkt)
            continue;
        ret = flac_write_picture(s, pkt);
        av_packet_unref(pkt);
        if (ret < 0 && (s->error_recognition & AV_EF_EXPLODE))
            return ret;
    }

    ret = flac_write_block_comment(s->pb, &s->metadata, !padding,
                                   s->flags & AVFMT_FLAG_BITEXACT);
    if (ret)
        return ret;

    /* The command line flac encoder defaults to placing a seekpoint
     * every 10s.  So one might add padding to allow that later
     * but there seems to be no simple way to get the duration here.
     * So just add the amount requested by the user. */
    if (padding)
        flac_write_block_padding(s->pb, padding, 1);

    return 0;
}

static int flac_init(struct AVFormatContext *s)
{
    AVCodecParameters *par;
    FlacMuxerContext *c = s->priv_data;
    int i;

    c->audio_stream_idx = -1;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (c->audio_stream_idx >= 0 || st->codecpar->codec_id != AV_CODEC_ID_FLAC) {
                av_log(s, AV_LOG_ERROR, "Invalid audio stream. Exactly one FLAC "
                       "audio stream is required.\n");
                return AVERROR(EINVAL);
            }
            par = s->streams[i]->codecpar;
            c->audio_stream_idx = i;
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (!(st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
                av_log(s, AV_LOG_WARNING, "Video stream #%d is not an attached picture. Ignoring\n", i);
                continue;
            } else if (st->codecpar->codec_id == AV_CODEC_ID_GIF) {
                av_log(s, AV_LOG_ERROR, "GIF image support is not implemented.\n");
                return AVERROR_PATCHWELCOME;
            } else if (!c->write_header) {
                av_log(s, AV_LOG_ERROR, "Can't write attached pictures without a header.\n");
                return AVERROR(EINVAL);
            }
            c->waiting_pics++;
        } else {
            av_log(s, AV_LOG_ERROR, "Only audio streams and pictures are allowed in FLAC.\n");
            return AVERROR(EINVAL);
        }
    }
    if (c->audio_stream_idx < 0) {
        av_log(s, AV_LOG_ERROR, "No audio stream present.\n");
        return AVERROR(EINVAL);
    }

    /* add the channel layout tag */
    if (par->channel_layout &&
        !(par->channel_layout & ~0x3ffffULL) &&
        !ff_flac_is_native_layout(par->channel_layout)) {
        AVDictionaryEntry *chmask = av_dict_get(s->metadata, "WAVEFORMATEXTENSIBLE_CHANNEL_MASK",
                                                NULL, 0);

        if (chmask) {
            av_log(s, AV_LOG_WARNING, "A WAVEFORMATEXTENSIBLE_CHANNEL_MASK is "
                   "already present, this muxer will not overwrite it.\n");
        } else {
            uint8_t buf[32];
            snprintf(buf, sizeof(buf), "0x%"PRIx64, par->channel_layout);
            av_dict_set(&s->metadata, "WAVEFORMATEXTENSIBLE_CHANNEL_MASK", buf, 0);
        }
    }

    return 0;
}

static int flac_write_header(struct AVFormatContext *s)
{
    FlacMuxerContext *c = s->priv_data;
    AVCodecParameters *par = s->streams[c->audio_stream_idx]->codecpar;
    int ret;

    if (!c->write_header)
        return 0;

    ret = ff_flac_write_header(s->pb, par->extradata,
                               par->extradata_size, 0);
    if (ret < 0)
        return ret;

    if (!c->waiting_pics)
        ret = flac_finish_header(s);

    return ret;
}

static int flac_write_audio_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    FlacMuxerContext *c = s->priv_data;
    uint8_t *streaminfo;
    int streaminfo_size;

    /* check for updated streaminfo */
    streaminfo = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
                                         &streaminfo_size);
    if (streaminfo && streaminfo_size == FLAC_STREAMINFO_SIZE) {
        memcpy(c->streaminfo, streaminfo, FLAC_STREAMINFO_SIZE);
        c->updated_streaminfo = 1;
    }

    if (pkt->size)
        avio_write(s->pb, pkt->data, pkt->size);
    return 0;
}

static int flac_queue_flush(AVFormatContext *s)
{
    FlacMuxerContext *c = s->priv_data;
    AVPacket pkt;
    int ret, write = 1;

    ret = flac_finish_header(s);
    if (ret < 0)
        write = 0;

    while (c->queue) {
        avpriv_packet_list_get(&c->queue, &c->queue_end, &pkt);
        if (write && (ret = flac_write_audio_packet(s, &pkt)) < 0)
            write = 0;
        av_packet_unref(&pkt);
    }
    return ret;
}

static int flac_write_trailer(struct AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    int64_t file_size;
    FlacMuxerContext *c = s->priv_data;

    if (c->waiting_pics) {
        av_log(s, AV_LOG_WARNING, "No packets were sent for some of the "
               "attached pictures.\n");
        flac_queue_flush(s);
    }

    if (!c->write_header || !c->updated_streaminfo)
        return 0;

    if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
        /* rewrite the STREAMINFO header block data */
        file_size = avio_tell(pb);
        avio_seek(pb, 8, SEEK_SET);
        avio_write(pb, c->streaminfo, FLAC_STREAMINFO_SIZE);
        avio_seek(pb, file_size, SEEK_SET);
    } else {
        av_log(s, AV_LOG_WARNING, "unable to rewrite FLAC header.\n");
    }

    return 0;
}

static void flac_deinit(struct AVFormatContext *s)
{
    FlacMuxerContext *c = s->priv_data;

    avpriv_packet_list_free(&c->queue, &c->queue_end);
}

static int flac_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    FlacMuxerContext *c = s->priv_data;
    int ret;

    if (pkt->stream_index == c->audio_stream_idx) {
        if (c->waiting_pics) {
            /* buffer audio packets until we get all the pictures */
            ret = avpriv_packet_list_put(&c->queue, &c->queue_end, pkt, av_packet_ref, 0);
            if (ret < 0) {
                av_log(s, AV_LOG_ERROR, "Out of memory in packet queue; skipping attached pictures\n");
                c->waiting_pics = 0;
                ret = flac_queue_flush(s);
                if (ret < 0)
                    return ret;
                return flac_write_audio_packet(s, pkt);
            }
        } else
            return flac_write_audio_packet(s, pkt);
    } else {
        AVStream *st = s->streams[pkt->stream_index];

        if (!c->waiting_pics ||
            !(st->disposition & AV_DISPOSITION_ATTACHED_PIC))
            return 0;

        /* warn only once for each stream */
        if (st->nb_frames == 1) {
            av_log(s, AV_LOG_WARNING, "Got more than one picture in stream %d,"
                   " ignoring.\n", pkt->stream_index);
        }
        if (st->nb_frames >= 1)
            return 0;

        st->priv_data = av_packet_clone(pkt);
        if (!st->priv_data)
            av_log(s, AV_LOG_ERROR, "Out of memory queueing an attached picture; skipping\n");
        c->waiting_pics--;

        /* flush the buffered audio packets */
        if (!c->waiting_pics &&
            (ret = flac_queue_flush(s)) < 0)
            return ret;
    }

    return 0;
}

static const AVOption flacenc_options[] = {
    { "write_header", "Write the file header", offsetof(FlacMuxerContext, write_header), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL },
};

static const AVClass flac_muxer_class = {
    .class_name = "flac muxer",
    .item_name  = av_default_item_name,
    .option     = flacenc_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_flac_muxer = {
    .name              = "flac",
    .long_name         = NULL_IF_CONFIG_SMALL("raw FLAC"),
    .priv_data_size    = sizeof(FlacMuxerContext),
    .mime_type         = "audio/x-flac",
    .extensions        = "flac",
    .audio_codec       = AV_CODEC_ID_FLAC,
    .video_codec       = AV_CODEC_ID_PNG,
    .init              = flac_init,
    .write_header      = flac_write_header,
    .write_packet      = flac_write_packet,
    .write_trailer     = flac_write_trailer,
    .deinit            = flac_deinit,
    .flags             = AVFMT_NOTIMESTAMPS,
    .priv_class        = &flac_muxer_class,
};
