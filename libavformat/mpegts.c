/*
 * MPEG2 transport stream (aka DVB) demuxer
 * Copyright (c) 2002-2003 Fabrice Bellard
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

#include "libavutil/crc.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "mpegts.h"
#include "internal.h"

//#define DEBUG_SI
//#define DEBUG_SEEK

/* 1.0 second at 24Mbit/s */
#define MAX_SCAN_PACKETS 32000

/* maximum size in which we look for synchronisation if
   synchronisation is lost */
#define MAX_RESYNC_SIZE 4096
#define REGISTRATION_DESCRIPTOR 5

typedef struct PESContext PESContext;

static PESContext* add_pes_stream(MpegTSContext *ts, int pid, int pcr_pid, int stream_type);
static AVStream* new_pes_av_stream(PESContext *pes, uint32_t code);

enum MpegTSFilterType {
    MPEGTS_PES,
    MPEGTS_SECTION,
};

typedef struct MpegTSFilter MpegTSFilter;

typedef void PESCallback(MpegTSFilter *f, const uint8_t *buf, int len, int is_start, int64_t pos);

typedef struct MpegTSPESFilter {
    PESCallback *pes_cb;
    void *opaque;
} MpegTSPESFilter;

typedef void SectionCallback(MpegTSFilter *f, const uint8_t *buf, int len);

typedef void SetServiceCallback(void *opaque, int ret);

typedef struct MpegTSSectionFilter {
    int section_index;
    int section_h_size;
    uint8_t *section_buf;
    unsigned int check_crc:1;
    unsigned int end_of_section_reached:1;
    SectionCallback *section_cb;
    void *opaque;
} MpegTSSectionFilter;

struct MpegTSFilter {
    int pid;
    int last_cc; /* last cc code (-1 if first packet) */
    enum MpegTSFilterType type;
    union {
        MpegTSPESFilter pes_filter;
        MpegTSSectionFilter section_filter;
    } u;
};

#define MAX_PIDS_PER_PROGRAM 64
struct Program {
    unsigned int id; //program id/service id
    unsigned int nb_pids;
    unsigned int pids[MAX_PIDS_PER_PROGRAM];
};

struct MpegTSContext {
    /* user data */
    AVFormatContext *stream;
    /** raw packet size, including FEC if present            */
    int raw_packet_size;

    int pos47;

    /** if true, all pids are analyzed to find streams       */
    int auto_guess;

    /** compute exact PCR for each transport stream packet   */
    int mpeg2ts_compute_pcr;

    int64_t cur_pcr;    /**< used to estimate the exact PCR  */
    int pcr_incr;       /**< used to estimate the exact PCR  */

    /* data needed to handle file based ts */
    /** stop parsing loop                                    */
    int stop_parse;
    /** packet containing Audio/Video data                   */
    AVPacket *pkt;

    /******************************************/
    /* private mpegts data */
    /* scan context */
    /** structure to keep track of Program->pids mapping     */
    unsigned int nb_prg;
    struct Program *prg;


    /** filters for various streams specified by PMT + for the PAT and PMT */
    MpegTSFilter *pids[NB_PID_MAX];
};

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

struct PESContext {
    int pid;
    int pcr_pid; /**< if -1 then all packets containing PCR are considered */
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
    int64_t ts_packet_pos; /**< position of first TS packet of this PES packet */
    uint8_t header[MAX_PES_HEADER_SIZE];
};

extern AVInputFormat mpegts_demuxer;

static void clear_program(MpegTSContext *ts, unsigned int programid)
{
    int i;

    for(i=0; i<ts->nb_prg; i++)
        if(ts->prg[i].id == programid)
            ts->prg[i].nb_pids = 0;
}

static void clear_programs(MpegTSContext *ts)
{
    av_freep(&ts->prg);
    ts->nb_prg=0;
}

static void add_pat_entry(MpegTSContext *ts, unsigned int programid)
{
    struct Program *p;
    void *tmp = av_realloc(ts->prg, (ts->nb_prg+1)*sizeof(struct Program));
    if(!tmp)
        return;
    ts->prg = tmp;
    p = &ts->prg[ts->nb_prg];
    p->id = programid;
    p->nb_pids = 0;
    ts->nb_prg++;
}

static void add_pid_to_pmt(MpegTSContext *ts, unsigned int programid, unsigned int pid)
{
    int i;
    struct Program *p = NULL;
    for(i=0; i<ts->nb_prg; i++) {
        if(ts->prg[i].id == programid) {
            p = &ts->prg[i];
            break;
        }
    }
    if(!p)
        return;

    if(p->nb_pids >= MAX_PIDS_PER_PROGRAM)
        return;
    p->pids[p->nb_pids++] = pid;
}

/**
 * \brief discard_pid() decides if the pid is to be discarded according
 *                      to caller's programs selection
 * \param ts    : - TS context
 * \param pid   : - pid
 * \return 1 if the pid is only comprised in programs that have .discard=AVDISCARD_ALL
 *         0 otherwise
 */
static int discard_pid(MpegTSContext *ts, unsigned int pid)
{
    int i, j, k;
    int used = 0, discarded = 0;
    struct Program *p;
    for(i=0; i<ts->nb_prg; i++) {
        p = &ts->prg[i];
        for(j=0; j<p->nb_pids; j++) {
            if(p->pids[j] != pid)
                continue;
            //is program with id p->id set to be discarded?
            for(k=0; k<ts->stream->nb_programs; k++) {
                if(ts->stream->programs[k]->id == p->id) {
                    if(ts->stream->programs[k]->discard == AVDISCARD_ALL)
                        discarded++;
                    else
                        used++;
                }
            }
        }
    }

    return !used && discarded;
}

/**
 *  Assembles PES packets out of TS packets, and then calls the "section_cb"
 *  function when they are complete.
 */
static void write_section_data(AVFormatContext *s, MpegTSFilter *tss1,
                               const uint8_t *buf, int buf_size, int is_start)
{
    MpegTSSectionFilter *tss = &tss1->u.section_filter;
    int len;

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
        len = (AV_RB16(tss->section_buf + 1) & 0xfff) + 3;
        if (len > 4096)
            return;
        tss->section_h_size = len;
    }

    if (tss->section_h_size != -1 && tss->section_index >= tss->section_h_size) {
        tss->end_of_section_reached = 1;
        if (!tss->check_crc ||
            av_crc(av_crc_get_table(AV_CRC_32_IEEE), -1,
                   tss->section_buf, tss->section_h_size) == 0)
            tss->section_cb(tss1, tss->section_buf, tss->section_h_size);
    }
}

static MpegTSFilter *mpegts_open_section_filter(MpegTSContext *ts, unsigned int pid,
                                         SectionCallback *section_cb, void *opaque,
                                         int check_crc)

{
    MpegTSFilter *filter;
    MpegTSSectionFilter *sec;

#ifdef DEBUG_SI
    av_log(ts->stream, AV_LOG_DEBUG, "Filter: pid=0x%x\n", pid);
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

static MpegTSFilter *mpegts_open_pes_filter(MpegTSContext *ts, unsigned int pid,
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

static void mpegts_close_filter(MpegTSContext *ts, MpegTSFilter *filter)
{
    int pid;

    pid = filter->pid;
    if (filter->type == MPEGTS_SECTION)
        av_freep(&filter->u.section_filter.section_buf);
    else if (filter->type == MPEGTS_PES) {
        /* referenced private data will be freed later in
         * av_close_input_stream */
        if (!((PESContext *)filter->u.pes_filter.opaque)->st) {
            av_freep(&filter->u.pes_filter.opaque);
        }
    }

    av_free(filter);
    ts->pids[pid] = NULL;
}

static int analyze(const uint8_t *buf, int size, int packet_size, int *index){
    int stat[packet_size];
    int i;
    int x=0;
    int best_score=0;

    memset(stat, 0, packet_size*sizeof(int));

    for(x=i=0; i<size-3; i++){
        if(buf[i] == 0x47 && !(buf[i+1] & 0x80) && (buf[i+3] & 0x30)){
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
    int score, fec_score, dvhs_score;

    if (size < (TS_FEC_PACKET_SIZE * 5 + 1))
        return -1;

    score    = analyze(buf, size, TS_PACKET_SIZE, NULL);
    dvhs_score    = analyze(buf, size, TS_DVHS_PACKET_SIZE, NULL);
    fec_score= analyze(buf, size, TS_FEC_PACKET_SIZE, NULL);
//    av_log(NULL, AV_LOG_DEBUG, "score: %d, dvhs_score: %d, fec_score: %d \n", score, dvhs_score, fec_score);

    if     (score > fec_score && score > dvhs_score) return TS_PACKET_SIZE;
    else if(dvhs_score > score && dvhs_score > fec_score) return TS_DVHS_PACKET_SIZE;
    else if(score < fec_score && dvhs_score < fec_score) return TS_FEC_PACKET_SIZE;
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
    c = AV_RB16(p);
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


static void pmt_cb(MpegTSFilter *filter, const uint8_t *section, int section_len)
{
    MpegTSContext *ts = filter->u.section_filter.opaque;
    SectionHeader h1, *h = &h1;
    PESContext *pes;
    AVStream *st;
    const uint8_t *p, *p_end, *desc_list_end, *desc_end;
    int program_info_length, pcr_pid, pid, stream_type;
    int desc_list_len, desc_len, desc_tag;
    int comp_page = 0, anc_page = 0; /* initialize to kill warnings */
    char language[4] = {0}; /* initialize to kill warnings */
    int has_hdmv_descr = 0;
    int has_dirac_descr = 0;

#ifdef DEBUG_SI
    av_log(ts->stream, AV_LOG_DEBUG, "PMT: len %i\n", section_len);
    av_hex_dump_log(ts->stream, AV_LOG_DEBUG, (uint8_t *)section, section_len);
#endif
    p_end = section + section_len - 4;
    p = section;
    if (parse_section_header(h, &p, p_end) < 0)
        return;
#ifdef DEBUG_SI
    av_log(ts->stream, AV_LOG_DEBUG, "sid=0x%x sec_num=%d/%d\n",
           h->id, h->sec_num, h->last_sec_num);
#endif
    if (h->tid != PMT_TID)
        return;

    clear_program(ts, h->id);
    pcr_pid = get16(&p, p_end) & 0x1fff;
    if (pcr_pid < 0)
        return;
    add_pid_to_pmt(ts, h->id, pcr_pid);
#ifdef DEBUG_SI
    av_log(ts->stream, AV_LOG_DEBUG, "pcr_pid=0x%x\n", pcr_pid);
#endif
    program_info_length = get16(&p, p_end) & 0xfff;
    if (program_info_length < 0)
        return;
    while(program_info_length >= 2) {
        uint8_t tag, len;
        tag = get8(&p, p_end);
        len = get8(&p, p_end);
        if(len > program_info_length - 2)
            //something else is broken, exit the program_descriptors_loop
            break;
        program_info_length -= len + 2;
        if(tag == REGISTRATION_DESCRIPTOR && len >= 4) {
            uint8_t bytes[4];
            bytes[0] = get8(&p, p_end);
            bytes[1] = get8(&p, p_end);
            bytes[2] = get8(&p, p_end);
            bytes[3] = get8(&p, p_end);
            len -= 4;
            if(bytes[0] == 'H' && bytes[1] == 'D' &&
               bytes[2] == 'M' && bytes[3] == 'V')
                has_hdmv_descr = 1;
        }
        p += len;
    }
    p += program_info_length;
    if (p >= p_end)
        return;
    for(;;) {
        language[0] = 0;
        st = 0;
        stream_type = get8(&p, p_end);
        if (stream_type < 0)
            break;
        pid = get16(&p, p_end) & 0x1fff;
        if (pid < 0)
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
            if (stream_type == STREAM_TYPE_PRIVATE_DATA) {
                if((desc_tag == 0x6A) || (desc_tag == 0x7A)) {
                    /*assume DVB AC-3 Audio*/
                    stream_type = STREAM_TYPE_AUDIO_AC3;
                } else if(desc_tag == 0x7B) {
                    /* DVB DTS audio */
                    stream_type = STREAM_TYPE_AUDIO_DTS;
                }
            }
            desc_len = get8(&p, desc_list_end);
            desc_end = p + desc_len;
            if (desc_end > desc_list_end)
                break;
#ifdef DEBUG_SI
            av_log(ts->stream, AV_LOG_DEBUG, "tag: 0x%02x len=%d\n",
                   desc_tag, desc_len);
#endif
            switch(desc_tag) {
            case DVB_SUBT_DESCID:
                if (stream_type == STREAM_TYPE_PRIVATE_DATA)
                    stream_type = STREAM_TYPE_SUBTITLE_DVB;

                language[0] = get8(&p, desc_end);
                language[1] = get8(&p, desc_end);
                language[2] = get8(&p, desc_end);
                language[3] = 0;
                get8(&p, desc_end);
                comp_page = get16(&p, desc_end);
                anc_page = get16(&p, desc_end);

                break;
            case 0x0a: /* ISO 639 language descriptor */
                language[0] = get8(&p, desc_end);
                language[1] = get8(&p, desc_end);
                language[2] = get8(&p, desc_end);
                language[3] = 0;
                break;
            case REGISTRATION_DESCRIPTOR: /*MPEG-2 Registration descriptor */
                {
                    uint8_t bytes[4];
                    bytes[0] = get8(&p, desc_end);
                    bytes[1] = get8(&p, desc_end);
                    bytes[2] = get8(&p, desc_end);
                    bytes[3] = get8(&p, desc_end);
                    if(bytes[0] == 'd' && bytes[1] == 'r' &&
                       bytes[2] == 'a' && bytes[3] == 'c')
                        has_dirac_descr = 1;
                    break;
                }
            default:
                break;
            }
            p = desc_end;
        }
        p = desc_list_end;

#ifdef DEBUG_SI
        av_log(ts->stream, AV_LOG_DEBUG, "stream_type=%d pid=0x%x\n",
               stream_type, pid);
#endif

        /* now create ffmpeg stream */
        switch(stream_type) {
        case STREAM_TYPE_AUDIO_MPEG1:
        case STREAM_TYPE_AUDIO_MPEG2:
        case STREAM_TYPE_VIDEO_MPEG1:
        case STREAM_TYPE_VIDEO_MPEG2:
        case STREAM_TYPE_VIDEO_MPEG4:
        case STREAM_TYPE_VIDEO_H264:
        case STREAM_TYPE_VIDEO_VC1:
        case STREAM_TYPE_VIDEO_DIRAC:
        case STREAM_TYPE_AUDIO_AAC:
        case STREAM_TYPE_AUDIO_AC3:
        case STREAM_TYPE_AUDIO_DTS:
        case STREAM_TYPE_AUDIO_HDMV_DTS:
        case STREAM_TYPE_SUBTITLE_DVB:
            if((stream_type == STREAM_TYPE_AUDIO_HDMV_DTS && !has_hdmv_descr)
            || (stream_type == STREAM_TYPE_VIDEO_DIRAC    && !has_dirac_descr))
                break;
            if(ts->pids[pid] && ts->pids[pid]->type == MPEGTS_PES){
                pes= ts->pids[pid]->u.pes_filter.opaque;
                st= pes->st;
            }else{
                if (ts->pids[pid]) mpegts_close_filter(ts, ts->pids[pid]); //wrongly added sdt filter probably
                pes = add_pes_stream(ts, pid, pcr_pid, stream_type);
                if (pes)
                    st = new_pes_av_stream(pes, 0);
            }
            add_pid_to_pmt(ts, h->id, pid);
            if(st)
                av_program_add_stream_index(ts->stream, h->id, st->index);
            break;
        default:
            /* we ignore the other streams */
            break;
        }

        if (st) {
            if (language[0] != 0) {
                av_metadata_set(&st->metadata, "language", language);
            }

            if (stream_type == STREAM_TYPE_SUBTITLE_DVB) {
                st->codec->sub_id = (anc_page << 16) | comp_page;
            }
        }
    }
    /* all parameters are there */
    ts->stop_parse++;
    mpegts_close_filter(ts, filter);
}

static void pat_cb(MpegTSFilter *filter, const uint8_t *section, int section_len)
{
    MpegTSContext *ts = filter->u.section_filter.opaque;
    SectionHeader h1, *h = &h1;
    const uint8_t *p, *p_end;
    int sid, pmt_pid;

#ifdef DEBUG_SI
    av_log(ts->stream, AV_LOG_DEBUG, "PAT:\n");
    av_hex_dump_log(ts->stream, AV_LOG_DEBUG, (uint8_t *)section, section_len);
#endif
    p_end = section + section_len - 4;
    p = section;
    if (parse_section_header(h, &p, p_end) < 0)
        return;
    if (h->tid != PAT_TID)
        return;

    clear_programs(ts);
    for(;;) {
        sid = get16(&p, p_end);
        if (sid < 0)
            break;
        pmt_pid = get16(&p, p_end) & 0x1fff;
        if (pmt_pid < 0)
            break;
#ifdef DEBUG_SI
        av_log(ts->stream, AV_LOG_DEBUG, "sid=0x%x pid=0x%x\n", sid, pmt_pid);
#endif
        if (sid == 0x0000) {
            /* NIT info */
        } else {
            av_new_program(ts->stream, sid);
            ts->stop_parse--;
            mpegts_open_section_filter(ts, pmt_pid, pmt_cb, ts, 1);
            add_pat_entry(ts, sid);
            add_pid_to_pmt(ts, sid, 0); //add pat pid to program
            add_pid_to_pmt(ts, sid, pmt_pid);
        }
    }
    /* not found */
    ts->stop_parse++;

    mpegts_close_filter(ts, filter);
}

static void mpegts_set_service(MpegTSContext *ts)
{
    mpegts_open_section_filter(ts, PAT_PID,
                                                pat_cb, ts, 1);
}

static void sdt_cb(MpegTSFilter *filter, const uint8_t *section, int section_len)
{
    MpegTSContext *ts = filter->u.section_filter.opaque;
    SectionHeader h1, *h = &h1;
    const uint8_t *p, *p_end, *desc_list_end, *desc_end;
    int onid, val, sid, desc_list_len, desc_tag, desc_len, service_type;
    char *name, *provider_name;

#ifdef DEBUG_SI
    av_log(ts->stream, AV_LOG_DEBUG, "SDT:\n");
    av_hex_dump_log(ts->stream, AV_LOG_DEBUG, (uint8_t *)section, section_len);
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
            av_log(ts->stream, AV_LOG_DEBUG, "tag: 0x%02x len=%d\n",
                   desc_tag, desc_len);
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
                if (name) {
                    AVProgram *program = av_new_program(ts->stream, sid);
                    if(program) {
                        av_metadata_set(&program->metadata, "name", name);
                        av_metadata_set(&program->metadata, "provider_name", provider_name);
                    }
                }
                av_free(name);
                av_free(provider_name);
                break;
            default:
                break;
            }
            p = desc_end;
        }
        p = desc_list_end;
    }
}

/* scan services in a transport stream by looking at the SDT */
static void mpegts_scan_sdt(MpegTSContext *ts)
{
    mpegts_open_section_filter(ts, SDT_PID,
                                                sdt_cb, ts, 1);
}

static int64_t get_pts(const uint8_t *p)
{
    int64_t pts = (int64_t)((p[0] >> 1) & 0x07) << 30;
    pts |= (AV_RB16(p + 1) >> 1) << 15;
    pts |=  AV_RB16(p + 3) >> 1;
    return pts;
}

/* return non zero if a packet could be constructed */
static void mpegts_push_data(MpegTSFilter *filter,
                             const uint8_t *buf, int buf_size, int is_start,
                             int64_t pos)
{
    PESContext *pes = filter->u.pes_filter.opaque;
    MpegTSContext *ts = pes->ts;
    const uint8_t *p;
    int len, code;

    if(!ts->pkt)
        return;

    if (is_start) {
        pes->state = MPEGTS_HEADER;
        pes->data_index = 0;
        pes->ts_packet_pos = pos;
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
                av_hex_dump_log(pes->stream, AV_LOG_DEBUG, pes->header, pes->data_index);
#endif
                if (pes->header[0] == 0x00 && pes->header[1] == 0x00 &&
                    pes->header[2] == 0x01) {
                    /* it must be an mpeg2 PES stream */
                    code = pes->header[3] | 0x100;
                    if (!((code >= 0x1c0 && code <= 0x1df) ||
                          (code >= 0x1e0 && code <= 0x1ef) ||
                          (code == 0x1bd) || (code == 0x1fd)))
                        goto skip;
                    if (!pes->st) {
                        /* allocate stream */
                        new_pes_av_stream(pes, code);
                    }
                    pes->state = MPEGTS_PESHEADER_FILL;
                    pes->total_size = AV_RB16(pes->header + 4);
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
                    pes->dts = pes->pts = get_pts(r);
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
                    /* store position of first TS packet of this PES packet */
                    pkt->pos = pes->ts_packet_pos;
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

static AVStream* new_pes_av_stream(PESContext *pes, uint32_t code)
{
    AVStream *st;
    enum CodecID codec_id;
    enum CodecType codec_type;

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
    case STREAM_TYPE_VIDEO_VC1:
        codec_type = CODEC_TYPE_VIDEO;
        codec_id = CODEC_ID_VC1;
        break;
    case STREAM_TYPE_VIDEO_DIRAC:
        codec_type = CODEC_TYPE_VIDEO;
        codec_id = CODEC_ID_DIRAC;
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
    case STREAM_TYPE_AUDIO_HDMV_DTS:
        codec_type = CODEC_TYPE_AUDIO;
        codec_id = CODEC_ID_DTS;
        break;
    case STREAM_TYPE_SUBTITLE_DVB:
        codec_type = CODEC_TYPE_SUBTITLE;
        codec_id = CODEC_ID_DVB_SUBTITLE;
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
            codec_id = CODEC_ID_PROBE;
        }
        break;
    }
    st = av_new_stream(pes->stream, pes->pid);
    if (st) {
        av_set_pts_info(st, 33, 1, 90000);
        st->priv_data = pes;
        st->codec->codec_type = codec_type;
        st->codec->codec_id = codec_id;
        st->need_parsing = AVSTREAM_PARSE_FULL;
        pes->st = st;
    }
    return st;
}


static PESContext *add_pes_stream(MpegTSContext *ts, int pid, int pcr_pid, int stream_type)
{
    MpegTSFilter *tss;
    PESContext *pes;

    /* if no pid found, then add a pid context */
    pes = av_mallocz(sizeof(PESContext));
    if (!pes)
        return 0;
    pes->ts = ts;
    pes->stream = ts->stream;
    pes->pid = pid;
    pes->pcr_pid = pcr_pid;
    pes->stream_type = stream_type;
    tss = mpegts_open_pes_filter(ts, pid, mpegts_push_data, pes);
    if (!tss) {
        av_free(pes);
        return 0;
    }
    return pes;
}

/* handle one TS packet */
static void handle_packet(MpegTSContext *ts, const uint8_t *packet)
{
    AVFormatContext *s = ts->stream;
    MpegTSFilter *tss;
    int len, pid, cc, cc_ok, afc, is_start;
    const uint8_t *p, *p_end;
    int64_t pos;

    pid = AV_RB16(packet + 1) & 0x1fff;
    if(pid && discard_pid(ts, pid))
        return;
    is_start = packet[1] & 0x40;
    tss = ts->pids[pid];
    if (ts->auto_guess && tss == NULL && is_start) {
        add_pes_stream(ts, pid, -1, 0);
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

    pos = url_ftell(ts->stream->pb);
    ts->pos47= pos % ts->raw_packet_size;

    if (tss->type == MPEGTS_SECTION) {
        if (is_start) {
            /* pointer field present */
            len = *p++;
            if (p + len > p_end)
                return;
            if (len && cc_ok) {
                /* write remaining section bytes */
                write_section_data(s, tss,
                                   p, len, 0);
                /* check whether filter has been closed */
                if (!ts->pids[pid])
                    return;
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
        // Note: The position here points actually behind the current packet.
        tss->u.pes_filter.pes_cb(tss,
                                 p, p_end - p, is_start, pos - ts->raw_packet_size);
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
            return AVERROR(EIO);
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
    ByteIOContext *pb = s->pb;
    uint8_t packet[TS_PACKET_SIZE];
    int packet_num, ret;

    ts->stop_parse = 0;
    packet_num = 0;
    for(;;) {
        if (ts->stop_parse>0)
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
    int score, fec_score, dvhs_score;
    int check_count= size / TS_FEC_PACKET_SIZE;
#define CHECK_COUNT 10

    if (check_count < CHECK_COUNT)
        return -1;

    score     = analyze(p->buf, TS_PACKET_SIZE     *check_count, TS_PACKET_SIZE     , NULL)*CHECK_COUNT/check_count;
    dvhs_score= analyze(p->buf, TS_DVHS_PACKET_SIZE*check_count, TS_DVHS_PACKET_SIZE, NULL)*CHECK_COUNT/check_count;
    fec_score = analyze(p->buf, TS_FEC_PACKET_SIZE *check_count, TS_FEC_PACKET_SIZE , NULL)*CHECK_COUNT/check_count;
//    av_log(NULL, AV_LOG_DEBUG, "score: %d, dvhs_score: %d, fec_score: %d \n", score, dvhs_score, fec_score);

// we need a clear definition for the returned score otherwise things will become messy sooner or later
    if     (score > fec_score && score > dvhs_score && score > 6) return AVPROBE_SCORE_MAX + score     - CHECK_COUNT;
    else if(dvhs_score > score && dvhs_score > fec_score && dvhs_score > 6) return AVPROBE_SCORE_MAX + dvhs_score  - CHECK_COUNT;
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

/* return the 90kHz PCR and the extension for the 27MHz PCR. return
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
    v = AV_RB32(p);
    *ppcr_high = ((int64_t)v << 1) | (p[4] >> 7);
    *ppcr_low = ((p[4] & 1) << 8) | p[5];
    return 0;
}

static int mpegts_read_header(AVFormatContext *s,
                              AVFormatParameters *ap)
{
    MpegTSContext *ts = s->priv_data;
    ByteIOContext *pb = s->pb;
    uint8_t buf[5*1024];
    int len;
    int64_t pos;

    if (ap) {
        ts->mpeg2ts_compute_pcr = ap->mpeg2ts_compute_pcr;
        if(ap->mpeg2ts_raw){
            av_log(s, AV_LOG_ERROR, "use mpegtsraw_demuxer!\n");
            return -1;
        }
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

    if (s->iformat == &mpegts_demuxer) {
        /* normal demux */

        /* first do a scaning to get all the services */
        url_fseek(pb, pos, SEEK_SET);
        mpegts_scan_sdt(ts);

        mpegts_set_service(ts);

        handle_packets(ts, s->probesize);
        /* if could not find service, enable auto_guess */

        ts->auto_guess = 1;

#ifdef DEBUG_SI
        av_log(ts->stream, AV_LOG_DEBUG, "tuning done\n");
#endif
        s->ctx_flags |= AVFMTCTX_NOHEADER;
    } else {
        AVStream *st;
        int pcr_pid, pid, nb_packets, nb_pcrs, ret, pcr_l;
        int64_t pcrs[2], pcr_h;
        int packet_count[2];
        uint8_t packet[TS_PACKET_SIZE];

        /* only read packets */

        st = av_new_stream(s, 0);
        if (!st)
            goto fail;
        av_set_pts_info(st, 60, 1, 27000000);
        st->codec->codec_type = CODEC_TYPE_DATA;
        st->codec->codec_id = CODEC_ID_MPEG2TS;

        /* we iterate until we find two PCRs to estimate the bitrate */
        pcr_pid = -1;
        nb_pcrs = 0;
        nb_packets = 0;
        for(;;) {
            ret = read_packet(s->pb, packet, ts->raw_packet_size);
            if (ret < 0)
                return -1;
            pid = AV_RB16(packet + 1) & 0x1fff;
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

        /* NOTE1: the bitrate is computed without the FEC */
        /* NOTE2: it is only the bitrate of the start of the stream */
        ts->pcr_incr = (pcrs[1] - pcrs[0]) / (packet_count[1] - packet_count[0]);
        ts->cur_pcr = pcrs[0] - ts->pcr_incr * packet_count[0];
        s->bit_rate = (TS_PACKET_SIZE * 8) * 27e6 / ts->pcr_incr;
        st->codec->bit_rate = s->bit_rate;
        st->start_time = ts->cur_pcr;
#if 0
        av_log(ts->stream, AV_LOG_DEBUG, "start=%0.3f pcr=%0.3f incr=%d\n",
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
        return AVERROR(ENOMEM);
    pkt->pos= url_ftell(s->pb);
    ret = read_packet(s->pb, pkt->data, ts->raw_packet_size);
    if (ret < 0) {
        av_free_packet(pkt);
        return ret;
    }
    if (ts->mpeg2ts_compute_pcr) {
        /* compute exact PCR for each packet */
        if (parse_pcr(&pcr_h, &pcr_l, pkt->data) == 0) {
            /* we read the next PCR (XXX: optimize it by using a bigger buffer */
            pos = url_ftell(s->pb);
            for(i = 0; i < MAX_PACKET_READAHEAD; i++) {
                url_fseek(s->pb, pos + i * ts->raw_packet_size, SEEK_SET);
                get_buffer(s->pb, pcr_buf, 12);
                if (parse_pcr(&next_pcr_h, &next_pcr_l, pcr_buf) == 0) {
                    /* XXX: not precise enough */
                    ts->pcr_incr = ((next_pcr_h - pcr_h) * 300 + (next_pcr_l - pcr_l)) /
                        (i + 1);
                    break;
                }
            }
            url_fseek(s->pb, pos, SEEK_SET);
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

    ts->pkt = pkt;
    return handle_packets(ts, 0);
}

static int mpegts_read_close(AVFormatContext *s)
{
    MpegTSContext *ts = s->priv_data;
    int i;

    clear_programs(ts);

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
    int pcr_l, pcr_pid = ((PESContext*)s->streams[stream_index]->priv_data)->pcr_pid;
    const int find_next= 1;
    pos = ((*ppos  + ts->raw_packet_size - 1 - ts->pos47) / ts->raw_packet_size) * ts->raw_packet_size + ts->pos47;
    if (find_next) {
        for(;;) {
            url_fseek(s->pb, pos, SEEK_SET);
            if (get_buffer(s->pb, buf, TS_PACKET_SIZE) != TS_PACKET_SIZE)
                return AV_NOPTS_VALUE;
            if ((pcr_pid < 0 || (AV_RB16(buf + 1) & 0x1fff) == pcr_pid) &&
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
            url_fseek(s->pb, pos, SEEK_SET);
            if (get_buffer(s->pb, buf, TS_PACKET_SIZE) != TS_PACKET_SIZE)
                return AV_NOPTS_VALUE;
            if ((pcr_pid < 0 || (AV_RB16(buf + 1) & 0x1fff) == pcr_pid) &&
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

    pos= url_ftell(s->pb);

    for(;;) {
        url_fseek(s->pb, pos, SEEK_SET);
        if (get_buffer(s->pb, buf, TS_PACKET_SIZE) != TS_PACKET_SIZE)
            return -1;
//        pid = AV_RB16(buf + 1) & 0x1fff;
        if(buf[1] & 0x40) break;
        pos += ts->raw_packet_size;
    }
    url_fseek(s->pb, pos, SEEK_SET);

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
        if (ts->stop_parse>0)
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

AVInputFormat mpegts_demuxer = {
    "mpegts",
    NULL_IF_CONFIG_SMALL("MPEG-2 transport stream format"),
    sizeof(MpegTSContext),
    mpegts_probe,
    mpegts_read_header,
    mpegts_read_packet,
    mpegts_read_close,
    read_seek,
    mpegts_get_pcr,
    .flags = AVFMT_SHOW_IDS|AVFMT_TS_DISCONT,
};

AVInputFormat mpegtsraw_demuxer = {
    "mpegtsraw",
    NULL_IF_CONFIG_SMALL("MPEG-2 raw transport stream format"),
    sizeof(MpegTSContext),
    NULL,
    mpegts_read_header,
    mpegts_raw_read_packet,
    mpegts_read_close,
    read_seek,
    mpegts_get_pcr,
    .flags = AVFMT_SHOW_IDS|AVFMT_TS_DISCONT,
};
