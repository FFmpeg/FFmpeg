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
#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "avio_internal.h"
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
    &ff_opus_codec,
    &ff_vp8_codec,
    &ff_old_dirac_codec,
    &ff_old_flac_codec,
    &ff_ogm_video_codec,
    &ff_ogm_audio_codec,
    &ff_ogm_text_codec,
    &ff_ogm_old_codec,
    NULL
};

static int64_t ogg_calc_pts(AVFormatContext *s, int idx, int64_t *dts);
static int ogg_new_stream(AVFormatContext *s, uint32_t serial);
static int ogg_restore(AVFormatContext *s);

static void free_stream(AVFormatContext *s, int i)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *stream = &ogg->streams[i];

    av_freep(&stream->buf);
    if (stream->codec &&
        stream->codec->cleanup) {
        stream->codec->cleanup(s, i);
    }

    av_freep(&stream->private);
    av_freep(&stream->new_metadata);
}

//FIXME We could avoid some structure duplication
static int ogg_save(AVFormatContext *s)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_state *ost =
        av_malloc(sizeof(*ost) + (ogg->nstreams - 1) * sizeof(*ogg->streams));
    int i;
    int ret = 0;

    if (!ost)
        return AVERROR(ENOMEM);

    ost->pos      = avio_tell(s->pb);
    ost->curidx   = ogg->curidx;
    ost->next     = ogg->state;
    ost->nstreams = ogg->nstreams;
    memcpy(ost->streams, ogg->streams, ogg->nstreams * sizeof(*ogg->streams));

    for (i = 0; i < ogg->nstreams; i++) {
        struct ogg_stream *os = ogg->streams + i;
        os->buf = av_mallocz(os->bufsize + AV_INPUT_BUFFER_PADDING_SIZE);
        if (os->buf)
            memcpy(os->buf, ost->streams[i].buf, os->bufpos);
        else
            ret = AVERROR(ENOMEM);
        os->new_metadata      = NULL;
        os->new_metadata_size = 0;
    }

    ogg->state = ost;

    if (ret < 0)
        ogg_restore(s);

    return ret;
}

static int ogg_restore(AVFormatContext *s)
{
    struct ogg *ogg = s->priv_data;
    AVIOContext *bc = s->pb;
    struct ogg_state *ost = ogg->state;
    int i, err;

    if (!ost)
        return 0;

    ogg->state = ost->next;

        for (i = 0; i < ogg->nstreams; i++) {
            struct ogg_stream *stream = &ogg->streams[i];
            av_freep(&stream->buf);
            av_freep(&stream->new_metadata);

            if (i >= ost->nstreams || !ost->streams[i].private) {
                free_stream(s, i);
            }
        }

        avio_seek(bc, ost->pos, SEEK_SET);
        ogg->page_pos = -1;
        ogg->curidx   = ost->curidx;
        ogg->nstreams = ost->nstreams;
        if ((err = av_reallocp_array(&ogg->streams, ogg->nstreams,
                                     sizeof(*ogg->streams))) < 0) {
            ogg->nstreams = 0;
            return err;
        } else
            memcpy(ogg->streams, ost->streams,
                   ost->nstreams * sizeof(*ogg->streams));

    av_free(ost);

    return 0;
}

static int ogg_reset(AVFormatContext *s)
{
    struct ogg *ogg = s->priv_data;
    int i;
    int64_t start_pos = avio_tell(s->pb);

    for (i = 0; i < ogg->nstreams; i++) {
        struct ogg_stream *os = ogg->streams + i;
        os->bufpos     = 0;
        os->pstart     = 0;
        os->psize      = 0;
        os->granule    = -1;
        os->lastpts    = AV_NOPTS_VALUE;
        os->lastdts    = AV_NOPTS_VALUE;
        os->sync_pos   = -1;
        os->page_pos   = 0;
        os->nsegs      = 0;
        os->segp       = 0;
        os->incomplete = 0;
        os->got_data = 0;
        if (start_pos <= ffformatcontext(s)->data_offset) {
            os->lastpts = 0;
        }
        os->start_trimming = 0;
        os->end_trimming = 0;
        av_freep(&os->new_metadata);
        os->new_metadata_size = 0;
    }

    ogg->page_pos = -1;
    ogg->curidx = -1;

    return 0;
}

static const struct ogg_codec *ogg_find_codec(uint8_t *buf, int size)
{
    int i;

    for (i = 0; ogg_codecs[i]; i++)
        if (size >= ogg_codecs[i]->magicsize &&
            !memcmp(buf, ogg_codecs[i]->magic, ogg_codecs[i]->magicsize))
            return ogg_codecs[i];

    return NULL;
}

/**
 * Replace the current stream with a new one. This is a typical webradio
 * situation where a new audio stream spawn (identified with a new serial) and
 * must replace the previous one (track switch).
 */
static int ogg_replace_stream(AVFormatContext *s, uint32_t serial, char *magic, int page_size,
                              int probing)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os;
    const struct ogg_codec *codec;
    int i = 0;

    if (ogg->nstreams != 1) {
        avpriv_report_missing_feature(s, "Changing stream parameters in multistream ogg");
        return AVERROR_PATCHWELCOME;
    }

    /* Check for codecs */
    codec = ogg_find_codec(magic, page_size);
    if (!codec && !probing) {
        av_log(s, AV_LOG_ERROR, "Cannot identify new stream\n");
        return AVERROR_INVALIDDATA;
    }

    os = &ogg->streams[0];
    if (os->codec != codec)
        return AVERROR(EINVAL);

    os->serial  = serial;
    os->codec   = codec;
    os->serial  = serial;
    os->lastpts = 0;
    os->lastdts = 0;
    os->start_trimming = 0;
    os->end_trimming = 0;

    /* Chained files have extradata as a new packet */
    if (codec == &ff_opus_codec)
        os->header = -1;

    return i;
}

static int ogg_new_stream(AVFormatContext *s, uint32_t serial)
{
    struct ogg *ogg = s->priv_data;
    int idx         = ogg->nstreams;
    AVStream *st;
    struct ogg_stream *os;

    if (ogg->state) {
        av_log(s, AV_LOG_ERROR, "New streams are not supposed to be added "
               "in between Ogg context save/restore operations.\n");
        return AVERROR_BUG;
    }

    /* Allocate and init a new Ogg Stream */
    if (!(os = av_realloc_array(ogg->streams, ogg->nstreams + 1,
                                sizeof(*ogg->streams))))
        return AVERROR(ENOMEM);
    ogg->streams = os;
    os           = ogg->streams + idx;
    memset(os, 0, sizeof(*os));
    os->serial        = serial;
    os->bufsize       = DECODER_BUFFER_SIZE;
    os->buf           = av_malloc(os->bufsize + AV_INPUT_BUFFER_PADDING_SIZE);
    os->header        = -1;
    os->start_granule = OGG_NOGRANULE_VALUE;
    if (!os->buf)
        return AVERROR(ENOMEM);

    /* Create the associated AVStream */
    st = avformat_new_stream(s, NULL);
    if (!st) {
        av_freep(&os->buf);
        return AVERROR(ENOMEM);
    }
    st->id = idx;
    avpriv_set_pts_info(st, 64, 1, 1000000);

    ogg->nstreams++;
    return idx;
}

static int data_packets_seen(const struct ogg *ogg)
{
    int i;

    for (i = 0; i < ogg->nstreams; i++)
        if (ogg->streams[i].got_data)
            return 1;
    return 0;
}

static int buf_realloc(struct ogg_stream *os, int size)
{
    /* Even if invalid guarantee there's enough memory to read the page */
    if (os->bufsize - os->bufpos < size) {
        uint8_t *nb = av_realloc(os->buf, 2*os->bufsize + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!nb)
            return AVERROR(ENOMEM);
        os->buf = nb;
        os->bufsize *= 2;
    }

    return 0;
}

static int ogg_read_page(AVFormatContext *s, int *sid, int probing)
{
    AVIOContext *bc = s->pb;
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os;
    int ret, i = 0;
    int flags, nsegs;
    uint64_t gp;
    uint32_t serial;
    uint32_t crc, crc_tmp;
    int size = 0, idx;
    int64_t version, page_pos;
    int64_t start_pos;
    uint8_t sync[4];
    uint8_t segments[255];
    uint8_t *readout_buf;
    int sp = 0;

    ret = avio_read(bc, sync, 4);
    if (ret < 4)
        return ret < 0 ? ret : AVERROR_EOF;

    do {
        int c;

        if (sync[sp & 3] == 'O' &&
            sync[(sp + 1) & 3] == 'g' &&
            sync[(sp + 2) & 3] == 'g' && sync[(sp + 3) & 3] == 'S')
            break;

        if(!i && (bc->seekable & AVIO_SEEKABLE_NORMAL) && ogg->page_pos > 0) {
            memset(sync, 0, 4);
            avio_seek(bc, ogg->page_pos+4, SEEK_SET);
            ogg->page_pos = -1;
        }

        c = avio_r8(bc);

        if (avio_feof(bc))
            return AVERROR_EOF;

        sync[sp++ & 3] = c;
    } while (i++ < MAX_PAGE_SIZE);

    if (i >= MAX_PAGE_SIZE) {
        av_log(s, AV_LOG_INFO, "cannot find sync word\n");
        return AVERROR_INVALIDDATA;
    }

    /* 0x4fa9b05f = av_crc(AV_CRC_32_IEEE, 0x0, "OggS", 4) */
    ffio_init_checksum(bc, ff_crc04C11DB7_update, 0x4fa9b05f);

    /* To rewind if checksum is bad/check magic on switches - this is the max packet size */
    ffio_ensure_seekback(bc, MAX_PAGE_SIZE);
    start_pos = avio_tell(bc);

    version = avio_r8(bc);
    flags   = avio_r8(bc);
    gp      = avio_rl64(bc);
    serial  = avio_rl32(bc);
    avio_skip(bc, 4); /* seq */

    crc_tmp = ffio_get_checksum(bc);
    crc     = avio_rb32(bc);
    crc_tmp = ff_crc04C11DB7_update(crc_tmp, (uint8_t[4]){0}, 4);
    ffio_init_checksum(bc, ff_crc04C11DB7_update, crc_tmp);

    nsegs    = avio_r8(bc);
    page_pos = avio_tell(bc) - 27;

    ret = avio_read(bc, segments, nsegs);
    if (ret < nsegs)
        return ret < 0 ? ret : AVERROR_EOF;

    for (i = 0; i < nsegs; i++)
        size += segments[i];

    idx = ogg_find_stream(ogg, serial);
    if (idx >= 0) {
        os = ogg->streams + idx;

        ret = buf_realloc(os, size);
        if (ret < 0)
            return ret;

        readout_buf = os->buf + os->bufpos;
    } else {
        readout_buf = av_malloc(size);
    }

    ret = avio_read(bc, readout_buf, size);
    if (ret < size) {
        if (idx < 0)
            av_free(readout_buf);
        return ret < 0 ? ret : AVERROR_EOF;
    }

    if (crc ^ ffio_get_checksum(bc)) {
        av_log(s, AV_LOG_ERROR, "CRC mismatch!\n");
        if (idx < 0)
            av_free(readout_buf);
        avio_seek(bc, start_pos, SEEK_SET);
        *sid = -1;
        return 0;
    }

    /* Since we're almost sure its a valid packet, checking the version after
     * the checksum lets the demuxer be more tolerant */
    if (version) {
        av_log(s, AV_LOG_ERROR, "Invalid Ogg vers!\n");
        if (idx < 0)
            av_free(readout_buf);
        avio_seek(bc, start_pos, SEEK_SET);
        *sid = -1;
        return 0;
    }

    /* CRC is correct so we can be 99% sure there's an actual change here */
    if (idx < 0) {
        if (data_packets_seen(ogg))
            idx = ogg_replace_stream(s, serial, readout_buf, size, probing);
        else
            idx = ogg_new_stream(s, serial);

        if (idx < 0) {
            av_log(s, AV_LOG_ERROR, "failed to create or replace stream\n");
            av_free(readout_buf);
            return idx;
        }

        os = ogg->streams + idx;

        ret = buf_realloc(os, size);
        if (ret < 0) {
            av_free(readout_buf);
            return ret;
        }

        memcpy(os->buf + os->bufpos, readout_buf, size);
        av_free(readout_buf);
    }

    ogg->page_pos = page_pos;
    os->page_pos  = page_pos;
    os->nsegs     = nsegs;
    os->segp      = 0;
    os->got_data  = !(flags & OGG_FLAG_BOS);
    os->bufpos   += size;
    os->granule   = gp;
    os->flags     = flags;
    memcpy(os->segments, segments, nsegs);
    memset(os->buf + os->bufpos, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    if (flags & OGG_FLAG_CONT || os->incomplete) {
        if (!os->psize) {
            // If this is the very first segment we started
            // playback in the middle of a continuation packet.
            // Discard it since we missed the start of it.
            while (os->segp < os->nsegs) {
                int seg = os->segments[os->segp++];
                os->pstart += seg;
                if (seg < 255)
                    break;
            }
            os->sync_pos = os->page_pos;
        }
    } else {
        os->psize    = 0;
        os->sync_pos = os->page_pos;
    }

    /* This function is always called with sid != NULL */
    *sid = idx;

    return 0;
}

/**
 * @brief find the next Ogg packet
 * @param *sid is set to the stream for the packet or -1 if there is
 *             no matching stream, in that case assume all other return
 *             values to be uninitialized.
 * @return negative value on error or EOF.
 */
static int ogg_packet(AVFormatContext *s, int *sid, int *dstart, int *dsize,
                      int64_t *fpos)
{
    FFFormatContext *const si = ffformatcontext(s);
    struct ogg *ogg = s->priv_data;
    int idx, i, ret;
    struct ogg_stream *os;
    int complete = 0;
    int segp     = 0, psize = 0;

    av_log(s, AV_LOG_TRACE, "ogg_packet: curidx=%i\n", ogg->curidx);
    if (sid)
        *sid = -1;

    do {
        idx = ogg->curidx;

        while (idx < 0) {
            ret = ogg_read_page(s, &idx, 0);
            if (ret < 0)
                return ret;
        }

        os = ogg->streams + idx;

        av_log(s, AV_LOG_TRACE, "ogg_packet: idx=%d pstart=%d psize=%d segp=%d nsegs=%d\n",
                idx, os->pstart, os->psize, os->segp, os->nsegs);

        if (!os->codec) {
            if (os->header < 0) {
                os->codec = ogg_find_codec(os->buf, os->bufpos);
                if (!os->codec) {
                    av_log(s, AV_LOG_WARNING, "Codec not found\n");
                    os->header = 0;
                    return 0;
                }
            } else {
                return 0;
            }
        }

        segp  = os->segp;
        psize = os->psize;

        while (os->segp < os->nsegs) {
            int ss = os->segments[os->segp++];
            os->psize += ss;
            if (ss < 255) {
                complete = 1;
                break;
            }
        }

        if (!complete && os->segp == os->nsegs) {
            ogg->curidx    = -1;
            // Do not set incomplete for empty packets.
            // Together with the code in ogg_read_page
            // that discards all continuation of empty packets
            // we would get an infinite loop.
            os->incomplete = !!os->psize;
        }
    } while (!complete);


    if (os->granule == -1)
        av_log(s, AV_LOG_WARNING,
               "Page at %"PRId64" is missing granule\n",
               os->page_pos);

    ogg->curidx    = idx;
    os->incomplete = 0;

    if (os->header) {
        if ((ret = os->codec->header(s, idx)) < 0) {
            av_log(s, AV_LOG_ERROR, "Header processing failed: %s\n", av_err2str(ret));
            return ret;
        }
        os->header = ret;
        if (!os->header) {
            os->segp  = segp;
            os->psize = psize;

            // We have reached the first non-header packet in this stream.
            // Unfortunately more header packets may still follow for others,
            // but if we continue with header parsing we may lose data packets.
            ogg->headers = 1;

            // Update the header state for all streams and
            // compute the data_offset.
            if (!si->data_offset)
                si->data_offset = os->sync_pos;

            for (i = 0; i < ogg->nstreams; i++) {
                struct ogg_stream *cur_os = ogg->streams + i;

                // if we have a partial non-header packet, its start is
                // obviously at or after the data start
                if (cur_os->incomplete)
                    si->data_offset = FFMIN(si->data_offset, cur_os->sync_pos);
            }
        } else {
            os->nb_header++;
            os->pstart += os->psize;
            os->psize   = 0;
        }
    } else {
        os->pflags    = 0;
        os->pduration = 0;
        if (os->codec && os->codec->packet) {
            if ((ret = os->codec->packet(s, idx)) < 0) {
                av_log(s, AV_LOG_ERROR, "Packet processing failed: %s\n", av_err2str(ret));
                return ret;
            }
        }
        if (sid)
            *sid = idx;
        if (dstart)
            *dstart = os->pstart;
        if (dsize)
            *dsize = os->psize;
        if (fpos)
            *fpos = os->sync_pos;
        os->pstart  += os->psize;
        os->psize    = 0;
        if(os->pstart == os->bufpos)
            os->bufpos = os->pstart = 0;
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

static int ogg_get_length(AVFormatContext *s)
{
    struct ogg *ogg = s->priv_data;
    int i, ret;
    int64_t size, end;
    int streams_left=0;

    if (!(s->pb->seekable & AVIO_SEEKABLE_NORMAL))
        return 0;

// already set
    if (s->duration != AV_NOPTS_VALUE)
        return 0;

    size = avio_size(s->pb);
    if (size < 0)
        return 0;
    end = size > MAX_PAGE_SIZE ? size - MAX_PAGE_SIZE : 0;

    ret = ogg_save(s);
    if (ret < 0)
        return ret;
    avio_seek(s->pb, end, SEEK_SET);
    ogg->page_pos = -1;

    while (!ogg_read_page(s, &i, 1)) {
        if (i >= 0 && ogg->streams[i].granule != -1 && ogg->streams[i].granule != 0 &&
            ogg->streams[i].codec) {
            s->streams[i]->duration =
                ogg_gptopts(s, i, ogg->streams[i].granule, NULL);
            if (s->streams[i]->start_time != AV_NOPTS_VALUE) {
                s->streams[i]->duration -= s->streams[i]->start_time;
                streams_left-= (ogg->streams[i].got_start==-1);
                ogg->streams[i].got_start= 1;
            } else if(!ogg->streams[i].got_start) {
                ogg->streams[i].got_start= -1;
                streams_left++;
            }
        }
    }

    ogg_restore(s);

    ret = ogg_save(s);
    if (ret < 0)
        return ret;

    avio_seek (s->pb, ffformatcontext(s)->data_offset, SEEK_SET);
    ogg_reset(s);
    while (streams_left > 0 && !ogg_packet(s, &i, NULL, NULL, NULL)) {
        int64_t pts;
        if (i < 0) continue;
        pts = ogg_calc_pts(s, i, NULL);
        if (s->streams[i]->duration == AV_NOPTS_VALUE)
            continue;
        if (pts != AV_NOPTS_VALUE && s->streams[i]->start_time == AV_NOPTS_VALUE && !ogg->streams[i].got_start) {
            s->streams[i]->duration -= pts;
            ogg->streams[i].got_start= 1;
            streams_left--;
        }else if(s->streams[i]->start_time != AV_NOPTS_VALUE && !ogg->streams[i].got_start) {
            ogg->streams[i].got_start= 1;
            streams_left--;
        }
    }
    ogg_restore (s);

    return 0;
}

static int ogg_read_close(AVFormatContext *s)
{
    struct ogg *ogg = s->priv_data;
    int i;

    for (i = 0; i < ogg->nstreams; i++) {
        free_stream(s, i);
    }

    ogg->nstreams = 0;

    av_freep(&ogg->streams);
    return 0;
}

static int ogg_read_header(AVFormatContext *s)
{
    struct ogg *ogg = s->priv_data;
    int ret, i;

    ogg->curidx = -1;

    //linear headers seek from start
    do {
        ret = ogg_packet(s, NULL, NULL, NULL, NULL);
        if (ret < 0)
            return ret;
    } while (!ogg->headers);
    av_log(s, AV_LOG_TRACE, "found headers\n");

    for (i = 0; i < ogg->nstreams; i++) {
        struct ogg_stream *os = ogg->streams + i;

        if (ogg->streams[i].header < 0) {
            av_log(s, AV_LOG_ERROR, "Header parsing failed for stream %d\n", i);
            ogg->streams[i].codec = NULL;
            av_freep(&ogg->streams[i].private);
        } else if (os->codec && os->nb_header < os->codec->nb_header) {
            av_log(s, AV_LOG_WARNING,
                   "Headers mismatch for stream %d: "
                   "expected %d received %d.\n",
                   i, os->codec->nb_header, os->nb_header);
            if (s->error_recognition & AV_EF_EXPLODE)
                return AVERROR_INVALIDDATA;
        }
        if (os->start_granule != OGG_NOGRANULE_VALUE)
            os->lastpts = s->streams[i]->start_time =
                ogg_gptopts(s, i, os->start_granule, NULL);
    }

    //linear granulepos seek from end
    ret = ogg_get_length(s);
    if (ret < 0)
        return ret;

    return 0;
}

static int64_t ogg_calc_pts(AVFormatContext *s, int idx, int64_t *dts)
{
    struct ogg *ogg       = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    int64_t pts           = AV_NOPTS_VALUE;

    if (dts)
        *dts = AV_NOPTS_VALUE;

    if (os->lastpts != AV_NOPTS_VALUE) {
        pts         = os->lastpts;
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

static void ogg_validate_keyframe(AVFormatContext *s, int idx, int pstart, int psize)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    int invalid = 0;
    if (psize) {
        switch (s->streams[idx]->codecpar->codec_id) {
        case AV_CODEC_ID_THEORA:
            invalid = !!(os->pflags & AV_PKT_FLAG_KEY) != !(os->buf[pstart] & 0x40);
        break;
        case AV_CODEC_ID_VP8:
            invalid = !!(os->pflags & AV_PKT_FLAG_KEY) != !(os->buf[pstart] & 1);
        }
        if (invalid) {
            os->pflags ^= AV_PKT_FLAG_KEY;
            av_log(s, AV_LOG_WARNING, "Broken file, %skeyframe not correctly marked.\n",
                   (os->pflags & AV_PKT_FLAG_KEY) ? "" : "non-");
        }
    }
}

static int ogg_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    struct ogg *ogg;
    struct ogg_stream *os;
    int idx, ret;
    int pstart, psize;
    int64_t fpos, pts, dts;

    if (s->io_repositioned) {
        ogg_reset(s);
        s->io_repositioned = 0;
    }

    //Get an ogg packet
retry:
    do {
        ret = ogg_packet(s, &idx, &pstart, &psize, &fpos);
        if (ret < 0)
            return ret;
    } while (idx < 0 || !s->streams[idx]);

    ogg = s->priv_data;
    os  = ogg->streams + idx;

    // pflags might not be set until after this
    pts = ogg_calc_pts(s, idx, &dts);
    ogg_validate_keyframe(s, idx, pstart, psize);

    if (os->keyframe_seek && !(os->pflags & AV_PKT_FLAG_KEY))
        goto retry;
    os->keyframe_seek = 0;

    //Alloc a pkt
    ret = av_new_packet(pkt, psize);
    if (ret < 0)
        return ret;
    pkt->stream_index = idx;
    memcpy(pkt->data, os->buf + pstart, psize);

    pkt->pts      = pts;
    pkt->dts      = dts;
    pkt->flags    = os->pflags;
    pkt->duration = os->pduration;
    pkt->pos      = fpos;

    if (os->start_trimming || os->end_trimming) {
        uint8_t *side_data = av_packet_new_side_data(pkt,
                                                     AV_PKT_DATA_SKIP_SAMPLES,
                                                     10);
        if(!side_data)
            return AVERROR(ENOMEM);
         AV_WL32(side_data + 0, os->start_trimming);
        AV_WL32(side_data + 4, os->end_trimming);
        os->start_trimming = 0;
        os->end_trimming = 0;
    }

    if (os->new_metadata) {
        ret = av_packet_add_side_data(pkt, AV_PKT_DATA_METADATA_UPDATE,
                                      os->new_metadata, os->new_metadata_size);
        if (ret < 0)
            return ret;

        os->new_metadata      = NULL;
        os->new_metadata_size = 0;
    }

    return psize;
}

static int64_t ogg_read_timestamp(AVFormatContext *s, int stream_index,
                                  int64_t *pos_arg, int64_t pos_limit)
{
    struct ogg *ogg = s->priv_data;
    AVIOContext *bc = s->pb;
    int64_t pts     = AV_NOPTS_VALUE;
    int64_t keypos  = -1;
    int i;
    int pstart, psize;
    avio_seek(bc, *pos_arg, SEEK_SET);
    ogg_reset(s);

    while (   avio_tell(bc) <= pos_limit
           && !ogg_packet(s, &i, &pstart, &psize, pos_arg)) {
        if (i == stream_index) {
            struct ogg_stream *os = ogg->streams + stream_index;
            // Do not trust the last timestamps of an ogm video
            if (    (os->flags & OGG_FLAG_EOS)
                && !(os->flags & OGG_FLAG_BOS)
                && os->codec == &ff_ogm_video_codec)
                continue;
            pts = ogg_calc_pts(s, i, NULL);
            ogg_validate_keyframe(s, i, pstart, psize);
            if (os->pflags & AV_PKT_FLAG_KEY) {
                keypos = *pos_arg;
            } else if (os->keyframe_seek) {
                // if we had a previous keyframe but no pts for it,
                // return that keyframe with this pts value.
                if (keypos >= 0)
                    *pos_arg = keypos;
                else
                    pts = AV_NOPTS_VALUE;
            }
        }
        if (pts != AV_NOPTS_VALUE)
            break;
    }
    ogg_reset(s);
    return pts;
}

static int ogg_read_seek(AVFormatContext *s, int stream_index,
                         int64_t timestamp, int flags)
{
    struct ogg *ogg       = s->priv_data;
    struct ogg_stream *os = ogg->streams + stream_index;
    int ret;

    av_assert0(stream_index < ogg->nstreams);
    // Ensure everything is reset even when seeking via
    // the generated index.
    ogg_reset(s);

    // Try seeking to a keyframe first. If this fails (very possible),
    // av_seek_frame will fall back to ignoring keyframes
    if (s->streams[stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
        && !(flags & AVSEEK_FLAG_ANY))
        os->keyframe_seek = 1;

    ret = ff_seek_frame_binary(s, stream_index, timestamp, flags);
    ogg_reset(s);
    os  = ogg->streams + stream_index;
    if (ret < 0)
        os->keyframe_seek = 0;
    return ret;
}

static int ogg_probe(const AVProbeData *p)
{
    if (!memcmp("OggS", p->buf, 5) && p->buf[5] <= 0x7)
        return AVPROBE_SCORE_MAX;
    return 0;
}

const AVInputFormat ff_ogg_demuxer = {
    .name           = "ogg",
    .long_name      = NULL_IF_CONFIG_SMALL("Ogg"),
    .priv_data_size = sizeof(struct ogg),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .read_probe     = ogg_probe,
    .read_header    = ogg_read_header,
    .read_packet    = ogg_read_packet,
    .read_close     = ogg_read_close,
    .read_seek      = ogg_read_seek,
    .read_timestamp = ogg_read_timestamp,
    .extensions     = "ogg",
    .flags          = AVFMT_GENERIC_INDEX | AVFMT_TS_DISCONT | AVFMT_NOBINSEARCH,
};
