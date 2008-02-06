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

#include <stdlib.h>
#include "avformat.h"
#include "bitstream.h"
#include "bytestream.h"
#include "bswap.h"
#include "oggdec.h"
#include "avstring.h"

static int speex_header(AVFormatContext *s, int idx) {
    ogg_t *ogg = s->priv_data;
    ogg_stream_t *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    uint8_t *p = os->buf + os->pstart;

    if (os->psize < 80)
        return 1;

    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_SPEEX;

    st->codec->sample_rate = AV_RL32(p + 36);
    st->codec->channels = AV_RL32(p + 48);
    st->codec->extradata_size = os->psize;
    st->codec->extradata = av_malloc(st->codec->extradata_size);
    memcpy(st->codec->extradata, p, st->codec->extradata_size);

    st->time_base.num = 1;
    st->time_base.den = st->codec->sample_rate;

    return 0;
}

ogg_codec_t speex_codec = {
    .magic = "Speex   ",
    .magicsize = 8,
    .header = speex_header
};
