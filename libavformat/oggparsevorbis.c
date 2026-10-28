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
#include "libavutil/dict.h"
#include "libavutil/mem.h"

#include "libavcodec/bytestream.h"
#include "libavcodec/vorbis_parser.h"

#include "avformat.h"
#include "demux.h"
#include "flac_picture.h"
#include "internal.h"
#include "oggdec.h"
#include "vorbiscomment.h"
#include "replaygain.h"

static int ogm_chapter(AVFormatContext *as, const uint8_t *key, const uint8_t *val)
{
    int i, cnum, h, m, s, ms, keylen = strlen(key);
    AVChapter *chapter = NULL;

    if (keylen < 9 || av_strncasecmp(key, "CHAPTER", 7) || sscanf(key+7, "%03d", &cnum) != 1)
        return 0;

    if (keylen <= 10) {
        if (sscanf(val, "%02d:%02d:%02d.%03d", &h, &m, &s, &ms) < 4)
            return 0;

        avpriv_new_chapter(as, cnum, (AVRational) { 1, 1000 },
                           ms + 1000 * (s + 60 * (m + 60 * h)),
                           AV_NOPTS_VALUE, NULL);
    } else if (!av_strcasecmp(key + keylen - 4, "NAME")) {
        for (i = 0; i < as->nb_chapters; i++)
            if (as->chapters[i]->id == cnum) {
                chapter = as->chapters[i];
                break;
            }
        if (!chapter)
            return 0;

        av_dict_set(&chapter->metadata, "title", val, 0);
    } else
        return 0;

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

/**
 * This function temporarily modifies the (const qualified) input buffer
 * and reverts its changes before return. The input buffer needs to have
 * at least one byte of padding.
 */
static int vorbis_parse_single_comment(AVFormatContext *as, AVDictionary **m,
                                       const uint8_t *buf, uint32_t size,
                                       int *updates, int parse_picture)
{
    char *t = (char*)buf, *v = memchr(t, '=', size);
    int tl, vl;
    char backup;

    if (!v)
        return 0;

    tl = v - t;
    vl = size - tl - 1;
    v++;

    if (!tl || !vl)
        return 0;

    t[tl]  = 0;

    backup = v[vl];
    v[vl]  = 0;

    /* The format in which the pictures are stored is the FLAC format.
     * Xiph says: "The binary FLAC picture structure is base64 encoded
     * and placed within a VorbisComment with the tag name
     * 'METADATA_BLOCK_PICTURE'. This is the preferred and
     * recommended way of embedding cover art within VorbisComments."
     */
    if (!av_strcasecmp(t, "METADATA_BLOCK_PICTURE") && parse_picture) {
        int ret, len = AV_BASE64_DECODE_SIZE(vl);
        uint8_t *pict = av_malloc(len + AV_INPUT_BUFFER_PADDING_SIZE);

        if (!pict) {
            av_log(as, AV_LOG_WARNING, "out-of-memory error. Skipping cover art block.\n");
            goto end;
        }
        ret = av_base64_decode(pict, v, len);
        if (ret > 0)
            ret = ff_flac_parse_picture(as, &pict, ret, 0);
        av_freep(&pict);
        if (ret < 0) {
            av_log(as, AV_LOG_WARNING, "Failed to parse cover art block.\n");
            goto end;
        }
    } else if (!ogm_chapter(as, t, v)) {
        (*updates)++;
        if (av_dict_get(*m, t, NULL, 0))
            av_dict_set(m, t, ";", AV_DICT_APPEND);
        av_dict_set(m, t, v, AV_DICT_APPEND);
    }
end:
    t[tl] = '=';
    v[vl] = backup;

    return 0;
}

int ff_vorbis_comment(AVFormatContext *as, AVDictionary **m,
                      const uint8_t *buf, int size,
                      int parse_picture)
{
    const uint8_t *p   = buf;
    const uint8_t *end = buf + size;
    int updates        = 0;
    unsigned n;
    int s, ret;

    /* must have vendor_length and user_comment_list_length */
    if (size < 8)
        return AVERROR_INVALIDDATA;

    s = bytestream_get_le32(&p);

    if (end - p - 4 < s || s < 0)
        return AVERROR_INVALIDDATA;

    p += s;

    n = bytestream_get_le32(&p);

    while (end - p >= 4 && n > 0) {
        s = bytestream_get_le32(&p);

        if (end - p < s || s < 0)
            break;

        ret = vorbis_parse_single_comment(as, m, p, s, &updates, parse_picture);
        if (ret < 0)
            return ret;
        p += s;
        n--;
    }

    if (p != end)
        av_log(as, AV_LOG_INFO,
               "%td bytes of comment header remain\n", end - p);
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

enum VorbisPacketType {
    VORBIS_PACKET_DATA,
    VORBIS_PACKET_ID,
    VORBIS_PACKET_COMMENT,
    VORBIS_PACKET_SETUP,
    VORBIS_PACKET_RESERVED,
};

struct oggvorbis_private {
    unsigned int len[3];
    unsigned char *packet[3];
    enum VorbisPacketType last_packet; ///< type of the last packet seen by vorbis_packet()
    AVVorbisParseContext *vp;
    int64_t bos_page_pos;
    int ts_offset;
    int64_t last_page_granule;
    int eos_page_duration;
};

static int fixup_vorbis_headers(AVFormatContext *as,
                                struct oggvorbis_private *priv,
                                uint8_t **buf)
{
    int i, offset, len, err;
    int buf_len;
    unsigned char *ptr;
    uint64_t total_len;

    total_len = (uint64_t)priv->len[0] + priv->len[1] + priv->len[2];
    if (total_len + total_len / 255 + 64 > INT_MAX)
        return AVERROR_INVALIDDATA;

    len = total_len;
    buf_len = len + len / 255 + 64;

    if (*buf)
        return AVERROR_INVALIDDATA;

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

int ff_vorbis_update_metadata(AVFormatContext *s, AVStream *st,
                              const uint8_t *buf, int size)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + st->index;
    int ret;

    /* New metadata packet; release old data. */
    av_dict_free(&st->metadata);
    ret = ff_vorbis_stream_comment(s, st, buf, size);
    if (ret < 0)
        return ret;

    /* Update the metadata if possible. */
    av_freep(&os->new_metadata);
    if (st->metadata) {
        os->new_metadata = av_packet_pack_dictionary(st->metadata, &os->new_metadata_size);
    /* Send an empty dictionary to indicate that metadata has been cleared. */
    } else {
        os->new_metadata = av_mallocz(1);
        os->new_metadata_size = 0;
    }

    return ret;
}

static int vorbis_parse_header(AVFormatContext *s, AVStream *st,
                               const uint8_t *p, unsigned int psize)
{
    unsigned blocksize, bs0, bs1;
    int srate;
    int channels;

    if (psize != 30)
        return AVERROR_INVALIDDATA;

    p += 7; /* skip "\001vorbis" tag */

    if (bytestream_get_le32(&p) != 0) /* vorbis_version */
        return AVERROR_INVALIDDATA;

    channels = bytestream_get_byte(&p);
    if (st->codecpar->ch_layout.nb_channels &&
        channels != st->codecpar->ch_layout.nb_channels) {
        av_log(s, AV_LOG_ERROR, "Channel change is not supported\n");
        return AVERROR_PATCHWELCOME;
    }
    st->codecpar->ch_layout.nb_channels = channels;
    srate               = bytestream_get_le32(&p);
    p += 4; // skip maximum bitrate
    st->codecpar->bit_rate = bytestream_get_le32(&p); // nominal bitrate
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

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id   = AV_CODEC_ID_VORBIS;

    if (srate > 0) {
        if (st->codecpar->sample_rate &&
            srate != st->codecpar->sample_rate) {
            av_log(s, AV_LOG_ERROR, "Sample rate change is not supported\n");
            return AVERROR_PATCHWELCOME;
        }

        st->codecpar->sample_rate = srate;
        avpriv_set_pts_info(st, 64, 1, srate);
    }

    return 1;
}

static int vorbis_update_metadata(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];

    if (os->psize <= 8)
        return 0;

    return ff_vorbis_update_metadata(s, st, os->buf + os->pstart + 7,
                                     os->psize - 8);
}

static int vorbis_header(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    AVStream *st    = s->streams[idx];
    struct ogg_stream *os = ogg->streams + idx;
    struct oggvorbis_private *priv;
    int pkt_type = os->buf[os->pstart];

    if (!os->private)
        os->private = av_mallocz(sizeof(struct oggvorbis_private));

    if (!os->private)
        return AVERROR(ENOMEM);

    priv = os->private;
    priv->bos_page_pos = -1;

    if (!(pkt_type & 1))
        return priv->vp ? 0 : AVERROR_INVALIDDATA;

    if (pkt_type > 5) {
        av_log(s, AV_LOG_VERBOSE, "Ignoring packet with unknown type %d\n", pkt_type);
        return 1;
    }

    if (os->psize < 1)
        return AVERROR_INVALIDDATA;

    if (priv->packet[pkt_type >> 1])
        return AVERROR_INVALIDDATA;
    if (pkt_type > 1 && !priv->packet[0] || pkt_type > 3 && !priv->packet[1])
        return priv->vp ? 0 : AVERROR_INVALIDDATA;

    priv->len[pkt_type >> 1]    = os->psize;
    priv->packet[pkt_type >> 1] = av_memdup(os->buf + os->pstart, os->psize);
    if (!priv->packet[pkt_type >> 1])
        return AVERROR(ENOMEM);
    if (pkt_type == 1)
        return vorbis_parse_header(s, st, os->buf + os->pstart, os->psize);

    if (pkt_type == 3) {
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
        int ret;

        if (priv->vp)
             return AVERROR_INVALIDDATA;

        ret = fixup_vorbis_headers(s, priv, &st->codecpar->extradata);
        if (ret < 0) {
            st->codecpar->extradata_size = 0;
            return ret;
        }
        st->codecpar->extradata_size = ret;

        priv->vp = av_vorbis_parse_init(st->codecpar->extradata, st->codecpar->extradata_size);
        if (!priv->vp) {
            av_freep(&st->codecpar->extradata);
            st->codecpar->extradata_size = 0;
            return AVERROR_UNKNOWN;
        }
    }

    return 1;
}

/**
 * Check that a packet follows the identification, comment, setup order of a
 * chained stream's headers, and record it as the last packet.
 *
 * An identification header starts a new set. A misplaced packet discards the
 * buffered headers, so an incomplete or misordered set is never packed.
 * Reserved packet types are ignored, per Vorbis I §1.3.2.
 *
 * @return 0 if the packet is in order, AVERROR_INVALIDDATA otherwise
 */
static int vorbis_check_packet_order(AVFormatContext *s,
                                     struct oggvorbis_private *priv,
                                     enum VorbisPacketType type)
{
    enum VorbisPacketType last = priv->last_packet;
    const char *error = NULL;

    switch (type) {
    case VORBIS_PACKET_RESERVED:
        return 0;
    case VORBIS_PACKET_DATA:
        if (last == VORBIS_PACKET_ID || last == VORBIS_PACKET_COMMENT)
            error = "data packet before the setup header";
        break;
    case VORBIS_PACKET_ID:
        if (last == VORBIS_PACKET_ID || last == VORBIS_PACKET_COMMENT)
            error = "identification header before the setup header";
        break;
    case VORBIS_PACKET_COMMENT:
        if (last != VORBIS_PACKET_ID)
            error = "comment header without identification header";
        break;
    case VORBIS_PACKET_SETUP:
        if (last != VORBIS_PACKET_COMMENT)
            error = "setup header without comment header";
        break;
    }

    if (error)
        av_log(s, AV_LOG_WARNING,
               "Invalid chained stream headers: %s, discarding them\n", error);

    if (error || type == VORBIS_PACKET_ID)
        for (int i = 0; i < 3; i++)
            av_freep(&priv->packet[i]);

    priv->last_packet = error && type != VORBIS_PACKET_ID ? VORBIS_PACKET_DATA : type;
    return error ? AVERROR_INVALIDDATA : 0;
}

static int vorbis_packet(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    struct oggvorbis_private *priv = os->private;
    int duration, flags = 0;
    int bos, skip_packet = 0;
    int ret;

    if (os->psize == 0)
        return AVERROR_INVALIDDATA;

    if (!priv->vp)
        return AVERROR_INVALIDDATA;

    /* Track the page holding the first data packet by its file position,
     * which stays stable when probing rescans the start of the stream. */
    if (priv->bos_page_pos < 0)
        priv->bos_page_pos = os->page_pos;

    bos = os->page_start && os->page_pos == priv->bos_page_pos;

    if (bos || (os->page_start && os->lastpts == AV_NOPTS_VALUE))
        av_vorbis_parse_reset(priv->vp);
    if (bos)
        priv->last_page_granule = 0;

    duration = av_vorbis_parse_frame_flags(priv->vp, os->buf + os->pstart, 1, &flags);
    if (duration < 0) {
        os->pflags |= AV_PKT_FLAG_CORRUPT;
        return 0;
    }
    os->pduration = duration;

    /* Save ts_offset from the very first chained stream to account for the
     * first priming packet. This only shifts timestamps; the Vorbis decoder
     * discards the leading priming samples itself, so we do not set
     * codecpar->initial_padding. */
    if (!priv->ts_offset)
        priv->ts_offset = os->pduration;

    /* Initial position shift to account for encoder delay, or page scan
     * to recover the page start timestamp after a seek. */
    if (bos) {
        if (os->lastpts != AV_NOPTS_VALUE) {
            if (s->streams[idx]->start_time == AV_NOPTS_VALUE) {
                s->streams[idx]->start_time = os->lastpts;
                if (s->streams[idx]->duration != AV_NOPTS_VALUE)
                    s->streams[idx]->duration -= s->streams[idx]->start_time;
            }
            os->lastpts -= priv->ts_offset;
        }
        if (os->lastdts != AV_NOPTS_VALUE)
            os->lastdts -= priv->ts_offset;
    } else if (os->page_start && os->lastpts == AV_NOPTS_VALUE &&
               !(os->flags & OGG_FLAG_EOS) && (int64_t)os->granule >= 0) {
        int seg, page_duration = os->pduration;
        uint8_t *last_pkt = os->buf + os->pstart + os->psize;
        uint8_t *next_pkt = last_pkt;

        for (seg = os->segp; seg < os->nsegs; seg++) {
            next_pkt += os->segments[seg];
            if (os->segments[seg] < 255 && next_pkt != last_pkt) {
                int pkt_flags = 0;
                int d = av_vorbis_parse_frame_flags(priv->vp, last_pkt, 1, &pkt_flags);
                if (d < 0) {
                    page_duration = os->granule;
                    break;
                }
                page_duration += d;
                last_pkt = next_pkt;
            }
        }
        av_vorbis_parse_frame_flags(priv->vp, os->buf + os->pstart, 1, &flags);
        os->lastpts =
        os->lastdts = os->granule - page_duration;
    }

    /* Broken files may have granule=0 on pages that contain audio data.
     * In that case timestamps are unreliable, so discard them.
     * See https://trac.ffmpeg.org/ticket/3710 */
    if (!os->granule && os->pduration)
        os->lastpts = os->lastdts = AV_NOPTS_VALUE;

    if (os->flags & OGG_FLAG_EOS) {
        /* Reset at the start of the EOS page to avoid carrying over stale
         * duration from a previously processed EOS page, as can happen on
         * short or chained ogg streams and when probing rescans the page.
         * os->segp has already been advanced past the current packet by
         * ogg_packet(), so rely on os->page_start to detect the page start. */
        if (os->page_start)
            priv->eos_page_duration = 0;

        if (os->segp != os->nsegs) {
            priv->eos_page_duration += os->pduration;
        } else {
            int64_t skip = priv->eos_page_duration + os->pduration -
                           (os->granule - priv->last_page_granule);
            if (skip > 0)
                os->end_trimming = skip;
        }
    } else
        priv->last_page_granule = os->granule;

    /* Handle in-stream header packets from chained streams. The packet is
     * skipped; the headers are buffered and attached as extradata to the
     * first data packet that follows. */
    if (flags & VORBIS_FLAG_HEADER) {
        vorbis_check_packet_order(s, priv, VORBIS_PACKET_ID);

        ret = vorbis_parse_header(s, s->streams[idx], os->buf + os->pstart, os->psize);
        if (ret < 0)
            return ret;

        ret = av_reallocp(&priv->packet[0], os->psize);
        if (ret < 0)
            return ret;

        memcpy(priv->packet[0], os->buf + os->pstart, os->psize);
        priv->len[0] = os->psize;

        skip_packet = 1;
    }

    if (flags & VORBIS_FLAG_COMMENT) {
        if (vorbis_check_packet_order(s, priv, VORBIS_PACKET_COMMENT) < 0)
            return 1;

        ret = vorbis_update_metadata(s, idx);
        if (ret < 0)
            return ret;

        ret = av_reallocp(&priv->packet[1], os->psize);
        if (ret < 0)
            return ret;

        memcpy(priv->packet[1], os->buf + os->pstart, os->psize);
        priv->len[1] = os->psize;

        flags = 0;
        skip_packet = 1;
    }

    if (flags & VORBIS_FLAG_SETUP) {
        if (vorbis_check_packet_order(s, priv, VORBIS_PACKET_SETUP) < 0)
            return 1;

        ret = av_reallocp(&priv->packet[2], os->psize);
        if (ret < 0)
            return ret;

        memcpy(priv->packet[2], os->buf + os->pstart, os->psize);
        priv->len[2] = os->psize;

        skip_packet = 1;
    }

    if (!skip_packet)
        vorbis_check_packet_order(s, priv, os->buf[os->pstart] & 1 ?
                                  VORBIS_PACKET_RESERVED : VORBIS_PACKET_DATA);

    /* All three headers for the new chained stream have been collected;
     * pack them into new extradata to be attached to the next data packet. */
    if (priv->packet[0] && priv->packet[1] && priv->packet[2]) {
        av_freep(&os->new_extradata);
        ret = fixup_vorbis_headers(s, priv, &os->new_extradata);
        if (ret < 0) {
            os->new_extradata_size = 0;
            return ret;
        }
        os->new_extradata_size = ret;

        av_vorbis_parse_free(&priv->vp);
        priv->vp = av_vorbis_parse_init(os->new_extradata, os->new_extradata_size);
        if (!priv->vp) {
            av_log(s, AV_LOG_ERROR, "Failed to re-initialize Vorbis parser\n");
            return AVERROR_INVALIDDATA;
        }

        /* Reset bos_page_pos so the next packet will initialize it for the
         * new chained stream. */
        priv->bos_page_pos = -1;
    }

    return skip_packet;
}

const struct ogg_codec ff_vorbis_codec = {
    .magic     = "\001vorbis",
    .magicsize = 7,
    .header    = vorbis_header,
    .packet    = vorbis_packet,
    .cleanup   = vorbis_cleanup,
    .nb_header = 3,
};
