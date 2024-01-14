/*
 * Raw Captions With Time (RCWT) muxer
 * Author: Marth64 <marth64@proxyid.net>
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
 * Raw Captions With Time (RCWT) is a format native to ccextractor, a commonly
 * used open source tool for processing 608/708 closed caption (CC) sources.
 * It can be used to archive the original, raw CC bitstream and to produce
 * a source file for later CC processing or conversion. As a result,
 * it also allows for interopability with ccextractor for processing CC data
 * extracted via ffmpeg. The format is simple to parse and can be used
 * to retain all lines and variants of CC.
 *
 * This muxer implements the specification as of 2024-01-05, which has
 * been stable and unchanged for 10 years as of this writing.
 *
 * This muxer will have some nuances from the way that ccextractor muxes RCWT.
 * No compatibility issues when processing the output with ccextractor
 * have been observed as a result of this so far, but mileage may vary
 * and outputs will not be a bit-exact match.
 *
 * Specifically, the differences are:
 * (1) This muxer will identify as "FF" as the writing program identifier, so
 *     as to be honest about the output's origin.
 *
 * (2) ffmpeg's MPEG-1/2, H264, HEVC, etc. decoders extract closed captioning
 *     data differently than ccextractor from embedded SEI/user data.
 *     For example, DVD captioning bytes will be translated to ATSC A53 format.
 *     This allows ffmpeg to handle 608/708 in a consistant way downstream.
 *     This is a lossless conversion and the meaningful data is retained.
 *
 * (3) This muxer will not alter the extracted data except to remove invalid
 *     packets in between valid CC blocks. On the other hand, ccextractor
 *     will by default remove mid-stream padding, and add padding at the end
 *     of the stream (in order to convey the end time of the source video).
 *
 * A free specification of RCWT can be found here:
 * @url{https://github.com/CCExtractor/ccextractor/blob/master/docs/BINARY_FILE_FORMAT.TXT}
 */

#include "avformat.h"
#include "internal.h"
#include "mux.h"
#include "libavutil/log.h"
#include "libavutil/intreadwrite.h"

#define RCWT_CLUSTER_MAX_BLOCKS             65535
#define RCWT_BLOCK_SIZE                     3

typedef struct RCWTContext {
    int cluster_nb_blocks;
    int cluster_pos;
    int64_t cluster_pts;
    uint8_t cluster_buf[RCWT_CLUSTER_MAX_BLOCKS * RCWT_BLOCK_SIZE];
} RCWTContext;

static void rcwt_init_cluster(AVFormatContext *avf)
{
    RCWTContext *rcwt = avf->priv_data;

    rcwt->cluster_nb_blocks = 0;
    rcwt->cluster_pos = 0;
    rcwt->cluster_pts = AV_NOPTS_VALUE;
    memset(rcwt->cluster_buf, 0, sizeof(rcwt->cluster_buf));
}

static void rcwt_flush_cluster(AVFormatContext *avf)
{
    RCWTContext *rcwt = avf->priv_data;

    if (rcwt->cluster_nb_blocks > 0) {
        avio_wl64(avf->pb, rcwt->cluster_pts);
        avio_wl16(avf->pb, rcwt->cluster_nb_blocks);
        avio_write(avf->pb, rcwt->cluster_buf, (rcwt->cluster_nb_blocks * RCWT_BLOCK_SIZE));
    }

    rcwt_init_cluster(avf);
}

static int rcwt_write_header(AVFormatContext *avf)
{
    if (avf->nb_streams != 1 || avf->streams[0]->codecpar->codec_id != AV_CODEC_ID_EIA_608) {
        av_log(avf, AV_LOG_ERROR,
                "RCWT supports only one CC (608/708) stream, more than one stream was "
                "provided or its codec type was not CC (608/708)\n");
        return AVERROR(EINVAL);
    }

    avpriv_set_pts_info(avf->streams[0], 64, 1, 1000);

    /* magic number */
    avio_wb16(avf->pb, 0xCCCC);
    avio_w8(avf->pb, 0xED);

    /* program version (identify as ffmpeg) */
    avio_wb16(avf->pb, 0xFF00);
    avio_w8(avf->pb, 0x60);

    /* format version, only version 0.001 supported for now */
    avio_wb16(avf->pb, 0x0001);

    /* reserved */
    avio_wb16(avf->pb, 0x000);
    avio_w8(avf->pb, 0x00);

    rcwt_init_cluster(avf);

    return 0;
}

static int rcwt_write_packet(AVFormatContext *avf, AVPacket *pkt)
{
    RCWTContext *rcwt = avf->priv_data;

    int in_block = 0;
    int nb_block_bytes = 0;

    if (pkt->size == 0)
        return 0;

    /* new PTS, new cluster */
    if (pkt->pts != rcwt->cluster_pts) {
        rcwt_flush_cluster(avf);
        rcwt->cluster_pts = pkt->pts;
    }

    if (pkt->pts == AV_NOPTS_VALUE) {
        av_log(avf, AV_LOG_WARNING, "Ignoring CC packet with no PTS\n");
        return 0;
    }

    for (int i = 0; i < pkt->size; i++) {
        uint8_t cc_valid;
        uint8_t cc_type;

        if (rcwt->cluster_nb_blocks == RCWT_CLUSTER_MAX_BLOCKS) {
            av_log(avf, AV_LOG_WARNING, "Starting new cluster due to size\n");
            rcwt_flush_cluster(avf);
        }

        cc_valid = (pkt->data[i] & 0x04) >> 2;
        cc_type = pkt->data[i] & 0x03;

        if (!in_block && !(cc_valid || cc_type == 3))
            continue;

        memcpy(&rcwt->cluster_buf[rcwt->cluster_pos], &pkt->data[i], 1);
        rcwt->cluster_pos++;

        if (!in_block) {
            in_block = 1;
            nb_block_bytes = 1;
            continue;
        }

        nb_block_bytes++;

        if (nb_block_bytes == RCWT_BLOCK_SIZE) {
            in_block = 0;
            nb_block_bytes = 0;
            rcwt->cluster_nb_blocks++;
        }
    }

    return 0;
}

static int rcwt_write_trailer(AVFormatContext *avf)
{
    rcwt_flush_cluster(avf);

    return 0;
}

const FFOutputFormat ff_rcwt_muxer = {
    .p.name             = "rcwt",
    .p.long_name        = NULL_IF_CONFIG_SMALL("Raw Captions With Time"),
    .p.extensions       = "bin",
    .p.flags            = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS | AVFMT_TS_NONSTRICT,
    .p.subtitle_codec   = AV_CODEC_ID_EIA_608,
    .priv_data_size     = sizeof(RCWTContext),
    .write_header       = rcwt_write_header,
    .write_packet       = rcwt_write_packet,
    .write_trailer      = rcwt_write_trailer
};
