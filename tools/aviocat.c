/*
 * Copyright (c) 2012 Martin Storsjo
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>

#include "libavutil/time.h"
#include "libavformat/avformat.h"

static int usage(const char *argv0, int ret)
{
    fprintf(stderr, "%s [-b bytespersec] [-d duration] input_url output_url\n", argv0);
    return ret;
}

int main(int argc, char **argv)
{
    int bps = 0, duration = 0, ret, i;
    const char *input_url = NULL, *output_url = NULL;
    int64_t stream_pos = 0;
    int64_t start_time;
    char errbuf[50];
    AVIOContext *input, *output;

    av_register_all();
    avformat_network_init();

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-b") && i + 1 < argc) {
            bps = atoi(argv[i + 1]);
            i++;
        } else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
            duration = atoi(argv[i + 1]);
            i++;
        } else if (!input_url) {
            input_url = argv[i];
        } else if (!output_url) {
            output_url = argv[i];
        } else {
            return usage(argv[0], 1);
        }
    }
    if (!output_url)
        return usage(argv[0], 1);

    ret = avio_open2(&input, input_url, AVIO_FLAG_READ, NULL, NULL);
    if (ret) {
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Unable to open %s: %s\n", input_url, errbuf);
        return 1;
    }
    if (duration && !bps) {
        int64_t size = avio_size(input);
        if (size < 0) {
            av_strerror(ret, errbuf, sizeof(errbuf));
            fprintf(stderr, "Unable to get size of %s: %s\n", input_url, errbuf);
            goto fail;
        }
        bps = size / duration;
    }
    ret = avio_open2(&output, output_url, AVIO_FLAG_WRITE, NULL, NULL);
    if (ret) {
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Unable to open %s: %s\n", output_url, errbuf);
        goto fail;
    }

    start_time = av_gettime();
    while (1) {
        uint8_t buf[1024];
        int n;
        n = avio_read(input, buf, sizeof(buf));
        if (n <= 0)
            break;
        avio_write(output, buf, n);
        stream_pos += n;
        if (bps) {
            avio_flush(output);
            while ((av_gettime() - start_time) * bps / AV_TIME_BASE < stream_pos)
                av_usleep(50 * 1000);
        }
    }

    avio_flush(output);
    avio_close(output);

fail:
    avio_close(input);
    avformat_network_deinit();
    return ret ? 1 : 0;
}
