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
#include <string.h>

#include "libavutil/common.h"
#include "libavutil/mathematics.h"
#include "libavformat/avformat.h"

static char buffer[20];

static const char *ret_str(int v)
{
    switch (v) {
    case AVERROR_EOF:     return "-EOF";
    case AVERROR(EIO):    return "-EIO";
    case AVERROR(ENOMEM): return "-ENOMEM";
    case AVERROR(EINVAL): return "-EINVAL";
    default:
        snprintf(buffer, sizeof(buffer), "%2d", v);
        return buffer;
    }
}

static void ts_str(char buffer[60], int64_t ts, AVRational base)
{
    if (ts == AV_NOPTS_VALUE) {
        strcpy(buffer, " NOPTS   ");
        return;
    }
    ts= av_rescale_q(ts, base, (AVRational){1, 1000000});
    snprintf(buffer, 60, "%c%"PRId64".%06"PRId64"", ts<0 ? '-' : ' ', FFABS(ts)/1000000, FFABS(ts)%1000000);
}

int main(int argc, char **argv)
{
    const char *filename;
    AVFormatContext *ic = NULL;
    int i, ret, stream_id;
    int j;
    int64_t timestamp;
    AVDictionary *format_opts = NULL;
    int64_t seekfirst = AV_NOPTS_VALUE;
    int firstback=0;
    int frame_count = 1;
    int duration = 4;

    for(i=2; i<argc; i+=2){
        if       (!strcmp(argv[i], "-seekforw")){
            seekfirst = atoi(argv[i+1]);
        } else if(!strcmp(argv[i], "-seekback")){
            seekfirst = atoi(argv[i+1]);
            firstback = 1;
        } else if(!strcmp(argv[i], "-frames")){
            frame_count = atoi(argv[i+1]);
        } else if(!strcmp(argv[i], "-duration")){
            duration = atoi(argv[i+1]);
        } else {
            argc = 1;
        }
    }

    av_dict_set(&format_opts, "channels", "1", 0);
    av_dict_set(&format_opts, "sample_rate", "22050", 0);

    /* initialize libavcodec, and register all codecs and formats */
    av_register_all();

    if (argc < 2) {
        printf("usage: %s input_file\n"
               "\n", argv[0]);
        return 1;
    }

    filename = argv[1];

    ret = avformat_open_input(&ic, filename, NULL, &format_opts);
    av_dict_free(&format_opts);
    if (ret < 0) {
        fprintf(stderr, "cannot open %s\n", filename);
        return 1;
    }

    ret = avformat_find_stream_info(ic, NULL);
    if (ret < 0) {
        fprintf(stderr, "%s: could not find codec parameters\n", filename);
        return 1;
    }

    if(seekfirst != AV_NOPTS_VALUE){
        if(firstback)   avformat_seek_file(ic, -1, INT64_MIN, seekfirst, seekfirst, 0);
        else            avformat_seek_file(ic, -1, seekfirst, seekfirst, INT64_MAX, 0);
    }
    for(i=0; ; i++){
        AVPacket pkt = { 0 };
        AVStream *av_uninit(st);
        char ts_buf[60];

        if(ret>=0){
            for(j=0; j<frame_count; j++) {
            ret= av_read_frame(ic, &pkt);
            if(ret>=0){
                char dts_buf[60];
                st= ic->streams[pkt.stream_index];
                ts_str(dts_buf, pkt.dts, st->time_base);
                ts_str(ts_buf,  pkt.pts, st->time_base);
                printf("ret:%-10s st:%2d flags:%d dts:%s pts:%s pos:%7" PRId64 " size:%6d", ret_str(ret), pkt.stream_index, pkt.flags, dts_buf, ts_buf, pkt.pos, pkt.size);
                av_free_packet(&pkt);
            } else
                printf("ret:%s", ret_str(ret)); // necessary to avoid trailing whitespace
            printf("\n");
            }
        }

        if(i>25) break;

        stream_id= (i>>1)%(ic->nb_streams+1) - 1;
        timestamp= (i*19362894167LL) % (duration*AV_TIME_BASE) - AV_TIME_BASE;
        if(stream_id>=0){
            st= ic->streams[stream_id];
            timestamp= av_rescale_q(timestamp, AV_TIME_BASE_Q, st->time_base);
        }
        //FIXME fully test the new seek API
        if(i&1) ret = avformat_seek_file(ic, stream_id, INT64_MIN, timestamp, timestamp, 0);
        else    ret = avformat_seek_file(ic, stream_id, timestamp, timestamp, INT64_MAX, 0);
        ts_str(ts_buf, timestamp, stream_id < 0 ? AV_TIME_BASE_Q : st->time_base);
        printf("ret:%-10s st:%2d flags:%d  ts:%s\n", ret_str(ret), stream_id, i&1, ts_buf);
    }

    avformat_close_input(&ic);

    return 0;
}
