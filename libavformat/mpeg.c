/*
 * MPEG1/2 demuxer
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avformat.h"
#include "internal.h"
#include "mpeg.h"

#undef NDEBUG
#include <assert.h>

/*********************************************/
/* demux code */

#define MAX_SYNC_SIZE 100000

static int check_pes(uint8_t *p, uint8_t *end){
    int pes1;
    int pes2=      (p[3] & 0xC0) == 0x80
                && (p[4] & 0xC0) != 0x40
                &&((p[4] & 0xC0) == 0x00 || (p[4]&0xC0)>>2 == (p[6]&0xF0));

    for(p+=3; p<end && *p == 0xFF; p++);
    if((*p&0xC0) == 0x40) p+=2;
    if((*p&0xF0) == 0x20){
        pes1= p[0]&p[2]&p[4]&1;
    }else if((*p&0xF0) == 0x30){
        pes1= p[0]&p[2]&p[4]&p[5]&p[7]&p[9]&1;
    }else
        pes1 = *p == 0x0F;

    return pes1||pes2;
}

static int check_pack_header(const uint8_t *buf) {
    return (buf[1] & 0xC0) == 0x40 || (buf[1] & 0xF0) == 0x20;
}

static int mpegps_probe(AVProbeData *p)
{
    uint32_t code= -1;
    int sys=0, pspack=0, priv1=0, vid=0, audio=0, invalid=0;
    int i;
    int score=0;

    for(i=0; i<p->buf_size; i++){
        code = (code<<8) + p->buf[i];
        if ((code & 0xffffff00) == 0x100) {
            int len= p->buf[i+1] << 8 | p->buf[i+2];
            int pes= check_pes(p->buf+i, p->buf+p->buf_size);
            int pack = check_pack_header(p->buf+i);

            if(code == SYSTEM_HEADER_START_CODE) sys++;
            else if(code == PACK_START_CODE && pack) pspack++;
            else if((code & 0xf0) == VIDEO_ID &&  pes) vid++;
            // skip pes payload to avoid start code emulation for private
            // and audio streams
            else if((code & 0xe0) == AUDIO_ID &&  pes) {audio++; i+=len;}
            else if(code == PRIVATE_STREAM_1  &&  pes) {priv1++; i+=len;}
            else if(code == 0x1fd             &&  pes) vid++; //VC1

            else if((code & 0xf0) == VIDEO_ID && !pes) invalid++;
            else if((code & 0xe0) == AUDIO_ID && !pes) invalid++;
            else if(code == PRIVATE_STREAM_1  && !pes) invalid++;
        }
    }

    if(vid+audio > invalid+1)     /* invalid VDR files nd short PES streams */
        score= AVPROBE_SCORE_MAX/4;

//av_log(NULL, AV_LOG_ERROR, "%d %d %d %d %d %d len:%d\n", sys, priv1, pspack,vid, audio, invalid, p->buf_size);
    if(sys>invalid && sys*9 <= pspack*10)
        return pspack > 2 ? AVPROBE_SCORE_MAX/2+2 : AVPROBE_SCORE_MAX/4; // +1 for .mpg
    if(pspack > invalid && (priv1+vid+audio)*10 >= pspack*9)
        return pspack > 2 ? AVPROBE_SCORE_MAX/2+2 : AVPROBE_SCORE_MAX/4; // +1 for .mpg
    if((!!vid ^ !!audio) && (audio > 4 || vid > 1) && !sys && !pspack && p->buf_size>2048 && vid + audio > invalid) /* PES stream */
        return (audio > 12 || vid > 3) ? AVPROBE_SCORE_MAX/2+2 : AVPROBE_SCORE_MAX/4;

    //02-Penguin.flac has sys:0 priv1:0 pspack:0 vid:0 audio:1
    //mp3_misidentified_2.mp3 has sys:0 priv1:0 pspack:0 vid:0 audio:6
    return score;
}


typedef struct MpegDemuxContext {
    int32_t header_state;
    unsigned char psm_es_type[256];
    int sofdec;
} MpegDemuxContext;

static int mpegps_read_header(AVFormatContext *s,
                              AVFormatParameters *ap)
{
    MpegDemuxContext *m = s->priv_data;
    const char *sofdec = "Sofdec";
    int v, i = 0;
    int64_t last_pos = avio_tell(s->pb);

    m->header_state = 0xff;
    s->ctx_flags |= AVFMTCTX_NOHEADER;

    m->sofdec = -1;
    do {
        v = avio_r8(s->pb);
        m->sofdec++;
    } while (v == sofdec[i] && i++ < 6);

    m->sofdec = (m->sofdec == 6) ? 1 : 0;

    if (!m->sofdec)
       avio_seek(s->pb, last_pos, SEEK_SET);

    /* no need to do more */
    return 0;
}

static int64_t get_pts(AVIOContext *pb, int c)
{
    uint8_t buf[5];

    buf[0] = c<0 ? avio_r8(pb) : c;
    avio_read(pb, buf+1, 4);

    return ff_parse_pes_pts(buf);
}

static int find_next_start_code(AVIOContext *pb, int *size_ptr,
                                int32_t *header_state)
{
    unsigned int state, v;
    int val, n;

    state = *header_state;
    n = *size_ptr;
    while (n > 0) {
        if (url_feof(pb))
            break;
        v = avio_r8(pb);
        n--;
        if (state == 0x000001) {
            state = ((state << 8) | v) & 0xffffff;
            val = state;
            goto found;
        }
        state = ((state << 8) | v) & 0xffffff;
    }
    val = -1;
 found:
    *header_state = state;
    *size_ptr = n;
    return val;
}

#if 0 /* unused, remove? */
/* XXX: optimize */
static int find_prev_start_code(AVIOContext *pb, int *size_ptr)
{
    int64_t pos, pos_start;
    int max_size, start_code;

    max_size = *size_ptr;
    pos_start = avio_tell(pb);

    /* in order to go faster, we fill the buffer */
    pos = pos_start - 16386;
    if (pos < 0)
        pos = 0;
    avio_seek(pb, pos, SEEK_SET);
    avio_r8(pb);

    pos = pos_start;
    for(;;) {
        pos--;
        if (pos < 0 || (pos_start - pos) >= max_size) {
            start_code = -1;
            goto the_end;
        }
        avio_seek(pb, pos, SEEK_SET);
        start_code = avio_rb32(pb);
        if ((start_code & 0xffffff00) == 0x100)
            break;
    }
 the_end:
    *size_ptr = pos_start - pos;
    return start_code;
}
#endif

/**
 * Extract stream types from a program stream map
 * According to ISO/IEC 13818-1 ('MPEG-2 Systems') table 2-35
 *
 * @return number of bytes occupied by PSM in the bitstream
 */
static long mpegps_psm_parse(MpegDemuxContext *m, AVIOContext *pb)
{
    int psm_length, ps_info_length, es_map_length;

    psm_length = avio_rb16(pb);
    avio_r8(pb);
    avio_r8(pb);
    ps_info_length = avio_rb16(pb);

    /* skip program_stream_info */
    avio_skip(pb, ps_info_length);
    es_map_length = avio_rb16(pb);

    /* at least one es available? */
    while (es_map_length >= 4){
        unsigned char type      = avio_r8(pb);
        unsigned char es_id     = avio_r8(pb);
        uint16_t es_info_length = avio_rb16(pb);
        /* remember mapping from stream id to stream type */
        m->psm_es_type[es_id] = type;
        /* skip program_stream_info */
        avio_skip(pb, es_info_length);
        es_map_length -= 4 + es_info_length;
    }
    avio_rb32(pb); /* crc32 */
    return 2 + psm_length;
}

/* read the next PES header. Return its position in ppos
   (if not NULL), and its start code, pts and dts.
 */
static int mpegps_read_pes_header(AVFormatContext *s,
                                  int64_t *ppos, int *pstart_code,
                                  int64_t *ppts, int64_t *pdts)
{
    MpegDemuxContext *m = s->priv_data;
    int len, size, startcode, c, flags, header_len;
    int pes_ext, ext2_len, id_ext, skip;
    int64_t pts, dts;
    int64_t last_sync= avio_tell(s->pb);

 error_redo:
        avio_seek(s->pb, last_sync, SEEK_SET);
 redo:
        /* next start code (should be immediately after) */
        m->header_state = 0xff;
        size = MAX_SYNC_SIZE;
        startcode = find_next_start_code(s->pb, &size, &m->header_state);
        last_sync = avio_tell(s->pb);
    //printf("startcode=%x pos=0x%"PRIx64"\n", startcode, avio_tell(s->pb));
    if (startcode < 0){
        if(url_feof(s->pb))
            return AVERROR_EOF;
        //FIXME we should remember header_state
        return AVERROR(EAGAIN);
    }

    if (startcode == PACK_START_CODE)
        goto redo;
    if (startcode == SYSTEM_HEADER_START_CODE)
        goto redo;
    if (startcode == PADDING_STREAM) {
        avio_skip(s->pb, avio_rb16(s->pb));
        goto redo;
    }
    if (startcode == PRIVATE_STREAM_2) {
        len = avio_rb16(s->pb);
        if (!m->sofdec) {
            while (len-- >= 6) {
                if (avio_r8(s->pb) == 'S') {
                    uint8_t buf[5];
                    avio_read(s->pb, buf, sizeof(buf));
                    m->sofdec = !memcmp(buf, "ofdec", 5);
                    len -= sizeof(buf);
                    break;
                }
            }
            m->sofdec -= !m->sofdec;
        }
        avio_skip(s->pb, len);
        goto redo;
    }
    if (startcode == PROGRAM_STREAM_MAP) {
        mpegps_psm_parse(m, s->pb);
        goto redo;
    }

    /* find matching stream */
    if (!((startcode >= 0x1c0 && startcode <= 0x1df) ||
          (startcode >= 0x1e0 && startcode <= 0x1ef) ||
          (startcode == 0x1bd) || (startcode == 0x1fd)))
        goto redo;
    if (ppos) {
        *ppos = avio_tell(s->pb) - 4;
    }
    len = avio_rb16(s->pb);
    pts =
    dts = AV_NOPTS_VALUE;
    /* stuffing */
    for(;;) {
        if (len < 1)
            goto error_redo;
        c = avio_r8(s->pb);
        len--;
        /* XXX: for mpeg1, should test only bit 7 */
        if (c != 0xff)
            break;
    }
    if ((c & 0xc0) == 0x40) {
        /* buffer scale & size */
        avio_r8(s->pb);
        c = avio_r8(s->pb);
        len -= 2;
    }
    if ((c & 0xe0) == 0x20) {
        dts = pts = get_pts(s->pb, c);
        len -= 4;
        if (c & 0x10){
            dts = get_pts(s->pb, -1);
            len -= 5;
        }
    } else if ((c & 0xc0) == 0x80) {
        /* mpeg 2 PES */
#if 0 /* some streams have this field set for no apparent reason */
        if ((c & 0x30) != 0) {
            /* Encrypted multiplex not handled */
            goto redo;
        }
#endif
        flags = avio_r8(s->pb);
        header_len = avio_r8(s->pb);
        len -= 2;
        if (header_len > len)
            goto error_redo;
        len -= header_len;
        if (flags & 0x80) {
            dts = pts = get_pts(s->pb, -1);
            header_len -= 5;
            if (flags & 0x40) {
                dts = get_pts(s->pb, -1);
                header_len -= 5;
            }
        }
        if (flags & 0x3f && header_len == 0){
            flags &= 0xC0;
            av_log(s, AV_LOG_WARNING, "Further flags set but no bytes left\n");
        }
        if (flags & 0x01) { /* PES extension */
            pes_ext = avio_r8(s->pb);
            header_len--;
            /* Skip PES private data, program packet sequence counter and P-STD buffer */
            skip = (pes_ext >> 4) & 0xb;
            skip += skip & 0x9;
            if (pes_ext & 0x40 || skip > header_len){
                av_log(s, AV_LOG_WARNING, "pes_ext %X is invalid\n", pes_ext);
                pes_ext=skip=0;
            }
            avio_skip(s->pb, skip);
            header_len -= skip;

            if (pes_ext & 0x01) { /* PES extension 2 */
                ext2_len = avio_r8(s->pb);
                header_len--;
                if ((ext2_len & 0x7f) > 0) {
                    id_ext = avio_r8(s->pb);
                    if ((id_ext & 0x80) == 0)
                        startcode = ((startcode & 0xff) << 8) | id_ext;
                    header_len--;
                }
            }
        }
        if(header_len < 0)
            goto error_redo;
        avio_skip(s->pb, header_len);
    }
    else if( c!= 0xf )
        goto redo;

    if (startcode == PRIVATE_STREAM_1 && !m->psm_es_type[startcode & 0xff]) {
        startcode = avio_r8(s->pb);
        len--;
        if (startcode >= 0x80 && startcode <= 0xcf) {
            /* audio: skip header */
            avio_r8(s->pb);
            avio_r8(s->pb);
            avio_r8(s->pb);
            len -= 3;
            if (startcode >= 0xb0 && startcode <= 0xbf) {
                /* MLP/TrueHD audio has a 4-byte header */
                avio_r8(s->pb);
                len--;
            }
        }
    }
    if(len<0)
        goto error_redo;
    if(dts != AV_NOPTS_VALUE && ppos){
        int i;
        for(i=0; i<s->nb_streams; i++){
            if(startcode == s->streams[i]->id &&
               s->pb->seekable /* index useless on streams anyway */) {
                ff_reduce_index(s, i);
                av_add_index_entry(s->streams[i], *ppos, dts, 0, 0, AVINDEX_KEYFRAME /* FIXME keyframe? */);
            }
        }
    }

    *pstart_code = startcode;
    *ppts = pts;
    *pdts = dts;
    return len;
}

static int mpegps_read_packet(AVFormatContext *s,
                              AVPacket *pkt)
{
    MpegDemuxContext *m = s->priv_data;
    AVStream *st;
    int len, startcode, i, es_type, ret;
    int request_probe= 0;
    enum CodecID codec_id = CODEC_ID_NONE;
    enum AVMediaType type;
    int64_t pts, dts, dummy_pos; //dummy_pos is needed for the index building to work
    uint8_t av_uninit(dvdaudio_substream_type);

 redo:
    len = mpegps_read_pes_header(s, &dummy_pos, &startcode, &pts, &dts);
    if (len < 0)
        return len;

    if(startcode == 0x1bd) {
        dvdaudio_substream_type = avio_r8(s->pb);
        avio_skip(s->pb, 3);
        len -= 4;
    }

    /* now find stream */
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        if (st->id == startcode)
            goto found;
    }

    es_type = m->psm_es_type[startcode & 0xff];
    if(es_type > 0 && es_type != STREAM_TYPE_PRIVATE_DATA){
        if(es_type == STREAM_TYPE_VIDEO_MPEG1){
            codec_id = CODEC_ID_MPEG2VIDEO;
            type = AVMEDIA_TYPE_VIDEO;
        } else if(es_type == STREAM_TYPE_VIDEO_MPEG2){
            codec_id = CODEC_ID_MPEG2VIDEO;
            type = AVMEDIA_TYPE_VIDEO;
        } else if(es_type == STREAM_TYPE_AUDIO_MPEG1 ||
                  es_type == STREAM_TYPE_AUDIO_MPEG2){
            codec_id = CODEC_ID_MP3;
            type = AVMEDIA_TYPE_AUDIO;
        } else if(es_type == STREAM_TYPE_AUDIO_AAC){
            codec_id = CODEC_ID_AAC;
            type = AVMEDIA_TYPE_AUDIO;
        } else if(es_type == STREAM_TYPE_VIDEO_MPEG4){
            codec_id = CODEC_ID_MPEG4;
            type = AVMEDIA_TYPE_VIDEO;
        } else if(es_type == STREAM_TYPE_VIDEO_H264){
            codec_id = CODEC_ID_H264;
            type = AVMEDIA_TYPE_VIDEO;
        } else if(es_type == STREAM_TYPE_AUDIO_AC3){
            codec_id = CODEC_ID_AC3;
            type = AVMEDIA_TYPE_AUDIO;
        } else {
            goto skip;
        }
    } else if (startcode >= 0x1e0 && startcode <= 0x1ef) {
        static const unsigned char avs_seqh[4] = { 0, 0, 1, 0xb0 };
        unsigned char buf[8];
        avio_read(s->pb, buf, 8);
        avio_seek(s->pb, -8, SEEK_CUR);
        if(!memcmp(buf, avs_seqh, 4) && (buf[6] != 0 || buf[7] != 1))
            codec_id = CODEC_ID_CAVS;
        else
            request_probe= 1;
        type = AVMEDIA_TYPE_VIDEO;
    } else if (startcode >= 0x1c0 && startcode <= 0x1df) {
        type = AVMEDIA_TYPE_AUDIO;
        codec_id = m->sofdec > 0 ? CODEC_ID_ADPCM_ADX : CODEC_ID_MP2;
    } else if (startcode >= 0x80 && startcode <= 0x87) {
        type = AVMEDIA_TYPE_AUDIO;
        codec_id = CODEC_ID_AC3;
    } else if (  ( startcode >= 0x88 && startcode <= 0x8f)
               ||( startcode >= 0x98 && startcode <= 0x9f)) {
        /* 0x90 - 0x97 is reserved for SDDS in DVD specs */
        type = AVMEDIA_TYPE_AUDIO;
        codec_id = CODEC_ID_DTS;
    } else if (startcode >= 0xa0 && startcode <= 0xaf) {
        type = AVMEDIA_TYPE_AUDIO;
        /* 16 bit form will be handled as CODEC_ID_PCM_S16BE */
        codec_id = CODEC_ID_PCM_DVD;
    } else if (startcode >= 0xb0 && startcode <= 0xbf) {
        type = AVMEDIA_TYPE_AUDIO;
        codec_id = CODEC_ID_TRUEHD;
    } else if (startcode >= 0xc0 && startcode <= 0xcf) {
        /* Used for both AC-3 and E-AC-3 in EVOB files */
        type = AVMEDIA_TYPE_AUDIO;
        codec_id = CODEC_ID_AC3;
    } else if (startcode >= 0x20 && startcode <= 0x3f) {
        type = AVMEDIA_TYPE_SUBTITLE;
        codec_id = CODEC_ID_DVD_SUBTITLE;
    } else if (startcode >= 0xfd55 && startcode <= 0xfd5f) {
        type = AVMEDIA_TYPE_VIDEO;
        codec_id = CODEC_ID_VC1;
    } else if (startcode == 0x1bd) {
        // check dvd audio substream type
        type = AVMEDIA_TYPE_AUDIO;
        switch(dvdaudio_substream_type & 0xe0) {
        case 0xa0:  codec_id = CODEC_ID_PCM_DVD;
                    break;
        case 0x80:  if((dvdaudio_substream_type & 0xf8) == 0x88)
                         codec_id = CODEC_ID_DTS;
                    else codec_id = CODEC_ID_AC3;
                    break;
        default:    av_log(s, AV_LOG_ERROR, "Unknown 0x1bd sub-stream\n");
                    goto skip;
        }
    } else {
    skip:
        /* skip packet */
        avio_skip(s->pb, len);
        goto redo;
    }
    /* no stream found: add a new stream */
    st = avformat_new_stream(s, NULL);
    if (!st)
        goto skip;
    st->id = startcode;
    st->codec->codec_type = type;
    st->codec->codec_id = codec_id;
    st->request_probe     = request_probe;
    if (codec_id != CODEC_ID_PCM_S16BE)
        st->need_parsing = AVSTREAM_PARSE_FULL;
 found:
    if(st->discard >= AVDISCARD_ALL)
        goto skip;
    if ((startcode >= 0xa0 && startcode <= 0xaf) ||
        (startcode == 0x1bd && ((dvdaudio_substream_type & 0xe0) == 0xa0))) {
        int b1, freq;

        /* for LPCM, we just skip the header and consider it is raw
           audio data */
        if (len <= 3)
            goto skip;
        avio_r8(s->pb); /* emphasis (1), muse(1), reserved(1), frame number(5) */
        b1 = avio_r8(s->pb); /* quant (2), freq(2), reserved(1), channels(3) */
        avio_r8(s->pb); /* dynamic range control (0x80 = off) */
        len -= 3;
        freq = (b1 >> 4) & 3;
        st->codec->sample_rate = lpcm_freq_tab[freq];
        st->codec->channels = 1 + (b1 & 7);
        st->codec->bits_per_coded_sample = 16 + ((b1 >> 6) & 3) * 4;
        st->codec->bit_rate = st->codec->channels *
                              st->codec->sample_rate *
                              st->codec->bits_per_coded_sample;
        if (st->codec->bits_per_coded_sample == 16)
            st->codec->codec_id = CODEC_ID_PCM_S16BE;
        else if (st->codec->bits_per_coded_sample == 28)
            return AVERROR(EINVAL);
    }
    ret = av_get_packet(s->pb, pkt, len);
    pkt->pts = pts;
    pkt->dts = dts;
    pkt->pos = dummy_pos;
    pkt->stream_index = st->index;
    av_dlog(s, "%d: pts=%0.3f dts=%0.3f size=%d\n",
            pkt->stream_index, pkt->pts / 90000.0, pkt->dts / 90000.0,
            pkt->size);

    return (ret < 0) ? ret : 0;
}

static int64_t mpegps_read_dts(AVFormatContext *s, int stream_index,
                               int64_t *ppos, int64_t pos_limit)
{
    int len, startcode;
    int64_t pos, pts, dts;

    pos = *ppos;
    if (avio_seek(s->pb, pos, SEEK_SET) < 0)
        return AV_NOPTS_VALUE;

    for(;;) {
        len = mpegps_read_pes_header(s, &pos, &startcode, &pts, &dts);
        if (len < 0) {
            av_dlog(s, "none (ret=%d)\n", len);
            return AV_NOPTS_VALUE;
        }
        if (startcode == s->streams[stream_index]->id &&
            dts != AV_NOPTS_VALUE) {
            break;
        }
        avio_skip(s->pb, len);
    }
    av_dlog(s, "pos=0x%"PRIx64" dts=0x%"PRIx64" %0.3f\n",
            pos, dts, dts / 90000.0);
    *ppos = pos;
    return dts;
}

AVInputFormat ff_mpegps_demuxer = {
    .name           = "mpeg",
    .long_name      = NULL_IF_CONFIG_SMALL("MPEG-PS format"),
    .priv_data_size = sizeof(MpegDemuxContext),
    .read_probe     = mpegps_probe,
    .read_header    = mpegps_read_header,
    .read_packet    = mpegps_read_packet,
    .read_timestamp = mpegps_read_dts,
    .flags = AVFMT_SHOW_IDS|AVFMT_TS_DISCONT,
};
