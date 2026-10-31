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
#include "libavutil/attributes.h"
#include "libavutil/mem.h"
#include "libavcodec/get_bits.h"
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavformat/ogg/mapping.h"
#include "libavformat/vorbis/comment.h"

enum TheoraPacketType {
    THEORA_PACKET_DATA,
    THEORA_PACKET_ID,
    THEORA_PACKET_COMMENT,
    THEORA_PACKET_SETUP,
    THEORA_PACKET_RESERVED,
};

typedef struct TheoraParams {
    int gpshift;
    int gpmask;
    unsigned version;
    uint8_t *packet[3];                 ///< headers of the link, until all three are in
    int len[3];
    enum TheoraPacketType last_packet;  ///< type of the last packet seen by theora_packet()
} TheoraParams;

/**
 * Pack the three headers of a link with Xiph lacing, which carries headers
 * of any size, and free them.
 */
static int theora_pack_headers(TheoraParams *thp, uint8_t **buf)
{
    uint64_t size = 1 + thp->len[0] / 255 + 1 + thp->len[1] / 255 + 1 +
                    (uint64_t)thp->len[0] + thp->len[1] + thp->len[2];
    int offset;
    uint8_t *ptr;

    if (size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        return AVERROR_INVALIDDATA;

    ptr = *buf = av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!ptr)
        return AVERROR(ENOMEM);

    ptr[0]  = 2;
    offset  = 1;
    offset += av_xiphlacing(&ptr[offset], thp->len[0]);
    offset += av_xiphlacing(&ptr[offset], thp->len[1]);
    for (int i = 0; i < 3; i++) {
        memcpy(&ptr[offset], thp->packet[i], thp->len[i]);
        offset += thp->len[i];
        av_freep(&thp->packet[i]);
    }
    return offset;
}

static int theora_header(AVFormatContext *s, int idx)
{
    struct ogg *ogg       = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st          = s->streams[idx];
    TheoraParams *thp     = os->private;
    int type              = os->buf[os->pstart] & 0x7f;
    int ret;

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

        thp->version = get_bits(&gb, 24);
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
            int width  = get_bits(&gb, 24);
            int height = get_bits(&gb, 24);
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

        st->sample_aspect_ratio.num = get_bits(&gb, 24);
        st->sample_aspect_ratio.den = get_bits(&gb, 24);

        if (thp->version >= 0x030200)
            skip_bits_long(&gb, 38);
        if (thp->version >= 0x304000)
            skip_bits(&gb, 2);

        thp->gpshift = get_bits(&gb, 5);
        thp->gpmask  = (1U << thp->gpshift) - 1;

        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id   = AV_CODEC_ID_THEORA;
        ffstream(st)->need_parsing = AVSTREAM_PARSE_HEADERS;
    }
    break;
    case 0x81:
        ff_vorbis_comment_read_stream(s, st, os->buf + os->pstart + 7, os->psize - 7);
        av_fallthrough;
    case 0x82:
        if (!thp->version)
            return AVERROR_INVALIDDATA;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "Unknown header type %X\n", os->buf[os->pstart]);
        return AVERROR_INVALIDDATA;
    }

    /* The headers are freed once packed, so a repeated one is ignored. */
    if (type > 0 && !thp->packet[0] || type > 1 && !thp->packet[1])
        return 1;

    ret = av_reallocp(&thp->packet[type], os->psize);
    if (ret < 0)
        return ret;
    memcpy(thp->packet[type], os->buf + os->pstart, os->psize);
    thp->len[type] = os->psize;

    if (type == 2) {
        av_freep(&st->codecpar->extradata);
        ret = theora_pack_headers(thp, &st->codecpar->extradata);
        if (ret < 0) {
            st->codecpar->extradata_size = 0;
            return ret;
        }
        st->codecpar->extradata_size = ret;
    }

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

static enum TheoraPacketType theora_packet_type(const struct ogg_stream *os)
{
    uint8_t type = os->psize > 0 ? os->buf[os->pstart] : 0;

    if (!(type & 0x80))
        return THEORA_PACKET_DATA;
    return type <= 0x82 ? THEORA_PACKET_ID + (type & 0x7f) : THEORA_PACKET_RESERVED;
}

static int theora_check_packet_order(AVFormatContext *s, TheoraParams *thp,
                                     enum TheoraPacketType type)
{
    enum TheoraPacketType last = thp->last_packet;
    const char *error = NULL;

    switch (type) {
    case THEORA_PACKET_RESERVED:
        return 0;
    case THEORA_PACKET_DATA:
        if (last == THEORA_PACKET_ID || last == THEORA_PACKET_COMMENT)
            error = "data packet before the setup header";
        break;
    case THEORA_PACKET_ID:
        if (last == THEORA_PACKET_ID || last == THEORA_PACKET_COMMENT)
            error = "identification header before the setup header";
        break;
    case THEORA_PACKET_COMMENT:
        if (last != THEORA_PACKET_ID)
            error = "comment header without identification header";
        break;
    case THEORA_PACKET_SETUP:
        if (last != THEORA_PACKET_COMMENT)
            error = "setup header without comment header";
        break;
    }

    if (error)
        av_log(s, AV_LOG_WARNING,
               "Invalid chained stream headers: %s, discarding them\n", error);

    if (error || type == THEORA_PACKET_ID)
        for (int i = 0; i < 3; i++)
            av_freep(&thp->packet[i]);

    thp->last_packet = error && type != THEORA_PACKET_ID ? THEORA_PACKET_DATA : type;
    return error ? AVERROR_INVALIDDATA : 0;
}

static int theora_packet(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    TheoraParams *thp = os->private;
    enum TheoraPacketType type = theora_packet_type(os);
    int duration, ret;

    /* A later link's headers are skipped and attached as extradata to the
     * first data packet that follows. That packet keeps its timestamp, which
     * ogg_read_packet() would clear after a stream replacement. */
    if (type != THEORA_PACKET_DATA && type != THEORA_PACKET_RESERVED) {
        int i = type - THEORA_PACKET_ID;

        os->replace = 0;

        if (theora_check_packet_order(s, thp, type) < 0 && type != THEORA_PACKET_ID)
            return 1;

        ret = av_reallocp(&thp->packet[i], os->psize);
        if (ret < 0)
            return ret;
        memcpy(thp->packet[i], os->buf + os->pstart, os->psize);
        thp->len[i] = os->psize;

        if (type == THEORA_PACKET_COMMENT && os->psize > 7) {
            ret = ff_vorbis_update_metadata(s, s->streams[idx],
                                            os->buf + os->pstart + 7, os->psize - 7);
            if (ret == AVERROR_INVALIDDATA)
                av_log(s, AV_LOG_WARNING, "Invalid Theora comment header, metadata ignored\n");
            else if (ret < 0)
                return ret;
        }

        if (thp->packet[0] && thp->packet[1] && thp->packet[2]) {
            av_freep(&os->new_extradata);
            ret = theora_pack_headers(thp, &os->new_extradata);
            if (ret < 0) {
                os->new_extradata_size = 0;
                return ret;
            }
            os->new_extradata_size = ret;
        }

        return 1;
    }
    theora_check_packet_order(s, thp, type);

    /* first packet handling
       here we parse the duration of each packet in the first page and compare
       the total duration to the page granule to find the encoder delay and
       set the first timestamp */

    if ((!os->lastpts || os->lastpts == AV_NOPTS_VALUE) && !(os->flags & OGG_FLAG_EOS)) {
        int seg;
        int64_t pts;

        duration = 1;
        for (seg = os->segp; seg < os->nsegs; seg++) {
            if (os->segments[seg] < 255)
                duration ++;
        }

        pts = theora_gptopts(s, idx, os->granule, NULL);
        if (pts != AV_NOPTS_VALUE)
            pts = av_sat_sub64(pts, duration);
        os->lastpts = os->lastdts = pts;
        if(s->streams[idx]->start_time == AV_NOPTS_VALUE && os->lastpts != AV_NOPTS_VALUE) {
            s->streams[idx]->start_time = os->lastpts;
            if (s->streams[idx]->duration > 0)
                s->streams[idx]->duration = av_sat_sub64(s->streams[idx]->duration, s->streams[idx]->start_time);
        }
    }

    /* parse packet duration */
    if (os->psize > 0) {
        os->pduration = 1;
    }

    return 0;
}

static void theora_cleanup(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    TheoraParams *thp = ogg->streams[idx].private;

    if (thp)
        for (int i = 0; i < 3; i++)
            av_freep(&thp->packet[i]);
}

const struct ogg_codec ff_theora_codec = {
    .magic     = "\200theora",
    .magicsize = 7,
    .header    = theora_header,
    .packet    = theora_packet,
    .gptopts   = theora_gptopts,
    .cleanup   = theora_cleanup,
    .nb_header = 3,
};
