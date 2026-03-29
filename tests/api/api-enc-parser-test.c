/*
 * Copyright (c) 2026 Soham Kute
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

/*
 * Encoder + parser API test.
 * Usage: api-enc-parser-test [codec_name [width height]]
 * Defaults: h261, 176, 144
 *
 * Encodes two frames with the named encoder, concatenates the packets,
 * and feeds the result to the matching parser to verify frame boundary
 * detection.  For each non-empty output the size and up to four bytes
 * at the start and end are printed for comparison against a reference file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavcodec/avcodec.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"

/* Garbage with no PSC - parser must return out_size == 0 */
static const uint8_t garbage[] = {
    0xff, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78,
};

/*
 * Encode n_frames of video at width x height using enc.
 * Returns concatenated raw bitstream; caller must av_free() it.
 * Returns NULL on error.
 */
static uint8_t *encode_frames(const AVCodec *enc, int width, int height,
                               int n_frames, size_t *out_size)
{
    AVCodecContext *enc_ctx = NULL;
    AVFrame *frame = NULL;
    AVPacket *pkt = NULL;
    uint8_t *buf = NULL, *tmp;
    size_t buf_size = 0;
    const enum AVPixelFormat *pix_fmts;
    const AVPixFmtDescriptor *desc;
    int num_pix_fmts;
    int chroma_h;
    int ret;

    *out_size = 0;

    enc_ctx = avcodec_alloc_context3(enc);
    if (!enc_ctx)
        return NULL;

    /* use first supported pixel format, fall back to yuv420p */
    ret = avcodec_get_supported_config(enc_ctx, enc, AV_CODEC_CONFIG_PIX_FORMAT,
                                       0, (const void **)&pix_fmts, &num_pix_fmts);
    enc_ctx->pix_fmt   = (ret >= 0 && num_pix_fmts > 0) ? pix_fmts[0]
                                                          : AV_PIX_FMT_YUV420P;
    enc_ctx->width     = width;
    enc_ctx->height    = height;
    enc_ctx->time_base = (AVRational){ 1, 25 };

    if (avcodec_open2(enc_ctx, enc, NULL) < 0)
        goto fail;

    desc = av_pix_fmt_desc_get(enc_ctx->pix_fmt);
    if (!desc)
        goto fail;
    chroma_h = AV_CEIL_RSHIFT(height, desc->log2_chroma_h);

    frame = av_frame_alloc();
    if (!frame)
        goto fail;

    frame->format = enc_ctx->pix_fmt;
    frame->width  = width;
    frame->height = height;

    if (av_frame_get_buffer(frame, 0) < 0)
        goto fail;

    pkt = av_packet_alloc();
    if (!pkt)
        goto fail;

    for (int i = 0; i < n_frames; i++) {
        frame->pts = i;
        if (av_frame_make_writable(frame) < 0)
            goto fail;
        /* fill with flat color so encoder produces deterministic output */
        memset(frame->data[0], 128, (size_t)frame->linesize[0] * height);
        if (frame->data[1])
            memset(frame->data[1], 64, (size_t)frame->linesize[1] * chroma_h);
        if (frame->data[2])
            memset(frame->data[2], 64, (size_t)frame->linesize[2] * chroma_h);

        ret = avcodec_send_frame(enc_ctx, frame);
        if (ret < 0)
            goto fail;

        while (ret >= 0) {
            ret = avcodec_receive_packet(enc_ctx, pkt);
            if (ret == AVERROR(EAGAIN))
                break;
            if (ret < 0)
                goto fail;

            tmp = av_realloc(buf, buf_size + pkt->size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!tmp) {
                av_packet_unref(pkt);
                goto fail;
            }
            buf = tmp;
            memcpy(buf + buf_size, pkt->data, pkt->size);
            buf_size += pkt->size;
            av_packet_unref(pkt);
        }
    }

    /* flush encoder */
    ret = avcodec_send_frame(enc_ctx, NULL);
    if (ret < 0 && ret != AVERROR_EOF)
        goto fail;
    while (1) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
            break;
        if (ret < 0)
            goto fail;
        tmp = av_realloc(buf, buf_size + pkt->size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!tmp) {
            av_packet_unref(pkt);
            goto fail;
        }
        buf = tmp;
        memcpy(buf + buf_size, pkt->data, pkt->size);
        buf_size += pkt->size;
        av_packet_unref(pkt);
    }

    if (!buf)
        goto fail;
    memset(buf + buf_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    *out_size = buf_size;
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&enc_ctx);
    return buf;

fail:
    av_free(buf);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&enc_ctx);
    return NULL;
}

/* Print label, out_size, and first/last 4 bytes of out when non-empty. */
static void print_parse_result(const char *label,
                                const uint8_t *out, int out_size)
{
    printf("%s: out_size=%d", label, out_size);
    if (out && out_size > 0) {
        int n = out_size < 4 ? out_size : 4;
        int k;
        printf(" first=");
        for (k = 0; k < n; k++)
            printf(k ? " %02x" : "%02x", out[k]);
        if (out_size > 4) {
            printf(" last=");
            for (k = out_size - 4; k < out_size; k++)
                printf(k > out_size - 4 ? " %02x" : "%02x", out[k]);
        }
    }
    printf("\n");
}

/*
 * Single parse call on buf — prints the result with label.
 * Returns out_size on success, negative AVERROR on error.
 * No flush; used to verify the parser does not emit output for a given input.
 */
static int parse_once(AVCodecContext *avctx, enum AVCodecID codec_id,
                      const char *label,
                      const uint8_t *buf, int buf_size)
{
    AVCodecParserContext *parser = av_parser_init(codec_id);
    uint8_t *out;
    int out_size, ret;

    if (!parser)
        return AVERROR(ENOSYS);
    ret = av_parser_parse2(parser, avctx, &out, &out_size,
                           buf, buf_size,
                           AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
    print_parse_result(label, out, ret < 0 ? 0 : out_size);
    av_parser_close(parser);
    return ret < 0 ? ret : out_size;
}

/*
 * Feed buf through a fresh parser in chunks of chunk_size bytes.
 * chunk_size=0 feeds all data in one call.
 * Prints each emitted frame as "tag[N]".
 * Returns frame count (>=0) or negative AVERROR on error.
 */
static int parse_stream(AVCodecContext *avctx, enum AVCodecID codec_id,
                        const char *tag,
                        const uint8_t *buf, int buf_size, int chunk_size,
                        uint8_t **all_out, size_t *all_size)
{
    AVCodecParserContext *parser = av_parser_init(codec_id);
    const uint8_t *p = buf;
    int remaining = buf_size;
    int n = 0;
    uint8_t *out;
    int out_size, consumed;

    if (!parser)
        return AVERROR(ENOSYS);

    if (chunk_size <= 0)
        chunk_size = buf_size ? buf_size : 1;

    while (remaining > 0) {
        int feed = remaining < chunk_size ? remaining : chunk_size;
        consumed = av_parser_parse2(parser, avctx, &out, &out_size,
                                    p, feed,
                                    AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
        if (consumed < 0) {
            av_parser_close(parser);
            return consumed;
        }
        if (out_size > 0) {
            char label[64];
            snprintf(label, sizeof(label), "%s[%d]", tag, n++);
            print_parse_result(label, out, out_size);
            if (all_out) {
                uint8_t *tmp = av_realloc(*all_out, *all_size + out_size);
                if (!tmp) {
                    av_parser_close(parser);
                    return AVERROR(ENOMEM);
                }
                memcpy(tmp + *all_size, out, out_size);
                *all_out  = tmp;
                *all_size += out_size;
            }
        }
        /* advance by consumed bytes; if parser consumed nothing, skip the
         * fed chunk to avoid an infinite loop */
        p         += consumed > 0 ? consumed : feed;
        remaining -= consumed > 0 ? consumed : feed;
    }

    /* flush any frame the parser held waiting for a next-frame start code */
    consumed = av_parser_parse2(parser, avctx, &out, &out_size,
                                NULL, 0,
                                AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
    if (consumed < 0) {
        av_parser_close(parser);
        return consumed;
    }
    if (out_size > 0) {
        char label[64];
        snprintf(label, sizeof(label), "%s[%d]", tag, n++);
        print_parse_result(label, out, out_size);
        if (all_out) {
            uint8_t *tmp = av_realloc(*all_out, *all_size + out_size);
            if (!tmp) {
                av_parser_close(parser);
                return AVERROR(ENOMEM);
            }
            memcpy(tmp + *all_size, out, out_size);
            *all_out  = tmp;
            *all_size += out_size;
        }
    }

    av_parser_close(parser);
    return n;
}

int main(int argc, char **argv)
{
    const char *codec_name = argc > 1 ? argv[1] : "h261";
    int width              = argc > 2 ? atoi(argv[2]) : 176;
    int height             = argc > 3 ? atoi(argv[3]) : 144;
    AVCodecContext *avctx = NULL;
    AVCodecParserContext *parser;
    uint8_t *encoded = NULL;
    size_t encoded_size;
    enum AVCodecID codec_id;
    const AVCodec *enc;
    uint8_t *bulk_data = NULL, *split_data = NULL;
    size_t bulk_sz = 0, split_sz = 0;
    int n, ret;

    av_log_set_level(AV_LOG_ERROR);

    enc = avcodec_find_encoder_by_name(codec_name);
    if (!enc) {
        av_log(NULL, AV_LOG_ERROR, "encoder '%s' not found\n", codec_name);
        return 1;
    }
    codec_id = enc->id;

    /* verify parser is available before running tests */
    parser = av_parser_init(codec_id);
    if (!parser) {
        av_log(NULL, AV_LOG_ERROR, "parser for '%s' not available\n", codec_name);
        return 1;
    }
    av_parser_close(parser);

    avctx = avcodec_alloc_context3(NULL);
    if (!avctx)
        return 1;
    avctx->codec_id = codec_id;

    /* encode two real frames to use as parser input */
    encoded = encode_frames(enc, width, height, 2, &encoded_size);
    if (!encoded || encoded_size == 0) {
        av_log(NULL, AV_LOG_ERROR, "encoder '%s' failed\n", codec_name);
        avcodec_free_context(&avctx);
        return 1;
    }

    /* test 1: single parse call on garbage — no PSC means out_size must be 0 */
    ret = parse_once(avctx, codec_id, "garbage", garbage, (int)sizeof(garbage));
    if (ret != 0) {
        av_log(NULL, AV_LOG_ERROR, "garbage test failed\n");
        goto fail;
    }

    /* test 2: two real encoded frames fed all at once — parser must split
     * them and emit exactly 2 frames */
    n = parse_stream(avctx, codec_id, "bulk", encoded, (int)encoded_size, 0,
                     &bulk_data, &bulk_sz);
    if (n != 2) {
        av_log(NULL, AV_LOG_ERROR, "bulk test failed: got %d frames\n", n);
        goto fail;
    }

    /* test 3: same two frames split mid-stream — verify the parser handles
     * partial input and still emits exactly 2 frames, with identical bytes */
    n = parse_stream(avctx, codec_id, "split", encoded, (int)encoded_size,
                     (int)encoded_size / 2, &split_data, &split_sz);
    if (n != 2) {
        av_log(NULL, AV_LOG_ERROR, "split test failed: got %d frames\n", n);
        goto fail;
    }

    if (bulk_sz != split_sz || memcmp(bulk_data, split_data, bulk_sz) != 0) {
        av_log(NULL, AV_LOG_ERROR, "bulk and split outputs differ\n");
        goto fail;
    }

    av_free(bulk_data);
    av_free(split_data);
    av_free(encoded);
    avcodec_free_context(&avctx);
    return 0;

fail:
    av_free(bulk_data);
    av_free(split_data);
    av_free(encoded);
    avcodec_free_context(&avctx);
    return 1;
}
