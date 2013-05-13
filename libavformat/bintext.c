/*
 * Binary text demuxer
 * eXtended BINary text (XBIN) demuxer
 * Artworx Data Format demuxer
 * iCEDraw File demuxer
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
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
 * Binary text demuxer
 * eXtended BINary text (XBIN) demuxer
 * Artworx Data Format demuxer
 * iCEDraw File demuxer
 */

#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avformat.h"
#include "internal.h"
#include "sauce.h"
#include "libavcodec/bintext.h"

typedef struct {
    const AVClass *class;
    int chars_per_frame; /**< characters to send decoder per frame;
                              set by private options as characters per second, and then
                              converted to characters per frame at runtime */
    int width, height;    /**< video size (WxH pixels) (private option) */
    AVRational framerate; /**< frames per second (private option) */
    uint64_t fsize;  /**< file size less metadata buffer */
} BinDemuxContext;

static AVStream * init_stream(AVFormatContext *s)
{
    BinDemuxContext *bin = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return NULL;
    st->codec->codec_tag   = 0;
    st->codec->codec_type  = AVMEDIA_TYPE_VIDEO;

    if (!bin->width) {
        st->codec->width  = (80<<3);
        st->codec->height = (25<<4);
    }

    avpriv_set_pts_info(st, 60, bin->framerate.den, bin->framerate.num);

    /* simulate tty display speed */
    bin->chars_per_frame = FFMAX(av_q2d(st->time_base) * bin->chars_per_frame, 1);

    return st;
}

#if CONFIG_BINTEXT_DEMUXER | CONFIG_ADF_DEMUXER | CONFIG_IDF_DEMUXER
/**
 * Given filesize and width, calculate height (assume font_height of 16)
 */
static void calculate_height(AVCodecContext *avctx, uint64_t fsize)
{
    avctx->height = (fsize / ((avctx->width>>3)*2)) << 4;
}
#endif

#if CONFIG_BINTEXT_DEMUXER
static const uint8_t next_magic[]={
    0x1A, 0x1B, '[', '0', ';', '3', '0', ';', '4', '0', 'm', 'N', 'E', 'X', 'T', 0x00
};

static int next_tag_read(AVFormatContext *avctx, uint64_t *fsize)
{
    AVIOContext *pb = avctx->pb;
    char buf[36];
    int len;
    uint64_t start_pos = avio_size(pb) - 256;

    avio_seek(pb, start_pos, SEEK_SET);
    if (avio_read(pb, buf, sizeof(next_magic)) != sizeof(next_magic))
        return -1;
    if (memcmp(buf, next_magic, sizeof(next_magic)))
        return -1;
    if (avio_r8(pb) != 0x01)
        return -1;

    *fsize -= 256;

#define GET_EFI2_META(name,size) \
    len = avio_r8(pb); \
    if (len < 1 || len > size) \
        return -1; \
    if (avio_read(pb, buf, size) == size && *buf) { \
        buf[len] = 0; \
        av_dict_set(&avctx->metadata, name, buf, 0); \
    }

    GET_EFI2_META("filename",  12)
    GET_EFI2_META("author",    20)
    GET_EFI2_META("publisher", 20)
    GET_EFI2_META("title",     35)

    return 0;
}

static void predict_width(AVCodecContext *avctx, uint64_t fsize, int got_width)
{
    /** attempt to guess width */
    if (!got_width)
        avctx->width = fsize > 4000 ? (160<<3) : (80<<3);
}

static int bintext_read_header(AVFormatContext *s)
{
    BinDemuxContext *bin = s->priv_data;
    AVIOContext *pb = s->pb;

    AVStream *st = init_stream(s);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_id    = AV_CODEC_ID_BINTEXT;

    st->codec->extradata_size = 2;
    st->codec->extradata = av_malloc(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codec->extradata)
        return AVERROR(ENOMEM);
    st->codec->extradata[0] = 16;
    st->codec->extradata[1] = 0;

    if (pb->seekable) {
        int got_width = 0;
        bin->fsize = avio_size(pb);
        if (ff_sauce_read(s, &bin->fsize, &got_width, 0) < 0)
            next_tag_read(s, &bin->fsize);
        if (!bin->width) {
            predict_width(st->codec, bin->fsize, got_width);
            calculate_height(st->codec, bin->fsize);
        }
        avio_seek(pb, 0, SEEK_SET);
    }
    return 0;
}
#endif /* CONFIG_BINTEXT_DEMUXER */

#if CONFIG_XBIN_DEMUXER
static int xbin_probe(AVProbeData *p)
{
    const uint8_t *d = p->buf;

    if (AV_RL32(d) == MKTAG('X','B','I','N') && d[4] == 0x1A &&
        AV_RL16(d+5) > 0 && AV_RL16(d+5) <= 160 &&
        d[9] > 0 && d[9] <= 32)
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int xbin_read_header(AVFormatContext *s)
{
    BinDemuxContext *bin = s->priv_data;
    AVIOContext *pb = s->pb;
    char fontheight, flags;

    AVStream *st = init_stream(s);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(pb, 5);
    st->codec->width   = avio_rl16(pb)<<3;
    st->codec->height  = avio_rl16(pb);
    fontheight         = avio_r8(pb);
    st->codec->height *= fontheight;
    flags              = avio_r8(pb);

    st->codec->extradata_size = 2;
    if ((flags & BINTEXT_PALETTE))
        st->codec->extradata_size += 48;
    if ((flags & BINTEXT_FONT))
        st->codec->extradata_size += fontheight * (flags & 0x10 ? 512 : 256);
    st->codec->codec_id    = flags & 4 ? AV_CODEC_ID_XBIN : AV_CODEC_ID_BINTEXT;

    st->codec->extradata = av_malloc(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codec->extradata)
        return AVERROR(ENOMEM);
    st->codec->extradata[0] = fontheight;
    st->codec->extradata[1] = flags;
    if (avio_read(pb, st->codec->extradata + 2, st->codec->extradata_size - 2) < 0)
        return AVERROR(EIO);

    if (pb->seekable) {
        bin->fsize = avio_size(pb) - 9 - st->codec->extradata_size;
        ff_sauce_read(s, &bin->fsize, NULL, 0);
        avio_seek(pb, 9 + st->codec->extradata_size, SEEK_SET);
    }

    return 0;
}
#endif /* CONFIG_XBIN_DEMUXER */

#if CONFIG_ADF_DEMUXER
static int adf_read_header(AVFormatContext *s)
{
    BinDemuxContext *bin = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;

    if (avio_r8(pb) != 1)
        return AVERROR_INVALIDDATA;

    st = init_stream(s);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_id    = AV_CODEC_ID_BINTEXT;

    st->codec->extradata_size = 2 + 48 + 4096;
    st->codec->extradata = av_malloc(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codec->extradata)
        return AVERROR(ENOMEM);
    st->codec->extradata[0] = 16;
    st->codec->extradata[1] = BINTEXT_PALETTE|BINTEXT_FONT;

    if (avio_read(pb, st->codec->extradata + 2, 24) < 0)
        return AVERROR(EIO);
    avio_skip(pb, 144);
    if (avio_read(pb, st->codec->extradata + 2 + 24, 24) < 0)
        return AVERROR(EIO);
    if (avio_read(pb, st->codec->extradata + 2 + 48, 4096) < 0)
        return AVERROR(EIO);

    if (pb->seekable) {
        int got_width = 0;
        bin->fsize = avio_size(pb) - 1 - 192 - 4096;
        st->codec->width = 80<<3;
        ff_sauce_read(s, &bin->fsize, &got_width, 0);
        if (!bin->width)
            calculate_height(st->codec, bin->fsize);
        avio_seek(pb, 1 + 192 + 4096, SEEK_SET);
    }
    return 0;
}
#endif /* CONFIG_ADF_DEMUXER */

#if CONFIG_IDF_DEMUXER
static const uint8_t idf_magic[] = {
    0x04, 0x31, 0x2e, 0x34, 0x00, 0x00, 0x00, 0x00, 0x4f, 0x00, 0x15, 0x00
};

static int idf_probe(AVProbeData *p)
{
    if (p->buf_size < sizeof(idf_magic))
        return 0;
    if (!memcmp(p->buf, idf_magic, sizeof(idf_magic)))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int idf_read_header(AVFormatContext *s)
{
    BinDemuxContext *bin = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    int got_width = 0;

    if (!pb->seekable)
        return AVERROR(EIO);

    st = init_stream(s);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_id    = AV_CODEC_ID_IDF;

    st->codec->extradata_size = 2 + 48 + 4096;
    st->codec->extradata = av_malloc(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codec->extradata)
        return AVERROR(ENOMEM);
    st->codec->extradata[0] = 16;
    st->codec->extradata[1] = BINTEXT_PALETTE|BINTEXT_FONT;

    avio_seek(pb, avio_size(pb) - 4096 - 48, SEEK_SET);

    if (avio_read(pb, st->codec->extradata + 2 + 48, 4096) < 0)
        return AVERROR(EIO);
    if (avio_read(pb, st->codec->extradata + 2, 48) < 0)
        return AVERROR(EIO);

    bin->fsize = avio_size(pb) - 12 - 4096 - 48;
    ff_sauce_read(s, &bin->fsize, &got_width, 0);
    if (!bin->width)
        calculate_height(st->codec, bin->fsize);
    avio_seek(pb, 12, SEEK_SET);
    return 0;
}
#endif /* CONFIG_IDF_DEMUXER */

static int read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    BinDemuxContext *bin = s->priv_data;

    if (bin->fsize > 0) {
        if (av_get_packet(s->pb, pkt, bin->fsize) < 0)
            return AVERROR(EIO);
        bin->fsize = -1; /* done */
    } else if (!bin->fsize) {
        if (url_feof(s->pb))
            return AVERROR(EIO);
        if (av_get_packet(s->pb, pkt, bin->chars_per_frame) < 0)
            return AVERROR(EIO);
    } else {
        return AVERROR(EIO);
    }

    pkt->flags |= AV_PKT_FLAG_KEY;
    return 0;
}

#define OFFSET(x) offsetof(BinDemuxContext, x)
static const AVOption options[] = {
    { "linespeed", "set simulated line speed (bytes per second)", OFFSET(chars_per_frame), AV_OPT_TYPE_INT, {.i64 = 6000}, 1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM},
    { "video_size", "set video size, such as 640x480 or hd720.", OFFSET(width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM },
    { "framerate", "set framerate (frames per second)", OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, 0, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

#define CLASS(name) \
(const AVClass[1]){{ \
    .class_name     = name, \
    .item_name      = av_default_item_name, \
    .option         = options, \
    .version        = LIBAVUTIL_VERSION_INT, \
}}

#if CONFIG_BINTEXT_DEMUXER
AVInputFormat ff_bintext_demuxer = {
    .name           = "bin",
    .long_name      = NULL_IF_CONFIG_SMALL("Binary text"),
    .priv_data_size = sizeof(BinDemuxContext),
    .read_header    = bintext_read_header,
    .read_packet    = read_packet,
    .extensions     = "bin",
    .priv_class     = CLASS("Binary text demuxer"),
};
#endif

#if CONFIG_XBIN_DEMUXER
AVInputFormat ff_xbin_demuxer = {
    .name           = "xbin",
    .long_name      = NULL_IF_CONFIG_SMALL("eXtended BINary text (XBIN)"),
    .priv_data_size = sizeof(BinDemuxContext),
    .read_probe     = xbin_probe,
    .read_header    = xbin_read_header,
    .read_packet    = read_packet,
    .priv_class     = CLASS("eXtended BINary text (XBIN) demuxer"),
};
#endif

#if CONFIG_ADF_DEMUXER
AVInputFormat ff_adf_demuxer = {
    .name           = "adf",
    .long_name      = NULL_IF_CONFIG_SMALL("Artworx Data Format"),
    .priv_data_size = sizeof(BinDemuxContext),
    .read_header    = adf_read_header,
    .read_packet    = read_packet,
    .extensions     = "adf",
    .priv_class     = CLASS("Artworx Data Format demuxer"),
};
#endif

#if CONFIG_IDF_DEMUXER
AVInputFormat ff_idf_demuxer = {
    .name           = "idf",
    .long_name      = NULL_IF_CONFIG_SMALL("iCE Draw File"),
    .priv_data_size = sizeof(BinDemuxContext),
    .read_probe     = idf_probe,
    .read_header    = idf_read_header,
    .read_packet    = read_packet,
    .extensions     = "idf",
    .priv_class     = CLASS("iCE Draw File demuxer"),
};
#endif
