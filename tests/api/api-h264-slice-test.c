/*
 * Copyright (c) 2001 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define MAX_SLICES 8

#include "config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_IO_H
#include <io.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "libavcodec/avcodec.h"
#include "libavutil/pixdesc.h"
#include "libavutil/hash.h"
#include "libavutil/bswap.h"

static int header = 0;

static int decode(AVCodecContext *dec_ctx, AVFrame *frame,
           AVPacket *pkt)
{
    static uint64_t frame_cnt = 0;
    int ret;

    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error sending a packet for decoding: %s\n", av_err2str(ret));
        return ret;
    }

    while (ret >= 0) {
        const AVPixFmtDescriptor *desc;
        char sum[AV_HASH_MAX_SIZE * 2 + 1];
        struct AVHashContext *hash;

        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        } else if (ret < 0) {
            fprintf(stderr, "Error during decoding: %s\n", av_err2str(ret));
            return ret;
        }

        if (!header) {
            printf(
            "#format: frame checksums\n"
            "#version: 2\n"
            "#hash: MD5\n"
            "#tb 0: 1/30\n"
            "#media_type 0: video\n"
            "#codec_id 0: rawvideo\n"
            "#dimensions 0: 352x288\n"
            "#sar 0: 128/117\n"
            "#stream#, dts,        pts, duration,     size, hash\n");
            header = 1;
        }
        desc = av_pix_fmt_desc_get(dec_ctx->pix_fmt);
        if ((ret = av_hash_alloc(&hash, "md5")) < 0) {
            return ret;
        }
        av_hash_init(hash);

        for (int i = 0; i < frame->height; i++)
            av_hash_update(hash, &frame->data[0][i * frame->linesize[0]], frame->width);
        for (int i = 0; i < frame->height >> desc->log2_chroma_h; i++)
            av_hash_update(hash, &frame->data[1][i * frame->linesize[1]], frame->width >> desc->log2_chroma_w);
        for (int i = 0; i < frame->height >> desc->log2_chroma_h; i++)
            av_hash_update(hash, &frame->data[2][i * frame->linesize[2]], frame->width >> desc->log2_chroma_w);

        av_hash_final_hex(hash, sum, av_hash_get_size(hash) * 2 + 1);
        printf("0, %10"PRId64", %10"PRId64",        1, %8d, %s\n",
            frame_cnt, frame_cnt,
            (frame->width * frame->height + 2 * (frame->height >> desc->log2_chroma_h) * (frame->width >> desc->log2_chroma_w)), sum);
        frame_cnt += 1;
        av_hash_freep(&hash);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const AVCodec *codec = NULL;
    AVCodecContext *c = NULL;
    AVFrame *frame = NULL;
    unsigned int threads;
    AVPacket *pkt;
    FILE *file = NULL;
    char * nal = NULL;
    int nals = 0, ret = 0;
    char *p;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <threads> <input file>\n", argv[0]);
        return -1;
    }

    if (!(threads = strtoul(argv[1], NULL, 0)))
        threads = 1;
    else if (threads > MAX_SLICES)
        threads = MAX_SLICES;

#ifdef _WIN32
    setmode(fileno(stdout), O_BINARY);
#endif

    if (!(pkt = av_packet_alloc())) {
        return -1;
    }

    nal = av_malloc(MAX_SLICES * UINT16_MAX + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!nal)
        goto err;
    p = nal;

    if (!(codec = avcodec_find_decoder(AV_CODEC_ID_H264))) {
        fprintf(stderr, "Codec not found\n");
        ret = -1;
        goto err;
    }

    if (!(c = avcodec_alloc_context3(codec))) {
        fprintf(stderr, "Could not allocate video codec context\n");
        ret = -1;
        goto err;
    }

    c->width  = 352;
    c->height = 288;

    c->flags2 |= AV_CODEC_FLAG2_CHUNKS;
    c->thread_type = FF_THREAD_SLICE;
    c->thread_count = threads;

    if ((ret = avcodec_open2(c, codec, NULL)) < 0) {
        fprintf(stderr, "Could not open codec\n");
        goto err;
    }

#if HAVE_THREADS
    if (c->active_thread_type != FF_THREAD_SLICE) {
        fprintf(stderr, "Couldn't activate slice threading: %d\n", c->active_thread_type);
        ret = -1;
        goto err;
    }
#else
    fprintf(stderr, "WARN: not using threads, only checking decoding slice NALUs\n");
#endif

    if (!(frame = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate video frame\n");
        ret = -1;
        goto err;
    }

    if (!(file = fopen(argv[2], "rb"))) {
        fprintf(stderr, "Couldn't open NALU file: %s\n", argv[2]);
        ret = -1;
        goto err;
    }

    while(1) {
        uint16_t size = 0;
        size_t ret = fread(&size, 1, sizeof(uint16_t), file);
        if (ret != sizeof(uint16_t))
            break;

        size = av_be2ne16(size);
        ret = fread(p, 1, size, file);
        if (ret != size) {
            perror("Couldn't read data");
            goto err;
        }
        p += ret;

        if (++nals >= threads) {
            int decret = 0;
            pkt->data = nal;
            pkt->size = p - nal;
            if ((decret = decode(c, frame, pkt)) < 0) {
                goto err;
            }
            memset(nal, 0, MAX_SLICES * UINT16_MAX + AV_INPUT_BUFFER_PADDING_SIZE);
            nals = 0;
            p = nal;
        }
    }

    if (nals) {
        pkt->data = nal;
        pkt->size = p - nal;
        if ((ret = decode(c, frame, pkt)) < 0) {
            goto err;
        }
    }

    ret = decode(c, frame, NULL);

err:
    if (nal)
        av_free(nal);
    if (file)
        fclose(file);
    av_frame_free(&frame);
    avcodec_free_context(&c);
    av_packet_free(&pkt);

    return ret;
}
