/*
      Copyright (C) 2008  Reimar Döffinger

      Permission is hereby granted, free of charge, to any person
      obtaining a copy of this software and associated documentation
      files (the "Software"), to deal in the Software without
      restriction, including without limitation the rights to use, copy,
      modify, merge, publish, distribute, sublicense, and/or sell copies
      of the Software, and to permit persons to whom the Software is
      furnished to do so, subject to the following conditions:

      The above copyright notice and this permission notice shall be
      included in all copies or substantial portions of the Software.

      THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
      EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
      MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
      NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
      HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
      WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
      OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
      DEALINGS IN THE SOFTWARE.
**/

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "avformat.h"
#include "internal.h"
#include "oggdec.h"

struct speex_params {
    int packet_size;
    int final_packet_duration;
    int seq;
};

static int speex_header(AVFormatContext *s, int idx) {
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    struct speex_params *spxp = os->private;
    AVStream *st = s->streams[idx];
    uint8_t *p = os->buf + os->pstart;
    int ret;

    if (!spxp) {
        spxp = av_mallocz(sizeof(*spxp));
        if (!spxp)
            return AVERROR(ENOMEM);
        os->private = spxp;
    }

    if (spxp->seq > 1)
        return 0;

    if (spxp->seq == 0) {
        int frames_per_packet;
        int channels;
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id = AV_CODEC_ID_SPEEX;

        if (os->psize < 68) {
            av_log(s, AV_LOG_ERROR, "speex packet too small\n");
            return AVERROR_INVALIDDATA;
        }

        st->codecpar->sample_rate = AV_RL32(p + 36);
        if (st->codecpar->sample_rate <= 0) {
            av_log(s, AV_LOG_ERROR, "Invalid sample rate %d\n", st->codecpar->sample_rate);
            return AVERROR_INVALIDDATA;
        }
        channels = AV_RL32(p + 48);
        if (channels < 1 || channels > 2) {
            av_log(s, AV_LOG_ERROR, "invalid channel count. Speex must be mono or stereo.\n");
            return AVERROR_INVALIDDATA;
        }
        av_channel_layout_default(&st->codecpar->ch_layout, channels);

        spxp->packet_size  = AV_RL32(p + 56);
        frames_per_packet  = AV_RL32(p + 64);
        if (spxp->packet_size < 0 ||
            frames_per_packet < 0 ||
            spxp->packet_size * (int64_t)frames_per_packet > INT32_MAX / 256) {
            av_log(s, AV_LOG_ERROR, "invalid packet_size, frames_per_packet %d %d\n", spxp->packet_size, frames_per_packet);
            spxp->packet_size = 0;
            return AVERROR_INVALIDDATA;
        }
        if (frames_per_packet)
            spxp->packet_size *= frames_per_packet;

        if ((ret = ff_alloc_extradata(st->codecpar, os->psize)) < 0)
            return ret;
        memcpy(st->codecpar->extradata, p, st->codecpar->extradata_size);

        avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    } else
        ff_vorbis_stream_comment(s, st, p, os->psize);

    spxp->seq++;
    return 1;
}

static int ogg_page_packets(struct ogg_stream *os)
{
    int i;
    int packets = 0;
    for (i = 0; i < os->nsegs; i++)
        if (os->segments[i] < 255)
            packets++;
    return packets;
}

static int speex_packet(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    struct speex_params *spxp = os->private;
    int packet_size = spxp->packet_size;

    if (os->flags & OGG_FLAG_EOS && os->lastpts != AV_NOPTS_VALUE &&
        os->granule > 0) {
        /* first packet of final page. we have to calculate the final packet
           duration here because it is the only place we know the next-to-last
           granule position. */
        spxp->final_packet_duration = os->granule - os->lastpts -
                                      packet_size * (ogg_page_packets(os) - 1);
    }

    if (!os->lastpts && os->granule > 0)
        /* first packet */
        os->lastpts = os->lastdts = os->granule - packet_size *
                                    ogg_page_packets(os);
    if (os->flags & OGG_FLAG_EOS && os->segp == os->nsegs &&
        spxp->final_packet_duration)
        /* final packet */
        os->pduration = spxp->final_packet_duration;
    else
        os->pduration = packet_size;

    return 0;
}

const struct ogg_codec ff_speex_codec = {
    .magic = "Speex   ",
    .magicsize = 8,
    .header = speex_header,
    .packet = speex_packet,
    .nb_header = 2,
};
