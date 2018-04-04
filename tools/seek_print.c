/*
 * Copyright (c) 2013 Nicolas George
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#if HAVE_UNISTD_H
#include <unistd.h>             /* getopt */
#endif

#include "libavformat/avformat.h"
#include "libavutil/timestamp.h"

#if !HAVE_GETOPT
#include "compat/getopt.c"
#endif

static void usage(int ret)
{
    fprintf(ret ? stderr : stdout,
            "Usage: seek_print file [command ...]\n"
            "Commands:\n"
            "    read\n"
            "    seek:stream:min_ts:ts:max_ts:flags\n"
            );
    exit(ret);
}

int main(int argc, char **argv)
{
    int opt, ret, stream, flags;
    const char *filename;
    AVFormatContext *avf = NULL;
    int64_t min_ts, max_ts, ts;
    AVPacket packet;

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
        case 'h':
            usage(0);
        default:
            usage(1);
        }
    }
    argc -= optind;
    argv += optind;
    if (!argc)
        usage(1);
    filename = *argv;
    argv++;
    argc--;

    if ((ret = avformat_open_input(&avf, filename, NULL, NULL)) < 0) {
        fprintf(stderr, "%s: %s\n", filename, av_err2str(ret));
        return 1;
    }
    if ((ret = avformat_find_stream_info(avf, NULL)) < 0) {
        fprintf(stderr, "%s: could not find codec parameters: %s\n", filename,
                av_err2str(ret));
        return 1;
    }

    for (; argc; argc--, argv++) {
        if (!strcmp(*argv, "read")) {
            ret = av_read_frame(avf, &packet);
            if (ret < 0) {
                printf("read: %d (%s)\n", ret, av_err2str(ret));
            } else {
                AVRational *tb = &avf->streams[packet.stream_index]->time_base;
                printf("read: %d size=%d stream=%d dts=%s (%s) pts=%s (%s)\n",
                       ret, packet.size, packet.stream_index,
                       av_ts2str(packet.dts), av_ts2timestr(packet.dts, tb),
                       av_ts2str(packet.pts), av_ts2timestr(packet.pts, tb));
                av_packet_unref(&packet);
            }
        } else if (sscanf(*argv, "seek:%i:%"SCNi64":%"SCNi64":%"SCNi64":%i",
                   &stream, &min_ts, &ts, &max_ts, &flags) == 5) {
            ret = avformat_seek_file(avf, stream, min_ts, ts, max_ts, flags);
            printf("seek: %d (%s)\n", ret, av_err2str(ret));
        } else {
            fprintf(stderr, "'%s': unknown command\n", *argv);
            return 1;
        }
    }

    avformat_close_input(&avf);

    return 0;
}
