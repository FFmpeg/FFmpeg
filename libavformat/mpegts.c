/*
 * MPEG2 transport stream (aka DVB) demux
 * Copyright (c) 2002-2003 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"

#include "mpegts.h"

//#define DEBUG_SI
//#define DEBUG_SEEK

/* 1.0 second at 24Mbit/s */
#define MAX_SCAN_PACKETS 32000

/* maximum size in which we look for synchronisation if
   synchronisation is lost */
#define MAX_RESYNC_SIZE 4096

static int add_pes_stream(MpegTSContext *ts, int pid, int stream_type);

enum MpegTSFilterType {
    MPEGTS_PES,
    MPEGTS_SECTION,
};

typedef void PESCallback(void *opaque, const uint8_t *buf, int len, int is_start);

typedef struct MpegTSPESFilter {
    PESCallback *pes_cb;
    void *opaque;
} MpegTSPESFilter;

typedef void SectionCallback(void *opaque, const uint8_t *buf, int len);

typedef void SetServiceCallback(void *opaque, int ret);

typedef struct MpegTSSectionFilter {
    int section_index;
    int section_h_size;
    uint8_t *section_buf;
    int check_crc:1;
    int end_of_section_reached:1;
    SectionCallback *section_cb;
    void *opaque;
} MpegTSSectionFilter;

typedef struct MpegTSFilter {
    int pid;
    int last_cc; /* last cc code (-1 if first packet) */
    enum MpegTSFilterType type;
    union {
        MpegTSPESFilter pes_filter;
        MpegTSSectionFilter section_filter;
    } u;
} MpegTSFilter;

typedef struct MpegTSService {
    int running:1;
    int sid;
    char *provider_name;
    char *name;
} MpegTSService;

struct MpegTSContext {
    /* user data */
    AVFormatContext *stream;
    int raw_packet_size; /* raw packet size, including FEC if present */
    int auto_guess; /* if true, all pids are analized to find streams */
    int set_service_ret;

    int mpeg2ts_raw;  /* force raw MPEG2 transport stream output, if possible */
    int mpeg2ts_compute_pcr; /* compute exact PCR for each transport stream packet */

    /* used to estimate the exact PCR */
    int64_t cur_pcr;
    int pcr_incr;
    int pcr_pid;
    
    /* data needed to handle file based ts */
    int stop_parse; /* stop parsing loop */
    AVPacket *pkt; /* packet containing av data */

    /******************************************/
    /* private mpegts data */
    /* scan context */
    MpegTSFilter *sdt_filter;
    int nb_services;
    MpegTSService **services;
    
    /* set service context (XXX: allocated it ?) */
    SetServiceCallback *set_service_cb;
    void *set_service_opaque;
    MpegTSFilter *pat_filter;
    MpegTSFilter *pmt_filter;
    int req_sid;

    MpegTSFilter *pids[NB_PID_MAX];
};

static void write_section_data(AVFormatContext *s, MpegTSFilter *tss1,
                               const uint8_t *buf, int buf_size, int is_start)
{
    MpegTSSectionFilter *tss = &tss1->u.section_filter;
    int len;
    unsigned int crc;
    
    if (is_start) {
        memcpy(tss->section_buf, buf, buf_size);
        tss->section_index = buf_size;
        tss->section_h_size = -1;
        tss->end_of_section_reached = 0;
    } else {
        if (tss->end_of_section_reached)
            return;
        len = 4096 - tss->section_index;
        if (buf_size < len)
            len = buf_size;
        memcpy(tss->section_buf + tss->section_index, buf, len);
        tss->section_index += len;
    }

    /* compute section length if possible */
    if (tss->section_h_size == -1 && tss->section_index >= 3) {
        len = (((tss->section_buf[1] & 0xf) << 8) | tss->section_buf[2]) + 3;
        if (len > 4096)
            return;
        tss->section_h_size = len;
    }

    if (tss->section_h_size != -1 && tss->section_index >= tss->section_h_size) {
        if (tss->check_crc) {
            crc = mpegts_crc32(tss->section_buf, tss->section_h_size);
            if (crc != 0)
                goto invalid_crc;
        }
        tss->section_cb(tss->opaque, tss->section_buf, tss->section_h_size);
    invalid_crc:
        tss->end_of_section_reached = 1;
    }
}

MpegTSFilter *mpegts_open_section_filter(MpegTSContext *ts, unsigned int pid, 
                                         SectionCallback *section_cb, void *opaque,
                                         int check_crc)

{
    MpegTSFilter *filter;
    MpegTSSectionFilter *sec;
    
#ifdef DEBUG_SI
    printf("Filter: pid=0x%x\n", pid);
#endif
    if (pid >= NB_PID_MAX || ts->pids[pid])
        return NULL;
    filter = av_mallocz(sizeof(MpegTSFilter));
    if (!filter) 
        return NULL;
    ts->pids[pid] = filter;
    filter->type = MPEGTS_SECTION;
    filter->pid = pid;
    filter->last_cc = -1;
    sec = &filter->u.section_filter;
    sec->section_cb = section_cb;
    sec->opaque = opaque;
    sec->section_buf = av_malloc(MAX_SECTION_SIZE);
    sec->check_crc = check_crc;
    if (!sec->section_buf) {
        av_free(filter);
        return NULL;
    }
    return filter;
}

MpegTSFilter *mpegts_open_pes_filter(MpegTSContext *ts, unsigned int pid, 
                                     PESCallback *pes_cb,
                                     void *opaque)
{
    MpegTSFilter *filter;
    MpegTSPESFilter *pes;

    if (pid >= NB_PID_MAX || ts->pids[pid])
        return NULL;
    filter = av_mallocz(sizeof(MpegTSFilter));
    if (!filter) 
        return NULL;
    ts->pids[pid] = filter;
    filter->type = MPEGTS_PES;
    filter->pid = pid;
    filter->last_cc = -1;
    pes = &filter->u.pes_filter;
    pes->pes_cb = pes_cb;
    pes->opaque = opaque;
    return filter;
}

void mpegts_close_filter(MpegTSContext *ts, MpegTSFilter *filter)
{
    int pid;

    pid = filter->pid;
    if (filter->type == MPEGTS_SECTION)
        av_freep(&filter->u.section_filter.section_buf);
    else if (filter->type == MPEGTS_PES)
        av_freep(&filter->u.pes_filter.opaque);

    av_free(filter);
    ts->pids[pid] = NULL;
}

static int analyze(const uint8_t *buf, int size, int packet_size, int *index){
    int stat[packet_size];
    int i;
    int x=0;
    int best_score=0;

    memset(stat, 0, packet_size*sizeof(int));

    for(x=i=0; i<size; i++){
        if(buf[i] == 0x47){
            stat[x]++;
            if(stat[x] > best_score){
                best_score= stat[x];
                if(index) *index= x;
            }
        }

        x++;
        if(x == packet_size) x= 0;
    }

    return best_score;
}

/* autodetect fec presence. Must have at least 1024 bytes  */
static int get_packet_size(const uint8_t *buf, int size)
{
    int score, fec_score;

    if (size < (TS_FEC_PACKET_SIZE * 5 + 1))
        return -1;
        
    score    = analyze(buf, size, TS_PACKET_SIZE, NULL);
    fec_score= analyze(buf, size, TS_FEC_PACKET_SIZE, NULL);
//    av_log(NULL, AV_LOG_DEBUG, "score: %d, fec_score: %d \n", score, fec_score);
    
    if     (score > fec_score) return TS_PACKET_SIZE;
    else if(score < fec_score) return TS_FEC_PACKET_SIZE;
    else                       return -1;
}

typedef struct SectionHeader {
    uint8_t tid;
    uint16_t id;
    uint8_t version;
    uint8_t sec_num;
    uint8_t last_sec_num;
} SectionHeader;

static inline int get8(const uint8_t **pp, const uint8_t *p_end)
{
    const uint8_t *p;
    int c;

    p = *pp;
    if (p >= p_end)
        return -1;
    c = *p++;
    *pp = p;
    return c;
}

static inline int get16(const uint8_t **pp, const uint8_t *p_end)
{
    const uint8_t *p;
    int c;

    p = *pp;
    if ((p + 1) >= p_end)
        return -1;
    c = (p[0] << 8) | p[1];
    p += 2;
    *pp = p;
    return c;
}

/* read and allocate a DVB string preceeded by its length */
static char *getstr8(const uint8_t **pp, const uint8_t *p_end)
{
    int len;
    const uint8_t *p;
    char *str;

    p = *pp;
    len = get8(&p, p_end);
    if (len < 0)
        return NULL;
    if ((p + len) > p_end)
        return NULL;
    str = av_malloc(len + 1);
    if (!str)
        return NULL;
    memcpy(str, p, len);
    str[len] = '\0';
    p += len;
    *pp = p;
    return str;
}

static int parse_section_header(SectionHeader *h, 
                                const uint8_t **pp, const uint8_t *p_end)
{
    int val;

    val = get8(pp, p_end);
    if (val < 0)
        return -1;
    h->tid = val;
    *pp += 2;
    val = get16(pp, p_end);
    if (val < 0)
        return -1;
    h->id = val;
    val = get8(pp, p_end);
    if (val < 0)
        return -1;
    h->version = (val >> 1) & 0x1f;
    val = get8(pp, p_end);
    if (val < 0)
        return -1;
    h->sec_num = val;
    val = get8(pp, p_end);
    if (val < 0)
        return -1;
    h->last_sec_num = val;
    return 0;
}

static MpegTSService *new_service(MpegTSContext *ts, int sid, 
                                  char *provider_name, char *name)
{
    MpegTSService *service;

#ifdef DEBUG_SI
    printf("new_service: sid=0x%04x provider='%s' name='%s'\n", 
           sid, provider_name, name);
#endif

    service = av_mallocz(sizeof(MpegTSService));
    if (!service)
        return NULL;
    service->sid = sid;
    service->provider_name = provider_name;
    service->name = name;
    dynarray_add(&ts->services, &ts->nb_services, service);
    return service;
}

static void pmt_cb(void *opaque, const uint8_t *section, int section_len)
{
    MpegTSContext *ts = opaque;
    SectionHeader h1, *h = &h1;
    const uint8_t *p, *p_end;
    int program_info_length, pcr_pid, pid, stream_type, desc_length;
    
#ifdef DEBUG_SI
    printf("PMT:\n");
    av_hex_dump(stdout, (uint8_t *)section, section_len);
#endif
    p_end = section + section_len - 4;
    p = section;
    if (parse_section_header(h, &p, p_end) < 0)
        return;
#ifdef DEBUG_SI
    printf("sid=0x%x sec_num=%d/%d\n", h->id, h->sec_num, h->last_sec_num);
#endif
    if (h->tid != PMT_TID || (ts->req_sid >= 0 && h->id != ts->req_sid) )
        return;

    pcr_pid = get16(&p, p_end) & 0x1fff;
    if (pcr_pid < 0)
        return;
    ts->pcr_pid = pcr_pid;
#ifdef DEBUG_SI
    printf("pcr_pid=0x%x\n", pcr_pid);
#endif
    program_info_length = get16(&p, p_end) & 0xfff;
    if (program_info_length < 0)
        return;
    p += program_info_length;
    if (p >= p_end)
        return;
    for(;;) {
        stream_type = get8(&p, p_end);
        if (stream_type < 0)
            break;
        pid = get16(&p, p_end) & 0x1fff;
        if (pid < 0)
            break;
        desc_length = get16(&p, p_end) & 0xfff;
        if (desc_length < 0)
            break;
        p += desc_length;
        if (p > p_end)
            return;

#ifdef DEBUG_SI
        printf("stream_type=%d pid=0x%x\n", stream_type, pid);
#endif

        /* now create ffmpeg stream */
        switch(stream_type) {
        case STREAM_TYPE_AUDIO_MPEG1:
        case STREAM_TYPE_AUDIO_MPEG2:
        case STREAM_TYPE_VIDEO_MPEG1:
        case STREAM_TYPE_VIDEO_MPEG2:
        case STREAM_TYPE_VIDEO_MPEG4:
        case STREAM_TYPE_VIDEO_H264:
        case STREAM_TYPE_AUDIO_AAC:
        case STREAM_TYPE_AUDIO_AC3:
        case STREAM_TYPE_AUDIO_DTS:
            add_pes_stream(ts, pid, stream_type);
            break;
        default:
            /* we ignore the other streams */
            break;
        }
    }
    /* all parameters are there */
    ts->set_service_cb(ts->set_service_opaque, 0);
    mpegts_close_filter(ts, ts->pmt_filter);
    ts->pmt_filter = NULL;
}

static void pat_cb(void *opaque, const uint8_t *section, int section_len)
{
    MpegTSContext *ts = opaque;
    SectionHeader h1, *h = &h1;
    const uint8_t *p, *p_end;
    int sid, pmt_pid;

#ifdef DEBUG_SI
    printf("PAT:\n");
    av_hex_dump(stdout, (uint8_t *)section, section_len);
#endif
    p_end = section + section_len - 4;
    p = section;
    if (parse_section_header(h, &p, p_end) < 0)
        return;
    if (h->tid != PAT_TID)
        return;

    for(;;) {
        sid = get16(&p, p_end);
        if (sid < 0)
            break;
        pmt_pid = get16(&p, p_end) & 0x1fff;
        if (pmt_pid < 0)
            break;
#ifdef DEBUG_SI
        printf("sid=0x%x pid=0x%x\n", sid, pmt_pid);
#endif
        if (sid == 0x0000) {
            /* NIT info */
        } else {
            if (ts->req_sid == sid) {
                ts->pmt_filter = mpegts_open_section_filter(ts, pmt_pid, 
                                                            pmt_cb, ts, 1);
                goto found;
            }
        }
    }
    /* not found */
    ts->set_service_cb(ts->set_service_opaque, -1);

 found:
    mpegts_close_filter(ts, ts->pat_filter);
    ts->pat_filter = NULL;
}

/* add all services found in the PAT */
static void pat_scan_cb(void *opaque, const uint8_t *section, int section_len)
{
    MpegTSContext *ts = opaque;
    SectionHeader h1, *h = &h1;
    const uint8_t *p, *p_end;
    int sid, pmt_pid;
    char *provider_name, *name;
    char buf[256];

#ifdef DEBUG_SI
    printf("PAT:\n");
    av_hex_dump(stdout, (uint8_t *)section, section_len);
#endif
    p_end = section + section_len - 4;
    p = section;
    if (parse_section_header(h, &p, p_end) < 0)
        return;
    if (h->tid != PAT_TID)
        return;

    for(;;) {
        sid = get16(&p, p_end);
        if (sid < 0)
            break;
        pmt_pid = get16(&p, p_end) & 0x1fff;
        if (pmt_pid < 0)
            break;
#ifdef DEBUG_SI
        printf("sid=0x%x pid=0x%x\n", sid, pmt_pid);
#endif
        if (sid == 0x0000) {
            /* NIT info */
        } else {
            /* add the service with a dummy name */
            snprintf(buf, sizeof(buf), "Service %x\n", sid);
            name = av_strdup(buf);
            provider_name = av_strdup("");
            if (name && provider_name) {
                new_service(ts, sid, provider_name, name);
            } else {
                av_freep(&name);
                av_freep(&provider_name);
            }
        }
    }
    ts->stop_parse = 1;

    /* remove filter */
    mpegts_close_filter(ts, ts->pat_filter);
    ts->pat_filter = NULL;
}

void mpegts_set_service(MpegTSContext *ts, int sid,
                        SetServiceCallback *set_service_cb, void *opaque)
{
    ts->set_service_cb = set_service_cb;
    ts->set_service_opaque = opaque;
    ts->req_sid = sid;
    ts->pat_filter = mpegts_open_section_filter(ts, PAT_PID, 
                                                pat_cb, ts, 1);
}

static void sdt_cb(void *opaque, const uint8_t *section, int section_len)
{
    MpegTSContext *ts = opaque;
    SectionHeader h1, *h = &h1;
    const uint8_t *p, *p_end, *desc_list_end, *desc_end;
    int onid, val, sid, desc_list_len, desc_tag, desc_len, service_type;
    char *name, *provider_name;

#ifdef DEBUG_SI
    printf("SDT:\n");
    av_hex_dump(stdout, (uint8_t *)section, section_len);
#endif

    p_end = section + section_len - 4;
    p = section;
    if (parse_section_header(h, &p, p_end) < 0)
        return;
    if (h->tid != SDT_TID)
        return;
    onid = get16(&p, p_end);
    if (onid < 0)
        return;
    val = get8(&p, p_end);
    if (val < 0)
        return;
    for(;;) {
        sid = get16(&p, p_end);
        if (sid < 0)
            break;
        val = get8(&p, p_end);
        if (val < 0)
            break;
        desc_list_len = get16(&p, p_end) & 0xfff;
        if (desc_list_len < 0)
            break;
        desc_list_end = p + desc_list_len;
        if (desc_list_end > p_end)
            break;
        for(;;) {
            desc_tag = get8(&p, desc_list_end);
            if (desc_tag < 0)
                break;
            desc_len = get8(&p, desc_list_end);
            desc_end = p + desc_len;
            if (desc_end > desc_list_end)
                break;
#ifdef DEBUG_SI
            printf("tag: 0x%02x len=%d\n", desc_tag, desc_len);
#endif
            switch(desc_tag) {
            case 0x48:
                service_type = get8(&p, p_end);
                if (service_type < 0)
                    break;
                provider_name = getstr8(&p, p_end);
                if (!provider_name)
                    break;
                name = getstr8(&p, p_end);
                if (!name)
                    break;
                new_service(ts, sid, provider_name, name);
                break;
            default:
                break;
            }
            p = desc_end;
        }
        p = desc_list_end;
    }
    ts->stop_parse = 1;

    /* remove filter */
    mpegts_close_filter(ts, ts->sdt_filter);
    ts->sdt_filter = NULL;
}

/* scan services in a transport stream by looking at the SDT */
void mpegts_scan_sdt(MpegTSContext *ts)
{
    ts->sdt_filter = mpegts_open_section_filter(ts, SDT_PID, 
                                                sdt_cb, ts, 1);
}

/* scan services in a transport stream by looking at the PAT (better
   than nothing !) */
void mpegts_scan_pat(MpegTSContext *ts)
{
    ts->pat_filter = mpegts_open_section_filter(ts, PAT_PID, 
                                                pat_scan_cb, ts, 1);
}

/* TS stream handling */

enum MpegTSState {
    MPEGTS_HEADER = 0,
    MPEGTS_PESHEADER_FILL,
    MPEGTS_PAYLOAD,
    MPEGTS_SKIP,
};

/* enough for PES header + length */
#define PES_START_SIZE 9
#define MAX_PES_HEADER_SIZE (9 + 255)

typedef struct PESContext {
    int pid;
    int stream_type;
    MpegTSContext *ts;
    AVFormatContext *stream;
    AVStream *st;
    enum MpegTSState state;
    /* used to get the format */
    int data_index;
    int total_size;
    int pes_header_size;
    int64_t pts, dts;
    uint8_t header[MAX_PES_HEADER_SIZE];
} PESContext;

static int64_t get_pts(const uint8_t *p)
{
    int64_t pts;
    int val;

    pts = (int64_t)((p[0] >> 1) & 0x07) << 30;
    val = (p[1] << 8) | p[2];
    pts |= (int64_t)(val >> 1) << 15;
    val = (p[3] << 8) | p[4];
    pts |= (int64_t)(val >> 1);
    return pts;
}

/* return non zero if a packet could be constructed */
static void mpegts_push_data(void *opaque,
                             const uint8_t *buf, int buf_size, int is_start)
{
    PESContext *pes = opaque;
    MpegTSContext *ts = pes->ts;
    AVStream *st;
    const uint8_t *p;
    int len, code, codec_type, codec_id;
    
    if (is_start) {
        pes->state = MPEGTS_HEADER;
        pes->data_index = 0;
    }
    p = buf;
    while (buf_size > 0) {
        switch(pes->state) {
        case MPEGTS_HEADER:
            len = PES_START_SIZE - pes->data_index;
            if (len > buf_size)
                len = buf_size;
            memcpy(pes->header + pes->data_index, p, len);
            pes->data_index += len;
            p += len;
            buf_size -= len;
            if (pes->data_index == PES_START_SIZE) {
                /* we got all the PES or section header. We can now
                   decide */
#if 0
                av_hex_dump(pes->header, pes->data_index);
#endif
                if (pes->header[0] == 0x00 && pes->header[1] == 0x00 &&
                    pes->header[2] == 0x01) {
                    /* it must be an mpeg2 PES stream */
                    code = pes->header[3] | 0x100;
                    if (!((code >= 0x1c0 && code <= 0x1df) ||
                          (code >= 0x1e0 && code <= 0x1ef) ||
                          (code == 0x1bd)))
                        goto skip;
                    if (!pes->st) {
                        /* allocate stream */
                        switch(pes->stream_type){
                        case STREAM_TYPE_AUDIO_MPEG1:
                        case STREAM_TYPE_AUDIO_MPEG2:
                            codec_type = CODEC_TYPE_AUDIO;
                            codec_id = CODEC_ID_MP3;
                            break;
                        case STREAM_TYPE_VIDEO_MPEG1:
                        case STREAM_TYPE_VIDEO_MPEG2:
                            codec_type = CODEC_TYPE_VIDEO;
                            codec_id = CODEC_ID_MPEG2VIDEO;
                            break;
                        case STREAM_TYPE_VIDEO_MPEG4:
                            codec_type = CODEC_TYPE_VIDEO;
                            codec_id = CODEC_ID_MPEG4;
                            break;
                        case STREAM_TYPE_VIDEO_H264:
                            codec_type = CODEC_TYPE_VIDEO;
                            codec_id = CODEC_ID_H264;
                            break;
                        case STREAM_TYPE_AUDIO_AAC:
                            codec_type = CODEC_TYPE_AUDIO;
                            codec_id = CODEC_ID_AAC;
                            break;
                        case STREAM_TYPE_AUDIO_AC3:
                            codec_type = CODEC_TYPE_AUDIO;
                            codec_id = CODEC_ID_AC3;
                            break;
                        case STREAM_TYPE_AUDIO_DTS:
                            codec_type = CODEC_TYPE_AUDIO;
                            codec_id = CODEC_ID_DTS;
                            break;
                        default:
                            if (code >= 0x1c0 && code <= 0x1df) {
                                codec_type = CODEC_TYPE_AUDIO;
                                codec_id = CODEC_ID_MP2;
                            } else if (code == 0x1bd) {
                                codec_type = CODEC_TYPE_AUDIO;
                                codec_id = CODEC_ID_AC3;
                            } else {
                                codec_type = CODEC_TYPE_VIDEO;
                                codec_id = CODEC_ID_MPEG1VIDEO;
                            }
                            break;
                        }
                        st = av_new_stream(pes->stream, pes->pid);
                        if (st) {
                            av_set_pts_info(st, 60, 1, 90000);
                            st->priv_data = pes;
                            st->codec.codec_type = codec_type;
                            st->codec.codec_id = codec_id;
                            st->need_parsing = 1;
                            pes->st = st;
                        }
                    }
                    pes->state = MPEGTS_PESHEADER_FILL;
                    pes->total_size = (pes->header[4] << 8) | pes->header[5];
                    /* NOTE: a zero total size means the PES size is
                       unbounded */
                    if (pes->total_size)
                        pes->total_size += 6;
                    pes->pes_header_size = pes->header[8] + 9;
                } else {
                    /* otherwise, it should be a table */
                    /* skip packet */
                skip:
                    pes->state = MPEGTS_SKIP;
                    continue;
                }
            }
            break;
            /**********************************************/
            /* PES packing parsing */
        case MPEGTS_PESHEADER_FILL:
            len = pes->pes_header_size - pes->data_index;
            if (len > buf_size)
                len = buf_size;
            memcpy(pes->header + pes->data_index, p, len);
            pes->data_index += len;
            p += len;
            buf_size -= len;
            if (pes->data_index == pes->pes_header_size) {
                const uint8_t *r;
                unsigned int flags;

                flags = pes->header[7];
                r = pes->header + 9;
                pes->pts = AV_NOPTS_VALUE;
                pes->dts = AV_NOPTS_VALUE;
                if ((flags & 0xc0) == 0x80) {
                    pes->pts = get_pts(r);
                    r += 5;
                } else if ((flags & 0xc0) == 0xc0) {
                    pes->pts = get_pts(r);
                    r += 5;
                    pes->dts = get_pts(r);
                    r += 5;
                }
                /* we got the full header. We parse it and get the payload */
                pes->state = MPEGTS_PAYLOAD;
            }
            break;
        case MPEGTS_PAYLOAD:
            if (pes->total_size) {
                len = pes->total_size - pes->data_index;
                if (len > buf_size)
                    len = buf_size;
            } else {
                len = buf_size;
            }
            if (len > 0) {
                AVPacket *pkt = ts->pkt;
                if (pes->st && av_new_packet(pkt, len) == 0) {
                    memcpy(pkt->data, p, len);
                    pkt->stream_index = pes->st->index;
                    pkt->pts = pes->pts;
                    pkt->dts = pes->dts;
                    /* reset pts values */
                    pes->pts = AV_NOPTS_VALUE;
                    pes->dts = AV_NOPTS_VALUE;
                    ts->stop_parse = 1;
                    return;
                }
            }
            buf_size = 0;
            break;
        case MPEGTS_SKIP:
            buf_size = 0;
            break;
        }
    }
}

static int add_pes_stream(MpegTSContext *ts, int pid, int stream_type)
{
    MpegTSFilter *tss;
    PESContext *pes;

    /* if no pid found, then add a pid context */
    pes = av_mallocz(sizeof(PESContext));
    if (!pes)
        return -1;
    pes->ts = ts;
    pes->stream = ts->stream;
    pes->pid = pid;
    pes->stream_type = stream_type;
    tss = mpegts_open_pes_filter(ts, pid, mpegts_push_data, pes);
    if (!tss) {
        av_free(pes);
        return -1;
    }
    return 0;
}

/* handle one TS packet */
static void handle_packet(MpegTSContext *ts, const uint8_t *packet)
{
    AVFormatContext *s = ts->stream;
    MpegTSFilter *tss;
    int len, pid, cc, cc_ok, afc, is_start;
    const uint8_t *p, *p_end;

    pid = ((packet[1] & 0x1f) << 8) | packet[2];
    is_start = packet[1] & 0x40;
    tss = ts->pids[pid];
    if (ts->auto_guess && tss == NULL && is_start) {
        add_pes_stream(ts, pid, 0);
        tss = ts->pids[pid];
    }
    if (!tss)
        return;

    /* continuity check (currently not used) */
    cc = (packet[3] & 0xf);
    cc_ok = (tss->last_cc < 0) || ((((tss->last_cc + 1) & 0x0f) == cc));
    tss->last_cc = cc;
    
    /* skip adaptation field */
    afc = (packet[3] >> 4) & 3;
    p = packet + 4;
    if (afc == 0) /* reserved value */
        return;
    if (afc == 2) /* adaptation field only */
        return;
    if (afc == 3) {
        /* skip adapation field */
        p += p[0] + 1;
    }
    /* if past the end of packet, ignore */
    p_end = packet + TS_PACKET_SIZE;
    if (p >= p_end)
        return;
    
    if (tss->type == MPEGTS_SECTION) {
        if (is_start) {
            /* pointer field present */
            len = *p++;
            if (p + len > p_end)
                return;
            if (len && cc_ok) {
                /* write remaning section bytes */
                write_section_data(s, tss, 
                                   p, len, 0);
            }
            p += len;
            if (p < p_end) {
                write_section_data(s, tss, 
                                   p, p_end - p, 1);
            }
        } else {
            if (cc_ok) {
                write_section_data(s, tss, 
                                   p, p_end - p, 0);
            }
        }
    } else {
        tss->u.pes_filter.pes_cb(tss->u.pes_filter.opaque, 
                                 p, p_end - p, is_start);
    }
}

/* XXX: try to find a better synchro over several packets (use
   get_packet_size() ?) */
static int mpegts_resync(ByteIOContext *pb)
{
    int c, i;

    for(i = 0;i < MAX_RESYNC_SIZE; i++) {
        c = url_fgetc(pb);
        if (c < 0)
            return -1;
        if (c == 0x47) {
            url_fseek(pb, -1, SEEK_CUR);
            return 0;
        }
    }
    /* no sync found */
    return -1;
}

/* return -1 if error or EOF. Return 0 if OK. */
static int read_packet(ByteIOContext *pb, uint8_t *buf, int raw_packet_size)
{
    int skip, len;

    for(;;) {
        len = get_buffer(pb, buf, TS_PACKET_SIZE);
        if (len != TS_PACKET_SIZE)
            return AVERROR_IO;
        /* check paquet sync byte */
        if (buf[0] != 0x47) {
            /* find a new packet start */
            url_fseek(pb, -TS_PACKET_SIZE, SEEK_CUR);
            if (mpegts_resync(pb) < 0)
                return AVERROR_INVALIDDATA;
            else
                continue;
        } else {
            skip = raw_packet_size - TS_PACKET_SIZE;
            if (skip > 0)
                url_fskip(pb, skip);
            break;
        }
    }
    return 0;
}

static int handle_packets(MpegTSContext *ts, int nb_packets)
{
    AVFormatContext *s = ts->stream;
    ByteIOContext *pb = &s->pb;
    uint8_t packet[TS_PACKET_SIZE];
    int packet_num, ret;

    ts->stop_parse = 0;
    packet_num = 0;
    for(;;) {
        if (ts->stop_parse)
            break;
        packet_num++;
        if (nb_packets != 0 && packet_num >= nb_packets)
            break;
        ret = read_packet(pb, packet, ts->raw_packet_size);
        if (ret != 0)
            return ret;
        handle_packet(ts, packet);
    }
    return 0;
}

static int mpegts_probe(AVProbeData *p)
{
#if 1
    const int size= p->buf_size;
    int score, fec_score;
#define CHECK_COUNT 10
    
    if (size < (TS_FEC_PACKET_SIZE * CHECK_COUNT))
        return -1;
    
    score    = analyze(p->buf, TS_PACKET_SIZE    *CHECK_COUNT, TS_PACKET_SIZE, NULL);
    fec_score= analyze(p->buf, TS_FEC_PACKET_SIZE*CHECK_COUNT, TS_FEC_PACKET_SIZE, NULL);
//    av_log(NULL, AV_LOG_DEBUG, "score: %d, fec_score: %d \n", score, fec_score);
  
// we need a clear definition for the returned score otherwise things will become messy sooner or later
    if     (score > fec_score && score > 6) return AVPROBE_SCORE_MAX + score     - CHECK_COUNT;
    else if(                 fec_score > 6) return AVPROBE_SCORE_MAX + fec_score - CHECK_COUNT;
    else                                    return -1;
#else
    /* only use the extension for safer guess */
    if (match_ext(p->filename, "ts"))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
#endif
}

void set_service_cb(void *opaque, int ret)
{
    MpegTSContext *ts = opaque;
    ts->set_service_ret = ret;
    ts->stop_parse = 1;
}

/* return the 90 kHz PCR and the extension for the 27 MHz PCR. return
   (-1) if not available */
static int parse_pcr(int64_t *ppcr_high, int *ppcr_low, 
                     const uint8_t *packet)
{
    int afc, len, flags;
    const uint8_t *p;
    unsigned int v;

    afc = (packet[3] >> 4) & 3;
    if (afc <= 1)
        return -1;
    p = packet + 4;
    len = p[0];
    p++;
    if (len == 0)
        return -1;
    flags = *p++;
    len--;
    if (!(flags & 0x10))
        return -1;
    if (len < 6)
        return -1;
    v = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    *ppcr_high = ((int64_t)v << 1) | (p[4] >> 7);
    *ppcr_low = ((p[4] & 1) << 8) | p[5];
    return 0;
}

static int mpegts_read_header(AVFormatContext *s,
                              AVFormatParameters *ap)
{
    MpegTSContext *ts = s->priv_data;
    ByteIOContext *pb = &s->pb;
    uint8_t buf[1024];
    int len, sid, i;
    int64_t pos;
    MpegTSService *service;

    if (ap) {
        ts->mpeg2ts_raw = ap->mpeg2ts_raw;
        ts->mpeg2ts_compute_pcr = ap->mpeg2ts_compute_pcr;
    }

    /* read the first 1024 bytes to get packet size */
    pos = url_ftell(pb);
    len = get_buffer(pb, buf, sizeof(buf));
    if (len != sizeof(buf))
        goto fail;
    ts->raw_packet_size = get_packet_size(buf, sizeof(buf));
    if (ts->raw_packet_size <= 0)
        goto fail;
    ts->stream = s;
    ts->auto_guess = 0;

    if (!ts->mpeg2ts_raw) {
        /* normal demux */

        if (!ts->auto_guess) {
            ts->set_service_ret = -1;

            /* first do a scaning to get all the services */
            url_fseek(pb, pos, SEEK_SET);
            mpegts_scan_sdt(ts);
            
            handle_packets(ts, MAX_SCAN_PACKETS);
            
            if (ts->nb_services <= 0) {
                /* no SDT found, we try to look at the PAT */
                
                /* First remove the SDT filters from each PID */
                int i;
                for (i=0; i < NB_PID_MAX; i++) {
                    if (ts->pids[i])
                        mpegts_close_filter(ts, ts->pids[i]);
                }
                url_fseek(pb, pos, SEEK_SET);
                mpegts_scan_pat(ts);
                
                handle_packets(ts, MAX_SCAN_PACKETS);
            }
            
            if (ts->nb_services <= 0) {
		/* raw transport stream */
		ts->auto_guess = 1;
		s->ctx_flags |= AVFMTCTX_NOHEADER;
		goto do_pcr;
	    }
            
            /* tune to first service found */
            for(i=0; i<ts->nb_services && ts->set_service_ret; i++){
                service = ts->services[i];
                sid = service->sid;
#ifdef DEBUG_SI
                printf("tuning to '%s'\n", service->name);
#endif
            
                /* now find the info for the first service if we found any,
                otherwise try to filter all PATs */
                
                url_fseek(pb, pos, SEEK_SET);
                mpegts_set_service(ts, sid, set_service_cb, ts);
                
                handle_packets(ts, MAX_SCAN_PACKETS);
            }
            /* if could not find service, exit */
            
            if (ts->set_service_ret != 0)
                return -1;
            
#ifdef DEBUG_SI
            printf("tuning done\n");
#endif
        }
        s->ctx_flags |= AVFMTCTX_NOHEADER;
    } else {
        AVStream *st;
        int pcr_pid, pid, nb_packets, nb_pcrs, ret, pcr_l;
        int64_t pcrs[2], pcr_h;
        int packet_count[2];
        uint8_t packet[TS_PACKET_SIZE];
        
        /* only read packets */
        
    do_pcr:
        st = av_new_stream(s, 0);
        if (!st)
            goto fail;
        av_set_pts_info(st, 60, 1, 27000000);
        st->codec.codec_type = CODEC_TYPE_DATA;
        st->codec.codec_id = CODEC_ID_MPEG2TS;
        
        /* we iterate until we find two PCRs to estimate the bitrate */
        pcr_pid = -1;
        nb_pcrs = 0;
        nb_packets = 0;
        for(;;) {
            ret = read_packet(&s->pb, packet, ts->raw_packet_size);
            if (ret < 0)
                return -1;
            pid = ((packet[1] & 0x1f) << 8) | packet[2];
            if ((pcr_pid == -1 || pcr_pid == pid) &&
                parse_pcr(&pcr_h, &pcr_l, packet) == 0) {
                pcr_pid = pid;
                packet_count[nb_pcrs] = nb_packets;
                pcrs[nb_pcrs] = pcr_h * 300 + pcr_l;
                nb_pcrs++;
                if (nb_pcrs >= 2)
                    break;
            }
            nb_packets++;
        }
        ts->pcr_pid = pcr_pid;

        /* NOTE1: the bitrate is computed without the FEC */
        /* NOTE2: it is only the bitrate of the start of the stream */
        ts->pcr_incr = (pcrs[1] - pcrs[0]) / (packet_count[1] - packet_count[0]);
        ts->cur_pcr = pcrs[0] - ts->pcr_incr * packet_count[0];
        s->bit_rate = (TS_PACKET_SIZE * 8) * 27e6 / ts->pcr_incr;
        st->codec.bit_rate = s->bit_rate;
        st->start_time = ts->cur_pcr * 1000000.0 / 27.0e6;
#if 0
        printf("start=%0.3f pcr=%0.3f incr=%d\n",
               st->start_time / 1000000.0, pcrs[0] / 27e6, ts->pcr_incr);
#endif
    }

    url_fseek(pb, pos, SEEK_SET);
    return 0;
 fail:
    return -1;
}

#define MAX_PACKET_READAHEAD ((128 * 1024) / 188)

static int mpegts_raw_read_packet(AVFormatContext *s,
                                  AVPacket *pkt)
{
    MpegTSContext *ts = s->priv_data;
    int ret, i;
    int64_t pcr_h, next_pcr_h, pos;
    int pcr_l, next_pcr_l;
    uint8_t pcr_buf[12];

    if (av_new_packet(pkt, TS_PACKET_SIZE) < 0)
        return -ENOMEM;
    ret = read_packet(&s->pb, pkt->data, ts->raw_packet_size);
    if (ret < 0) {
        av_free_packet(pkt);
        return ret;
    }
    if (ts->mpeg2ts_compute_pcr) {
        /* compute exact PCR for each packet */
        if (parse_pcr(&pcr_h, &pcr_l, pkt->data) == 0) {
            /* we read the next PCR (XXX: optimize it by using a bigger buffer */
            pos = url_ftell(&s->pb);
            for(i = 0; i < MAX_PACKET_READAHEAD; i++) {
                url_fseek(&s->pb, pos + i * ts->raw_packet_size, SEEK_SET);
                get_buffer(&s->pb, pcr_buf, 12);
                if (parse_pcr(&next_pcr_h, &next_pcr_l, pcr_buf) == 0) {
                    /* XXX: not precise enough */
                    ts->pcr_incr = ((next_pcr_h - pcr_h) * 300 + (next_pcr_l - pcr_l)) / 
                        (i + 1);
                    break;
                }
            }
            url_fseek(&s->pb, pos, SEEK_SET);
            /* no next PCR found: we use previous increment */
            ts->cur_pcr = pcr_h * 300 + pcr_l;
        }
        pkt->pts = ts->cur_pcr;
        pkt->duration = ts->pcr_incr;
        ts->cur_pcr += ts->pcr_incr;
    }
    pkt->stream_index = 0;
    return 0;
}

static int mpegts_read_packet(AVFormatContext *s,
                              AVPacket *pkt)
{
    MpegTSContext *ts = s->priv_data;

    if (!ts->mpeg2ts_raw) {
        ts->pkt = pkt;
        return handle_packets(ts, 0);
    } else {
        return mpegts_raw_read_packet(s, pkt);
    }
}

static int mpegts_read_close(AVFormatContext *s)
{
    MpegTSContext *ts = s->priv_data;
    int i;
    for(i=0;i<NB_PID_MAX;i++)
        if (ts->pids[i]) mpegts_close_filter(ts, ts->pids[i]);
    return 0;
}

static int64_t mpegts_get_pcr(AVFormatContext *s, int stream_index, 
                              int64_t *ppos, int64_t pos_limit)
{
    MpegTSContext *ts = s->priv_data;
    int64_t pos, timestamp;
    uint8_t buf[TS_PACKET_SIZE];
    int pcr_l, pid;
    const int find_next= 1;
    pos = ((*ppos  + ts->raw_packet_size - 1) / ts->raw_packet_size) * ts->raw_packet_size;
    if (find_next) {
        for(;;) {
            url_fseek(&s->pb, pos, SEEK_SET);
            if (get_buffer(&s->pb, buf, TS_PACKET_SIZE) != TS_PACKET_SIZE)
                return AV_NOPTS_VALUE;
            pid = ((buf[1] & 0x1f) << 8) | buf[2];
            if (pid == ts->pcr_pid &&
                parse_pcr(&timestamp, &pcr_l, buf) == 0) {
                break;
            }
            pos += ts->raw_packet_size;
        }
    } else {
        for(;;) {
            pos -= ts->raw_packet_size;
            if (pos < 0)
                return AV_NOPTS_VALUE;
            url_fseek(&s->pb, pos, SEEK_SET);
            if (get_buffer(&s->pb, buf, TS_PACKET_SIZE) != TS_PACKET_SIZE)
                return AV_NOPTS_VALUE;
            pid = ((buf[1] & 0x1f) << 8) | buf[2];
            if (pid == ts->pcr_pid &&
                parse_pcr(&timestamp, &pcr_l, buf) == 0) {
                break;
            }
        }
    }
    *ppos = pos;

    return timestamp;
}

static int read_seek(AVFormatContext *s, int stream_index, int64_t target_ts, int flags){
    MpegTSContext *ts = s->priv_data;
    uint8_t buf[TS_PACKET_SIZE];
    int64_t pos;

    if(av_seek_frame_binary(s, stream_index, target_ts, flags) < 0)
        return -1;

    pos= url_ftell(&s->pb);

    for(;;) {
        url_fseek(&s->pb, pos, SEEK_SET);
        if (get_buffer(&s->pb, buf, TS_PACKET_SIZE) != TS_PACKET_SIZE)
            return -1;
//        pid = ((buf[1] & 0x1f) << 8) | buf[2];
        if(buf[1] & 0x40) break;
        pos += ts->raw_packet_size;
    }    
    url_fseek(&s->pb, pos, SEEK_SET);

    return 0;
}

/**************************************************************/
/* parsing functions - called from other demuxers such as RTP */

MpegTSContext *mpegts_parse_open(AVFormatContext *s)
{
    MpegTSContext *ts;
    
    ts = av_mallocz(sizeof(MpegTSContext));
    if (!ts)
        return NULL;
    /* no stream case, currently used by RTP */
    ts->raw_packet_size = TS_PACKET_SIZE;
    ts->stream = s;
    ts->auto_guess = 1;
    return ts;
}

/* return the consumed length if a packet was output, or -1 if no
   packet is output */
int mpegts_parse_packet(MpegTSContext *ts, AVPacket *pkt,
                        const uint8_t *buf, int len)
{
    int len1;

    len1 = len;
    ts->pkt = pkt;
    ts->stop_parse = 0;
    for(;;) {
        if (ts->stop_parse)
            break;
        if (len < TS_PACKET_SIZE)
            return -1;
        if (buf[0] != 0x47) {
            buf++;
            len--;
        } else {
            handle_packet(ts, buf);
            buf += TS_PACKET_SIZE;
            len -= TS_PACKET_SIZE;
        }
    }
    return len1 - len;
}

void mpegts_parse_close(MpegTSContext *ts)
{
    int i;

    for(i=0;i<NB_PID_MAX;i++)
        av_free(ts->pids[i]);
    av_free(ts);
}

AVInputFormat mpegts_demux = {
    "mpegts",
    "MPEG2 transport stream format",
    sizeof(MpegTSContext),
    mpegts_probe,
    mpegts_read_header,
    mpegts_read_packet,
    mpegts_read_close,
    read_seek,
    mpegts_get_pcr,
    .flags = AVFMT_SHOW_IDS,
};

int mpegts_init(void)
{
    av_register_input_format(&mpegts_demux);
#ifdef CONFIG_ENCODERS
    av_register_output_format(&mpegts_mux);
#endif
    return 0;
}
