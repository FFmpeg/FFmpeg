/*
 * RCWT (Raw Captions With Time) demuxer
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

/*
 * RCWT (Raw Captions With Time) is a format native to ccextractor, a commonly
 * used open source tool for processing 608/708 Closed Captions (CC) sources.
 *
 * This demuxer implements the specification as of March 2024, which has
 * been stable and unchanged since April 2014.
 *
 * A free specification of RCWT can be found here:
 * @url{https://github.com/CCExtractor/ccextractor/blob/master/docs/BINARY_FILE_FORMAT.TXT}
 */

#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "subtitles.h"
#include "libavutil/intreadwrite.h"

#define RCWT_HEADER_SIZE                    11

typedef struct RCWTContext {
    FFDemuxSubtitlesQueue q;
} RCWTContext;

static int rcwt_read_header(AVFormatContext *avf)
{
    RCWTContext *rcwt = avf->priv_data;

    AVStream      *st;
    uint8_t       header[RCWT_HEADER_SIZE];
    int           ret;

    /* read header */
    ret = ffio_read_size(avf->pb, header, RCWT_HEADER_SIZE);
    if (ret < 0)
        return ret;

    if (AV_RB16(header + 6) != 0x0001) {
        av_log(avf, AV_LOG_ERROR, "RCWT format version is not compatible "
                                  "(only version 0.001 is known)\n");
        return AVERROR_INVALIDDATA;
    }

    av_log(avf, AV_LOG_DEBUG, "RCWT writer application: %02X version: %02x\n",
                              header[3], header[5]);

    /* setup stream */
    st = avformat_new_stream(avf, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_EIA_608;

    avpriv_set_pts_info(st, 64, 1, 1000);

    /* demux */
    while (!avio_feof(avf->pb)) {
        AVPacket      *sub;
        int64_t       cluster_pos       = avio_tell(avf->pb);
        int64_t       cluster_pts       = avio_rl64(avf->pb);
        int           cluster_nb_blocks = avio_rl16(avf->pb);

        if (cluster_nb_blocks == 0)
            continue;

        sub = ff_subtitles_queue_insert(&rcwt->q, NULL, 0, 0);
        if (!sub)
            return AVERROR(ENOMEM);

        ret = av_get_packet(avf->pb, sub, cluster_nb_blocks * 3);
        if (ret < 0)
            return ret;

        sub->pos = cluster_pos;
        sub->pts = cluster_pts;
    }

    ff_subtitles_queue_finalize(avf, &rcwt->q);

    return 0;
}

static int rcwt_probe(const AVProbeData *p)
{
    return p->buf_size > RCWT_HEADER_SIZE   &&
           AV_RB16(p->buf) == 0xCCCC        &&
           AV_RB8(p->buf + 2) == 0xED       &&
           AV_RB16(p->buf + 6) == 0x0001    ? 50 : 0;
}

const FFInputFormat ff_rcwt_demuxer = {
    .p.name         = "rcwt",
    .p.long_name    = NULL_IF_CONFIG_SMALL("RCWT (Raw Captions With Time)"),
    .p.flags        = AVFMT_TS_DISCONT,
    .priv_data_size = sizeof(RCWTContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = rcwt_probe,
    .read_header    = rcwt_read_header,
    .read_packet    = ff_subtitles_read_packet,
    .read_seek2     = ff_subtitles_read_seek,
    .read_close     = ff_subtitles_read_close
};
