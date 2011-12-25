/*
 * Ogg bitstream support
 * Luca Barbato <lu_zero@gentoo.org>
 * Based on tcvp implementation
 *
 */

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


#include <stdio.h>
#include "oggdec.h"
#include "avformat.h"
#include "vorbiscomment.h"

#define MAX_PAGE_SIZE 65307
#define DECODER_BUFFER_SIZE MAX_PAGE_SIZE

static const struct ogg_codec * const ogg_codecs[] = {
    &ff_skeleton_codec,
    &ff_dirac_codec,
    &ff_speex_codec,
    &ff_vorbis_codec,
    &ff_theora_codec,
    &ff_flac_codec,
    &ff_old_dirac_codec,
    &ff_old_flac_codec,
    &ff_ogm_video_codec,
    &ff_ogm_audio_codec,
    &ff_ogm_text_codec,
    &ff_ogm_old_codec,
    NULL
};

//FIXME We could avoid some structure duplication
static int
ogg_save (AVFormatContext * s)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_state *ost =
        av_malloc(sizeof (*ost) + (ogg->nstreams-1) * sizeof (*ogg->streams));
    int i;
    ost->pos = url_ftell (s->pb);
    ost->curidx = ogg->curidx;
    ost->next = ogg->state;
    ost->nstreams = ogg->nstreams;
    memcpy(ost->streams, ogg->streams, ogg->nstreams * sizeof(*ogg->streams));

    for (i = 0; i < ogg->nstreams; i++){
        struct ogg_stream *os = ogg->streams + i;
        os->buf = av_malloc (os->bufsize);
        memset (os->buf, 0, os->bufsize);
        memcpy (os->buf, ost->streams[i].buf, os->bufpos);
    }

    ogg->state = ost;

    return 0;
}

static int
ogg_restore (AVFormatContext * s, int discard)
{
    struct ogg *ogg = s->priv_data;
    ByteIOContext *bc = s->pb;
    struct ogg_state *ost = ogg->state;
    int i;

    if (!ost)
        return 0;

    ogg->state = ost->next;

    if (!discard){
        struct ogg_stream *old_streams = ogg->streams;

        for (i = 0; i < ogg->nstreams; i++)
            av_free (ogg->streams[i].buf);

        url_fseek (bc, ost->pos, SEEK_SET);
        ogg->curidx = ost->curidx;
        ogg->nstreams = ost->nstreams;
        ogg->streams = av_realloc (ogg->streams,
                                   ogg->nstreams * sizeof (*ogg->streams));

        if (ogg->streams) {
            memcpy(ogg->streams, ost->streams,
                   ost->nstreams * sizeof(*ogg->streams));
        } else {
            av_free(old_streams);
            ogg->nstreams = 0;
        }
    }

    av_free (ost);

    return 0;
}

static int
ogg_reset (struct ogg * ogg)
{
    int i;

    for (i = 0; i < ogg->nstreams; i++){
        struct ogg_stream *os = ogg->streams + i;
        os->bufpos = 0;
        os->pstart = 0;
        os->psize = 0;
        os->granule = -1;
        os->lastpts = AV_NOPTS_VALUE;
        os->lastdts = AV_NOPTS_VALUE;
        os->sync_pos = -1;
        os->page_pos = 0;
        os->nsegs = 0;
        os->segp = 0;
        os->incomplete = 0;
    }

    ogg->curidx = -1;

    return 0;
}

static const struct ogg_codec *
ogg_find_codec (uint8_t * buf, int size)
{
    int i;

    for (i = 0; ogg_codecs[i]; i++)
        if (size >= ogg_codecs[i]->magicsize &&
            !memcmp (buf, ogg_codecs[i]->magic, ogg_codecs[i]->magicsize))
            return ogg_codecs[i];

    return NULL;
}

static int
ogg_new_stream (AVFormatContext * s, uint32_t serial)
{

    struct ogg *ogg = s->priv_data;
    int idx = ogg->nstreams++;
    AVStream *st;
    struct ogg_stream *os;

    ogg->streams = av_realloc (ogg->streams,
                               ogg->nstreams * sizeof (*ogg->streams));
    memset (ogg->streams + idx, 0, sizeof (*ogg->streams));
    os = ogg->streams + idx;
    os->serial = serial;
    os->bufsize = DECODER_BUFFER_SIZE;
    os->buf = av_malloc(os->bufsize);
    os->header = -1;

    st = av_new_stream (s, idx);
    if (!st)
        return AVERROR(ENOMEM);

    av_set_pts_info(st, 64, 1, 1000000);

    return idx;
}

static int
ogg_new_buf(struct ogg *ogg, int idx)
{
    struct ogg_stream *os = ogg->streams + idx;
    uint8_t *nb = av_malloc(os->bufsize);
    int size = os->bufpos - os->pstart;
    if(os->buf){
        memcpy(nb, os->buf + os->pstart, size);
        av_free(os->buf);
    }
    os->buf = nb;
    os->bufpos = size;
    os->pstart = 0;

    return 0;
}

static int
ogg_read_page (AVFormatContext * s, int *str)
{
    ByteIOContext *bc = s->pb;
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os;
    int i = 0;
    int flags, nsegs;
    uint64_t gp;
    uint32_t serial;
    uint32_t seq;
    uint32_t crc;
    int size, idx;
    uint8_t sync[4];
    int sp = 0;

    if (get_buffer (bc, sync, 4) < 4)
        return -1;

    do{
        int c;

        if (sync[sp & 3] == 'O' &&
            sync[(sp + 1) & 3] == 'g' &&
            sync[(sp + 2) & 3] == 'g' && sync[(sp + 3) & 3] == 'S')
            break;

        c = url_fgetc (bc);
        if (c < 0)
            return -1;
        sync[sp++ & 3] = c;
    }while (i++ < MAX_PAGE_SIZE);

    if (i >= MAX_PAGE_SIZE){
        av_log (s, AV_LOG_INFO, "ogg, can't find sync word\n");
        return -1;
    }

    if (url_fgetc (bc) != 0)      /* version */
        return -1;

    flags = url_fgetc (bc);
    gp = get_le64 (bc);
    serial = get_le32 (bc);
    seq = get_le32 (bc);
    crc = get_le32 (bc);
    nsegs = url_fgetc (bc);

    idx = ogg_find_stream (ogg, serial);
    if (idx < 0){
        idx = ogg_new_stream (s, serial);
        if (idx < 0)
            return -1;
    }

    os = ogg->streams + idx;
    os->page_pos = url_ftell(bc) - 27;

    if(os->psize > 0)
        ogg_new_buf(ogg, idx);

    if (get_buffer (bc, os->segments, nsegs) < nsegs)
        return -1;

    os->nsegs = nsegs;
    os->segp = 0;

    size = 0;
    for (i = 0; i < nsegs; i++)
        size += os->segments[i];

    if (flags & OGG_FLAG_CONT || os->incomplete){
        if (!os->psize){
            while (os->segp < os->nsegs){
                int seg = os->segments[os->segp++];
                os->pstart += seg;
                if (seg < 255)
                    break;
            }
            os->sync_pos = os->page_pos;
        }
    }else{
        os->psize = 0;
        os->sync_pos = os->page_pos;
    }

    if (os->bufsize - os->bufpos < size){
        uint8_t *nb = av_malloc (os->bufsize *= 2);
        memcpy (nb, os->buf, os->bufpos);
        av_free (os->buf);
        os->buf = nb;
    }

    if (get_buffer (bc, os->buf + os->bufpos, size) < size)
        return -1;

    os->bufpos += size;
    os->granule = gp;
    os->flags = flags;

    if (str)
        *str = idx;

    return 0;
}

static int
ogg_packet (AVFormatContext * s, int *str, int *dstart, int *dsize, int64_t *fpos)
{
    struct ogg *ogg = s->priv_data;
    int idx, i;
    struct ogg_stream *os;
    int complete = 0;
    int segp = 0, psize = 0;

#if 0
    av_log (s, AV_LOG_DEBUG, "ogg_packet: curidx=%i\n", ogg->curidx);
#endif

    do{
        idx = ogg->curidx;

        while (idx < 0){
            if (ogg_read_page (s, &idx) < 0)
                return -1;
        }

        os = ogg->streams + idx;

#if 0
        av_log (s, AV_LOG_DEBUG,
                "ogg_packet: idx=%d pstart=%d psize=%d segp=%d nsegs=%d\n",
                idx, os->pstart, os->psize, os->segp, os->nsegs);
#endif

        if (!os->codec){
            if (os->header < 0){
                os->codec = ogg_find_codec (os->buf, os->bufpos);
                if (!os->codec){
                    os->header = 0;
                    return 0;
                }
            }else{
                return 0;
            }
        }

        segp = os->segp;
        psize = os->psize;

        while (os->segp < os->nsegs){
            int ss = os->segments[os->segp++];
            os->psize += ss;
            if (ss < 255){
                complete = 1;
                break;
            }
        }

        if (!complete && os->segp == os->nsegs){
            ogg->curidx = -1;
            os->incomplete = 1;
        }
    }while (!complete);

#if 0
    av_log (s, AV_LOG_DEBUG,
            "ogg_packet: idx %i, frame size %i, start %i\n",
            idx, os->psize, os->pstart);
#endif

    if (os->granule == -1)
        av_log(s, AV_LOG_WARNING, "Page at %lld is missing granule\n", os->page_pos);

    ogg->curidx = idx;
    os->incomplete = 0;

    if (os->header) {
        os->header = os->codec->header (s, idx);
        if (!os->header){
            os->segp = segp;
            os->psize = psize;
            if (!ogg->headers)
                s->data_offset = os->sync_pos;
            ogg->headers = 1;
        }else{
            os->pstart += os->psize;
            os->psize = 0;
        }
    } else {
        os->pflags = 0;
        os->pduration = 0;
        if (os->codec && os->codec->packet)
            os->codec->packet (s, idx);
        if (str)
            *str = idx;
        if (dstart)
            *dstart = os->pstart;
        if (dsize)
            *dsize = os->psize;
        if (fpos)
            *fpos = os->sync_pos;
        os->pstart += os->psize;
        os->psize = 0;
        os->sync_pos = os->page_pos;
    }

    // determine whether there are more complete packets in this page
    // if not, the page's granule will apply to this packet
    os->page_end = 1;
    for (i = os->segp; i < os->nsegs; i++)
        if (os->segments[i] < 255) {
            os->page_end = 0;
            break;
        }

    if (os->segp == os->nsegs)
        ogg->curidx = -1;

    return 0;
}

static int
ogg_get_headers (AVFormatContext * s)
{
    struct ogg *ogg = s->priv_data;

    do{
        if (ogg_packet (s, NULL, NULL, NULL, NULL) < 0)
            return -1;
    }while (!ogg->headers);

#if 0
    av_log (s, AV_LOG_DEBUG, "found headers\n");
#endif

    return 0;
}

static int
ogg_get_length (AVFormatContext * s)
{
    struct ogg *ogg = s->priv_data;
    int i;
    int64_t size, end;

    if(url_is_streamed(s->pb))
        return 0;

// already set
    if (s->duration != AV_NOPTS_VALUE)
        return 0;

    size = url_fsize(s->pb);
    if(size < 0)
        return 0;
    end = size > MAX_PAGE_SIZE? size - MAX_PAGE_SIZE: 0;

    ogg_save (s);
    url_fseek (s->pb, end, SEEK_SET);

    while (!ogg_read_page (s, &i)){
        if (ogg->streams[i].granule != -1 && ogg->streams[i].granule != 0 &&
            ogg->streams[i].codec) {
            s->streams[i]->duration =
                ogg_gptopts (s, i, ogg->streams[i].granule, NULL);
            if (s->streams[i]->start_time != AV_NOPTS_VALUE)
                s->streams[i]->duration -= s->streams[i]->start_time;
        }
    }

    ogg_restore (s, 0);

    return 0;
}


static int
ogg_read_header (AVFormatContext * s, AVFormatParameters * ap)
{
    struct ogg *ogg = s->priv_data;
    int i;
    ogg->curidx = -1;
    //linear headers seek from start
    if (ogg_get_headers (s) < 0){
        return -1;
    }

    for (i = 0; i < ogg->nstreams; i++)
        if (ogg->streams[i].header < 0)
            ogg->streams[i].codec = NULL;

    //linear granulepos seek from end
    ogg_get_length (s);

    //fill the extradata in the per codec callbacks
    return 0;
}

static int64_t ogg_calc_pts(AVFormatContext *s, int idx, int64_t *dts)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    int64_t pts = AV_NOPTS_VALUE;

    if (dts)
        *dts = AV_NOPTS_VALUE;

    if (os->lastpts != AV_NOPTS_VALUE) {
        pts = os->lastpts;
        os->lastpts = AV_NOPTS_VALUE;
    }
    if (os->lastdts != AV_NOPTS_VALUE) {
        if (dts)
            *dts = os->lastdts;
        os->lastdts = AV_NOPTS_VALUE;
    }
    if (os->page_end) {
        if (os->granule != -1LL) {
            if (os->codec && os->codec->granule_is_start)
                pts = ogg_gptopts(s, idx, os->granule, dts);
            else
                os->lastpts = ogg_gptopts(s, idx, os->granule, &os->lastdts);
            os->granule = -1LL;
        }
    }
    return pts;
}

static int
ogg_read_packet (AVFormatContext * s, AVPacket * pkt)
{
    struct ogg *ogg;
    struct ogg_stream *os;
    int idx = -1;
    int pstart, psize;
    int64_t fpos, pts, dts;

    //Get an ogg packet
retry:
    do{
        if (ogg_packet (s, &idx, &pstart, &psize, &fpos) < 0)
            return AVERROR(EIO);
    }while (idx < 0 || !s->streams[idx]);

    ogg = s->priv_data;
    os = ogg->streams + idx;

    // pflags might not be set until after this
    pts = ogg_calc_pts(s, idx, &dts);

    if (os->keyframe_seek && !(os->pflags & AV_PKT_FLAG_KEY))
        goto retry;
    os->keyframe_seek = 0;

    //Alloc a pkt
    if (av_new_packet (pkt, psize) < 0)
        return AVERROR(EIO);
    pkt->stream_index = idx;
    memcpy (pkt->data, os->buf + pstart, psize);

    pkt->pts = pts;
    pkt->dts = dts;
    pkt->flags = os->pflags;
    pkt->duration = os->pduration;
    pkt->pos = fpos;

    return psize;
}


static int
ogg_read_close (AVFormatContext * s)
{
    struct ogg *ogg = s->priv_data;
    int i;

    for (i = 0; i < ogg->nstreams; i++){
        av_free (ogg->streams[i].buf);
        av_free (ogg->streams[i].private);
    }
    av_free (ogg->streams);
    return 0;
}


static int64_t ogg_read_timestamp (AVFormatContext * s, int stream_index,
                                   int64_t * pos_arg, int64_t pos_limit)
{
    struct ogg *ogg = s->priv_data;
    ByteIOContext *bc = s->pb;
    int64_t pts = AV_NOPTS_VALUE;
    int i = -1;
    url_fseek(bc, *pos_arg, SEEK_SET);
    ogg_reset(ogg);

    while (url_ftell(bc) < pos_limit && !ogg_packet(s, &i, NULL, NULL, pos_arg)) {
        if (i == stream_index) {
            struct ogg_stream *os = ogg->streams + stream_index;
            pts = ogg_calc_pts(s, i, NULL);
            if (os->keyframe_seek && !(os->pflags & AV_PKT_FLAG_KEY))
                pts = AV_NOPTS_VALUE;
        }
        if (pts != AV_NOPTS_VALUE)
            break;
    }
    ogg_reset(ogg);
    return pts;
}

static int ogg_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + stream_index;
    int ret;

    // Try seeking to a keyframe first. If this fails (very possible),
    // av_seek_frame will fall back to ignoring keyframes
    if (s->streams[stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO
        && !(flags & AVSEEK_FLAG_ANY))
        os->keyframe_seek = 1;

    ret = av_seek_frame_binary(s, stream_index, timestamp, flags);
    os = ogg->streams + stream_index;
    if (ret < 0)
        os->keyframe_seek = 0;
    return ret;
}

static int ogg_probe(AVProbeData *p)
{
    if (p->buf[0] == 'O' && p->buf[1] == 'g' &&
        p->buf[2] == 'g' && p->buf[3] == 'S' &&
        p->buf[4] == 0x0 && p->buf[5] <= 0x7 )
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

AVInputFormat ogg_demuxer = {
    "ogg",
    NULL_IF_CONFIG_SMALL("Ogg"),
    sizeof (struct ogg),
    ogg_probe,
    ogg_read_header,
    ogg_read_packet,
    ogg_read_close,
    ogg_read_seek,
    ogg_read_timestamp,
    .extensions = "ogg",
    .metadata_conv = ff_vorbiscomment_metadata_conv,
    .flags = AVFMT_GENERIC_INDEX,
};
