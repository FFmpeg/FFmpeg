/*
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

#include <stdio.h>

#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/pixfmt.h"
#include "libavformat/avformat.h"
#include "libavformat/demux.h"

static int packets_read;
static int corrupt_after;
static int corrupt_duration;

static int read_header(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);

    if (!st)
        return AVERROR(ENOMEM);
    st->time_base = (AVRational) { 1, 15 };
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
    st->codecpar->format    = AV_PIX_FMT_GRAY8;
    st->codecpar->width     = 1;
    st->codecpar->height    = 1;
    return 0;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    if (packets_read >= 450)
        return AVERROR_EOF;
    ret = av_new_packet(pkt, 1);
    if (ret < 0)
        return ret;
    pkt->data[0] = 0;
    pkt->stream_index = 0;
    pkt->pts = pkt->dts = packets_read;
    pkt->duration = 1;
    if (packets_read >= corrupt_after) {
        pkt->pts = pkt->dts = corrupt_after +
                             (packets_read - corrupt_after) * corrupt_duration;
        pkt->duration = corrupt_duration;
        pkt->flags |= AV_PKT_FLAG_CORRUPT;
    }
    packets_read++;
    return 0;
}

static const FFInputFormat test_demuxer = {
    .p.name      = "test",
    .p.flags     = AVFMT_NOFILE,
    .read_header = read_header,
    .read_packet = read_packet,
};

int main(void)
{
    av_log_set_level(AV_LOG_ERROR);
    for (int corrupt = 0; corrupt < 3; corrupt++) {
        AVFormatContext *s = avformat_alloc_context();
        int ret;

        if (!s)
            return 1;
        packets_read = 0;
        corrupt_after = corrupt ? 5 : 450;
        corrupt_duration = corrupt == 2 ? 2 : 1;
        s->max_analyze_duration = 500000;
        /* Keep probing for FPS until the analysis-duration limit stops it. */
        s->fps_probe_size = 1000;
        ret = avformat_open_input(&s, NULL, &test_demuxer.p, NULL);
        if (ret >= 0)
            ret = avformat_find_stream_info(s, NULL);
        if (ret >= 0) {
            AVRational fps = s->streams[0]->avg_frame_rate;
            printf("%s: packets=%d fps=%d/%d\n",
                   corrupt == 2 ? "corrupt-long" : corrupt ? "corrupt" : "clean",
                   packets_read, fps.num, fps.den);
        }
        avformat_close_input(&s);
        if (ret < 0)
            return 1;
    }
    return 0;
}
