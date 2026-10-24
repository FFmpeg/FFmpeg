/*
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
 * along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/mem.h"
#include "libavformat/avformat.h"
#include "libavformat/avio_internal.h"

#define AU_PAYLOAD 80

static int run_case(int channels, int bps, int64_t bit_rate)
{
    AVFormatContext *out = NULL, *in = NULL;
    AVIOContext *pb = NULL;
    FFIOContext in_pb;
    AVStream *st;
    AVPacket *pkt = NULL;
    uint8_t *buf = NULL;
    int ret, size;

    ret = avformat_alloc_output_context2(&out, NULL, "au", NULL);
    if (ret < 0)
        return 1;
    if (avio_open_dyn_buf(&pb) < 0) {
        ret = 1;
        goto done;
    }
    out->pb = pb;

    st = avformat_new_stream(out, NULL);
    if (!st) {
        ret = 1;
        goto done;
    }
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = AV_CODEC_ID_ADPCM_G726LE;
    st->codecpar->sample_rate = 8000;
    st->codecpar->bits_per_coded_sample = bps;
    st->codecpar->bit_rate    = bit_rate;
    av_channel_layout_default(&st->codecpar->ch_layout, channels);

    ret = avformat_write_header(out, NULL);
    if (ret < 0) {
        printf("channels=%d bps=%d bit_rate=%"PRId64" rejected\n",
               channels, bps, bit_rate);
        ret = 0;
        goto done;
    }

    pkt = av_packet_alloc();
    if (!pkt || (ret = av_new_packet(pkt, AU_PAYLOAD)) < 0) {
        ret = 1;
        goto done;
    }
    memset(pkt->data, 0, pkt->size);
    pkt->stream_index = 0;
    pkt->pts = pkt->dts = 0;
    if ((ret = av_write_frame(out, pkt)) < 0 ||
        (ret = av_write_trailer(out)) < 0) {
        ret = 1;
        goto done;
    }

    size = avio_close_dyn_buf(pb, &buf);
    pb = NULL;
    if (size < 16) {
        ret = 1;
        goto done;
    }

    ffio_init_read_context(&in_pb, buf, size);
    in = avformat_alloc_context();
    if (!in) {
        ret = 1;
        goto done;
    }
    in->pb = &in_pb.pub;
    if ((ret = avformat_open_input(&in, NULL, av_find_input_format("au"), NULL)) < 0 ||
        !in->nb_streams) {
        ret = 1;
        goto done;
    }

    st = in->streams[0];
    printf("channels=%d bps=%d bit_rate=%"PRId64" tag=%02x %02x %02x %02x "
           "read_bps=%d read_bit_rate=%"PRId64" duration=%"PRId64"\n",
           channels, bps, bit_rate, buf[12], buf[13], buf[14], buf[15],
           st->codecpar->bits_per_coded_sample, st->codecpar->bit_rate,
           st->duration);
    ret = 0;

done:
    av_packet_free(&pkt);
    avformat_close_input(&in);
    if (pb) {
        uint8_t *tmp = NULL;
        avio_close_dyn_buf(pb, &tmp);
        av_free(tmp);
    }
    av_free(buf);
    avformat_free_context(out);
    return ret < 0 ? 1 : ret;
}

int main(void)
{
    static const int cases[][3] = {
        { 1, 2,     0 },
        { 1, 3,     0 },
        { 1, 4,     0 },
        { 1, 5,     0 },
        { 1, 0, 16000 },
        { 1, 0, 40000 },
        { 1, 1,     0 },
        { 1, 0,     0 },
        { 2, 2, 32000 },
        { 2, 0, 32000 },
        { 2, 4, 64000 },
        { 2, 0, 64000 },
    };
    int i, ret = 0;

    for (i = 0; i < FF_ARRAY_ELEMS(cases); i++)
        ret |= run_case(cases[i][0], cases[i][1], cases[i][2]);
    return ret;
}
