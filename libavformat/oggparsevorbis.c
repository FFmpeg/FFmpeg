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
#include "libavutil/avstring.h"
#include "libavutil/bswap.h"
#include "libavutil/dict.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "internal.h"
#include "oggdec.h"
#include "vorbiscomment.h"

static int ogm_chapter(AVFormatContext *as, uint8_t *key, uint8_t *val)
{
    int i, cnum, h, m, s, ms, keylen = strlen(key);
    AVChapter *chapter = NULL;

    if (keylen < 9 || sscanf(key, "CHAPTER%02d", &cnum) != 1)
        return 0;

    if (keylen == 9) {
        if (sscanf(val, "%02d:%02d:%02d.%03d", &h, &m, &s, &ms) < 4)
            return 0;

        avpriv_new_chapter(as, cnum, (AVRational){1,1000},
                       ms + 1000*(s + 60*(m + 60*h)),
                       AV_NOPTS_VALUE, NULL);
        av_free(val);
    } else if (!strcmp(key+9, "NAME")) {
        for(i = 0; i < as->nb_chapters; i++)
            if (as->chapters[i]->id == cnum) {
                chapter = as->chapters[i];
                break;
            }
        if (!chapter)
            return 0;

        av_dict_set(&chapter->metadata, "title", val,
                         AV_DICT_DONT_STRDUP_VAL);
    } else
        return 0;

    av_free(key);
    return 1;
}

int
ff_vorbis_comment(AVFormatContext * as, AVDictionary **m, const uint8_t *buf, int size)
{
    const uint8_t *p = buf;
    const uint8_t *end = buf + size;
    unsigned n, j;
    int s;

    if (size < 8) /* must have vendor_length and user_comment_list_length */
        return -1;

    s = bytestream_get_le32(&p);

    if (end - p - 4 < s || s < 0)
        return -1;

    p += s;

    n = bytestream_get_le32(&p);

    while (end - p >= 4 && n > 0) {
        const char *t, *v;
        int tl, vl;

        s = bytestream_get_le32(&p);

        if (end - p < s || s < 0)
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
            char *tt, *ct;

            tt = av_malloc(tl + 1);
            ct = av_malloc(vl + 1);
            if (!tt || !ct) {
                av_freep(&tt);
                av_freep(&ct);
                av_log(as, AV_LOG_WARNING, "out-of-memory error. skipping VorbisComment tag.\n");
                continue;
            }

            for (j = 0; j < tl; j++)
                tt[j] = toupper(t[j]);
            tt[tl] = 0;

            memcpy(ct, v, vl);
            ct[vl] = 0;

            if (!ogm_chapter(as, tt, ct))
                av_dict_set(m, tt, ct,
                                   AV_DICT_DONT_STRDUP_KEY |
                                   AV_DICT_DONT_STRDUP_VAL);
        }
    }

    if (p != end)
        av_log(as, AV_LOG_INFO, "%ti bytes of comment header remain\n", end-p);
    if (n > 0)
        av_log(as, AV_LOG_INFO,
               "truncated comment header, %i comments not found\n", n);

    ff_metadata_conv(m, NULL, ff_vorbiscomment_metadata_conv);

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

struct oggvorbis_private {
    unsigned int len[3];
    unsigned char *packet[3];
};


static unsigned int
fixup_vorbis_headers(AVFormatContext * as, struct oggvorbis_private *priv,
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
        av_freep(&priv->packet[i]);
    }
    *buf = av_realloc(*buf, offset + FF_INPUT_BUFFER_PADDING_SIZE);
    return offset;
}


static int
vorbis_header (AVFormatContext * s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    struct oggvorbis_private *priv;
    int pkt_type = os->buf[os->pstart];

    if (!(pkt_type & 1))
        return 0;

    if (!os->private) {
        os->private = av_mallocz(sizeof(struct oggvorbis_private));
        if (!os->private)
            return 0;
    }

    if (os->psize < 1 || pkt_type > 5)
        return -1;

    priv = os->private;

    if (priv->packet[pkt_type>>1])
        return -1;
    if (pkt_type > 1 && !priv->packet[0] || pkt_type > 3 && !priv->packet[1])
        return -1;

    priv->len[pkt_type >> 1] = os->psize;
    priv->packet[pkt_type >> 1] = av_mallocz(os->psize);
    memcpy(priv->packet[pkt_type >> 1], os->buf + os->pstart, os->psize);
    if (os->buf[os->pstart] == 1) {
        const uint8_t *p = os->buf + os->pstart + 7; /* skip "\001vorbis" tag */
        unsigned blocksize, bs0, bs1;
        int srate;

        if (os->psize != 30)
            return -1;

        if (bytestream_get_le32(&p) != 0) /* vorbis_version */
            return -1;

        st->codec->channels = bytestream_get_byte(&p);
        srate = bytestream_get_le32(&p);
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

        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id = CODEC_ID_VORBIS;

        if (srate > 0) {
            st->codec->sample_rate = srate;
            avpriv_set_pts_info(st, 64, 1, srate);
        }
    } else if (os->buf[os->pstart] == 3) {
        if (os->psize > 8 &&
            ff_vorbis_comment(s, &st->metadata, os->buf + os->pstart + 7, os->psize - 8) >= 0) {
            // drop all metadata we parsed and which is not required by libvorbis
            unsigned new_len = 7 + 4 + AV_RL32(priv->packet[1] + 7) + 4 + 1;
            if (new_len >= 16 && new_len < os->psize) {
                AV_WL32(priv->packet[1] + new_len - 5, 0);
                priv->packet[1][new_len - 1] = 1;
                priv->len[1] = new_len;
            }
        }
    } else {
        st->codec->extradata_size =
            fixup_vorbis_headers(s, priv, &st->codec->extradata);
    }

    return 1;
}

const struct ogg_codec ff_vorbis_codec = {
    .magic = "\001vorbis",
    .magicsize = 7,
    .header = vorbis_header
};
