/**
 *    Copyright (C) 2005  Matthieu CASTET, Alex Beregszaszi
 *
 *    Permission is hereby granted, free of charge, to any person
 *    obtaining a copy of this software and associated documentation
 *    files (the "Software"), to deal in the Software without
 *    restriction, including without limitation the rights to use, copy,
 *    modify, merge, publish, distribute, sublicense, and/or sell copies
 *    of the Software, and to permit persons to whom the Software is
 *    furnished to do so, subject to the following conditions:
 *
 *    The above copyright notice and this permission notice shall be
 *    included in all copies or substantial portions of the Software.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *    DEALINGS IN THE SOFTWARE.
 **/

#include <stdlib.h>
#include "libavutil/bswap.h"
#include "libavcodec/get_bits.h"
#include "avformat.h"
#include "internal.h"
#include "oggdec.h"

typedef struct TheoraParams {
    int gpshift;
    int gpmask;
    unsigned version;
} TheoraParams;

static int theora_header(AVFormatContext *s, int idx)
{
    struct ogg *ogg       = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st          = s->streams[idx];
    TheoraParams *thp     = os->private;
    int cds               = st->codecpar->extradata_size + os->psize + 2;
    int err;
    uint8_t *cdp;

    if (!(os->buf[os->pstart] & 0x80))
        return 0;

    if (!thp) {
        thp = av_mallocz(sizeof(*thp));
        if (!thp)
            return AVERROR(ENOMEM);
        os->private = thp;
    }

    switch (os->buf[os->pstart]) {
    case 0x80: {
        GetBitContext gb;
        AVRational timebase;

        init_get_bits(&gb, os->buf + os->pstart, os->psize * 8);

        /* 0x80"theora" */
        skip_bits_long(&gb, 7 * 8);

        thp->version = get_bits_long(&gb, 24);
        if (thp->version < 0x030100) {
            av_log(s, AV_LOG_ERROR,
                   "Too old or unsupported Theora (%x)\n", thp->version);
            return AVERROR(ENOSYS);
        }

        st->codecpar->width  = get_bits(&gb, 16) << 4;
        st->codecpar->height = get_bits(&gb, 16) << 4;

        if (thp->version >= 0x030400)
            skip_bits(&gb, 100);

        if (thp->version >= 0x030200) {
            int width  = get_bits_long(&gb, 24);
            int height = get_bits_long(&gb, 24);
            if (width  <= st->codecpar->width  && width  > st->codecpar->width  - 16 &&
                height <= st->codecpar->height && height > st->codecpar->height - 16) {
                st->codecpar->width  = width;
                st->codecpar->height = height;
            }

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
        thp->gpmask  = (1 << thp->gpshift) - 1;

        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id   = AV_CODEC_ID_THEORA;
        st->need_parsing      = AVSTREAM_PARSE_HEADERS;
    }
    break;
    case 0x81:
        ff_vorbis_stream_comment(s, st, os->buf + os->pstart + 7, os->psize - 7);
    case 0x82:
        if (!thp->version)
            return AVERROR_INVALIDDATA;
        break;
    default:
        return AVERROR_INVALIDDATA;
    }

    if ((err = av_reallocp(&st->codecpar->extradata,
                           cds + AV_INPUT_BUFFER_PADDING_SIZE)) < 0) {
        st->codecpar->extradata_size = 0;
        return err;
    }
    cdp    = st->codecpar->extradata + st->codecpar->extradata_size;
    *cdp++ = os->psize >> 8;
    *cdp++ = os->psize & 0xff;
    memcpy(cdp, os->buf + os->pstart, os->psize);
    st->codecpar->extradata_size = cds;

    return 1;
}

static uint64_t theora_gptopts(AVFormatContext *ctx, int idx, uint64_t gp,
                               int64_t *dts)
{
    struct ogg *ogg       = ctx->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    TheoraParams *thp     = os->private;
    uint64_t iframe, pframe;

    if (!thp)
        return AV_NOPTS_VALUE;

    iframe = gp >> thp->gpshift;
    pframe = gp & thp->gpmask;

    if (thp->version < 0x030201)
        iframe++;

    if (!pframe)
        os->pflags |= AV_PKT_FLAG_KEY;

    if (dts)
        *dts = iframe + pframe;

    return iframe + pframe;
}

const struct ogg_codec ff_theora_codec = {
    .magic     = "\200theora",
    .magicsize = 7,
    .header    = theora_header,
    .gptopts   = theora_gptopts,
    .nb_header = 3,
};
