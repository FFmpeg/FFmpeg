/*
 * RAW EVC video demuxer
 *
 * Copyright (c) 2021 Dawid Kozinski <d.kozinski@samsung.com>
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

#include "libavcodec/get_bits.h"
#include "libavcodec/golomb.h"
#include "libavcodec/internal.h"
#include "libavcodec/evc.h"
#include "libavcodec/bsf.h"

#include "libavutil/opt.h"

#include "rawdec.h"
#include "avformat.h"
#include "internal.h"


#define RAW_PACKET_SIZE 1024

typedef struct EVCDemuxContext {
    const AVClass *class;
    AVRational framerate;

    AVBSFContext *bsf;

} EVCDemuxContext;

#define DEC AV_OPT_FLAG_DECODING_PARAM
#define OFFSET(x) offsetof(EVCDemuxContext, x)
static const AVOption evc_options[] = {
    { "framerate", "", OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, DEC},
    { NULL },
};
#undef OFFSET

static const AVClass evc_demuxer_class = {
    .class_name = "EVC Annex B demuxer",
    .item_name  = av_default_item_name,
    .option     = evc_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int get_nalu_type(const uint8_t *bits, int bits_size)
{
    int unit_type_plus1 = 0;

    if (bits_size >= EVC_NALU_HEADER_SIZE) {
        unsigned char *p = (unsigned char *)bits;
        // forbidden_zero_bit
        if ((p[0] & 0x80) != 0)   // Cannot get bitstream information. Malformed bitstream.
            return -1;

        // nal_unit_type
        unit_type_plus1 = (p[0] >> 1) & 0x3F;
    }

    return unit_type_plus1 - 1;
}

static uint32_t read_nal_unit_length(const uint8_t *bits, int bits_size)
{
    uint32_t nalu_len = 0;

    if (bits_size >= EVC_NALU_LENGTH_PREFIX_SIZE) {

        int t = 0;
        unsigned char *p = (unsigned char *)bits;

        for (int i = 0; i < EVC_NALU_LENGTH_PREFIX_SIZE; i++)
            t = (t << 8) | p[i];

        nalu_len = t;
        if (nalu_len == 0)   // Invalid bitstream size
            return 0;
    }

    return nalu_len;
}

static int annexb_probe(const AVProbeData *p)
{
    int nalu_type;
    size_t nalu_size;
    int got_sps = 0, got_pps = 0, got_idr = 0, got_nonidr = 0;
    unsigned char *bits = (unsigned char *)p->buf;
    int bytes_to_read = p->buf_size;

    while (bytes_to_read > EVC_NALU_LENGTH_PREFIX_SIZE) {

        nalu_size = read_nal_unit_length(bits, EVC_NALU_LENGTH_PREFIX_SIZE);
        if (nalu_size == 0) break;

        bits += EVC_NALU_LENGTH_PREFIX_SIZE;
        bytes_to_read -= EVC_NALU_LENGTH_PREFIX_SIZE;

        if(bytes_to_read < nalu_size) break;

        nalu_type = get_nalu_type(bits, bytes_to_read);

        if (nalu_type == EVC_SPS_NUT)
            got_sps++;
        else if (nalu_type == EVC_PPS_NUT)
            got_pps++;
        else if (nalu_type == EVC_IDR_NUT )
            got_idr++;
        else if (nalu_type == EVC_NOIDR_NUT)
            got_nonidr++;

        bits += nalu_size;
        bytes_to_read -= nalu_size;
    }

    if (got_sps && got_pps && (got_idr || got_nonidr > 3))
        return AVPROBE_SCORE_EXTENSION + 1;  // 1 more than .mpg

    return 0;
}

static int evc_read_header(AVFormatContext *s)
{
    AVStream *st;
    FFStream *sti;
    const AVBitStreamFilter *filter = av_bsf_get_by_name("evc_frame_merge");
    EVCDemuxContext *c = s->priv_data;
    int ret = 0;

    st = avformat_new_stream(s, NULL);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    sti = ffstream(st);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_EVC;

    // This causes sending to the parser full frames, not chunks of data
    // The flag PARSER_FLAG_COMPLETE_FRAMES will be set in demux.c (demux.c: 1316)
    sti->need_parsing = AVSTREAM_PARSE_HEADERS;

    st->avg_frame_rate = c->framerate;

    // taken from rawvideo demuxers
    avpriv_set_pts_info(st, 64, 1, 1200000);

    ret = av_bsf_alloc(filter, &c->bsf);
    if (ret < 0)
        return ret;

    ret = avcodec_parameters_copy(c->bsf->par_in, st->codecpar);
    if (ret < 0)
        return ret;

    ret = av_bsf_init(c->bsf);
    if (ret < 0)
        return ret;

fail:
    return ret;
}

static int evc_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    int32_t nalu_size;
    int au_end_found = 0;

    EVCDemuxContext *const c = s->priv_data;

    int eof = avio_feof (s->pb);
    if(eof) {
        av_packet_unref(pkt);
        return AVERROR_EOF;
    }

    while(!au_end_found) {

        uint8_t buf[EVC_NALU_LENGTH_PREFIX_SIZE];
        ret = avio_read(s->pb, (unsigned char *)&buf, EVC_NALU_LENGTH_PREFIX_SIZE);
        if (ret < 0) {
            av_packet_unref(pkt);
            return ret;
        }

        nalu_size = read_nal_unit_length((const uint8_t *)&buf, EVC_NALU_LENGTH_PREFIX_SIZE);
        if(nalu_size <= 0) {
            av_packet_unref(pkt);
            return -1;
        }

        avio_seek(s->pb, -EVC_NALU_LENGTH_PREFIX_SIZE, SEEK_CUR);

        ret = av_get_packet(s->pb, pkt, nalu_size + EVC_NALU_LENGTH_PREFIX_SIZE);
        if (ret < 0)
            return ret;
        if (ret != (nalu_size + EVC_NALU_LENGTH_PREFIX_SIZE))
            return AVERROR(EIO);

        ret = av_bsf_send_packet(c->bsf, pkt);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Failed to send packet to "
                   "evc_frame_merge filter\n");
            return ret;
        }

        ret = av_bsf_receive_packet(c->bsf, pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            av_log(s, AV_LOG_ERROR, "evc_frame_merge filter failed to "
                   "send output packet\n");

        if (ret != AVERROR(EAGAIN))
            au_end_found = 1;
    }

    return ret;
}

static int evc_read_close(AVFormatContext *s)
{
    EVCDemuxContext *const c = s->priv_data;

    av_bsf_free(&c->bsf);
    return 0;
}

const AVInputFormat ff_evc_demuxer = {
    .name           = "evc",
    .long_name      = NULL_IF_CONFIG_SMALL("EVC Annex B"),
    .read_probe     = annexb_probe,
    .read_header    = evc_read_header, // annexb_read_header
    .read_packet    = evc_read_packet, // annexb_read_packet
    .read_close     = evc_read_close,
    .extensions     = "evc",
    .flags          = AVFMT_GENERIC_INDEX | AVFMT_NOTIMESTAMPS,
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .raw_codec_id   = AV_CODEC_ID_EVC,
    .priv_data_size = sizeof(EVCDemuxContext),
    .priv_class     = &evc_demuxer_class,
};
