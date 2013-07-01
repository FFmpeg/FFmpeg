/*
 * MPEG2 transport stream (aka DVB) muxer
 * Copyright (c) 2003 Fabrice Bellard
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

#include "libavutil/bswap.h"
#include "libavutil/crc.h"
#include "libavutil/dict.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"
#include "libavcodec/internal.h"
#include "avformat.h"
#include "internal.h"
#include "mpegts.h"

#define PCR_TIME_BASE 27000000

/* write DVB SI sections */

/*********************************************/
/* mpegts section writer */

typedef struct MpegTSSection {
    int pid;
    int cc;
    void (*write_packet)(struct MpegTSSection *s, const uint8_t *packet);
    void *opaque;
} MpegTSSection;

typedef struct MpegTSService {
    MpegTSSection pmt; /* MPEG2 pmt table context */
    int sid;           /* service ID */
    char *name;
    char *provider_name;
    int pcr_pid;
    int pcr_packet_count;
    int pcr_packet_period;
} MpegTSService;

typedef struct MpegTSWrite {
    const AVClass *av_class;
    MpegTSSection pat; /* MPEG2 pat table */
    MpegTSSection sdt; /* MPEG2 sdt table context */
    MpegTSService **services;
    int sdt_packet_count;
    int sdt_packet_period;
    int pat_packet_count;
    int pat_packet_period;
    int nb_services;
    int onid;
    int tsid;
    int64_t first_pcr;
    int mux_rate; ///< set to 1 when VBR
    int pes_payload_size;

    int transport_stream_id;
    int original_network_id;
    int service_id;

    int pmt_start_pid;
    int start_pid;
    int m2ts_mode;

    int reemit_pat_pmt; // backward compatibility

#define MPEGTS_FLAG_REEMIT_PAT_PMT  0x01
#define MPEGTS_FLAG_AAC_LATM        0x02
    int flags;
    int copyts;
} MpegTSWrite;

/* a PES packet header is generated every DEFAULT_PES_HEADER_FREQ packets */
#define DEFAULT_PES_HEADER_FREQ 16
#define DEFAULT_PES_PAYLOAD_SIZE ((DEFAULT_PES_HEADER_FREQ - 1) * 184 + 170)

static const AVOption options[] = {
    { "mpegts_transport_stream_id", "Set transport_stream_id field.",
      offsetof(MpegTSWrite, transport_stream_id), AV_OPT_TYPE_INT, {.i64 = 0x0001 }, 0x0001, 0xffff, AV_OPT_FLAG_ENCODING_PARAM},
    { "mpegts_original_network_id", "Set original_network_id field.",
      offsetof(MpegTSWrite, original_network_id), AV_OPT_TYPE_INT, {.i64 = 0x0001 }, 0x0001, 0xffff, AV_OPT_FLAG_ENCODING_PARAM},
    { "mpegts_service_id", "Set service_id field.",
      offsetof(MpegTSWrite, service_id), AV_OPT_TYPE_INT, {.i64 = 0x0001 }, 0x0001, 0xffff, AV_OPT_FLAG_ENCODING_PARAM},
    { "mpegts_pmt_start_pid", "Set the first pid of the PMT.",
      offsetof(MpegTSWrite, pmt_start_pid), AV_OPT_TYPE_INT, {.i64 = 0x1000 }, 0x0010, 0x1f00, AV_OPT_FLAG_ENCODING_PARAM},
    { "mpegts_start_pid", "Set the first pid.",
      offsetof(MpegTSWrite, start_pid), AV_OPT_TYPE_INT, {.i64 = 0x0100 }, 0x0100, 0x0f00, AV_OPT_FLAG_ENCODING_PARAM},
    {"mpegts_m2ts_mode", "Enable m2ts mode.",
        offsetof(MpegTSWrite, m2ts_mode), AV_OPT_TYPE_INT, {.i64 = -1 },
        -1,1, AV_OPT_FLAG_ENCODING_PARAM},
    { "muxrate", NULL, offsetof(MpegTSWrite, mux_rate), AV_OPT_TYPE_INT, {.i64 = 1}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
    { "pes_payload_size", "Minimum PES packet payload in bytes",
      offsetof(MpegTSWrite, pes_payload_size), AV_OPT_TYPE_INT, {.i64 = DEFAULT_PES_PAYLOAD_SIZE}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
    { "mpegts_flags", "MPEG-TS muxing flags", offsetof(MpegTSWrite, flags), AV_OPT_TYPE_FLAGS, {.i64 = 0}, 0, INT_MAX,
      AV_OPT_FLAG_ENCODING_PARAM, "mpegts_flags" },
    { "resend_headers", "Reemit PAT/PMT before writing the next packet",
      0, AV_OPT_TYPE_CONST, {.i64 = MPEGTS_FLAG_REEMIT_PAT_PMT}, 0, INT_MAX,
      AV_OPT_FLAG_ENCODING_PARAM, "mpegts_flags"},
    { "latm", "Use LATM packetization for AAC",
      0, AV_OPT_TYPE_CONST, {.i64 = MPEGTS_FLAG_AAC_LATM}, 0, INT_MAX,
      AV_OPT_FLAG_ENCODING_PARAM, "mpegts_flags"},
    // backward compatibility
    { "resend_headers", "Reemit PAT/PMT before writing the next packet",
      offsetof(MpegTSWrite, reemit_pat_pmt), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
    { "mpegts_copyts", "don't offset dts/pts",
      offsetof(MpegTSWrite, copyts), AV_OPT_TYPE_INT, {.i64=-1}, -1, 1, AV_OPT_FLAG_ENCODING_PARAM},
    { NULL },
};

static const AVClass mpegts_muxer_class = {
    .class_name     = "MPEGTS muxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

/* NOTE: 4 bytes must be left at the end for the crc32 */
static void mpegts_write_section(MpegTSSection *s, uint8_t *buf, int len)
{
    unsigned int crc;
    unsigned char packet[TS_PACKET_SIZE];
    const unsigned char *buf_ptr;
    unsigned char *q;
    int first, b, len1, left;

    crc = av_bswap32(av_crc(av_crc_get_table(AV_CRC_32_IEEE), -1, buf, len - 4));
    buf[len - 4] = (crc >> 24) & 0xff;
    buf[len - 3] = (crc >> 16) & 0xff;
    buf[len - 2] = (crc >> 8) & 0xff;
    buf[len - 1] = (crc) & 0xff;

    /* send each packet */
    buf_ptr = buf;
    while (len > 0) {
        first = (buf == buf_ptr);
        q = packet;
        *q++ = 0x47;
        b = (s->pid >> 8);
        if (first)
            b |= 0x40;
        *q++ = b;
        *q++ = s->pid;
        s->cc = (s->cc + 1) & 0xf;
        *q++ = 0x10 | s->cc;
        if (first)
            *q++ = 0; /* 0 offset */
        len1 = TS_PACKET_SIZE - (q - packet);
        if (len1 > len)
            len1 = len;
        memcpy(q, buf_ptr, len1);
        q += len1;
        /* add known padding data */
        left = TS_PACKET_SIZE - (q - packet);
        if (left > 0)
            memset(q, 0xff, left);

        s->write_packet(s, packet);

        buf_ptr += len1;
        len -= len1;
    }
}

static inline void put16(uint8_t **q_ptr, int val)
{
    uint8_t *q;
    q = *q_ptr;
    *q++ = val >> 8;
    *q++ = val;
    *q_ptr = q;
}

static int mpegts_write_section1(MpegTSSection *s, int tid, int id,
                          int version, int sec_num, int last_sec_num,
                          uint8_t *buf, int len)
{
    uint8_t section[1024], *q;
    unsigned int tot_len;
    /* reserved_future_use field must be set to 1 for SDT */
    unsigned int flags = tid == SDT_TID ? 0xf000 : 0xb000;

    tot_len = 3 + 5 + len + 4;
    /* check if not too big */
    if (tot_len > 1024)
        return AVERROR_INVALIDDATA;

    q = section;
    *q++ = tid;
    put16(&q, flags | (len + 5 + 4)); /* 5 byte header + 4 byte CRC */
    put16(&q, id);
    *q++ = 0xc1 | (version << 1); /* current_next_indicator = 1 */
    *q++ = sec_num;
    *q++ = last_sec_num;
    memcpy(q, buf, len);

    mpegts_write_section(s, section, tot_len);
    return 0;
}

/*********************************************/
/* mpegts writer */

#define DEFAULT_PROVIDER_NAME   "FFmpeg"
#define DEFAULT_SERVICE_NAME    "Service01"

/* we retransmit the SI info at this rate */
#define SDT_RETRANS_TIME 500
#define PAT_RETRANS_TIME 100
#define PCR_RETRANS_TIME 20

typedef struct MpegTSWriteStream {
    struct MpegTSService *service;
    int pid; /* stream associated pid */
    int cc;
    int payload_size;
    int first_pts_check; ///< first pts check needed
    int prev_payload_key;
    int64_t payload_pts;
    int64_t payload_dts;
    int payload_flags;
    uint8_t *payload;
    AVFormatContext *amux;
} MpegTSWriteStream;

static void mpegts_write_pat(AVFormatContext *s)
{
    MpegTSWrite *ts = s->priv_data;
    MpegTSService *service;
    uint8_t data[1012], *q;
    int i;

    q = data;
    for(i = 0; i < ts->nb_services; i++) {
        service = ts->services[i];
        put16(&q, service->sid);
        put16(&q, 0xe000 | service->pmt.pid);
    }
    mpegts_write_section1(&ts->pat, PAT_TID, ts->tsid, 0, 0, 0,
                          data, q - data);
}

static void mpegts_write_pmt(AVFormatContext *s, MpegTSService *service)
{
    MpegTSWrite *ts = s->priv_data;
    uint8_t data[1012], *q, *desc_length_ptr, *program_info_length_ptr;
    int val, stream_type, i;

    q = data;
    put16(&q, 0xe000 | service->pcr_pid);

    program_info_length_ptr = q;
    q += 2; /* patched after */

    /* put program info here */

    val = 0xf000 | (q - program_info_length_ptr - 2);
    program_info_length_ptr[0] = val >> 8;
    program_info_length_ptr[1] = val;

    for(i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MpegTSWriteStream *ts_st = st->priv_data;
        AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", NULL,0);
        switch(st->codec->codec_id) {
        case AV_CODEC_ID_MPEG1VIDEO:
        case AV_CODEC_ID_MPEG2VIDEO:
            stream_type = STREAM_TYPE_VIDEO_MPEG2;
            break;
        case AV_CODEC_ID_MPEG4:
            stream_type = STREAM_TYPE_VIDEO_MPEG4;
            break;
        case AV_CODEC_ID_H264:
            stream_type = STREAM_TYPE_VIDEO_H264;
            break;
        case AV_CODEC_ID_CAVS:
            stream_type = STREAM_TYPE_VIDEO_CAVS;
            break;
        case AV_CODEC_ID_DIRAC:
            stream_type = STREAM_TYPE_VIDEO_DIRAC;
            break;
        case AV_CODEC_ID_MP2:
        case AV_CODEC_ID_MP3:
            stream_type = STREAM_TYPE_AUDIO_MPEG1;
            break;
        case AV_CODEC_ID_AAC:
            stream_type = (ts->flags & MPEGTS_FLAG_AAC_LATM) ? STREAM_TYPE_AUDIO_AAC_LATM : STREAM_TYPE_AUDIO_AAC;
            break;
        case AV_CODEC_ID_AAC_LATM:
            stream_type = STREAM_TYPE_AUDIO_AAC_LATM;
            break;
        case AV_CODEC_ID_AC3:
            stream_type = STREAM_TYPE_AUDIO_AC3;
            break;
        default:
            stream_type = STREAM_TYPE_PRIVATE_DATA;
            break;
        }
        *q++ = stream_type;
        put16(&q, 0xe000 | ts_st->pid);
        desc_length_ptr = q;
        q += 2; /* patched after */

        /* write optional descriptors here */
        switch(st->codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            if(st->codec->codec_id==AV_CODEC_ID_EAC3){
                *q++=0x7a; // EAC3 descriptor see A038 DVB SI
                *q++=1; // 1 byte, all flags sets to 0
                *q++=0; // omit all fields...
            }
            if(st->codec->codec_id==AV_CODEC_ID_S302M){
                *q++ = 0x05; /* MPEG-2 registration descriptor*/
                *q++ = 4;
                *q++ = 'B';
                *q++ = 'S';
                *q++ = 'S';
                *q++ = 'D';
            }

            if (lang) {
                char *p;
                char *next = lang->value;
                uint8_t *len_ptr;

                *q++ = 0x0a; /* ISO 639 language descriptor */
                len_ptr = q++;
                *len_ptr = 0;

                for (p = lang->value; next && *len_ptr < 255 / 4 * 4; p = next + 1) {
                    next = strchr(p, ',');
                    if (strlen(p) != 3 && (!next || next != p + 3))
                        continue; /* not a 3-letter code */

                    *q++ = *p++;
                    *q++ = *p++;
                    *q++ = *p++;

                if (st->disposition & AV_DISPOSITION_CLEAN_EFFECTS)
                    *q++ = 0x01;
                else if (st->disposition & AV_DISPOSITION_HEARING_IMPAIRED)
                    *q++ = 0x02;
                else if (st->disposition & AV_DISPOSITION_VISUAL_IMPAIRED)
                    *q++ = 0x03;
                else
                    *q++ = 0; /* undefined type */

                    *len_ptr += 4;
                }

                if (*len_ptr == 0)
                    q -= 2; /* no language codes were written */
            }
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            {
                const char *language;
                language = lang && strlen(lang->value)==3 ? lang->value : "eng";
                *q++ = 0x59;
                *q++ = 8;
                *q++ = language[0];
                *q++ = language[1];
                *q++ = language[2];
                *q++ = 0x10; /* normal subtitles (0x20 = if hearing pb) */
                if(st->codec->extradata_size == 4) {
                    memcpy(q, st->codec->extradata, 4);
                    q += 4;
                } else {
                    put16(&q, 1); /* page id */
                    put16(&q, 1); /* ancillary page id */
                }
            }
            break;
        case AVMEDIA_TYPE_VIDEO:
            if (stream_type == STREAM_TYPE_VIDEO_DIRAC) {
                *q++ = 0x05; /*MPEG-2 registration descriptor*/
                *q++ = 4;
                *q++ = 'd';
                *q++ = 'r';
                *q++ = 'a';
                *q++ = 'c';
            }
            break;
        case AVMEDIA_TYPE_DATA:
            if (st->codec->codec_id == AV_CODEC_ID_SMPTE_KLV) {
                *q++ = 0x05; /* MPEG-2 registration descriptor */
                *q++ = 4;
                *q++ = 'K';
                *q++ = 'L';
                *q++ = 'V';
                *q++ = 'A';
            }
            break;
        }

        val = 0xf000 | (q - desc_length_ptr - 2);
        desc_length_ptr[0] = val >> 8;
        desc_length_ptr[1] = val;
    }
    mpegts_write_section1(&service->pmt, PMT_TID, service->sid, 0, 0, 0,
                          data, q - data);
}

/* NOTE: str == NULL is accepted for an empty string */
static void putstr8(uint8_t **q_ptr, const char *str)
{
    uint8_t *q;
    int len;

    q = *q_ptr;
    if (!str)
        len = 0;
    else
        len = strlen(str);
    *q++ = len;
    memcpy(q, str, len);
    q += len;
    *q_ptr = q;
}

static void mpegts_write_sdt(AVFormatContext *s)
{
    MpegTSWrite *ts = s->priv_data;
    MpegTSService *service;
    uint8_t data[1012], *q, *desc_list_len_ptr, *desc_len_ptr;
    int i, running_status, free_ca_mode, val;

    q = data;
    put16(&q, ts->onid);
    *q++ = 0xff;
    for(i = 0; i < ts->nb_services; i++) {
        service = ts->services[i];
        put16(&q, service->sid);
        *q++ = 0xfc | 0x00; /* currently no EIT info */
        desc_list_len_ptr = q;
        q += 2;
        running_status = 4; /* running */
        free_ca_mode = 0;

        /* write only one descriptor for the service name and provider */
        *q++ = 0x48;
        desc_len_ptr = q;
        q++;
        *q++ = 0x01; /* digital television service */
        putstr8(&q, service->provider_name);
        putstr8(&q, service->name);
        desc_len_ptr[0] = q - desc_len_ptr - 1;

        /* fill descriptor length */
        val = (running_status << 13) | (free_ca_mode << 12) |
            (q - desc_list_len_ptr - 2);
        desc_list_len_ptr[0] = val >> 8;
        desc_list_len_ptr[1] = val;
    }
    mpegts_write_section1(&ts->sdt, SDT_TID, ts->tsid, 0, 0, 0,
                          data, q - data);
}

static MpegTSService *mpegts_add_service(MpegTSWrite *ts,
                                         int sid,
                                         const char *provider_name,
                                         const char *name)
{
    MpegTSService *service;

    service = av_mallocz(sizeof(MpegTSService));
    if (!service)
        return NULL;
    service->pmt.pid = ts->pmt_start_pid + ts->nb_services;
    service->sid = sid;
    service->provider_name = av_strdup(provider_name);
    service->name = av_strdup(name);
    service->pcr_pid = 0x1fff;
    dynarray_add(&ts->services, &ts->nb_services, service);
    return service;
}

static int64_t get_pcr(const MpegTSWrite *ts, AVIOContext *pb)
{
    return av_rescale(avio_tell(pb) + 11, 8 * PCR_TIME_BASE, ts->mux_rate) +
           ts->first_pcr;
}

static void mpegts_prefix_m2ts_header(AVFormatContext *s)
{
    MpegTSWrite *ts = s->priv_data;
    if (ts->m2ts_mode) {
        int64_t pcr = get_pcr(s->priv_data, s->pb);
        uint32_t tp_extra_header = pcr % 0x3fffffff;
        tp_extra_header = AV_RB32(&tp_extra_header);
        avio_write(s->pb, (unsigned char *) &tp_extra_header,
                sizeof(tp_extra_header));
    }
}

static void section_write_packet(MpegTSSection *s, const uint8_t *packet)
{
    AVFormatContext *ctx = s->opaque;
    mpegts_prefix_m2ts_header(ctx);
    avio_write(ctx->pb, packet, TS_PACKET_SIZE);
}

static int mpegts_write_header(AVFormatContext *s)
{
    MpegTSWrite *ts = s->priv_data;
    MpegTSWriteStream *ts_st;
    MpegTSService *service;
    AVStream *st, *pcr_st = NULL;
    AVDictionaryEntry *title, *provider;
    int i, j;
    const char *service_name;
    const char *provider_name;
    int *pids;
    int ret;

    if (s->max_delay < 0) /* Not set by the caller */
        s->max_delay = 0;

    // round up to a whole number of TS packets
    ts->pes_payload_size = (ts->pes_payload_size + 14 + 183) / 184 * 184 - 14;

    ts->tsid = ts->transport_stream_id;
    ts->onid = ts->original_network_id;
    /* allocate a single DVB service */
    title = av_dict_get(s->metadata, "service_name", NULL, 0);
    if (!title)
        title = av_dict_get(s->metadata, "title", NULL, 0);
    service_name = title ? title->value : DEFAULT_SERVICE_NAME;
    provider = av_dict_get(s->metadata, "service_provider", NULL, 0);
    provider_name = provider ? provider->value : DEFAULT_PROVIDER_NAME;
    service = mpegts_add_service(ts, ts->service_id, provider_name, service_name);
    service->pmt.write_packet = section_write_packet;
    service->pmt.opaque = s;
    service->pmt.cc = 15;

    ts->pat.pid = PAT_PID;
    ts->pat.cc = 15; // Initialize at 15 so that it wraps and be equal to 0 for the first packet we write
    ts->pat.write_packet = section_write_packet;
    ts->pat.opaque = s;

    ts->sdt.pid = SDT_PID;
    ts->sdt.cc = 15;
    ts->sdt.write_packet = section_write_packet;
    ts->sdt.opaque = s;

    pids = av_malloc(s->nb_streams * sizeof(*pids));
    if (!pids)
        return AVERROR(ENOMEM);

    /* assign pids to each stream */
    for(i = 0;i < s->nb_streams; i++) {
        st = s->streams[i];
        avpriv_set_pts_info(st, 33, 1, 90000);
        ts_st = av_mallocz(sizeof(MpegTSWriteStream));
        if (!ts_st) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        st->priv_data = ts_st;
        ts_st->payload = av_mallocz(ts->pes_payload_size);
        if (!ts_st->payload) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        ts_st->service = service;
        /* MPEG pid values < 16 are reserved. Applications which set st->id in
         * this range are assigned a calculated pid. */
        if (st->id < 16) {
            ts_st->pid = ts->start_pid + i;
        } else if (st->id < 0x1FFF) {
            ts_st->pid = st->id;
        } else {
            av_log(s, AV_LOG_ERROR, "Invalid stream id %d, must be less than 8191\n", st->id);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        if (ts_st->pid == service->pmt.pid) {
            av_log(s, AV_LOG_ERROR, "Duplicate stream id %d\n", ts_st->pid);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        for (j = 0; j < i; j++)
            if (pids[j] == ts_st->pid) {
                av_log(s, AV_LOG_ERROR, "Duplicate stream id %d\n", ts_st->pid);
                ret = AVERROR(EINVAL);
                goto fail;
            }
        pids[i] = ts_st->pid;
        ts_st->payload_pts = AV_NOPTS_VALUE;
        ts_st->payload_dts = AV_NOPTS_VALUE;
        ts_st->first_pts_check = 1;
        ts_st->cc = 15;
        /* update PCR pid by using the first video stream */
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
            service->pcr_pid == 0x1fff) {
            service->pcr_pid = ts_st->pid;
            pcr_st = st;
        }
        if (st->codec->codec_id == AV_CODEC_ID_AAC &&
            st->codec->extradata_size > 0)
        {
            AVStream *ast;
            ts_st->amux = avformat_alloc_context();
            if (!ts_st->amux) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            ts_st->amux->oformat = av_guess_format((ts->flags & MPEGTS_FLAG_AAC_LATM) ? "latm" : "adts", NULL, NULL);
            if (!ts_st->amux->oformat) {
                ret = AVERROR(EINVAL);
                goto fail;
            }
            ast = avformat_new_stream(ts_st->amux, NULL);
            ret = avcodec_copy_context(ast->codec, st->codec);
            if (ret != 0)
                goto fail;
            ret = avformat_write_header(ts_st->amux, NULL);
            if (ret < 0)
                goto fail;
        }
    }

    av_free(pids);

    /* if no video stream, use the first stream as PCR */
    if (service->pcr_pid == 0x1fff && s->nb_streams > 0) {
        pcr_st = s->streams[0];
        ts_st = pcr_st->priv_data;
        service->pcr_pid = ts_st->pid;
    }

    if (ts->mux_rate > 1) {
        service->pcr_packet_period = (ts->mux_rate * PCR_RETRANS_TIME) /
            (TS_PACKET_SIZE * 8 * 1000);
        ts->sdt_packet_period      = (ts->mux_rate * SDT_RETRANS_TIME) /
            (TS_PACKET_SIZE * 8 * 1000);
        ts->pat_packet_period      = (ts->mux_rate * PAT_RETRANS_TIME) /
            (TS_PACKET_SIZE * 8 * 1000);

        if(ts->copyts < 1)
            ts->first_pcr = av_rescale(s->max_delay, PCR_TIME_BASE, AV_TIME_BASE);
    } else {
        /* Arbitrary values, PAT/PMT will also be written on video key frames */
        ts->sdt_packet_period = 200;
        ts->pat_packet_period = 40;
        if (pcr_st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (!pcr_st->codec->frame_size) {
                av_log(s, AV_LOG_WARNING, "frame size not set\n");
                service->pcr_packet_period =
                    pcr_st->codec->sample_rate/(10*512);
            } else {
                service->pcr_packet_period =
                    pcr_st->codec->sample_rate/(10*pcr_st->codec->frame_size);
            }
        } else {
            // max delta PCR 0.1s
            service->pcr_packet_period =
                pcr_st->codec->time_base.den/(10*pcr_st->codec->time_base.num);
        }
        if(!service->pcr_packet_period)
            service->pcr_packet_period = 1;
    }

    // output a PCR as soon as possible
    service->pcr_packet_count = service->pcr_packet_period;
    ts->pat_packet_count = ts->pat_packet_period-1;
    ts->sdt_packet_count = ts->sdt_packet_period-1;

    if (ts->mux_rate == 1)
        av_log(s, AV_LOG_VERBOSE, "muxrate VBR, ");
    else
        av_log(s, AV_LOG_VERBOSE, "muxrate %d, ", ts->mux_rate);
    av_log(s, AV_LOG_VERBOSE, "pcr every %d pkts, "
           "sdt every %d, pat/pmt every %d pkts\n",
           service->pcr_packet_period,
           ts->sdt_packet_period, ts->pat_packet_period);

    if (ts->m2ts_mode == -1) {
        if (av_match_ext(s->filename, "m2ts")) {
            ts->m2ts_mode = 1;
        } else {
            ts->m2ts_mode = 0;
        }
    }

    avio_flush(s->pb);

    return 0;

 fail:
    av_free(pids);
    for(i = 0;i < s->nb_streams; i++) {
        MpegTSWriteStream *ts_st;
        st = s->streams[i];
        ts_st = st->priv_data;
        if (ts_st) {
            av_freep(&ts_st->payload);
            if (ts_st->amux) {
                avformat_free_context(ts_st->amux);
                ts_st->amux = NULL;
            }
        }
        av_freep(&st->priv_data);
    }
    return ret;
}

/* send SDT, PAT and PMT tables regulary */
static void retransmit_si_info(AVFormatContext *s, int force_pat)
{
    MpegTSWrite *ts = s->priv_data;
    int i;

    if (++ts->sdt_packet_count == ts->sdt_packet_period) {
        ts->sdt_packet_count = 0;
        mpegts_write_sdt(s);
    }
    if (++ts->pat_packet_count == ts->pat_packet_period || force_pat) {
        ts->pat_packet_count = 0;
        mpegts_write_pat(s);
        for(i = 0; i < ts->nb_services; i++) {
            mpegts_write_pmt(s, ts->services[i]);
        }
    }
}

static int write_pcr_bits(uint8_t *buf, int64_t pcr)
{
    int64_t pcr_low = pcr % 300, pcr_high = pcr / 300;

    *buf++ = pcr_high >> 25;
    *buf++ = pcr_high >> 17;
    *buf++ = pcr_high >> 9;
    *buf++ = pcr_high >> 1;
    *buf++ = pcr_high << 7 | pcr_low >> 8 | 0x7e;
    *buf++ = pcr_low;

    return 6;
}

/* Write a single null transport stream packet */
static void mpegts_insert_null_packet(AVFormatContext *s)
{
    uint8_t *q;
    uint8_t buf[TS_PACKET_SIZE];

    q = buf;
    *q++ = 0x47;
    *q++ = 0x00 | 0x1f;
    *q++ = 0xff;
    *q++ = 0x10;
    memset(q, 0x0FF, TS_PACKET_SIZE - (q - buf));
    mpegts_prefix_m2ts_header(s);
    avio_write(s->pb, buf, TS_PACKET_SIZE);
}

/* Write a single transport stream packet with a PCR and no payload */
static void mpegts_insert_pcr_only(AVFormatContext *s, AVStream *st)
{
    MpegTSWrite *ts = s->priv_data;
    MpegTSWriteStream *ts_st = st->priv_data;
    uint8_t *q;
    uint8_t buf[TS_PACKET_SIZE];

    q = buf;
    *q++ = 0x47;
    *q++ = ts_st->pid >> 8;
    *q++ = ts_st->pid;
    *q++ = 0x20 | ts_st->cc;   /* Adaptation only */
    /* Continuity Count field does not increment (see 13818-1 section 2.4.3.3) */
    *q++ = TS_PACKET_SIZE - 5; /* Adaptation Field Length */
    *q++ = 0x10;               /* Adaptation flags: PCR present */

    /* PCR coded into 6 bytes */
    q += write_pcr_bits(q, get_pcr(ts, s->pb));

    /* stuffing bytes */
    memset(q, 0xFF, TS_PACKET_SIZE - (q - buf));
    mpegts_prefix_m2ts_header(s);
    avio_write(s->pb, buf, TS_PACKET_SIZE);
}

static void write_pts(uint8_t *q, int fourbits, int64_t pts)
{
    int val;

    val = fourbits << 4 | (((pts >> 30) & 0x07) << 1) | 1;
    *q++ = val;
    val = (((pts >> 15) & 0x7fff) << 1) | 1;
    *q++ = val >> 8;
    *q++ = val;
    val = (((pts) & 0x7fff) << 1) | 1;
    *q++ = val >> 8;
    *q++ = val;
}

/* Set an adaptation field flag in an MPEG-TS packet*/
static void set_af_flag(uint8_t *pkt, int flag)
{
    // expect at least one flag to set
    av_assert0(flag);

    if ((pkt[3] & 0x20) == 0) {
        // no AF yet, set adaptation field flag
        pkt[3] |= 0x20;
        // 1 byte length, no flags
        pkt[4] = 1;
        pkt[5] = 0;
    }
    pkt[5] |= flag;
}

/* Extend the adaptation field by size bytes */
static void extend_af(uint8_t *pkt, int size)
{
    // expect already existing adaptation field
    av_assert0(pkt[3] & 0x20);
    pkt[4] += size;
}

/* Get a pointer to MPEG-TS payload (right after TS packet header) */
static uint8_t *get_ts_payload_start(uint8_t *pkt)
{
    if (pkt[3] & 0x20)
        return pkt + 5 + pkt[4];
    else
        return pkt + 4;
}

/* Add a pes header to the front of payload, and segment into an integer number of
 * ts packets. The final ts packet is padded using an over-sized adaptation header
 * to exactly fill the last ts packet.
 * NOTE: 'payload' contains a complete PES payload.
 */
static void mpegts_write_pes(AVFormatContext *s, AVStream *st,
                             const uint8_t *payload, int payload_size,
                             int64_t pts, int64_t dts, int key)
{
    MpegTSWriteStream *ts_st = st->priv_data;
    MpegTSWrite *ts = s->priv_data;
    uint8_t buf[TS_PACKET_SIZE];
    uint8_t *q;
    int val, is_start, len, header_len, write_pcr, private_code, flags;
    int afc_len, stuffing_len;
    int64_t pcr = -1; /* avoid warning */
    int64_t delay = av_rescale(s->max_delay, 90000, AV_TIME_BASE);
    int force_pat = st->codec->codec_type == AVMEDIA_TYPE_VIDEO && key && !ts_st->prev_payload_key;

    is_start = 1;
    while (payload_size > 0) {
        retransmit_si_info(s, force_pat);
        force_pat = 0;

        write_pcr = 0;
        if (ts_st->pid == ts_st->service->pcr_pid) {
            if (ts->mux_rate > 1 || is_start) // VBR pcr period is based on frames
                ts_st->service->pcr_packet_count++;
            if (ts_st->service->pcr_packet_count >=
                ts_st->service->pcr_packet_period) {
                ts_st->service->pcr_packet_count = 0;
                write_pcr = 1;
            }
        }

        if (ts->mux_rate > 1 && dts != AV_NOPTS_VALUE &&
            (dts - get_pcr(ts, s->pb)/300) > delay) {
            /* pcr insert gets priority over null packet insert */
            if (write_pcr)
                mpegts_insert_pcr_only(s, st);
            else
                mpegts_insert_null_packet(s);
            continue; /* recalculate write_pcr and possibly retransmit si_info */
        }

        /* prepare packet header */
        q = buf;
        *q++ = 0x47;
        val = (ts_st->pid >> 8);
        if (is_start)
            val |= 0x40;
        *q++ = val;
        *q++ = ts_st->pid;
        ts_st->cc = (ts_st->cc + 1) & 0xf;
        *q++ = 0x10 | ts_st->cc; // payload indicator + CC
        if (key && is_start && pts != AV_NOPTS_VALUE) {
            // set Random Access for key frames
            if (ts_st->pid == ts_st->service->pcr_pid)
                write_pcr = 1;
            set_af_flag(buf, 0x40);
            q = get_ts_payload_start(buf);
        }
        if (write_pcr) {
            set_af_flag(buf, 0x10);
            q = get_ts_payload_start(buf);
            // add 11, pcr references the last byte of program clock reference base
            if (ts->mux_rate > 1)
                pcr = get_pcr(ts, s->pb);
            else
                pcr = (dts - delay)*300;
            if (dts != AV_NOPTS_VALUE && dts < pcr / 300)
                av_log(s, AV_LOG_WARNING, "dts < pcr, TS is invalid\n");
            extend_af(buf, write_pcr_bits(q, pcr));
            q = get_ts_payload_start(buf);
        }
        if (is_start) {
            int pes_extension = 0;
            /* write PES header */
            *q++ = 0x00;
            *q++ = 0x00;
            *q++ = 0x01;
            private_code = 0;
            if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
                if (st->codec->codec_id == AV_CODEC_ID_DIRAC) {
                    *q++ = 0xfd;
                } else
                    *q++ = 0xe0;
            } else if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
                       (st->codec->codec_id == AV_CODEC_ID_MP2 ||
                        st->codec->codec_id == AV_CODEC_ID_MP3 ||
                        st->codec->codec_id == AV_CODEC_ID_AAC)) {
                *q++ = 0xc0;
            } else if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
                        st->codec->codec_id == AV_CODEC_ID_AC3 &&
                        ts->m2ts_mode) {
                *q++ = 0xfd;
            } else {
                *q++ = 0xbd;
                if (st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                    private_code = 0x20;
                }
            }
            header_len = 0;
            flags = 0;
            if (pts != AV_NOPTS_VALUE) {
                header_len += 5;
                flags |= 0x80;
            }
            if (dts != AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE && dts != pts) {
                header_len += 5;
                flags |= 0x40;
            }
            if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
                st->codec->codec_id == AV_CODEC_ID_DIRAC) {
                /* set PES_extension_flag */
                pes_extension = 1;
                flags |= 0x01;

                /*
                * One byte for PES2 extension flag +
                * one byte for extension length +
                * one byte for extension id
                */
                header_len += 3;
            }
            /* for Blu-ray AC3 Audio the PES Extension flag should be as follow
             * otherwise it will not play sound on blu-ray
             */
            if (ts->m2ts_mode &&
                st->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
                st->codec->codec_id == AV_CODEC_ID_AC3) {
                        /* set PES_extension_flag */
                        pes_extension = 1;
                        flags |= 0x01;
                        header_len += 3;
            }
            len = payload_size + header_len + 3;
            if (private_code != 0)
                len++;
            if (len > 0xffff)
                len = 0;
            *q++ = len >> 8;
            *q++ = len;
            val = 0x80;
            /* data alignment indicator is required for subtitle and data streams */
            if (st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE || st->codec->codec_type == AVMEDIA_TYPE_DATA)
                val |= 0x04;
            *q++ = val;
            *q++ = flags;
            *q++ = header_len;
            if (pts != AV_NOPTS_VALUE) {
                write_pts(q, flags >> 6, pts);
                q += 5;
            }
            if (dts != AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE && dts != pts) {
                write_pts(q, 1, dts);
                q += 5;
            }
            if (pes_extension && st->codec->codec_id == AV_CODEC_ID_DIRAC) {
                flags = 0x01;  /* set PES_extension_flag_2 */
                *q++ = flags;
                *q++ = 0x80 | 0x01;  /* marker bit + extension length */
                /*
                * Set the stream id extension flag bit to 0 and
                * write the extended stream id
                */
                *q++ = 0x00 | 0x60;
            }
            /* For Blu-ray AC3 Audio Setting extended flags */
          if (ts->m2ts_mode &&
              pes_extension &&
              st->codec->codec_id == AV_CODEC_ID_AC3) {
                      flags = 0x01; /* set PES_extension_flag_2 */
                      *q++ = flags;
                      *q++ = 0x80 | 0x01; /* marker bit + extension length */
                      *q++ = 0x00 | 0x71; /* for AC3 Audio (specifically on blue-rays) */
              }


            if (private_code != 0)
                *q++ = private_code;
            is_start = 0;
        }
        /* header size */
        header_len = q - buf;
        /* data len */
        len = TS_PACKET_SIZE - header_len;
        if (len > payload_size)
            len = payload_size;
        stuffing_len = TS_PACKET_SIZE - header_len - len;
        if (stuffing_len > 0) {
            /* add stuffing with AFC */
            if (buf[3] & 0x20) {
                /* stuffing already present: increase its size */
                afc_len = buf[4] + 1;
                memmove(buf + 4 + afc_len + stuffing_len,
                        buf + 4 + afc_len,
                        header_len - (4 + afc_len));
                buf[4] += stuffing_len;
                memset(buf + 4 + afc_len, 0xff, stuffing_len);
            } else {
                /* add stuffing */
                memmove(buf + 4 + stuffing_len, buf + 4, header_len - 4);
                buf[3] |= 0x20;
                buf[4] = stuffing_len - 1;
                if (stuffing_len >= 2) {
                    buf[5] = 0x00;
                    memset(buf + 6, 0xff, stuffing_len - 2);
                }
            }
        }
        memcpy(buf + TS_PACKET_SIZE - len, payload, len);
        payload += len;
        payload_size -= len;
        mpegts_prefix_m2ts_header(s);
        avio_write(s->pb, buf, TS_PACKET_SIZE);
    }
    avio_flush(s->pb);
    ts_st->prev_payload_key = key;
}

static int mpegts_write_packet_internal(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[pkt->stream_index];
    int size = pkt->size;
    uint8_t *buf= pkt->data;
    uint8_t *data= NULL;
    MpegTSWrite *ts = s->priv_data;
    MpegTSWriteStream *ts_st = st->priv_data;
    const int64_t delay = av_rescale(s->max_delay, 90000, AV_TIME_BASE)*2;
    int64_t dts = pkt->dts, pts = pkt->pts;

    if (ts->reemit_pat_pmt) {
        av_log(s, AV_LOG_WARNING, "resend_headers option is deprecated, use -mpegts_flags resend_headers\n");
        ts->reemit_pat_pmt = 0;
        ts->flags |= MPEGTS_FLAG_REEMIT_PAT_PMT;
    }

    if (ts->flags & MPEGTS_FLAG_REEMIT_PAT_PMT) {
        ts->pat_packet_count = ts->pat_packet_period - 1;
        ts->sdt_packet_count = ts->sdt_packet_period - 1;
        ts->flags &= ~MPEGTS_FLAG_REEMIT_PAT_PMT;
    }

    if(ts->copyts < 1){
        if (pts != AV_NOPTS_VALUE)
            pts += delay;
        if (dts != AV_NOPTS_VALUE)
            dts += delay;
    }

    if (ts_st->first_pts_check && pts == AV_NOPTS_VALUE) {
        av_log(s, AV_LOG_ERROR, "first pts value must be set\n");
        return AVERROR_INVALIDDATA;
    }
    ts_st->first_pts_check = 0;

    if (st->codec->codec_id == AV_CODEC_ID_H264) {
        const uint8_t *p = buf, *buf_end = p+size;
        uint32_t state = -1;

        if (pkt->size < 5 || AV_RB32(pkt->data) != 0x0000001) {
            if (!st->nb_frames) {
            av_log(s, AV_LOG_ERROR, "H.264 bitstream malformed, "
                   "no startcode found, use the h264_mp4toannexb bitstream filter (-bsf h264_mp4toannexb)\n");
            return AVERROR(EINVAL);
            }
            av_log(s, AV_LOG_WARNING, "H.264 bitstream error, startcode missing\n");
        }

        do {
            p = avpriv_find_start_code(p, buf_end, &state);
            av_dlog(s, "nal %d\n", state & 0x1f);
        } while (p < buf_end && (state & 0x1f) != 9 &&
                 (state & 0x1f) != 5 && (state & 0x1f) != 1);

        if ((state & 0x1f) != 9) { // AUD NAL
            data = av_malloc(pkt->size+6);
            if (!data)
                return AVERROR(ENOMEM);
            memcpy(data+6, pkt->data, pkt->size);
            AV_WB32(data, 0x00000001);
            data[4] = 0x09;
            data[5] = 0xf0; // any slice type (0xe) + rbsp stop one bit
            buf  = data;
            size = pkt->size+6;
        }
    } else if (st->codec->codec_id == AV_CODEC_ID_AAC) {
        if (pkt->size < 2) {
            av_log(s, AV_LOG_ERROR, "AAC packet too short\n");
            return AVERROR_INVALIDDATA;
        }
        if ((AV_RB16(pkt->data) & 0xfff0) != 0xfff0) {
            int ret;
            AVPacket pkt2;

            if (!ts_st->amux) {
                av_log(s, AV_LOG_ERROR, "AAC bitstream not in ADTS format "
                       "and extradata missing\n");
                return AVERROR_INVALIDDATA;
            }

            av_init_packet(&pkt2);
            pkt2.data = pkt->data;
            pkt2.size = pkt->size;
            ret = avio_open_dyn_buf(&ts_st->amux->pb);
            if (ret < 0)
                return AVERROR(ENOMEM);

            ret = av_write_frame(ts_st->amux, &pkt2);
            if (ret < 0) {
                avio_close_dyn_buf(ts_st->amux->pb, &data);
                ts_st->amux->pb = NULL;
                av_free(data);
                return ret;
            }
            size = avio_close_dyn_buf(ts_st->amux->pb, &data);
            ts_st->amux->pb = NULL;
            buf = data;
        }
    }

    if (pkt->dts != AV_NOPTS_VALUE) {
        int i;
        for(i=0; i<s->nb_streams; i++){
            AVStream *st2 = s->streams[i];
            MpegTSWriteStream *ts_st2 = st2->priv_data;
            if(   ts_st2->payload_size
               && (ts_st2->payload_dts == AV_NOPTS_VALUE || dts - ts_st2->payload_dts > delay/2)){
                mpegts_write_pes(s, st2, ts_st2->payload, ts_st2->payload_size,
                                ts_st2->payload_pts, ts_st2->payload_dts,
                                ts_st2->payload_flags & AV_PKT_FLAG_KEY);
                ts_st2->payload_size = 0;
            }
        }
    }

    if (ts_st->payload_size && ts_st->payload_size + size > ts->pes_payload_size) {
        mpegts_write_pes(s, st, ts_st->payload, ts_st->payload_size,
                         ts_st->payload_pts, ts_st->payload_dts,
                         ts_st->payload_flags & AV_PKT_FLAG_KEY);
        ts_st->payload_size = 0;
    }

    if (st->codec->codec_type != AVMEDIA_TYPE_AUDIO || size > ts->pes_payload_size) {
        av_assert0(!ts_st->payload_size);
        // for video and subtitle, write a single pes packet
        mpegts_write_pes(s, st, buf, size, pts, dts, pkt->flags & AV_PKT_FLAG_KEY);
        av_free(data);
        return 0;
    }

    if (!ts_st->payload_size) {
        ts_st->payload_pts = pts;
        ts_st->payload_dts = dts;
        ts_st->payload_flags = pkt->flags;
    }

    memcpy(ts_st->payload + ts_st->payload_size, buf, size);
    ts_st->payload_size += size;

    av_free(data);

    return 0;
}

static void mpegts_write_flush(AVFormatContext *s)
{
    int i;

    /* flush current packets */
    for(i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MpegTSWriteStream *ts_st = st->priv_data;
        if (ts_st->payload_size > 0) {
            mpegts_write_pes(s, st, ts_st->payload, ts_st->payload_size,
                             ts_st->payload_pts, ts_st->payload_dts,
                             ts_st->payload_flags & AV_PKT_FLAG_KEY);
            ts_st->payload_size = 0;
        }
    }
    avio_flush(s->pb);
}

static int mpegts_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    if (!pkt) {
        mpegts_write_flush(s);
        return 1;
    } else {
        return mpegts_write_packet_internal(s, pkt);
    }
}

static int mpegts_write_end(AVFormatContext *s)
{
    MpegTSWrite *ts = s->priv_data;
    MpegTSService *service;
    int i;

    mpegts_write_flush(s);

    for(i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MpegTSWriteStream *ts_st = st->priv_data;
        av_freep(&ts_st->payload);
        if (ts_st->amux) {
            avformat_free_context(ts_st->amux);
            ts_st->amux = NULL;
        }
    }

    for(i = 0; i < ts->nb_services; i++) {
        service = ts->services[i];
        av_freep(&service->provider_name);
        av_freep(&service->name);
        av_free(service);
    }
    av_free(ts->services);

    return 0;
}

AVOutputFormat ff_mpegts_muxer = {
    .name              = "mpegts",
    .long_name         = NULL_IF_CONFIG_SMALL("MPEG-TS (MPEG-2 Transport Stream)"),
    .mime_type         = "video/x-mpegts",
    .extensions        = "ts,m2t,m2ts,mts",
    .priv_data_size    = sizeof(MpegTSWrite),
    .audio_codec       = AV_CODEC_ID_MP2,
    .video_codec       = AV_CODEC_ID_MPEG2VIDEO,
    .write_header      = mpegts_write_header,
    .write_packet      = mpegts_write_packet,
    .write_trailer     = mpegts_write_end,
    .flags             = AVFMT_ALLOW_FLUSH,
    .priv_class        = &mpegts_muxer_class,
};
