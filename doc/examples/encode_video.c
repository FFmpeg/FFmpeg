/*
 * copyright (c) 2001 Fabrice Bellard
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

/**
 * @file
 * video encoding with libavcodec API example
 *
 * @example encode_video.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavcodec/avcodec.h"

#include "libavutil/frame.h"
#include "libavutil/imgutils.h"

int main(int argc, char **argv)
{
    const char *filename;
    AVCodec *codec;
    AVCodecContext *c= NULL;
    int i, ret, x, y, got_output;
    FILE *f;
    AVFrame *picture;
    AVPacket pkt;
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };

    if (argc <= 1) {
        fprintf(stderr, "Usage: %s <output file>\n", argv[0]);
        exit(0);
    }
    filename = argv[1];

    avcodec_register_all();

    /* find the mpeg1video encoder */
    codec = avcodec_find_encoder(AV_CODEC_ID_MPEG1VIDEO);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    picture = av_frame_alloc();

    /* put sample parameters */
    c->bit_rate = 400000;
    /* resolution must be a multiple of two */
    c->width = 352;
    c->height = 288;
    /* frames per second */
    c->time_base= (AVRational){1,25};
    c->gop_size = 10; /* emit one intra frame every ten frames */
    c->max_b_frames=1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "could not open codec\n");
        exit(1);
    }

    f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "could not open %s\n", filename);
        exit(1);
    }

    ret = av_image_alloc(picture->data, picture->linesize, c->width, c->height,
                         c->pix_fmt, 32);
    if (ret < 0) {
        fprintf(stderr, "could not alloc raw picture buffer\n");
        exit(1);
    }
    picture->format = c->pix_fmt;
    picture->width  = c->width;
    picture->height = c->height;

    /* encode 1 second of video */
    for(i=0;i<25;i++) {
        av_init_packet(&pkt);
        pkt.data = NULL;    // packet data will be allocated by the encoder
        pkt.size = 0;

        fflush(stdout);
        /* prepare a dummy image */
        /* Y */
        for(y=0;y<c->height;y++) {
            for(x=0;x<c->width;x++) {
                picture->data[0][y * picture->linesize[0] + x] = x + y + i * 3;
            }
        }

        /* Cb and Cr */
        for(y=0;y<c->height/2;y++) {
            for(x=0;x<c->width/2;x++) {
                picture->data[1][y * picture->linesize[1] + x] = 128 + y + i * 2;
                picture->data[2][y * picture->linesize[2] + x] = 64 + x + i * 5;
            }
        }

        picture->pts = i;

        /* encode the image */
        ret = avcodec_encode_video2(c, &pkt, picture, &got_output);
        if (ret < 0) {
            fprintf(stderr, "error encoding frame\n");
            exit(1);
        }

        if (got_output) {
            printf("encoding frame %3d (size=%5d)\n", i, pkt.size);
            fwrite(pkt.data, 1, pkt.size, f);
            av_packet_unref(&pkt);
        }
    }

    /* get the delayed frames */
    for (got_output = 1; got_output; i++) {
        fflush(stdout);

        ret = avcodec_encode_video2(c, &pkt, NULL, &got_output);
        if (ret < 0) {
            fprintf(stderr, "error encoding frame\n");
            exit(1);
        }

        if (got_output) {
            printf("encoding frame %3d (size=%5d)\n", i, pkt.size);
            fwrite(pkt.data, 1, pkt.size, f);
            av_packet_unref(&pkt);
        }
    }

    /* add sequence end code to have a real MPEG file */
    fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);

    avcodec_free_context(&c);
    av_freep(&picture->data[0]);
    av_frame_free(&picture);

    return 0;
}
