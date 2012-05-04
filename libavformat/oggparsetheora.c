/**
      Copyright (C) 2005  Matthieu CASTET, Alex Beregszaszi

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

#include <stdlib.h>
#include "libavutil/bswap.h"
#include "libavcodec/get_bits.h"
#include "avformat.h"
#include "internal.h"
#include "oggdec.h"

struct theora_params {
    int gpshift;
    int gpmask;
    unsigned version;
};

static int
theora_header (AVFormatContext * s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    struct theora_params *thp = os->private;
    int cds = st->codec->extradata_size + os->psize + 2;
    uint8_t *cdp;

    if(!(os->buf[os->pstart] & 0x80))
        return 0;

    if(!thp){
        thp = av_mallocz(sizeof(*thp));
        os->private = thp;
    }

    if (os->buf[os->pstart] == 0x80) {
        GetBitContext gb;
        int width, height;
        AVRational timebase;

        init_get_bits(&gb, os->buf + os->pstart, os->psize*8);

        skip_bits_long(&gb, 7*8); /* 0x80"theora" */

        thp->version = get_bits_long(&gb, 24);
        if (thp->version < 0x030100)
        {
            av_log(s, AV_LOG_ERROR,
                "Too old or unsupported Theora (%x)\n", thp->version);
            return -1;
        }

        width  = get_bits(&gb, 16) << 4;
        height = get_bits(&gb, 16) << 4;
        avcodec_set_dimensions(st->codec, width, height);

        if (thp->version >= 0x030400)
            skip_bits(&gb, 100);

        if (thp->version >= 0x030200) {
            width  = get_bits_long(&gb, 24);
            height = get_bits_long(&gb, 24);
            if (   width  <= st->codec->width  && width  > st->codec->width-16
                && height <= st->codec->height && height > st->codec->height-16)
                avcodec_set_dimensions(st->codec, width, height);

            skip_bits(&gb, 16);
        }
        timebase.den = get_bits_long(&gb, 32);
        timebase.num = get_bits_long(&gb, 32);
        if (!(timebase.num > 0 && timebase.den > 0)) {
            av_log(s, AV_LOG_WARNING, "Invalid time base in theora stream, assuming 25 FPS\n");
            timebase.num = 1;
            timebase.den = 25;
        }
        avpriv_set_pts_info(st, 64, timebase.num, timebase.den);

        st->sample_aspect_ratio.num = get_bits_long(&gb, 24);
        st->sample_aspect_ratio.den = get_bits_long(&gb, 24);

        if (thp->version >= 0x030200)
            skip_bits_long(&gb, 38);
        if (thp->version >= 0x304000)
            skip_bits(&gb, 2);

        thp->gpshift = get_bits(&gb, 5);
        thp->gpmask = (1 << thp->gpshift) - 1;

        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codec->codec_id = CODEC_ID_THEORA;
        st->need_parsing = AVSTREAM_PARSE_HEADERS;

    } else if (os->buf[os->pstart] == 0x83) {
        ff_vorbis_comment (s, &st->metadata, os->buf + os->pstart + 7, os->psize - 8);
    }

    st->codec->extradata = av_realloc (st->codec->extradata,
                                       cds + FF_INPUT_BUFFER_PADDING_SIZE);
    cdp = st->codec->extradata + st->codec->extradata_size;
    *cdp++ = os->psize >> 8;
    *cdp++ = os->psize & 0xff;
    memcpy (cdp, os->buf + os->pstart, os->psize);
    st->codec->extradata_size = cds;

    return 1;
}

static uint64_t
theora_gptopts(AVFormatContext *ctx, int idx, uint64_t gp, int64_t *dts)
{
    struct ogg *ogg = ctx->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    struct theora_params *thp = os->private;
    uint64_t iframe, pframe;

    if (!thp)
        return AV_NOPTS_VALUE;

    iframe = gp >> thp->gpshift;
    pframe = gp & thp->gpmask;

    if (thp->version < 0x030201)
        iframe++;

    if(!pframe)
        os->pflags |= AV_PKT_FLAG_KEY;

    if (dts)
        *dts = iframe + pframe;

    return iframe + pframe;
}

static int theora_packet(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    int duration;

    /* first packet handling
       here we parse the duration of each packet in the first page and compare
       the total duration to the page granule to find the encoder delay and
       set the first timestamp */

    if (!os->lastpts) {
        int seg;
        uint8_t *last_pkt = os->buf + os->pstart;
        uint8_t *next_pkt = last_pkt;
        int first_duration = 0;

        duration = 0;
        for (seg = 0; seg < os->nsegs; seg++) {
            if (os->segments[seg] < 255) {
                if (!duration)
                    first_duration = 1;
                duration++;
                last_pkt = next_pkt + os->segments[seg];
            }
            next_pkt += os->segments[seg];
        }
        os->lastpts = os->lastdts   = theora_gptopts(s, idx, os->granule, NULL) - duration;
        s->streams[idx]->start_time = os->lastpts + first_duration;
        if (s->streams[idx]->duration)
            s->streams[idx]->duration -= s->streams[idx]->start_time;
    }

    /* parse packet duration */
    if (os->psize > 0) {
        os->pduration = 1;
    }

    return 0;
}

const struct ogg_codec ff_theora_codec = {
    .magic = "\200theora",
    .magicsize = 7,
    .header = theora_header,
    .packet = theora_packet,
    .gptopts = theora_gptopts
};
