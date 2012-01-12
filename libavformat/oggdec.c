/*
 * Ogg bitstream support
 * Luca Barbato <lu_zero@gentoo.org>
 * Based on tcvp implementation
 */

/*
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
 */


#include <stdio.h>
#include "oggdec.h"
#include "avformat.h"
#include "internal.h"
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
    &ff_celt_codec,
    &ff_old_dirac_codec,
    &ff_old_flac_codec,
    &ff_ogm_video_codec,
    &ff_ogm_audio_codec,
    &ff_ogm_text_codec,
    &ff_ogm_old_codec,
    NULL
};

//FIXME We could avoid some structure duplication
static int ogg_save(AVFormatContext *s)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_state *ost =
        av_malloc(sizeof (*ost) + (ogg->nstreams-1) * sizeof (*ogg->streams));
    int i;
    ost->pos = avio_tell (s->pb);
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

static int ogg_restore(AVFormatContext *s, int discard)
{
    struct ogg *ogg = s->priv_data;
    AVIOContext *bc = s->pb;
    struct ogg_state *ost = ogg->state;
    int i;

    if (!ost)
        return 0;

    ogg->state = ost->next;

    if (!discard){
        struct ogg_stream *old_streams = ogg->streams;

        for (i = 0; i < ogg->nstreams; i++)
            av_free (ogg->streams[i].buf);

        avio_seek (bc, ost->pos, SEEK_SET);
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

static int ogg_reset(struct ogg *ogg)
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

static const struct ogg_codec *ogg_find_codec(uint8_t *buf, int size)
{
    int i;

    for (i = 0; ogg_codecs[i]; i++)
        if (size >= ogg_codecs[i]->magicsize &&
            !memcmp (buf, ogg_codecs[i]->magic, ogg_codecs[i]->magicsize))
            return ogg_codecs[i];

    return NULL;
}

static int ogg_new_stream(AVFormatContext *s, uint32_t serial, int new_avstream)
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

    if (new_avstream) {
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);

        st->id = idx;
        avpriv_set_pts_info(st, 64, 1, 1000000);
    }

    return idx;
}

static int ogg_new_buf(struct ogg *ogg, int idx)
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

static int ogg_read_page(AVFormatContext *s, int *str)
{
    AVIOContext *bc = s->pb;
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os;
    int ret, i = 0;
    int flags, nsegs;
    uint64_t gp;
    uint32_t serial;
    int size, idx;
    uint8_t sync[4];
    int sp = 0;

    ret = avio_read(bc, sync, 4);
    if (ret < 4)
        return ret < 0 ? ret : AVERROR_EOF;

    do{
        int c;

        if (sync[sp & 3] == 'O' &&
            sync[(sp + 1) & 3] == 'g' &&
            sync[(sp + 2) & 3] == 'g' && sync[(sp + 3) & 3] == 'S')
            break;

        c = avio_r8(bc);
        if (bc->eof_reached)
            return AVERROR_EOF;
        sync[sp++ & 3] = c;
    }while (i++ < MAX_PAGE_SIZE);

    if (i >= MAX_PAGE_SIZE){
        av_log (s, AV_LOG_INFO, "ogg, can't find sync word\n");
        return AVERROR_INVALIDDATA;
    }

    if (avio_r8(bc) != 0)      /* version */
        return AVERROR_INVALIDDATA;

    flags = avio_r8(bc);
    gp = avio_rl64 (bc);
    serial = avio_rl32 (bc);
    avio_skip(bc, 8); /* seq, crc */
    nsegs = avio_r8(bc);

    idx = ogg_find_stream (ogg, serial);
    if (idx < 0){
        if (ogg->headers) {
            int n;

            for (n = 0; n < ogg->nstreams; n++) {
                av_freep(&ogg->streams[n].buf);
                if (!ogg->state || ogg->state->streams[n].private != ogg->streams[n].private)
                    av_freep(&ogg->streams[n].private);
            }
            ogg->curidx   = -1;
            ogg->nstreams = 0;
            idx = ogg_new_stream(s, serial, 0);
        } else {
            idx = ogg_new_stream(s, serial, 1);
        }
        if (idx < 0)
            return idx;
    }

    os = ogg->streams + idx;
    os->page_pos = avio_tell(bc) - 27;

    if(os->psize > 0)
        ogg_new_buf(ogg, idx);

    ret = avio_read(bc, os->segments, nsegs);
    if (ret < nsegs)
        return ret < 0 ? ret : AVERROR_EOF;

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

    ret = avio_read(bc, os->buf + os->bufpos, size);
    if (ret < size)
        return ret < 0 ? ret : AVERROR_EOF;

    os->bufpos += size;
    os->granule = gp;
    os->flags = flags;

    if (str)
        *str = idx;

    return 0;
}

static int ogg_packet(AVFormatContext *s, int *str, int *dstart, int *dsize,
                      int64_t *fpos)
{
    struct ogg *ogg = s->priv_data;
    int idx, i, ret;
    struct ogg_stream *os;
    int complete = 0;
    int segp = 0, psize = 0;

    av_dlog(s, "ogg_packet: curidx=%i\n", ogg->curidx);

    do{
        idx = ogg->curidx;

        while (idx < 0){
            ret = ogg_read_page(s, &idx);
            if (ret < 0)
                return ret;
        }

        os = ogg->streams + idx;

        av_dlog(s, "ogg_packet: idx=%d pstart=%d psize=%d segp=%d nsegs=%d\n",
                idx, os->pstart, os->psize, os->segp, os->nsegs);

        if (!os->codec){
            if (os->header < 0){
                os->codec = ogg_find_codec (os->buf, os->bufpos);
                if (!os->codec){
                    av_log(s, AV_LOG_WARNING, "Codec not found\n");
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

    av_dlog(s, "ogg_packet: idx %i, frame size %i, start %i\n",
            idx, os->psize, os->pstart);

    if (os->granule == -1)
        av_log(s, AV_LOG_WARNING, "Page at %"PRId64" is missing granule\n", os->page_pos);

    ogg->curidx = idx;
    os->incomplete = 0;

    if (os->header) {
        os->header = os->codec->header (s, idx);
        if (!os->header){
            os->segp = segp;
            os->psize = psize;

            // We have reached the first non-header packet in this stream.
            // Unfortunately more header packets may still follow for others,
            // but if we continue with header parsing we may lose data packets.
            ogg->headers = 1;

            // Update the header state for all streams and
            // compute the data_offset.
            if (!s->data_offset)
                s->data_offset = os->sync_pos;
            for (i = 0; i < ogg->nstreams; i++) {
                struct ogg_stream *cur_os = ogg->streams + i;

                // if we have a partial non-header packet, its start is
                // obviously at or after the data start
                if (cur_os->incomplete)
                    s->data_offset = FFMIN(s->data_offset, cur_os->sync_pos);
            }
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

static int ogg_get_headers(AVFormatContext *s)
{
    struct ogg *ogg = s->priv_data;
    int ret;

    do{
        ret = ogg_packet(s, NULL, NULL, NULL, NULL);
        if (ret < 0)
            return ret;
    }while (!ogg->headers);

    av_dlog(s, "found headers\n");

    return 0;
}

static int ogg_get_length(AVFormatContext *s)
{
    struct ogg *ogg = s->priv_data;
    int i;
    int64_t size, end;

    if(!s->pb->seekable)
        return 0;

// already set
    if (s->duration != AV_NOPTS_VALUE)
        return 0;

    size = avio_size(s->pb);
    if(size < 0)
        return 0;
    end = size > MAX_PAGE_SIZE? size - MAX_PAGE_SIZE: 0;

    ogg_save (s);
    avio_seek (s->pb, end, SEEK_SET);

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

static int ogg_read_header(AVFormatContext *s)
{
    struct ogg *ogg = s->priv_data;
    int ret, i;
    ogg->curidx = -1;
    //linear headers seek from start
    ret = ogg_get_headers(s);
    if (ret < 0)
        return ret;

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

static int ogg_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    struct ogg *ogg;
    struct ogg_stream *os;
    int idx = -1, ret;
    int pstart, psize;
    int64_t fpos, pts, dts;

    //Get an ogg packet
retry:
    do{
        ret = ogg_packet(s, &idx, &pstart, &psize, &fpos);
        if (ret < 0)
            return ret;
    }while (idx < 0 || !s->streams[idx]);

    ogg = s->priv_data;
    os = ogg->streams + idx;

    // pflags might not be set until after this
    pts = ogg_calc_pts(s, idx, &dts);

    if (os->keyframe_seek && !(os->pflags & AV_PKT_FLAG_KEY))
        goto retry;
    os->keyframe_seek = 0;

    //Alloc a pkt
    ret = av_new_packet(pkt, psize);
    if (ret < 0)
        return ret;
    pkt->stream_index = idx;
    memcpy (pkt->data, os->buf + pstart, psize);

    pkt->pts = pts;
    pkt->dts = dts;
    pkt->flags = os->pflags;
    pkt->duration = os->pduration;
    pkt->pos = fpos;

    return psize;
}

static int ogg_read_close(AVFormatContext *s)
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

static int64_t ogg_read_timestamp(AVFormatContext *s, int stream_index,
                                  int64_t *pos_arg, int64_t pos_limit)
{
    struct ogg *ogg = s->priv_data;
    AVIOContext *bc = s->pb;
    int64_t pts = AV_NOPTS_VALUE;
    int i = -1;
    avio_seek(bc, *pos_arg, SEEK_SET);
    ogg_reset(ogg);

    while (avio_tell(bc) < pos_limit && !ogg_packet(s, &i, NULL, NULL, pos_arg)) {
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

static int ogg_read_seek(AVFormatContext *s, int stream_index,
                         int64_t timestamp, int flags)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + stream_index;
    int ret;

    // Try seeking to a keyframe first. If this fails (very possible),
    // av_seek_frame will fall back to ignoring keyframes
    if (s->streams[stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO
        && !(flags & AVSEEK_FLAG_ANY))
        os->keyframe_seek = 1;

    ret = ff_seek_frame_binary(s, stream_index, timestamp, flags);
    os = ogg->streams + stream_index;
    if (ret < 0)
        os->keyframe_seek = 0;
    return ret;
}

static int ogg_probe(AVProbeData *p)
{
    if (!memcmp("OggS", p->buf, 5) && p->buf[5] <= 0x7)
        return AVPROBE_SCORE_MAX;
    return 0;
}

AVInputFormat ff_ogg_demuxer = {
    .name           = "ogg",
    .long_name      = NULL_IF_CONFIG_SMALL("Ogg"),
    .priv_data_size = sizeof(struct ogg),
    .read_probe     = ogg_probe,
    .read_header    = ogg_read_header,
    .read_packet    = ogg_read_packet,
    .read_close     = ogg_read_close,
    .read_seek      = ogg_read_seek,
    .read_timestamp = ogg_read_timestamp,
    .extensions     = "ogg",
    .flags          = AVFMT_GENERIC_INDEX,
};
