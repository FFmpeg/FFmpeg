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

#if CONFIG_VOBSUB_DEMUXER
# include "subtitles.h"
# include "libavutil/bprint.h"
#endif

#undef NDEBUG
#include <assert.h>
#include "libavutil/avassert.h"

/*********************************************/
/* demux code */

#define MAX_SYNC_SIZE 100000

static int check_pes(const uint8_t *p, const uint8_t *end){
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
        score = AVPROBE_SCORE_EXTENSION / 2;

    if(sys>invalid && sys*9 <= pspack*10)
        return (audio > 12 || vid > 3 || pspack > 2) ? AVPROBE_SCORE_EXTENSION + 2 : AVPROBE_SCORE_EXTENSION / 2; // 1 more than .mpg
    if(pspack > invalid && (priv1+vid+audio)*10 >= pspack*9)
        return pspack > 2 ? AVPROBE_SCORE_EXTENSION + 2 : AVPROBE_SCORE_EXTENSION / 2; // 1 more than .mpg
    if((!!vid ^ !!audio) && (audio > 4 || vid > 1) && !sys && !pspack && p->buf_size>2048 && vid + audio > invalid) /* PES stream */
        return (audio > 12 || vid > 3 + 2*invalid) ? AVPROBE_SCORE_EXTENSION + 2 : AVPROBE_SCORE_EXTENSION / 2;

    //02-Penguin.flac has sys:0 priv1:0 pspack:0 vid:0 audio:1
    //mp3_misidentified_2.mp3 has sys:0 priv1:0 pspack:0 vid:0 audio:6
    //Have\ Yourself\ a\ Merry\ Little\ Christmas.mp3 0 0 0 5 0 1 len:21618
    return score;
}


typedef struct MpegDemuxContext {
    int32_t header_state;
    unsigned char psm_es_type[256];
    int sofdec;
    int dvd;
    int imkh_cctv;
#if CONFIG_VOBSUB_DEMUXER
    AVFormatContext *sub_ctx;
    FFDemuxSubtitlesQueue q[32];
#endif
} MpegDemuxContext;

static int mpegps_read_header(AVFormatContext *s)
{
    MpegDemuxContext *m = s->priv_data;
    char buffer[7];
    int64_t last_pos = avio_tell(s->pb);

    m->header_state = 0xff;
    s->ctx_flags |= AVFMTCTX_NOHEADER;

    avio_get_str(s->pb, 6, buffer, sizeof(buffer));
    if (!memcmp("IMKH", buffer, 4)) {
        m->imkh_cctv = 1;
    } else if (!memcmp("Sofdec", buffer, 6)) {
        m->sofdec = 1;
    } else
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
        if (!m->sofdec) {
            /* Need to detect whether this from a DVD or a 'Sofdec' stream */
            int len = avio_rb16(s->pb);
            int bytesread = 0;
            uint8_t *ps2buf = av_malloc(len);

            if (ps2buf) {
                bytesread = avio_read(s->pb, ps2buf, len);

                if (bytesread != len) {
                    avio_skip(s->pb, len - bytesread);
                } else {
                    uint8_t *p = 0;
                    if (len >= 6)
                        p = memchr(ps2buf, 'S', len - 5);

                    if (p)
                        m->sofdec = !memcmp(p+1, "ofdec", 5);

                    m->sofdec -= !m->sofdec;

                    if (m->sofdec < 0) {
                        if (len == 980  && ps2buf[0] == 0) {
                            /* PCI structure? */
                            uint32_t startpts = AV_RB32(ps2buf + 0x0d);
                            uint32_t endpts = AV_RB32(ps2buf + 0x11);
                            uint8_t hours = ((ps2buf[0x19] >> 4) * 10) + (ps2buf[0x19] & 0x0f);
                            uint8_t mins  = ((ps2buf[0x1a] >> 4) * 10) + (ps2buf[0x1a] & 0x0f);
                            uint8_t secs  = ((ps2buf[0x1b] >> 4) * 10) + (ps2buf[0x1b] & 0x0f);

                            m->dvd = (hours <= 23 &&
                                      mins  <= 59 &&
                                      secs  <= 59 &&
                                      (ps2buf[0x19] & 0x0f) < 10 &&
                                      (ps2buf[0x1a] & 0x0f) < 10 &&
                                      (ps2buf[0x1b] & 0x0f) < 10 &&
                                      endpts >= startpts);
                        } else if (len == 1018 && ps2buf[0] == 1) {
                            /* DSI structure? */
                            uint8_t hours = ((ps2buf[0x1d] >> 4) * 10) + (ps2buf[0x1d] & 0x0f);
                            uint8_t mins  = ((ps2buf[0x1e] >> 4) * 10) + (ps2buf[0x1e] & 0x0f);
                            uint8_t secs  = ((ps2buf[0x1f] >> 4) * 10) + (ps2buf[0x1f] & 0x0f);

                            m->dvd = (hours <= 23 &&
                                      mins  <= 59 &&
                                      secs  <= 59 &&
                                      (ps2buf[0x1d] & 0x0f) < 10 &&
                                      (ps2buf[0x1e] & 0x0f) < 10 &&
                                      (ps2buf[0x1f] & 0x0f) < 10);
                        }
                    }
                }

                av_free(ps2buf);

                /* If this isn't a DVD packet or no memory
                 * could be allocated, just ignore it.
                 * If we did, move back to the start of the
                 * packet (plus 'length' field) */
                if (!m->dvd || avio_skip(s->pb, -(len + 2)) < 0) {
                    /* Skip back failed.
                     * This packet will be lost but that can't be helped
                     * if we can't skip back
                     */
                    goto redo;
                }
            } else {
                /* No memory */
                avio_skip(s->pb, len);
                goto redo;
            }
        } else if (!m->dvd) {
            int len = avio_rb16(s->pb);
            avio_skip(s->pb, len);
            goto redo;
        }
    }
    if (startcode == PROGRAM_STREAM_MAP) {
        mpegps_psm_parse(m, s->pb);
        goto redo;
    }

    /* find matching stream */
    if (!((startcode >= 0x1c0 && startcode <= 0x1df) ||
          (startcode >= 0x1e0 && startcode <= 0x1ef) ||
          (startcode == 0x1bd) ||
          (startcode == PRIVATE_STREAM_2) ||
          (startcode == 0x1fd)))
        goto redo;
    if (ppos) {
        *ppos = avio_tell(s->pb) - 4;
    }
    len = avio_rb16(s->pb);
    pts =
    dts = AV_NOPTS_VALUE;
    if (startcode != PRIVATE_STREAM_2)
    {
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
    }

    if (startcode == PRIVATE_STREAM_1) {
        startcode = avio_r8(s->pb);
        len--;
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
    int lpcm_header_len = -1; //Init to supress warning
    int request_probe= 0;
    enum AVCodecID codec_id = AV_CODEC_ID_NONE;
    enum AVMediaType type;
    int64_t pts, dts, dummy_pos; //dummy_pos is needed for the index building to work

 redo:
    len = mpegps_read_pes_header(s, &dummy_pos, &startcode, &pts, &dts);
    if (len < 0)
        return len;

    if (startcode >= 0x80 && startcode <= 0xcf) {
        if(len < 4)
            goto skip;

        /* audio: skip header */
        avio_r8(s->pb);
        lpcm_header_len = avio_rb16(s->pb);
        len -= 3;
        if (startcode >= 0xb0 && startcode <= 0xbf) {
            /* MLP/TrueHD audio has a 4-byte header */
            avio_r8(s->pb);
            len--;
        }
    }

    /* now find stream */
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        if (st->id == startcode)
            goto found;
    }

    es_type = m->psm_es_type[startcode & 0xff];
        if(es_type == STREAM_TYPE_VIDEO_MPEG1){
            codec_id = AV_CODEC_ID_MPEG2VIDEO;
            type = AVMEDIA_TYPE_VIDEO;
        } else if(es_type == STREAM_TYPE_VIDEO_MPEG2){
            codec_id = AV_CODEC_ID_MPEG2VIDEO;
            type = AVMEDIA_TYPE_VIDEO;
        } else if(es_type == STREAM_TYPE_AUDIO_MPEG1 ||
                  es_type == STREAM_TYPE_AUDIO_MPEG2){
            codec_id = AV_CODEC_ID_MP3;
            type = AVMEDIA_TYPE_AUDIO;
        } else if(es_type == STREAM_TYPE_AUDIO_AAC){
            codec_id = AV_CODEC_ID_AAC;
            type = AVMEDIA_TYPE_AUDIO;
        } else if(es_type == STREAM_TYPE_VIDEO_MPEG4){
            codec_id = AV_CODEC_ID_MPEG4;
            type = AVMEDIA_TYPE_VIDEO;
        } else if(es_type == STREAM_TYPE_VIDEO_H264){
            codec_id = AV_CODEC_ID_H264;
            type = AVMEDIA_TYPE_VIDEO;
        } else if(es_type == STREAM_TYPE_AUDIO_AC3){
            codec_id = AV_CODEC_ID_AC3;
            type = AVMEDIA_TYPE_AUDIO;
        } else if(m->imkh_cctv && es_type == 0x91){
            codec_id = AV_CODEC_ID_PCM_MULAW;
            type = AVMEDIA_TYPE_AUDIO;
    } else if (startcode >= 0x1e0 && startcode <= 0x1ef) {
        static const unsigned char avs_seqh[4] = { 0, 0, 1, 0xb0 };
        unsigned char buf[8];
        avio_read(s->pb, buf, 8);
        avio_seek(s->pb, -8, SEEK_CUR);
        if(!memcmp(buf, avs_seqh, 4) && (buf[6] != 0 || buf[7] != 1))
            codec_id = AV_CODEC_ID_CAVS;
        else
            request_probe= 1;
        type = AVMEDIA_TYPE_VIDEO;
    } else if (startcode == PRIVATE_STREAM_2) {
        type = AVMEDIA_TYPE_DATA;
        codec_id = AV_CODEC_ID_DVD_NAV;
    } else if (startcode >= 0x1c0 && startcode <= 0x1df) {
        type = AVMEDIA_TYPE_AUDIO;
        codec_id = m->sofdec > 0 ? AV_CODEC_ID_ADPCM_ADX : AV_CODEC_ID_MP2;
    } else if (startcode >= 0x80 && startcode <= 0x87) {
        type = AVMEDIA_TYPE_AUDIO;
        codec_id = AV_CODEC_ID_AC3;
    } else if (  ( startcode >= 0x88 && startcode <= 0x8f)
               ||( startcode >= 0x98 && startcode <= 0x9f)) {
        /* 0x90 - 0x97 is reserved for SDDS in DVD specs */
        type = AVMEDIA_TYPE_AUDIO;
        codec_id = AV_CODEC_ID_DTS;
    } else if (startcode >= 0xa0 && startcode <= 0xaf) {
        type = AVMEDIA_TYPE_AUDIO;
        if(lpcm_header_len == 6) {
            codec_id = AV_CODEC_ID_MLP;
        } else {
            codec_id = AV_CODEC_ID_PCM_DVD;
        }
    } else if (startcode >= 0xb0 && startcode <= 0xbf) {
        type = AVMEDIA_TYPE_AUDIO;
        codec_id = AV_CODEC_ID_TRUEHD;
    } else if (startcode >= 0xc0 && startcode <= 0xcf) {
        /* Used for both AC-3 and E-AC-3 in EVOB files */
        type = AVMEDIA_TYPE_AUDIO;
        codec_id = AV_CODEC_ID_AC3;
    } else if (startcode >= 0x20 && startcode <= 0x3f) {
        type = AVMEDIA_TYPE_SUBTITLE;
        codec_id = AV_CODEC_ID_DVD_SUBTITLE;
    } else if (startcode >= 0xfd55 && startcode <= 0xfd5f) {
        type = AVMEDIA_TYPE_VIDEO;
        codec_id = AV_CODEC_ID_VC1;
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
    if (st->codec->codec_id == AV_CODEC_ID_PCM_MULAW) {
        st->codec->channels = 1;
        st->codec->channel_layout = AV_CH_LAYOUT_MONO;
        st->codec->sample_rate = 8000;
    }
    st->request_probe     = request_probe;
    st->need_parsing = AVSTREAM_PARSE_FULL;
 found:
    if(st->discard >= AVDISCARD_ALL)
        goto skip;
    if (startcode >= 0xa0 && startcode <= 0xaf) {
      if (lpcm_header_len == 6 && st->codec->codec_id == AV_CODEC_ID_MLP) {
            if (len < 6)
                goto skip;
            avio_skip(s->pb, 6);
            len -=6;
      }
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
    .long_name      = NULL_IF_CONFIG_SMALL("MPEG-PS (MPEG-2 Program Stream)"),
    .priv_data_size = sizeof(MpegDemuxContext),
    .read_probe     = mpegps_probe,
    .read_header    = mpegps_read_header,
    .read_packet    = mpegps_read_packet,
    .read_timestamp = mpegps_read_dts,
    .flags          = AVFMT_SHOW_IDS | AVFMT_TS_DISCONT,
};

#if CONFIG_VOBSUB_DEMUXER

#define REF_STRING "# VobSub index file,"

static int vobsub_probe(AVProbeData *p)
{
    if (!strncmp(p->buf, REF_STRING, sizeof(REF_STRING) - 1))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int vobsub_read_header(AVFormatContext *s)
{
    int i, ret = 0, header_parsed = 0, langidx = 0;
    MpegDemuxContext *vobsub = s->priv_data;
    char *sub_name = NULL;
    size_t fname_len;
    char *ext, *header_str;
    AVBPrint header;
    int64_t delay = 0;
    AVStream *st = NULL;

    sub_name = av_strdup(s->filename);
    fname_len = strlen(sub_name);
    ext = sub_name - 3 + fname_len;
    if (fname_len < 4 || *(ext - 1) != '.') {
        av_log(s, AV_LOG_ERROR, "The input index filename is too short "
               "to guess the associated .SUB file\n");
        ret = AVERROR_INVALIDDATA;
        goto end;
    }
    memcpy(ext, !strncmp(ext, "IDX", 3) ? "SUB" : "sub", 3);
    av_log(s, AV_LOG_VERBOSE, "IDX/SUB: %s -> %s\n", s->filename, sub_name);
    ret = avformat_open_input(&vobsub->sub_ctx, sub_name, &ff_mpegps_demuxer, NULL);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Unable to open %s as MPEG subtitles\n", sub_name);
        goto end;
    }

    av_bprint_init(&header, 0, AV_BPRINT_SIZE_UNLIMITED);
    while (!url_feof(s->pb)) {
        char line[2048];
        int len = ff_get_line(s->pb, line, sizeof(line));

        if (!len)
            break;

        line[strcspn(line, "\r\n")] = 0;

        if (!strncmp(line, "id:", 3)) {
            int n, stream_id = 0;
            char id[64] = {0};

            n = sscanf(line, "id: %63[^,], index: %u", id, &stream_id);
            if (n != 2) {
                av_log(s, AV_LOG_WARNING, "Unable to parse index line '%s', "
                       "assuming 'id: und, index: 0'\n", line);
                strcpy(id, "und");
                stream_id = 0;
            }

            if (stream_id >= FF_ARRAY_ELEMS(vobsub->q)) {
                av_log(s, AV_LOG_ERROR, "Maximum number of subtitles streams reached\n");
                ret = AVERROR(EINVAL);
                goto end;
            }

            st = avformat_new_stream(s, NULL);
            if (!st) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
            st->id = stream_id;
            st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
            st->codec->codec_id   = AV_CODEC_ID_DVD_SUBTITLE;
            avpriv_set_pts_info(st, 64, 1, 1000);
            av_dict_set(&st->metadata, "language", id, 0);
            av_log(s, AV_LOG_DEBUG, "IDX stream[%d] id=%s\n", stream_id, id);
            header_parsed = 1;

        } else if (st && !strncmp(line, "timestamp:", 10)) {
            AVPacket *sub;
            int hh, mm, ss, ms;
            int64_t pos, timestamp;
            const char *p = line + 10;

            if (!s->nb_streams) {
                av_log(s, AV_LOG_ERROR, "Timestamp declared before any stream\n");
                ret = AVERROR_INVALIDDATA;
                goto end;
            }

            if (sscanf(p, "%02d:%02d:%02d:%03d, filepos: %"SCNx64,
                       &hh, &mm, &ss, &ms, &pos) != 5) {
                av_log(s, AV_LOG_ERROR, "Unable to parse timestamp line '%s', "
                       "abort parsing\n", line);
                break;
            }
            timestamp = (hh*3600LL + mm*60LL + ss) * 1000LL + ms + delay;
            timestamp = av_rescale_q(timestamp, (AVRational){1,1000}, st->time_base);

            sub = ff_subtitles_queue_insert(&vobsub->q[s->nb_streams - 1], "", 0, 0);
            if (!sub) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
            sub->pos = pos;
            sub->pts = timestamp;
            sub->stream_index = s->nb_streams - 1;

        } else if (st && !strncmp(line, "alt:", 4)) {
            const char *p = line + 4;

            while (*p == ' ')
                p++;
            av_dict_set(&st->metadata, "title", p, 0);
            av_log(s, AV_LOG_DEBUG, "IDX stream[%d] name=%s\n", st->id, p);
            header_parsed = 1;

        } else if (!strncmp(line, "delay:", 6)) {
            int sign = 1, hh = 0, mm = 0, ss = 0, ms = 0;
            const char *p = line + 6;

            while (*p == ' ')
                p++;
            if (*p == '-' || *p == '+') {
                sign = *p == '-' ? -1 : 1;
                p++;
            }
            sscanf(p, "%d:%d:%d:%d", &hh, &mm, &ss, &ms);
            delay = ((hh*3600LL + mm*60LL + ss) * 1000LL + ms) * sign;

        } else if (!strncmp(line, "langidx:", 8)) {
            const char *p = line + 8;

            if (sscanf(p, "%d", &langidx) != 1)
                av_log(s, AV_LOG_ERROR, "Invalid langidx specified\n");

        } else if (!header_parsed) {
            if (line[0] && line[0] != '#')
                av_bprintf(&header, "%s\n", line);
        }
    }

    if (langidx < s->nb_streams)
        s->streams[langidx]->disposition |= AV_DISPOSITION_DEFAULT;

    for (i = 0; i < s->nb_streams; i++) {
        vobsub->q[i].sort = SUB_SORT_POS_TS;
        ff_subtitles_queue_finalize(&vobsub->q[i]);
    }

    if (!av_bprint_is_complete(&header)) {
        av_bprint_finalize(&header, NULL);
        ret = AVERROR(ENOMEM);
        goto end;
    }
    av_bprint_finalize(&header, &header_str);
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *sub_st = s->streams[i];
        sub_st->codec->extradata      = av_strdup(header_str);
        sub_st->codec->extradata_size = header.len;
    }
    av_free(header_str);

end:
    av_free(sub_name);
    return ret;
}

#define FAIL(r) do { ret = r; goto fail; } while (0)

static int vobsub_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MpegDemuxContext *vobsub = s->priv_data;
    FFDemuxSubtitlesQueue *q;
    AVIOContext *pb = vobsub->sub_ctx->pb;
    int ret, psize, total_read = 0, i;
    AVPacket idx_pkt;

    int64_t min_ts = INT64_MAX;
    int sid = 0;
    for (i = 0; i < s->nb_streams; i++) {
        FFDemuxSubtitlesQueue *tmpq = &vobsub->q[i];
        int64_t ts = tmpq->subs[tmpq->current_sub_idx].pts;
        if (ts < min_ts) {
            min_ts = ts;
            sid = i;
        }
    }
    q = &vobsub->q[sid];
    ret = ff_subtitles_queue_read_packet(q, &idx_pkt);
    if (ret < 0)
        return ret;

    /* compute maximum packet size using the next packet position. This is
     * useful when the len in the header is non-sense */
    if (q->current_sub_idx < q->nb_subs) {
        psize = q->subs[q->current_sub_idx].pos - idx_pkt.pos;
    } else {
        int64_t fsize = avio_size(pb);
        psize = fsize < 0 ? 0xffff : fsize - idx_pkt.pos;
    }

    avio_seek(pb, idx_pkt.pos, SEEK_SET);

    av_init_packet(pkt);
    pkt->size = 0;
    pkt->data = NULL;

    do {
        int n, to_read, startcode;
        int64_t pts, dts;
        int64_t old_pos = avio_tell(pb), new_pos;
        int pkt_size;

        ret = mpegps_read_pes_header(vobsub->sub_ctx, NULL, &startcode, &pts, &dts);
        if (ret < 0) {
            if (pkt->size) // raise packet even if incomplete
                break;
            FAIL(ret);
        }
        to_read = ret & 0xffff;
        new_pos = avio_tell(pb);
        pkt_size = ret + (new_pos - old_pos);

        /* this prevents reads above the current packet */
        if (total_read + pkt_size > psize)
            break;
        total_read += pkt_size;

        /* the current chunk doesn't match the stream index (unlikely) */
        if ((startcode & 0x1f) != idx_pkt.stream_index)
            break;

        ret = av_grow_packet(pkt, to_read);
        if (ret < 0)
            FAIL(ret);

        n = avio_read(pb, pkt->data + (pkt->size - to_read), to_read);
        if (n < to_read)
            pkt->size -= to_read - n;
    } while (total_read < psize);

    pkt->pts = pkt->dts = idx_pkt.pts;
    pkt->pos = idx_pkt.pos;
    pkt->stream_index = idx_pkt.stream_index;

    av_free_packet(&idx_pkt);
    return 0;

fail:
    av_free_packet(pkt);
    av_free_packet(&idx_pkt);
    return ret;
}

static int vobsub_read_seek(AVFormatContext *s, int stream_index,
                            int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    MpegDemuxContext *vobsub = s->priv_data;

    /* Rescale requested timestamps based on the first stream (timebase is the
     * same for all subtitles stream within a .idx/.sub). Rescaling is done just
     * like in avformat_seek_file(). */
    if (stream_index == -1 && s->nb_streams != 1) {
        int i, ret = 0;
        AVRational time_base = s->streams[0]->time_base;
        ts = av_rescale_q(ts, AV_TIME_BASE_Q, time_base);
        min_ts = av_rescale_rnd(min_ts, time_base.den,
                                time_base.num * (int64_t)AV_TIME_BASE,
                                AV_ROUND_UP   | AV_ROUND_PASS_MINMAX);
        max_ts = av_rescale_rnd(max_ts, time_base.den,
                                time_base.num * (int64_t)AV_TIME_BASE,
                                AV_ROUND_DOWN | AV_ROUND_PASS_MINMAX);
        for (i = 0; i < s->nb_streams; i++) {
            int r = ff_subtitles_queue_seek(&vobsub->q[i], s, stream_index,
                                            min_ts, ts, max_ts, flags);
            if (r < 0)
                ret = r;
        }
        return ret;
    }

    return ff_subtitles_queue_seek(&vobsub->q[stream_index], s, stream_index,
                                   min_ts, ts, max_ts, flags);
}

static int vobsub_read_close(AVFormatContext *s)
{
    int i;
    MpegDemuxContext *vobsub = s->priv_data;

    for (i = 0; i < s->nb_streams; i++)
        ff_subtitles_queue_clean(&vobsub->q[i]);
    if (vobsub->sub_ctx)
        avformat_close_input(&vobsub->sub_ctx);
    return 0;
}

AVInputFormat ff_vobsub_demuxer = {
    .name           = "vobsub",
    .long_name      = NULL_IF_CONFIG_SMALL("VobSub subtitle format"),
    .priv_data_size = sizeof(MpegDemuxContext),
    .read_probe     = vobsub_probe,
    .read_header    = vobsub_read_header,
    .read_packet    = vobsub_read_packet,
    .read_seek2     = vobsub_read_seek,
    .read_close     = vobsub_read_close,
    .flags          = AVFMT_SHOW_IDS,
    .extensions     = "idx",
};
#endif
