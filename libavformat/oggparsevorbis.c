/*
 * Copyright (C) 2005  Michael Ahlberg, Måns Rullgård
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>

#include "libavutil/avstring.h"
#include "libavutil/base64.h"
#include "libavutil/bswap.h"
#include "libavutil/dict.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/vorbis_parser.h"
#include "avformat.h"
#include "flac_picture.h"
#include "internal.h"
#include "oggdec.h"
#include "vorbiscomment.h"
#include "replaygain.h"

static int ogm_chapter(AVFormatContext *as, uint8_t *key, uint8_t *val)
{
    int i, cnum, h, m, s, ms, keylen = strlen(key);
    AVChapter *chapter = NULL;

    if (keylen < 9 || sscanf(key, "CHAPTER%03d", &cnum) != 1)
        return 0;

    if (keylen <= 10) {
        if (sscanf(val, "%02d:%02d:%02d.%03d", &h, &m, &s, &ms) < 4)
            return 0;

        avpriv_new_chapter(as, cnum, (AVRational) { 1, 1000 },
                           ms + 1000 * (s + 60 * (m + 60 * h)),
                           AV_NOPTS_VALUE, NULL);
        av_free(val);
    } else if (!strcmp(key + keylen - 4, "NAME")) {
        for (i = 0; i < as->nb_chapters; i++)
            if (as->chapters[i]->id == cnum) {
                chapter = as->chapters[i];
                break;
            }
        if (!chapter)
            return 0;

        av_dict_set(&chapter->metadata, "title", val, AV_DICT_DONT_STRDUP_VAL);
    } else
        return 0;

    av_free(key);
    return 1;
}

int ff_vorbis_stream_comment(AVFormatContext *as, AVStream *st,
                             const uint8_t *buf, int size)
{
    int updates = ff_vorbis_comment(as, &st->metadata, buf, size, 1);

    if (updates > 0) {
        st->event_flags |= AVSTREAM_EVENT_FLAG_METADATA_UPDATED;
    }

    return updates;
}

int ff_vorbis_comment(AVFormatContext *as, AVDictionary **m,
                      const uint8_t *buf, int size,
                      int parse_picture)
{
    const uint8_t *p   = buf;
    const uint8_t *end = buf + size;
    int updates        = 0;
    unsigned n, j;
    int s;

    /* must have vendor_length and user_comment_list_length */
    if (size < 8)
        return AVERROR_INVALIDDATA;

    s = bytestream_get_le32(&p);

    if (end - p - 4 < s || s < 0)
        return AVERROR_INVALIDDATA;

    p += s;

    n = bytestream_get_le32(&p);

    while (end - p >= 4 && n > 0) {
        const char *t, *v;
        int tl, vl;

        s = bytestream_get_le32(&p);

        if (end - p < s || s < 0)
            break;

        t  = p;
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
                return AVERROR(ENOMEM);
            }

            for (j = 0; j < tl; j++)
                tt[j] = av_toupper(t[j]);
            tt[tl] = 0;

            memcpy(ct, v, vl);
            ct[vl] = 0;

            /* The format in which the pictures are stored is the FLAC format.
             * Xiph says: "The binary FLAC picture structure is base64 encoded
             * and placed within a VorbisComment with the tag name
             * 'METADATA_BLOCK_PICTURE'. This is the preferred and
             * recommended way of embedding cover art within VorbisComments."
             */
            if (!strcmp(tt, "METADATA_BLOCK_PICTURE") && parse_picture) {
                int ret;
                char *pict = av_malloc(vl);

                if (!pict) {
                    av_log(as, AV_LOG_WARNING, "out-of-memory error. Skipping cover art block.\n");
                    av_freep(&tt);
                    av_freep(&ct);
                    continue;
                }
                if ((ret = av_base64_decode(pict, ct, vl)) > 0)
                    ret = ff_flac_parse_picture(as, pict, ret);
                av_freep(&tt);
                av_freep(&ct);
                av_freep(&pict);
                if (ret < 0) {
                    av_log(as, AV_LOG_WARNING, "Failed to parse cover art block.\n");
                    continue;
                }
            } else if (!ogm_chapter(as, tt, ct)) {
                updates++;
                if (av_dict_get(*m, tt, NULL, 0)) {
                    av_dict_set(m, tt, ";", AV_DICT_APPEND);
                }
                av_dict_set(m, tt, ct,
                            AV_DICT_DONT_STRDUP_KEY |
                            AV_DICT_APPEND);
                av_freep(&ct);
            }
        }
    }

    if (p != end)
        av_log(as, AV_LOG_INFO,
               "%"PTRDIFF_SPECIFIER" bytes of comment header remain\n", end - p);
    if (n > 0)
        av_log(as, AV_LOG_INFO,
               "truncated comment header, %i comments not found\n", n);

    ff_metadata_conv(m, NULL, ff_vorbiscomment_metadata_conv);

    return updates;
}

/*
 * Parse the vorbis header
 *
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
 */

struct oggvorbis_private {
    unsigned int len[3];
    unsigned char *packet[3];
    AVVorbisParseContext *vp;
    int64_t final_pts;
    int final_duration;
};

static int fixup_vorbis_headers(AVFormatContext *as,
                                struct oggvorbis_private *priv,
                                uint8_t **buf)
{
    int i, offset, len, err;
    int buf_len;
    unsigned char *ptr;

    len = priv->len[0] + priv->len[1] + priv->len[2];
    buf_len = len + len / 255 + 64;
    ptr = *buf = av_realloc(NULL, buf_len);
    if (!ptr)
        return AVERROR(ENOMEM);
    memset(*buf, '\0', buf_len);

    ptr[0]  = 2;
    offset  = 1;
    offset += av_xiphlacing(&ptr[offset], priv->len[0]);
    offset += av_xiphlacing(&ptr[offset], priv->len[1]);
    for (i = 0; i < 3; i++) {
        memcpy(&ptr[offset], priv->packet[i], priv->len[i]);
        offset += priv->len[i];
        av_freep(&priv->packet[i]);
    }
    if ((err = av_reallocp(buf, offset + AV_INPUT_BUFFER_PADDING_SIZE)) < 0)
        return err;
    return offset;
}

static void vorbis_cleanup(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    struct oggvorbis_private *priv = os->private;
    int i;
    if (os->private) {
        av_vorbis_parse_free(&priv->vp);
        for (i = 0; i < 3; i++)
            av_freep(&priv->packet[i]);
    }
}

static int vorbis_update_metadata(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    int ret;

    if (os->psize <= 8)
        return 0;

    /* New metadata packet; release old data. */
    av_dict_free(&st->metadata);
    ret = ff_vorbis_stream_comment(s, st, os->buf + os->pstart + 7,
                                   os->psize - 8);
    if (ret < 0)
        return ret;

    /* Update the metadata if possible. */
    av_freep(&os->new_metadata);
    if (st->metadata) {
        os->new_metadata = av_packet_pack_dictionary(st->metadata, &os->new_metadata_size);
    /* Send an empty dictionary to indicate that metadata has been cleared. */
    } else {
        os->new_metadata = av_malloc(1);
        os->new_metadata_size = 0;
    }

    return ret;
}

static int vorbis_header(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    AVStream *st    = s->streams[idx];
    struct ogg_stream *os = ogg->streams + idx;
    struct oggvorbis_private *priv;
    int pkt_type = os->buf[os->pstart];

    if (!os->private) {
        os->private = av_mallocz(sizeof(struct oggvorbis_private));
        if (!os->private)
            return AVERROR(ENOMEM);
    }

    priv = os->private;

    if (!(pkt_type & 1))
        return priv->vp ? 0 : AVERROR_INVALIDDATA;

    if (os->psize < 1 || pkt_type > 5)
        return AVERROR_INVALIDDATA;

    if (priv->packet[pkt_type >> 1])
        return AVERROR_INVALIDDATA;
    if (pkt_type > 1 && !priv->packet[0] || pkt_type > 3 && !priv->packet[1])
        return AVERROR_INVALIDDATA;

    priv->len[pkt_type >> 1]    = os->psize;
    priv->packet[pkt_type >> 1] = av_mallocz(os->psize);
    if (!priv->packet[pkt_type >> 1])
        return AVERROR(ENOMEM);
    memcpy(priv->packet[pkt_type >> 1], os->buf + os->pstart, os->psize);
    if (os->buf[os->pstart] == 1) {
        const uint8_t *p = os->buf + os->pstart + 7; /* skip "\001vorbis" tag */
        unsigned blocksize, bs0, bs1;
        int srate;
        int channels;

        if (os->psize != 30)
            return AVERROR_INVALIDDATA;

        if (bytestream_get_le32(&p) != 0) /* vorbis_version */
            return AVERROR_INVALIDDATA;

        channels = bytestream_get_byte(&p);
        if (st->codec->channels && channels != st->codec->channels) {
            av_log(s, AV_LOG_ERROR, "Channel change is not supported\n");
            return AVERROR_PATCHWELCOME;
        }
        st->codec->channels = channels;
        srate               = bytestream_get_le32(&p);
        p += 4; // skip maximum bitrate
        st->codec->bit_rate = bytestream_get_le32(&p); // nominal bitrate
        p += 4; // skip minimum bitrate

        blocksize = bytestream_get_byte(&p);
        bs0       = blocksize & 15;
        bs1       = blocksize >> 4;

        if (bs0 > bs1)
            return AVERROR_INVALIDDATA;
        if (bs0 < 6 || bs1 > 13)
            return AVERROR_INVALIDDATA;

        if (bytestream_get_byte(&p) != 1) /* framing_flag */
            return AVERROR_INVALIDDATA;

        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id   = AV_CODEC_ID_VORBIS;

        if (srate > 0) {
            st->codec->sample_rate = srate;
            avpriv_set_pts_info(st, 64, 1, srate);
        }
    } else if (os->buf[os->pstart] == 3) {
        if (vorbis_update_metadata(s, idx) >= 0 && priv->len[1] > 10) {
            unsigned new_len;

            int ret = ff_replaygain_export(st, st->metadata);
            if (ret < 0)
                return ret;

            // drop all metadata we parsed and which is not required by libvorbis
            new_len = 7 + 4 + AV_RL32(priv->packet[1] + 7) + 4 + 1;
            if (new_len >= 16 && new_len < os->psize) {
                AV_WL32(priv->packet[1] + new_len - 5, 0);
                priv->packet[1][new_len - 1] = 1;
                priv->len[1]                 = new_len;
            }
        }
    } else {
        int ret = fixup_vorbis_headers(s, priv, &st->codec->extradata);
        if (ret < 0) {
            st->codec->extradata_size = 0;
            return ret;
        }
        st->codec->extradata_size = ret;

        priv->vp = av_vorbis_parse_init(st->codec->extradata, st->codec->extradata_size);
        if (!priv->vp) {
            av_freep(&st->codec->extradata);
            st->codec->extradata_size = 0;
            return AVERROR_UNKNOWN;
        }
    }

    return 1;
}

static int vorbis_packet(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    struct oggvorbis_private *priv = os->private;
    int duration, flags = 0;

    if (!priv->vp)
        return AVERROR_INVALIDDATA;

    /* first packet handling
     * here we parse the duration of each packet in the first page and compare
     * the total duration to the page granule to find the encoder delay and
     * set the first timestamp */
    if ((!os->lastpts || os->lastpts == AV_NOPTS_VALUE) && !(os->flags & OGG_FLAG_EOS) && (int64_t)os->granule>=0) {
        int seg, d;
        uint8_t *last_pkt  = os->buf + os->pstart;
        uint8_t *next_pkt  = last_pkt;

        av_vorbis_parse_reset(priv->vp);
        duration = 0;
        seg = os->segp;
        d = av_vorbis_parse_frame_flags(priv->vp, last_pkt, 1, &flags);
        if (d < 0) {
            os->pflags |= AV_PKT_FLAG_CORRUPT;
            return 0;
        } else if (flags & VORBIS_FLAG_COMMENT) {
            vorbis_update_metadata(s, idx);
            flags = 0;
        }
        duration += d;
        last_pkt = next_pkt =  next_pkt + os->psize;
        for (; seg < os->nsegs; seg++) {
            if (os->segments[seg] < 255) {
                int d = av_vorbis_parse_frame_flags(priv->vp, last_pkt, 1, &flags);
                if (d < 0) {
                    duration = os->granule;
                    break;
                } else if (flags & VORBIS_FLAG_COMMENT) {
                    vorbis_update_metadata(s, idx);
                    flags = 0;
                }
                duration += d;
                last_pkt  = next_pkt + os->segments[seg];
            }
            next_pkt += os->segments[seg];
        }
        os->lastpts                 =
        os->lastdts                 = os->granule - duration;

        if (!os->granule && duration) //hack to deal with broken files (Ticket3710)
            os->lastpts = os->lastdts = AV_NOPTS_VALUE;

        if (s->streams[idx]->start_time == AV_NOPTS_VALUE) {
            s->streams[idx]->start_time = FFMAX(os->lastpts, 0);
            if (s->streams[idx]->duration != AV_NOPTS_VALUE)
                s->streams[idx]->duration -= s->streams[idx]->start_time;
        }
        priv->final_pts          = AV_NOPTS_VALUE;
        av_vorbis_parse_reset(priv->vp);
    }

    /* parse packet duration */
    if (os->psize > 0) {
        duration = av_vorbis_parse_frame_flags(priv->vp, os->buf + os->pstart, 1, &flags);
        if (duration < 0) {
            os->pflags |= AV_PKT_FLAG_CORRUPT;
            return 0;
        } else if (flags & VORBIS_FLAG_COMMENT) {
            vorbis_update_metadata(s, idx);
            flags = 0;
        }
        os->pduration = duration;
    }

    /* final packet handling
     * here we save the pts of the first packet in the final page, sum up all
     * packet durations in the final page except for the last one, and compare
     * to the page granule to find the duration of the final packet */
    if (os->flags & OGG_FLAG_EOS) {
        if (os->lastpts != AV_NOPTS_VALUE) {
            priv->final_pts      = os->lastpts;
            priv->final_duration = 0;
        }
        if (os->segp == os->nsegs)
            os->pduration = os->granule - priv->final_pts - priv->final_duration;
        priv->final_duration += os->pduration;
    }

    return 0;
}

const struct ogg_codec ff_vorbis_codec = {
    .magic     = "\001vorbis",
    .magicsize = 7,
    .header    = vorbis_header,
    .packet    = vorbis_packet,
    .cleanup   = vorbis_cleanup,
    .nb_header = 3,
};
