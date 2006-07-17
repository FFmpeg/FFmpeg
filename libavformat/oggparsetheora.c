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
#include "avformat.h"
#include "bitstream.h"
#include "bswap.h"
#include "ogg2.h"

typedef struct theora_params {
    int gpshift;
    int gpmask;
} theora_params_t;

static int
theora_header (AVFormatContext * s, int idx)
{
    ogg_t *ogg = s->priv_data;
    ogg_stream_t *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    theora_params_t *thp = os->private;
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
        int version;

        init_get_bits(&gb, os->buf + os->pstart, os->psize*8);

        skip_bits(&gb, 7*8); /* 0x80"theora" */

        version = get_bits(&gb, 8) << 16;
        version |= get_bits(&gb, 8) << 8;
        version |= get_bits(&gb, 8);

        if (version < 0x030100)
        {
            av_log(s, AV_LOG_ERROR,
                "Too old or unsupported Theora (%x)\n", version);
            return -1;
        }

        st->codec->width = get_bits(&gb, 16) << 4;
        st->codec->height = get_bits(&gb, 16) << 4;

        if (version >= 0x030400)
            skip_bits(&gb, 164);
        else if (version >= 0x030200)
            skip_bits(&gb, 64);
        st->codec->time_base.den = get_bits(&gb, 32);
        st->codec->time_base.num = get_bits(&gb, 32);
        st->time_base = st->codec->time_base;

        st->codec->sample_aspect_ratio.num = get_bits(&gb, 24);
        st->codec->sample_aspect_ratio.den = get_bits(&gb, 24);

        if (version >= 0x030200)
            skip_bits(&gb, 38);
        if (version >= 0x304000)
            skip_bits(&gb, 2);

        thp->gpshift = get_bits(&gb, 5);
        thp->gpmask = (1 << thp->gpshift) - 1;

        st->codec->codec_type = CODEC_TYPE_VIDEO;
        st->codec->codec_id = CODEC_ID_THEORA;

    } else if (os->buf[os->pstart] == 0x83) {
        vorbis_comment (s, os->buf + os->pstart + 7, os->psize - 8);
    }

    st->codec->extradata = av_realloc (st->codec->extradata, cds);
    cdp = st->codec->extradata + st->codec->extradata_size;
    *cdp++ = os->psize >> 8;
    *cdp++ = os->psize & 0xff;
    memcpy (cdp, os->buf + os->pstart, os->psize);
    st->codec->extradata_size = cds;

    return 1;
}

static uint64_t
theora_gptopts(AVFormatContext *ctx, int idx, uint64_t gp)
{
    ogg_t *ogg = ctx->priv_data;
    ogg_stream_t *os = ogg->streams + idx;
    theora_params_t *thp = os->private;
    uint64_t iframe = gp >> thp->gpshift;
    uint64_t pframe = gp & thp->gpmask;

    return iframe + pframe;
}

ogg_codec_t theora_codec = {
    .magic = "\200theora",
    .magicsize = 7,
    .header = theora_header,
    .gptopts = theora_gptopts
};
