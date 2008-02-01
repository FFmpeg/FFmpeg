/**
      Copyright (C) 2005  Michael Ahlberg, Måns Rullgård

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

extern int
vorbis_comment(AVFormatContext * as, uint8_t *buf, int size)
{
    const uint8_t *p = buf;
    const uint8_t *end = buf + size;
    unsigned s, n, j;

    if (size < 8) /* must have vendor_length and user_comment_list_length */
        return -1;

    s = bytestream_get_le32(&p);

    if (end - p < s)
        return -1;

    p += s;

    n = bytestream_get_le32(&p);

    while (p < end && n > 0) {
        const char *t, *v;
        int tl, vl;

        s = bytestream_get_le32(&p);

        if (end - p < s)
            break;

        t = p;
        p += s;
        n--;

        v = memchr(t, '=', s);
        if (!v)
            continue;

        tl = v - t;
        vl = s - tl - 1;
        v++;

        if (tl && vl) {
            char tt[tl + 1];
            char ct[vl + 1];

            for (j = 0; j < tl; j++)
                tt[j] = toupper(t[j]);
            tt[tl] = 0;

            memcpy(ct, v, vl);
            ct[vl] = 0;

            // took from Vorbis_I_spec
            if (!strcmp(tt, "AUTHOR") || !strcmp(tt, "ARTIST"))
                av_strlcpy(as->author, ct, sizeof(as->author));
            else if (!strcmp(tt, "TITLE"))
                av_strlcpy(as->title, ct, sizeof(as->title));
            else if (!strcmp(tt, "COPYRIGHT"))
                av_strlcpy(as->copyright, ct, sizeof(as->copyright));
            else if (!strcmp(tt, "DESCRIPTION"))
                av_strlcpy(as->comment, ct, sizeof(as->comment));
            else if (!strcmp(tt, "GENRE"))
                av_strlcpy(as->genre, ct, sizeof(as->genre));
            else if (!strcmp(tt, "TRACKNUMBER"))
                as->track = atoi(ct);
            else if (!strcmp(tt, "ALBUM"))
                av_strlcpy(as->album, ct, sizeof(as->album));
        }
    }

    if (p != end)
        av_log(as, AV_LOG_INFO, "%ti bytes of comment header remain\n", p-end);
    if (n > 0)
        av_log(as, AV_LOG_INFO,
               "truncated comment header, %i comments not found\n", n);

    return 0;
}


/** Parse the vorbis header
 * Vorbis Identification header from Vorbis_I_spec.html#vorbis-spec-codec
 * [vorbis_version] = read 32 bits as unsigned integer | Not used
 * [audio_channels] = read 8 bit integer as unsigned | Used
 * [audio_sample_rate] = read 32 bits as unsigned integer | Used
 * [bitrate_maximum] = read 32 bits as signed integer | Not used yet
 * [bitrate_nominal] = read 32 bits as signed integer | Not used yet
 * [bitrate_minimum] = read 32 bits as signed integer | Used as bitrate
 * [blocksize_0] = read 4 bits as unsigned integer | Not Used
 * [blocksize_1] = read 4 bits as unsigned integer | Not Used
 * [framing_flag] = read one bit | Not Used
 *    */

typedef struct {
    unsigned int len[3];
    unsigned char *packet[3];
} oggvorbis_private_t;


static unsigned int
fixup_vorbis_headers(AVFormatContext * as, oggvorbis_private_t *priv,
                     uint8_t **buf)
{
    int i,offset, len;
    unsigned char *ptr;

    len = priv->len[0] + priv->len[1] + priv->len[2];
    ptr = *buf = av_mallocz(len + len/255 + 64);

    ptr[0] = 2;
    offset = 1;
    offset += av_xiphlacing(&ptr[offset], priv->len[0]);
    offset += av_xiphlacing(&ptr[offset], priv->len[1]);
    for (i = 0; i < 3; i++) {
        memcpy(&ptr[offset], priv->packet[i], priv->len[i]);
        offset += priv->len[i];
    }
    *buf = av_realloc(*buf, offset);
    return offset;
}


static int
vorbis_header (AVFormatContext * s, int idx)
{
    ogg_t *ogg = s->priv_data;
    ogg_stream_t *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    oggvorbis_private_t *priv;

    if (os->seq > 2)
        return 0;

    if (os->seq == 0) {
        os->private = av_mallocz(sizeof(oggvorbis_private_t));
        if (!os->private)
            return 0;
    }

    if (os->psize < 1)
        return -1;

    priv = os->private;
    priv->len[os->seq] = os->psize;
    priv->packet[os->seq] = av_mallocz(os->psize);
    memcpy(priv->packet[os->seq], os->buf + os->pstart, os->psize);
    if (os->buf[os->pstart] == 1) {
        const uint8_t *p = os->buf + os->pstart + 7; /* skip "\001vorbis" tag */
        unsigned blocksize, bs0, bs1;

        if (os->psize != 30)
            return -1;

        if (bytestream_get_le32(&p) != 0) /* vorbis_version */
            return -1;

        st->codec->channels = bytestream_get_byte(&p);
        st->codec->sample_rate = bytestream_get_le32(&p);
        p += 4; // skip maximum bitrate
        st->codec->bit_rate = bytestream_get_le32(&p); // nominal bitrate
        p += 4; // skip minimum bitrate

        blocksize = bytestream_get_byte(&p);
        bs0 = blocksize & 15;
        bs1 = blocksize >> 4;

        if (bs0 > bs1)
            return -1;
        if (bs0 < 6 || bs1 > 13)
            return -1;

        if (bytestream_get_byte(&p) != 1) /* framing_flag */
            return -1;

        st->codec->codec_type = CODEC_TYPE_AUDIO;
        st->codec->codec_id = CODEC_ID_VORBIS;

        st->time_base.num = 1;
        st->time_base.den = st->codec->sample_rate;
    } else if (os->buf[os->pstart] == 3) {
        if (os->psize > 8)
            vorbis_comment (s, os->buf + os->pstart + 7, os->psize - 8);
    } else {
        st->codec->extradata_size =
            fixup_vorbis_headers(s, priv, &st->codec->extradata);
    }

    return os->seq < 3;
}

ogg_codec_t vorbis_codec = {
    .magic = "\001vorbis",
    .magicsize = 7,
    .header = vorbis_header
};
