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

#include <inttypes.h>
#include <stdio.h>

#include "libavformat/avformat.h"

int main(int argc, char **argv)
{
    AVFormatContext *fmt = NULL;
    AVDictionary *opts = NULL;
    AVPacket *pkt = NULL;
    AVStream *st;
    AVRational rate;
    int ret = 1;

    if (argc != 2)
        return 1;

    av_log_set_level(AV_LOG_QUIET);
    /* Verify that stream probing retains the rate without a DV decoder. */
    if (av_dict_set(&opts, "codec_whitelist", "rawvideo", 0) < 0 ||
        avformat_open_input(&fmt, argv[1], NULL, &opts) < 0 ||
        !fmt->nb_streams)
        goto end;
    st = fmt->streams[0];
    rate = st->codecpar->framerate;
    printf("coded_rate_at_open=%d/%d\n", rate.num, rate.den);
    if (avformat_find_stream_info(fmt, NULL) < 0)
        goto end;
    rate = st->codecpar->framerate;
    printf("coded_rate_after_probe=%d/%d\n", rate.num, rate.den);

    pkt = av_packet_alloc();
    if (!pkt || av_read_frame(fmt, pkt) < 0)
        goto end;
    printf("duration=%" PRId64 "\n", pkt->duration);
    printf("avg_frame_rate=%d/%d|time_base=%d/%d\n",
           st->avg_frame_rate.num, st->avg_frame_rate.den,
           st->time_base.num, st->time_base.den);
    ret = 0;

end:
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    av_dict_free(&opts);
    return ret;
}
