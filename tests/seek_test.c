/*
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2007 Michael Niedermayer
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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "libavformat/avformat.h"

#undef exit

int main(int argc, char **argv)
{
    const char *filename;
    AVFormatContext *ic;
    int i, ret, stream_id;
    int64_t timestamp;
    AVFormatParameters params, *ap= &params;
    memset(ap, 0, sizeof(params));
    ap->channels=1;
    ap->sample_rate= 22050;

    /* initialize libavcodec, and register all codecs and formats */
    av_register_all();

    if (argc != 2) {
        printf("usage: %s input_file\n"
               "\n", argv[0]);
        exit(1);
    }

    filename = argv[1];

    /* allocate the media context */
    ic = avformat_alloc_context();
    if (!ic) {
        fprintf(stderr, "Memory error\n");
        exit(1);
    }

    ret = av_open_input_file(&ic, filename, NULL, 0, ap);
    if (ret < 0) {
        fprintf(stderr, "cannot open %s\n", filename);
        exit(1);
    }

    ret = av_find_stream_info(ic);
    if (ret < 0) {
        fprintf(stderr, "%s: could not find codec parameters\n", filename);
        exit(1);
    }

    for(i=0; ; i++){
        AVPacket pkt;
        AVStream *st;

        memset(&pkt, 0, sizeof(pkt));
        if(ret>=0){
            ret= av_read_frame(ic, &pkt);
            printf("ret:%2d", ret);
            if(ret>=0){
                st= ic->streams[pkt.stream_index];
                printf(" st:%2d dts:%f pts:%f pos:%" PRId64 " size:%d flags:%d", pkt.stream_index, pkt.dts*av_q2d(st->time_base), pkt.pts*av_q2d(st->time_base), pkt.pos, pkt.size, pkt.flags);
            }
            printf("\n");
        }

        if(i>25) break;

        stream_id= (i>>1)%(ic->nb_streams+1) - 1;
        timestamp= (i*19362894167LL) % (4*AV_TIME_BASE) - AV_TIME_BASE;
        if(stream_id>=0){
            st= ic->streams[stream_id];
            timestamp= av_rescale_q(timestamp, AV_TIME_BASE_Q, st->time_base);
        }
        ret = av_seek_frame(ic, stream_id, timestamp, (i&1)*AVSEEK_FLAG_BACKWARD);
        printf("ret:%2d st:%2d ts:%f flags:%d\n", ret, stream_id, timestamp*(stream_id<0 ? 1.0/AV_TIME_BASE : av_q2d(st->time_base)), i&1);
    }

    return 0;
}
