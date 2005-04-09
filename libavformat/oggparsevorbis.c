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
#include "bswap.h"
#include "ogg2.h"

extern int
vorbis_comment (AVFormatContext * as, char *buf, int size)
{
    char *p = buf;
    int s, n, j;

    if (size < 4)
        return -1;

    s = le2me_32 (unaligned32 (p));
    p += 4;
    size -= 4;

    if (size < s + 4)
        return -1;

    p += s;
    size -= s;

    n = le2me_32 (unaligned32 (p));
    p += 4;
    size -= 4;

    while (size >= 4){
        char *t, *v;
        int tl, vl;

        s = le2me_32 (unaligned32 (p));
        p += 4;
        size -= 4;

        if (size < s)
            break;

        t = p;
        p += s;
        size -= s;
        n--;

        v = memchr (t, '=', s);
        if (!v)
            continue;

        tl = v - t;
        vl = s - tl - 1;
        v++;

        if (tl && vl){
            char tt[tl + 1];
            char ct[vl + 1];

            for (j = 0; j < tl; j++)
                tt[j] = toupper (t[j]);
            tt[tl] = 0;

            memcpy (ct, v, vl);
            ct[vl] = 0;

            // took from Vorbis_I_spec 
            if (!strcmp (tt, "AUTHOR"))
                strncpy (as->author, ct, FFMIN(sizeof (as->author), vl));
            else if (!strcmp (tt, "TITLE"))
                strncpy (as->title, ct, FFMIN(sizeof (as->title), vl));
            else if (!strcmp (tt, "COPYRIGHT"))
                strncpy (as->copyright, ct, FFMIN(sizeof (as->copyright), vl));
            else if (!strcmp (tt, "DESCRIPTION"))
                strncpy (as->comment, ct, FFMIN(sizeof (as->comment), vl));
            else if (!strcmp (tt, "GENRE"))
                strncpy (as->genre, ct, FFMIN(sizeof (as->genre), vl));
            else if (!strcmp (tt, "TRACKNUMBER"))
                as->track = atoi (ct);
            //Too bored to add others for today
        }
    }

    if (size > 0)
        av_log (as, AV_LOG_INFO, "%i bytes of comment header remain\n", size);
    if (n > 0)
        av_log (as, AV_LOG_INFO,
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

static int
vorbis_header (AVFormatContext * s, int idx)
{
    ogg_t *ogg = s->priv_data;
    ogg_stream_t *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    int cds = st->codec.extradata_size + os->psize + 2;
    u_char *cdp;

    if (os->seq > 2)
        return 0;

    st->codec.extradata = av_realloc (st->codec.extradata, cds);
    cdp = st->codec.extradata + st->codec.extradata_size;
    *cdp++ = os->psize >> 8;
    *cdp++ = os->psize & 0xff;
    memcpy (cdp, os->buf + os->pstart, os->psize);
    st->codec.extradata_size = cds;

    if (os->buf[os->pstart] == 1) {
        u_char *p = os->buf + os->pstart + 11; //skip up to the audio channels
        st->codec.channels = *p++;
        st->codec.sample_rate = le2me_32 (unaligned32 (p));
        p += 8; //skip maximum and and nominal bitrate
        st->codec.bit_rate = le2me_32 (unaligned32 (p)); //Minimum bitrate

        st->codec.codec_type = CODEC_TYPE_AUDIO;
        st->codec.codec_id = CODEC_ID_VORBIS;

    } else if (os->buf[os->pstart] == 3) {
        vorbis_comment (s, os->buf + os->pstart + 7, os->psize - 8);
    }

    return os->seq < 3;
}

ogg_codec_t vorbis_codec = {
    .magic = "\001vorbis",
    .magicsize = 7,
    .header = vorbis_header
};
