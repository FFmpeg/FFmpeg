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

/*
 * Feed packets of several AC-3 frames, with skip samples side data on the
 * first one or two, through the full parser and decode the split packets.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/file.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavformat/demux.h"
#include "libavformat/internal.h"

#define FRAME_SIZE        1536 /* bytes per frame of the sample */
#define FRAMES_PER_PACKET 4

static uint8_t *file_buf;
static size_t file_size, file_pos;
static uint32_t skip_start, skip_end, next_skip_start;
static uint8_t reason_start, reason_end;

static int read_header(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);

    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = AV_CODEC_ID_AC3;
    st->codecpar->sample_rate = 48000;
    avpriv_set_pts_info(st, 64, 1, 48000);
    ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL;
    return 0;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    size_t size = FFMIN(file_size - file_pos, FRAME_SIZE * FRAMES_PER_PACKET);
    int ret;

    if (!size)
        return AVERROR_EOF;
    ret = av_new_packet(pkt, size);
    if (ret < 0)
        return ret;
    memcpy(pkt->data, file_buf + file_pos, size);
    if (!file_pos || (next_skip_start && file_pos == FRAME_SIZE * FRAMES_PER_PACKET)) {
        uint8_t *sd = av_packet_new_side_data(pkt, AV_PKT_DATA_SKIP_SAMPLES, 10);
        if (!sd)
            return AVERROR(ENOMEM);
        AV_WL32(sd,     file_pos ? next_skip_start : skip_start);
        AV_WL32(sd + 4, skip_end);
        sd[8] = reason_start;
        sd[9] = reason_end;
    }
    file_pos += size;
    return 0;
}

static const FFInputFormat test_demuxer = {
    .p.name      = "test",
    .p.flags     = AVFMT_NOFILE,
    .read_header = read_header,
    .read_packet = read_packet,
};

static int run(void)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AC3);
    AVFormatContext *s = NULL;
    AVCodecContext *avctx = NULL;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int64_t samples = 0;
    int ret, n = 0;

    file_pos = 0;
    if (!pkt || !frame || !codec) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    ret = avformat_open_input(&s, NULL, &test_demuxer.p, NULL);
    if (ret < 0)
        goto end;
    avctx = avcodec_alloc_context3(codec);
    if (!avctx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    ret = avcodec_open2(avctx, codec, NULL);
    if (ret < 0)
        goto end;

    printf("skip %"PRIu32"/%"PRIu32" reason %d/%d\n",
           skip_start, skip_end, reason_start, reason_end);
    while ((ret = av_read_frame(s, pkt)) >= 0) {
        size_t sd_size;
        const uint8_t *sd = av_packet_get_side_data(pkt, AV_PKT_DATA_SKIP_SAMPLES,
                                                    &sd_size);
        /* Check trailing discard at the end of the group and leading skip everywhere. */
        if (sd && sd_size >= 10 && (!skip_end || n == 0 || n >= FRAMES_PER_PACKET - 1 ||
                                   AV_RL32(sd) || sd[8]))
            printf("packet %d: pts %"PRId64" size %d skip %"PRIu32"/%"PRIu32
                   " reason %d/%d\n", n, pkt->pts, pkt->size,
                   AV_RL32(sd), AV_RL32(sd + 4), sd[8], sd[9]);
        n++;
        ret = avcodec_send_packet(avctx, pkt);
        av_packet_unref(pkt);
        if (ret < 0)
            goto end;
        while ((ret = avcodec_receive_frame(avctx, frame)) >= 0) {
            samples += frame->nb_samples;
            av_frame_unref(frame);
        }
        if (ret != AVERROR(EAGAIN))
            goto end;
    }
    if (ret != AVERROR_EOF)
        goto end;
    ret = avcodec_send_packet(avctx, NULL);
    while (ret >= 0 && (ret = avcodec_receive_frame(avctx, frame)) >= 0) {
        samples += frame->nb_samples;
        av_frame_unref(frame);
    }
    if (!skip_end)
        printf("packets %d, decoded samples %"PRId64"\n", n, samples);
    ret = ret == AVERROR_EOF ? 0 : ret;

end:
    avcodec_free_context(&avctx);
    avformat_close_input(&s);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    return ret;
}

int main(int argc, char **argv)
{
    static const struct {
        uint32_t start, end;
        uint8_t  reason_start, reason_end;
        uint32_t next_start;
    } tests[] = {
        { 2000,   0, 0, 0,    0 }, /* leading skip ending in the second frame */
        { 3500,   0, 0, 0,    0 }, /* leading skip over three frames */
        { 1000,   0, 0, 0, 1100 }, /* successive packets with distinct leading skips */
        { 3500, 100, 1, 1,    0 }, /* leading skip and trailing discard */
    };
    int ret = 0;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <ac3 file>\n", argv[0]);
        return 1;
    }
    av_log_set_level(AV_LOG_ERROR);
    if (av_file_map(argv[1], &file_buf, &file_size, 0, NULL) < 0)
        return 1;
    for (int i = 0; i < FF_ARRAY_ELEMS(tests) && ret >= 0; i++) {
        skip_start   = tests[i].start;
        skip_end     = tests[i].end;
        reason_start = tests[i].reason_start;
        reason_end   = tests[i].reason_end;
        next_skip_start = tests[i].next_start;
        ret = run();
    }
    av_file_unmap(file_buf, file_size);
    return ret < 0;
}
