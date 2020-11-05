/*
 * AV1 Annex B demuxer
 * Copyright (c) 2019 James Almer <jamrial@gmail.com>
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

#include "config.h"

#include "libavutil/common.h"
#include "libavutil/fifo.h"
#include "libavutil/opt.h"
#include "libavcodec/av1_parse.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"

//return < 0 if we need more data
static int get_score(int type, int *seq)
{
    switch (type) {
    case AV1_OBU_SEQUENCE_HEADER:
        *seq = 1;
        return -1;
    case AV1_OBU_FRAME:
    case AV1_OBU_FRAME_HEADER:
        return *seq ? AVPROBE_SCORE_EXTENSION + 1 : 0;
    case AV1_OBU_METADATA:
    case AV1_OBU_PADDING:
        return -1;
    default:
        break;
    }
    return 0;
}

static int read_header(AVFormatContext *s, const AVRational *framerate, AVBSFContext **bsf, void *logctx)
{
    const AVBitStreamFilter *filter = av_bsf_get_by_name("av1_frame_merge");
    AVStream *st;
    int ret;

    if (!filter) {
        av_log(logctx, AV_LOG_ERROR, "av1_frame_merge bitstream filter "
               "not found. This is a bug, please report it.\n");
        return AVERROR_BUG;
    }

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_AV1;
    st->need_parsing = AVSTREAM_PARSE_HEADERS;

    st->internal->avctx->framerate = *framerate;
    // taken from rawvideo demuxers
    avpriv_set_pts_info(st, 64, 1, 1200000);

    ret = av_bsf_alloc(filter, bsf);
    if (ret < 0)
        return ret;

    ret = avcodec_parameters_copy((*bsf)->par_in, st->codecpar);
    if (ret < 0) {
        av_bsf_free(bsf);
        return ret;
    }

    ret = av_bsf_init(*bsf);
    if (ret < 0)
        av_bsf_free(bsf);

    return ret;

}

#define DEC AV_OPT_FLAG_DECODING_PARAM

#if CONFIG_AV1_DEMUXER
typedef struct AnnexBContext {
    const AVClass *class;
    AVBSFContext *bsf;
    uint32_t temporal_unit_size;
    uint32_t frame_unit_size;
    AVRational framerate;
} AnnexBContext;

static int leb(AVIOContext *pb, uint32_t *len) {
    int more, i = 0;
    uint8_t byte;
    *len = 0;
    do {
        unsigned bits;
        byte = avio_r8(pb);
        more = byte & 0x80;
        bits = byte & 0x7f;
        if (i <= 3 || (i == 4 && bits < (1 << 4)))
            *len |= bits << (i * 7);
        else if (bits)
            return AVERROR_INVALIDDATA;
        if (++i == 8 && more)
            return AVERROR_INVALIDDATA;
        if (pb->eof_reached || pb->error)
            return pb->error ? pb->error : AVERROR(EIO);
    } while (more);
    return i;
}

static int read_obu(const uint8_t *buf, int size, int64_t *obu_size, int *type)
{
    int start_pos, temporal_id, spatial_id;
    int len;

    len = parse_obu_header(buf, size, obu_size, &start_pos,
                           type, &temporal_id, &spatial_id);
    if (len < 0)
        return len;

    return 0;
}

static int annexb_probe(const AVProbeData *p)
{
    AVIOContext pb;
    int64_t obu_size;
    uint32_t temporal_unit_size, frame_unit_size, obu_unit_size;
    int seq = 0;
    int ret, type, cnt = 0;

    ffio_init_context(&pb, p->buf, p->buf_size, 0,
                      NULL, NULL, NULL, NULL);

    ret = leb(&pb, &temporal_unit_size);
    if (ret < 0)
        return 0;
    cnt += ret;
    ret = leb(&pb, &frame_unit_size);
    if (ret < 0 || ((int64_t)frame_unit_size + ret) > temporal_unit_size)
        return 0;
    cnt += ret;
    ret = leb(&pb, &obu_unit_size);
    if (ret < 0 || ((int64_t)obu_unit_size + ret) >= frame_unit_size)
        return 0;
    cnt += ret;

    frame_unit_size -= obu_unit_size + ret;

    avio_skip(&pb, obu_unit_size);
    if (pb.eof_reached || pb.error)
        return 0;

    // Check that the first OBU is a Temporal Delimiter.
    ret = read_obu(p->buf + cnt, FFMIN(p->buf_size - cnt, obu_unit_size), &obu_size, &type);
    if (ret < 0 || type != AV1_OBU_TEMPORAL_DELIMITER || obu_size > 0)
        return 0;
    cnt += obu_unit_size;

    do {
        ret = leb(&pb, &obu_unit_size);
        if (ret < 0 || ((int64_t)obu_unit_size + ret) > frame_unit_size)
            return 0;
        cnt += ret;

        avio_skip(&pb, obu_unit_size);
        if (pb.eof_reached || pb.error)
            return 0;

        ret = read_obu(p->buf + cnt, FFMIN(p->buf_size - cnt, obu_unit_size), &obu_size, &type);
        if (ret < 0)
            return 0;
        cnt += obu_unit_size;

        ret = get_score(type, &seq);
        if (ret >= 0)
            return ret;

        frame_unit_size -= obu_unit_size + ret;
    } while (frame_unit_size);

    return 0;
}

static int annexb_read_header(AVFormatContext *s)
{
    AnnexBContext *c = s->priv_data;
    return read_header(s, &c->framerate, &c->bsf, c);
}

static int annexb_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AnnexBContext *c = s->priv_data;
    uint32_t obu_unit_size;
    int ret, len;

retry:
    if (avio_feof(s->pb)) {
        if (c->temporal_unit_size || c->frame_unit_size)
            return AVERROR(EIO);
        goto end;
    }

    if (!c->temporal_unit_size) {
        len = leb(s->pb, &c->temporal_unit_size);
        if (len < 0) return AVERROR_INVALIDDATA;
    }

    if (!c->frame_unit_size) {
        len = leb(s->pb, &c->frame_unit_size);
        if (len < 0 || ((int64_t)c->frame_unit_size + len) > c->temporal_unit_size)
            return AVERROR_INVALIDDATA;
        c->temporal_unit_size -= len;
    }

    len = leb(s->pb, &obu_unit_size);
    if (len < 0 || ((int64_t)obu_unit_size + len) > c->frame_unit_size)
        return AVERROR_INVALIDDATA;

    ret = av_get_packet(s->pb, pkt, obu_unit_size);
    if (ret < 0)
        return ret;
    if (ret != obu_unit_size)
        return AVERROR(EIO);

    c->temporal_unit_size -= obu_unit_size + len;
    c->frame_unit_size -= obu_unit_size + len;

end:
    ret = av_bsf_send_packet(c->bsf, pkt);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Failed to send packet to "
                                "av1_frame_merge filter\n");
        return ret;
    }

    ret = av_bsf_receive_packet(c->bsf, pkt);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        av_log(s, AV_LOG_ERROR, "av1_frame_merge filter failed to "
                                "send output packet\n");

    if (ret == AVERROR(EAGAIN))
        goto retry;

    return ret;
}

static int annexb_read_close(AVFormatContext *s)
{
    AnnexBContext *c = s->priv_data;

    av_bsf_free(&c->bsf);
    return 0;
}

#define OFFSET(x) offsetof(AnnexBContext, x)
static const AVOption annexb_options[] = {
    { "framerate", "", OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, DEC},
    { NULL },
};
#undef OFFSET

static const AVClass annexb_demuxer_class = {
    .class_name = "AV1 Annex B demuxer",
    .item_name  = av_default_item_name,
    .option     = annexb_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_av1_demuxer = {
    .name           = "av1",
    .long_name      = NULL_IF_CONFIG_SMALL("AV1 Annex B"),
    .priv_data_size = sizeof(AnnexBContext),
    .read_probe     = annexb_probe,
    .read_header    = annexb_read_header,
    .read_packet    = annexb_read_packet,
    .read_close     = annexb_read_close,
    .extensions     = "obu",
    .flags          = AVFMT_GENERIC_INDEX,
    .priv_class     = &annexb_demuxer_class,
};
#endif

#if CONFIG_OBU_DEMUXER
typedef struct ObuContext {
    const AVClass *class;
    AVBSFContext *bsf;
    AVRational framerate;
    AVFifoBuffer *fifo;
} ObuContext;

//For low overhead obu, we can't foresee the obu size before we parsed the header.
//So, we can't use parse_obu_header here, since it will check size <= buf_size
//see c27c7b49dc for more details
static int read_obu_with_size(const uint8_t *buf, int buf_size, int64_t *obu_size, int *type)
{
    GetBitContext gb;
    int ret, extension_flag, start_pos;
    int64_t size;

    ret = init_get_bits8(&gb, buf, FFMIN(buf_size, MAX_OBU_HEADER_SIZE));
    if (ret < 0)
        return ret;

    if (get_bits1(&gb) != 0) // obu_forbidden_bit
        return AVERROR_INVALIDDATA;

    *type      = get_bits(&gb, 4);
    extension_flag = get_bits1(&gb);
    if (!get_bits1(&gb))    // has_size_flag
        return AVERROR_INVALIDDATA;
    skip_bits1(&gb);        // obu_reserved_1bit

    if (extension_flag) {
        get_bits(&gb, 3);   // temporal_id
        get_bits(&gb, 2);   // spatial_id
        skip_bits(&gb, 3);  // extension_header_reserved_3bits
    }

    *obu_size  = leb128(&gb);
    if (*obu_size > INT_MAX)
        return AVERROR_INVALIDDATA;

    if (get_bits_left(&gb) < 0)
        return AVERROR_INVALIDDATA;

    start_pos = get_bits_count(&gb) / 8;

    size = *obu_size + start_pos;
    if (size > INT_MAX)
        return AVERROR_INVALIDDATA;
    return size;
}

static int obu_probe(const AVProbeData *p)
{
    int64_t obu_size;
    int seq = 0;
    int ret, type, cnt;

    // Check that the first OBU is a Temporal Delimiter.
    cnt = read_obu_with_size(p->buf, p->buf_size, &obu_size, &type);
    if (cnt < 0 || type != AV1_OBU_TEMPORAL_DELIMITER || obu_size != 0)
        return 0;

    while (1) {
        ret = read_obu_with_size(p->buf + cnt, p->buf_size - cnt, &obu_size, &type);
        if (ret < 0 || obu_size <= 0)
            return 0;
        cnt += FFMIN(ret, p->buf_size - cnt);

        ret = get_score(type, &seq);
        if (ret >= 0)
            return ret;
    }
    return 0;
}

static int obu_read_header(AVFormatContext *s)
{
    ObuContext *c = s->priv_data;
    c->fifo = av_fifo_alloc(MAX_OBU_HEADER_SIZE);
    if (!c->fifo)
        return AVERROR(ENOMEM);
    return read_header(s, &c->framerate, &c->bsf, c);
}

static int obu_get_packet(AVFormatContext *s, AVPacket *pkt)
{
    ObuContext *c = s->priv_data;
    uint8_t header[MAX_OBU_HEADER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    int64_t obu_size;
    int size = av_fifo_space(c->fifo);
    int ret, len, type;

    av_fifo_generic_write(c->fifo, s->pb, size,
                          (int (*)(void*, void*, int))avio_read);
    size = av_fifo_size(c->fifo);
    if (!size)
        return 0;

    av_fifo_generic_peek(c->fifo, header, size, NULL);

    len = read_obu_with_size(header, size, &obu_size, &type);
    if (len < 0) {
        av_log(c, AV_LOG_ERROR, "Failed to read obu\n");
        return len;
    }

    ret = av_new_packet(pkt, len);
    if (ret < 0) {
        av_log(c, AV_LOG_ERROR, "Failed to allocate packet for obu\n");
        return ret;
    }
    size = FFMIN(size, len);
    av_fifo_generic_read(c->fifo, pkt->data, size, NULL);
    len -= size;
    if (len > 0) {
        ret = avio_read(s->pb, pkt->data + size, len);
        if (ret != len) {
            av_log(c, AV_LOG_ERROR, "Failed to read %d frome file\n", len);
            return ret < 0 ? ret : AVERROR_INVALIDDATA;
        }
    }
    return 0;
}

static int obu_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ObuContext *c = s->priv_data;
    int ret;

    while (1) {
        ret = obu_get_packet(s, pkt);
        if (ret < 0)
            return ret;
        ret = av_bsf_send_packet(c->bsf, pkt);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Failed to send packet to "
                                    "av1_frame_merge filter\n");
            return ret;
        }
        ret = av_bsf_receive_packet(c->bsf, pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            av_log(s, AV_LOG_ERROR, "av1_frame_merge filter failed to "
                                    "send output packet\n");
        if (ret != AVERROR(EAGAIN))
            break;
    }

    return ret;
}

static int obu_read_close(AVFormatContext *s)
{
    ObuContext *c = s->priv_data;

    av_fifo_freep(&c->fifo);
    av_bsf_free(&c->bsf);
    return 0;
}

#define OFFSET(x) offsetof(ObuContext, x)
static const AVOption obu_options[] = {
    { "framerate", "", OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, DEC},
    { NULL },
};
#undef OFFSET

static const AVClass obu_demuxer_class = {
    .class_name = "AV1 low overhead OBU demuxer",
    .item_name  = av_default_item_name,
    .option     = obu_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_obu_demuxer = {
    .name           = "obu",
    .long_name      = NULL_IF_CONFIG_SMALL("AV1 low overhead OBU"),
    .priv_data_size = sizeof(ObuContext),
    .read_probe     = obu_probe,
    .read_header    = obu_read_header,
    .read_packet    = obu_read_packet,
    .read_close     = obu_read_close,
    .extensions     = "obu",
    .flags          = AVFMT_GENERIC_INDEX,
    .priv_class     = &obu_demuxer_class,
};
#endif
