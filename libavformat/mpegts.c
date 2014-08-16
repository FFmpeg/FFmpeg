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

#include "libavutil/buffer.h"
#include "libavutil/crc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/get_bits.h"
#include "avformat.h"
#include "mpegts.h"
#include "internal.h"
#include "avio_internal.h"
#include "seek.h"
#include "mpeg.h"
#include "isom.h"

/* maximum size in which we look for synchronisation if
 * synchronisation is lost */
#define MAX_RESYNC_SIZE 65536

#define MAX_PES_PAYLOAD 200 * 1024

#define MAX_MP4_DESCR_COUNT 16

#define MOD_UNLIKELY(modulus, dividend, divisor, prev_dividend)                \
    do {                                                                       \
        if ((prev_dividend) == 0 || (dividend) - (prev_dividend) != (divisor)) \
            (modulus) = (dividend) % (divisor);                                \
        (prev_dividend) = (dividend);                                          \
    } while (0)

enum MpegTSFilterType {
    MPEGTS_PES,
    MPEGTS_SECTION,
    MPEGTS_PCR,
};

typedef struct MpegTSFilter MpegTSFilter;

typedef int PESCallback (MpegTSFilter *f, const uint8_t *buf, int len,
                         int is_start, int64_t pos);

typedef struct MpegTSPESFilter {
    PESCallback *pes_cb;
    void *opaque;
} MpegTSPESFilter;

typedef void SectionCallback (MpegTSFilter *f, const uint8_t *buf, int len);

typedef void SetServiceCallback (void *opaque, int ret);

typedef struct MpegTSSectionFilter {
    int section_index;
    int section_h_size;
    uint8_t *section_buf;
    unsigned int check_crc : 1;
    unsigned int end_of_section_reached : 1;
    SectionCallback *section_cb;
    void *opaque;
} MpegTSSectionFilter;

struct MpegTSFilter {
    int pid;
    int es_id;
    int last_cc; /* last cc code (-1 if first packet) */
    int64_t last_pcr;
    enum MpegTSFilterType type;
    union {
        MpegTSPESFilter pes_filter;
        MpegTSSectionFilter section_filter;
    } u;
};

#define MAX_PIDS_PER_PROGRAM 64
struct Program {
    unsigned int id; // program id/service id
    unsigned int nb_pids;
    unsigned int pids[MAX_PIDS_PER_PROGRAM];

    /** have we found pmt for this program */
    int pmt_found;
};

struct MpegTSContext {
    const AVClass *class;
    /* user data */
    AVFormatContext *stream;
    /** raw packet size, including FEC if present */
    int raw_packet_size;

    int size_stat[3];
    int size_stat_count;
#define SIZE_STAT_THRESHOLD 10

    int64_t pos47_full;

    /** if true, all pids are analyzed to find streams */
    int auto_guess;

    /** compute exact PCR for each transport stream packet */
    int mpeg2ts_compute_pcr;

    /** fix dvb teletext pts                                 */
    int fix_teletext_pts;

    int64_t cur_pcr;    /**< used to estimate the exact PCR */
    int pcr_incr;       /**< used to estimate the exact PCR */

    /* data needed to handle file based ts */
    /** stop parsing loop */
    int stop_parse;
    /** packet containing Audio/Video data */
    AVPacket *pkt;
    /** to detect seek */
    int64_t last_pos;

    int skip_changes;
    int skip_clear;

    /******************************************/
    /* private mpegts data */
    /* scan context */
    /** structure to keep track of Program->pids mapping */
    unsigned int nb_prg;
    struct Program *prg;

    int8_t crc_validity[NB_PID_MAX];
    /** filters for various streams specified by PMT + for the PAT and PMT */
    MpegTSFilter *pids[NB_PID_MAX];
    int current_pid;
};

static const AVOption mpegtsraw_options[] = {
    { "compute_pcr",   "Compute exact PCR for each transport stream packet.",
          offsetof(MpegTSContext, mpeg2ts_compute_pcr), AV_OPT_TYPE_INT,
          { .i64 = 0 }, 0, 1,  AV_OPT_FLAG_DECODING_PARAM },
    { "ts_packetsize", "Output option carrying the raw packet size.",
      offsetof(MpegTSContext, raw_packet_size), AV_OPT_TYPE_INT,
      { .i64 = 0 }, 0, 0,
      AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_EXPORT | AV_OPT_FLAG_READONLY },
    { NULL },
};

static const AVClass mpegtsraw_class = {
    .class_name = "mpegtsraw demuxer",
    .item_name  = av_default_item_name,
    .option     = mpegtsraw_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVOption mpegts_options[] = {
    {"fix_teletext_pts", "Try to fix pts values of dvb teletext streams.", offsetof(MpegTSContext, fix_teletext_pts), AV_OPT_TYPE_INT,
     {.i64 = 1}, 0, 1, AV_OPT_FLAG_DECODING_PARAM },
    {"ts_packetsize", "Output option carrying the raw packet size.", offsetof(MpegTSContext, raw_packet_size), AV_OPT_TYPE_INT,
     {.i64 = 0}, 0, 0, AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_EXPORT | AV_OPT_FLAG_READONLY },
    {"skip_changes", "Skip changing / adding streams / programs.", offsetof(MpegTSContext, skip_changes), AV_OPT_TYPE_INT,
     {.i64 = 0}, 0, 1, 0 },
    {"skip_clear", "Skip clearing programs.", offsetof(MpegTSContext, skip_clear), AV_OPT_TYPE_INT,
     {.i64 = 0}, 0, 1, 0 },
    { NULL },
};

static const AVClass mpegts_class = {
    .class_name = "mpegts demuxer",
    .item_name  = av_default_item_name,
    .option     = mpegts_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

/* TS stream handling */

enum MpegTSState {
    MPEGTS_HEADER = 0,
    MPEGTS_PESHEADER,
    MPEGTS_PESHEADER_FILL,
    MPEGTS_PAYLOAD,
    MPEGTS_SKIP,
};

/* enough for PES header + length */
#define PES_START_SIZE  6
#define PES_HEADER_SIZE 9
#define MAX_PES_HEADER_SIZE (9 + 255)

typedef struct PESContext {
    int pid;
    int pcr_pid; /**< if -1 then all packets containing PCR are considered */
    int stream_type;
    MpegTSContext *ts;
    AVFormatContext *stream;
    AVStream *st;
    AVStream *sub_st; /**< stream for the embedded AC3 stream in HDMV TrueHD */
    enum MpegTSState state;
    /* used to get the format */
    int data_index;
    int flags; /**< copied to the AVPacket flags */
    int total_size;
    int pes_header_size;
    int extended_stream_id;
    int64_t pts, dts;
    int64_t ts_packet_pos; /**< position of first TS packet of this PES packet */
    uint8_t header[MAX_PES_HEADER_SIZE];
    AVBufferRef *buffer;
    SLConfigDescr sl;
} PESContext;

extern AVInputFormat ff_mpegts_demuxer;

static struct Program * get_program(MpegTSContext *ts, unsigned int programid)
{
    int i;
    for (i = 0; i < ts->nb_prg; i++) {
        if (ts->prg[i].id == programid) {
            return &ts->prg[i];
        }
    }
    return NULL;
}

static void clear_avprogram(MpegTSContext *ts, unsigned int programid)
{
    AVProgram *prg = NULL;
    int i;

    for (i = 0; i < ts->stream->nb_programs; i++)
        if (ts->stream->programs[i]->id == programid) {
            prg = ts->stream->programs[i];
            break;
        }
    if (!prg)
        return;
    prg->nb_stream_indexes = 0;
}

static void clear_program(MpegTSContext *ts, unsigned int programid)
{
    int i;

    clear_avprogram(ts, programid);
    for (i = 0; i < ts->nb_prg; i++)
        if (ts->prg[i].id == programid) {
            ts->prg[i].nb_pids = 0;
            ts->prg[i].pmt_found = 0;
        }
}

static void clear_programs(MpegTSContext *ts)
{
    av_freep(&ts->prg);
    ts->nb_prg = 0;
}

static void add_pat_entry(MpegTSContext *ts, unsigned int programid)
{
    struct Program *p;
    if (av_reallocp_array(&ts->prg, ts->nb_prg + 1, sizeof(*ts->prg)) < 0) {
        ts->nb_prg = 0;
        return;
    }
    p = &ts->prg[ts->nb_prg];
    p->id = programid;
    p->nb_pids = 0;
    p->pmt_found = 0;
    ts->nb_prg++;
}

static void add_pid_to_pmt(MpegTSContext *ts, unsigned int programid,
                           unsigned int pid)
{
    struct Program *p = get_program(ts, programid);
    if (!p)
        return;

    if (p->nb_pids >= MAX_PIDS_PER_PROGRAM)
        return;
    p->pids[p->nb_pids++] = pid;
}

static void set_pmt_found(MpegTSContext *ts, unsigned int programid)
{
    struct Program *p = get_program(ts, programid);
    if (!p)
        return;

    p->pmt_found = 1;
}

static void set_pcr_pid(AVFormatContext *s, unsigned int programid, unsigned int pid)
{
    int i;
    for (i = 0; i < s->nb_programs; i++) {
        if (s->programs[i]->id == programid) {
            s->programs[i]->pcr_pid = pid;
            break;
        }
    }
}

/**
 * @brief discard_pid() decides if the pid is to be discarded according
 *                      to caller's programs selection
 * @param ts    : - TS context
 * @param pid   : - pid
 * @return 1 if the pid is only comprised in programs that have .discard=AVDISCARD_ALL
 *         0 otherwise
 */
static int discard_pid(MpegTSContext *ts, unsigned int pid)
{
    int i, j, k;
    int used = 0, discarded = 0;
    struct Program *p;

    /* If none of the programs have .discard=AVDISCARD_ALL then there's
     * no way we have to discard this packet */
    for (k = 0; k < ts->stream->nb_programs; k++)
        if (ts->stream->programs[k]->discard == AVDISCARD_ALL)
            break;
    if (k == ts->stream->nb_programs)
        return 0;

    for (i = 0; i < ts->nb_prg; i++) {
        p = &ts->prg[i];
        for (j = 0; j < p->nb_pids; j++) {
            if (p->pids[j] != pid)
                continue;
            // is program with id p->id set to be discarded?
            for (k = 0; k < ts->stream->nb_programs; k++) {
                if (ts->stream->programs[k]->id == p->id) {
                    if (ts->stream->programs[k]->discard == AVDISCARD_ALL)
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
 *  Assemble PES packets out of TS packets, and then call the "section_cb"
 *  function when they are complete.
 */
static void write_section_data(MpegTSContext *ts, MpegTSFilter *tss1,
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

    if (tss->section_h_size != -1 &&
        tss->section_index >= tss->section_h_size) {
        int crc_valid = 1;
        tss->end_of_section_reached = 1;

        if (tss->check_crc) {
            crc_valid = !av_crc(av_crc_get_table(AV_CRC_32_IEEE), -1, tss->section_buf, tss->section_h_size);
            if (crc_valid) {
                ts->crc_validity[ tss1->pid ] = 100;
            }else if (ts->crc_validity[ tss1->pid ] > -10) {
                ts->crc_validity[ tss1->pid ]--;
            }else
                crc_valid = 2;
        }
        if (crc_valid)
            tss->section_cb(tss1, tss->section_buf, tss->section_h_size);
    }
}

static MpegTSFilter *mpegts_open_filter(MpegTSContext *ts, unsigned int pid,
                                        enum MpegTSFilterType type)
{
    MpegTSFilter *filter;

    av_dlog(ts->stream, "Filter: pid=0x%x\n", pid);

    if (pid >= NB_PID_MAX || ts->pids[pid])
        return NULL;
    filter = av_mallocz(sizeof(MpegTSFilter));
    if (!filter)
        return NULL;
    ts->pids[pid] = filter;

    filter->type    = type;
    filter->pid     = pid;
    filter->es_id   = -1;
    filter->last_cc = -1;
    filter->last_pcr= -1;

    return filter;
}

static MpegTSFilter *mpegts_open_section_filter(MpegTSContext *ts,
                                                unsigned int pid,
                                                SectionCallback *section_cb,
                                                void *opaque,
                                                int check_crc)
{
    MpegTSFilter *filter;
    MpegTSSectionFilter *sec;

    if (!(filter = mpegts_open_filter(ts, pid, MPEGTS_SECTION)))
        return NULL;
    sec = &filter->u.section_filter;
    sec->section_cb  = section_cb;
    sec->opaque      = opaque;
    sec->section_buf = av_malloc(MAX_SECTION_SIZE);
    sec->check_crc   = check_crc;
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

    if (!(filter = mpegts_open_filter(ts, pid, MPEGTS_PES)))
        return NULL;

    pes = &filter->u.pes_filter;
    pes->pes_cb = pes_cb;
    pes->opaque = opaque;
    return filter;
}

static MpegTSFilter *mpegts_open_pcr_filter(MpegTSContext *ts, unsigned int pid)
{
    return mpegts_open_filter(ts, pid, MPEGTS_PCR);
}

static void mpegts_close_filter(MpegTSContext *ts, MpegTSFilter *filter)
{
    int pid;

    pid = filter->pid;
    if (filter->type == MPEGTS_SECTION)
        av_freep(&filter->u.section_filter.section_buf);
    else if (filter->type == MPEGTS_PES) {
        PESContext *pes = filter->u.pes_filter.opaque;
        av_buffer_unref(&pes->buffer);
        /* referenced private data will be freed later in
         * avformat_close_input */
        if (!((PESContext *)filter->u.pes_filter.opaque)->st) {
            av_freep(&filter->u.pes_filter.opaque);
        }
    }

    av_free(filter);
    ts->pids[pid] = NULL;
}

static int analyze(const uint8_t *buf, int size, int packet_size, int *index)
{
    int stat[TS_MAX_PACKET_SIZE];
    int i;
    int best_score = 0;
    int best_score2 = 0;

    memset(stat, 0, packet_size * sizeof(*stat));

    for (i = 0; i < size - 3; i++) {
        if (buf[i] == 0x47 && !(buf[i + 1] & 0x80) && buf[i + 3] != 0x47) {
            int x = i % packet_size;
            stat[x]++;
            if (stat[x] > best_score) {
                best_score = stat[x];
                if (index)
                    *index = x;
            } else if (stat[x] > best_score2) {
                best_score2 = stat[x];
            }
        }
    }

    return best_score - best_score2;
}

/* autodetect fec presence. Must have at least 1024 bytes  */
static int get_packet_size(const uint8_t *buf, int size)
{
    int score, fec_score, dvhs_score;

    if (size < (TS_FEC_PACKET_SIZE * 5 + 1))
        return AVERROR_INVALIDDATA;

    score      = analyze(buf, size, TS_PACKET_SIZE, NULL);
    dvhs_score = analyze(buf, size, TS_DVHS_PACKET_SIZE, NULL);
    fec_score  = analyze(buf, size, TS_FEC_PACKET_SIZE, NULL);
    av_dlog(NULL, "score: %d, dvhs_score: %d, fec_score: %d \n",
            score, dvhs_score, fec_score);

    if (score > fec_score && score > dvhs_score)
        return TS_PACKET_SIZE;
    else if (dvhs_score > score && dvhs_score > fec_score)
        return TS_DVHS_PACKET_SIZE;
    else if (score < fec_score && dvhs_score < fec_score)
        return TS_FEC_PACKET_SIZE;
    else
        return AVERROR_INVALIDDATA;
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
        return AVERROR_INVALIDDATA;
    c   = *p++;
    *pp = p;
    return c;
}

static inline int get16(const uint8_t **pp, const uint8_t *p_end)
{
    const uint8_t *p;
    int c;

    p = *pp;
    if ((p + 1) >= p_end)
        return AVERROR_INVALIDDATA;
    c   = AV_RB16(p);
    p  += 2;
    *pp = p;
    return c;
}

/* read and allocate a DVB string preceded by its length */
static char *getstr8(const uint8_t **pp, const uint8_t *p_end)
{
    int len;
    const uint8_t *p;
    char *str;

    p   = *pp;
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
    p  += len;
    *pp = p;
    return str;
}

static int parse_section_header(SectionHeader *h,
                                const uint8_t **pp, const uint8_t *p_end)
{
    int val;

    val = get8(pp, p_end);
    if (val < 0)
        return val;
    h->tid = val;
    *pp += 2;
    val  = get16(pp, p_end);
    if (val < 0)
        return val;
    h->id = val;
    val = get8(pp, p_end);
    if (val < 0)
        return val;
    h->version = (val >> 1) & 0x1f;
    val = get8(pp, p_end);
    if (val < 0)
        return val;
    h->sec_num = val;
    val = get8(pp, p_end);
    if (val < 0)
        return val;
    h->last_sec_num = val;
    return 0;
}

typedef struct {
    uint32_t stream_type;
    enum AVMediaType codec_type;
    enum AVCodecID codec_id;
} StreamType;

static const StreamType ISO_types[] = {
    { 0x01, AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_MPEG2VIDEO },
    { 0x02, AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_MPEG2VIDEO },
    { 0x03, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_MP3        },
    { 0x04, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_MP3        },
    { 0x0f, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_AAC        },
    { 0x10, AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_MPEG4      },
    /* Makito encoder sets stream type 0x11 for AAC,
     * so auto-detect LOAS/LATM instead of hardcoding it. */
#if !CONFIG_LOAS_DEMUXER
    { 0x11, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_AAC_LATM   }, /* LATM syntax */
#endif
    { 0x1b, AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_H264       },
    { 0x24, AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_HEVC       },
    { 0x42, AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_CAVS       },
    { 0xd1, AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_DIRAC      },
    { 0xea, AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_VC1        },
    { 0 },
};

static const StreamType HDMV_types[] = {
    { 0x80, AVMEDIA_TYPE_AUDIO,    AV_CODEC_ID_PCM_BLURAY        },
    { 0x81, AVMEDIA_TYPE_AUDIO,    AV_CODEC_ID_AC3               },
    { 0x82, AVMEDIA_TYPE_AUDIO,    AV_CODEC_ID_DTS               },
    { 0x83, AVMEDIA_TYPE_AUDIO,    AV_CODEC_ID_TRUEHD            },
    { 0x84, AVMEDIA_TYPE_AUDIO,    AV_CODEC_ID_EAC3              },
    { 0x85, AVMEDIA_TYPE_AUDIO,    AV_CODEC_ID_DTS               }, /* DTS HD */
    { 0x86, AVMEDIA_TYPE_AUDIO,    AV_CODEC_ID_DTS               }, /* DTS HD MASTER*/
    { 0xa1, AVMEDIA_TYPE_AUDIO,    AV_CODEC_ID_EAC3              }, /* E-AC3 Secondary Audio */
    { 0xa2, AVMEDIA_TYPE_AUDIO,    AV_CODEC_ID_DTS               }, /* DTS Express Secondary Audio */
    { 0x90, AVMEDIA_TYPE_SUBTITLE, AV_CODEC_ID_HDMV_PGS_SUBTITLE },
    { 0 },
};

/* ATSC ? */
static const StreamType MISC_types[] = {
    { 0x81, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_AC3 },
    { 0x8a, AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_DTS },
    { 0 },
};

static const StreamType REGD_types[] = {
    { MKTAG('d', 'r', 'a', 'c'), AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_DIRAC },
    { MKTAG('A', 'C', '-', '3'), AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_AC3   },
    { MKTAG('B', 'S', 'S', 'D'), AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_S302M },
    { MKTAG('D', 'T', 'S', '1'), AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_DTS   },
    { MKTAG('D', 'T', 'S', '2'), AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_DTS   },
    { MKTAG('D', 'T', 'S', '3'), AVMEDIA_TYPE_AUDIO, AV_CODEC_ID_DTS   },
    { MKTAG('H', 'E', 'V', 'C'), AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_HEVC  },
    { MKTAG('K', 'L', 'V', 'A'), AVMEDIA_TYPE_DATA,  AV_CODEC_ID_SMPTE_KLV },
    { MKTAG('V', 'C', '-', '1'), AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_VC1   },
    { 0 },
};

static const StreamType METADATA_types[] = {
    { MKTAG('K','L','V','A'), AVMEDIA_TYPE_DATA, AV_CODEC_ID_SMPTE_KLV },
    { MKTAG('I','D','3',' '), AVMEDIA_TYPE_DATA, AV_CODEC_ID_TIMED_ID3 },
    { 0 },
};

/* descriptor present */
static const StreamType DESC_types[] = {
    { 0x6a, AVMEDIA_TYPE_AUDIO,    AV_CODEC_ID_AC3          }, /* AC-3 descriptor */
    { 0x7a, AVMEDIA_TYPE_AUDIO,    AV_CODEC_ID_EAC3         }, /* E-AC-3 descriptor */
    { 0x7b, AVMEDIA_TYPE_AUDIO,    AV_CODEC_ID_DTS          },
    { 0x56, AVMEDIA_TYPE_SUBTITLE, AV_CODEC_ID_DVB_TELETEXT },
    { 0x59, AVMEDIA_TYPE_SUBTITLE, AV_CODEC_ID_DVB_SUBTITLE }, /* subtitling descriptor */
    { 0 },
};

static void mpegts_find_stream_type(AVStream *st,
                                    uint32_t stream_type,
                                    const StreamType *types)
{
    if (avcodec_is_open(st->codec)) {
        av_log(NULL, AV_LOG_DEBUG, "cannot set stream info, codec is open\n");
        return;
    }

    for (; types->stream_type; types++)
        if (stream_type == types->stream_type) {
            st->codec->codec_type = types->codec_type;
            st->codec->codec_id   = types->codec_id;
            st->request_probe     = 0;
            return;
        }
}

static int mpegts_set_stream_info(AVStream *st, PESContext *pes,
                                  uint32_t stream_type, uint32_t prog_reg_desc)
{
    int old_codec_type = st->codec->codec_type;
    int old_codec_id  = st->codec->codec_id;

    if (avcodec_is_open(st->codec)) {
        av_log(pes->stream, AV_LOG_DEBUG, "cannot set stream info, codec is open\n");
        return 0;
    }

    avpriv_set_pts_info(st, 33, 1, 90000);
    st->priv_data         = pes;
    st->codec->codec_type = AVMEDIA_TYPE_DATA;
    st->codec->codec_id   = AV_CODEC_ID_NONE;
    st->need_parsing      = AVSTREAM_PARSE_FULL;
    pes->st          = st;
    pes->stream_type = stream_type;

    av_log(pes->stream, AV_LOG_DEBUG,
           "stream=%d stream_type=%x pid=%x prog_reg_desc=%.4s\n",
           st->index, pes->stream_type, pes->pid, (char *)&prog_reg_desc);

    st->codec->codec_tag = pes->stream_type;

    mpegts_find_stream_type(st, pes->stream_type, ISO_types);
    if ((prog_reg_desc == AV_RL32("HDMV") ||
         prog_reg_desc == AV_RL32("HDPR")) &&
        st->codec->codec_id == AV_CODEC_ID_NONE) {
        mpegts_find_stream_type(st, pes->stream_type, HDMV_types);
        if (pes->stream_type == 0x83) {
            // HDMV TrueHD streams also contain an AC3 coded version of the
            // audio track - add a second stream for this
            AVStream *sub_st;
            // priv_data cannot be shared between streams
            PESContext *sub_pes = av_malloc(sizeof(*sub_pes));
            if (!sub_pes)
                return AVERROR(ENOMEM);
            memcpy(sub_pes, pes, sizeof(*sub_pes));

            sub_st = avformat_new_stream(pes->stream, NULL);
            if (!sub_st) {
                av_free(sub_pes);
                return AVERROR(ENOMEM);
            }

            sub_st->id = pes->pid;
            avpriv_set_pts_info(sub_st, 33, 1, 90000);
            sub_st->priv_data         = sub_pes;
            sub_st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
            sub_st->codec->codec_id   = AV_CODEC_ID_AC3;
            sub_st->need_parsing      = AVSTREAM_PARSE_FULL;
            sub_pes->sub_st           = pes->sub_st = sub_st;
        }
    }
    if (st->codec->codec_id == AV_CODEC_ID_NONE)
        mpegts_find_stream_type(st, pes->stream_type, MISC_types);
    if (st->codec->codec_id == AV_CODEC_ID_NONE) {
        st->codec->codec_id  = old_codec_id;
        st->codec->codec_type = old_codec_type;
    }

    return 0;
}

static void reset_pes_packet_state(PESContext *pes)
{
    pes->pts        = AV_NOPTS_VALUE;
    pes->dts        = AV_NOPTS_VALUE;
    pes->data_index = 0;
    pes->flags      = 0;
    av_buffer_unref(&pes->buffer);
}

static void new_pes_packet(PESContext *pes, AVPacket *pkt)
{
    av_init_packet(pkt);

    pkt->buf  = pes->buffer;
    pkt->data = pes->buffer->data;
    pkt->size = pes->data_index;

    if (pes->total_size != MAX_PES_PAYLOAD &&
        pes->pes_header_size + pes->data_index != pes->total_size +
        PES_START_SIZE) {
        av_log(pes->stream, AV_LOG_WARNING, "PES packet size mismatch\n");
        pes->flags |= AV_PKT_FLAG_CORRUPT;
    }
    memset(pkt->data + pkt->size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    // Separate out the AC3 substream from an HDMV combined TrueHD/AC3 PID
    if (pes->sub_st && pes->stream_type == 0x83 && pes->extended_stream_id == 0x76)
        pkt->stream_index = pes->sub_st->index;
    else
        pkt->stream_index = pes->st->index;
    pkt->pts = pes->pts;
    pkt->dts = pes->dts;
    /* store position of first TS packet of this PES packet */
    pkt->pos   = pes->ts_packet_pos;
    pkt->flags = pes->flags;

    pes->buffer = NULL;
    reset_pes_packet_state(pes);
}

static uint64_t get_ts64(GetBitContext *gb, int bits)
{
    if (get_bits_left(gb) < bits)
        return AV_NOPTS_VALUE;
    return get_bits64(gb, bits);
}

static int read_sl_header(PESContext *pes, SLConfigDescr *sl,
                          const uint8_t *buf, int buf_size)
{
    GetBitContext gb;
    int au_start_flag = 0, au_end_flag = 0, ocr_flag = 0, idle_flag = 0;
    int padding_flag = 0, padding_bits = 0, inst_bitrate_flag = 0;
    int dts_flag = -1, cts_flag = -1;
    int64_t dts = AV_NOPTS_VALUE, cts = AV_NOPTS_VALUE;

    init_get_bits(&gb, buf, buf_size * 8);

    if (sl->use_au_start)
        au_start_flag = get_bits1(&gb);
    if (sl->use_au_end)
        au_end_flag = get_bits1(&gb);
    if (!sl->use_au_start && !sl->use_au_end)
        au_start_flag = au_end_flag = 1;
    if (sl->ocr_len > 0)
        ocr_flag = get_bits1(&gb);
    if (sl->use_idle)
        idle_flag = get_bits1(&gb);
    if (sl->use_padding)
        padding_flag = get_bits1(&gb);
    if (padding_flag)
        padding_bits = get_bits(&gb, 3);

    if (!idle_flag && (!padding_flag || padding_bits != 0)) {
        if (sl->packet_seq_num_len)
            skip_bits_long(&gb, sl->packet_seq_num_len);
        if (sl->degr_prior_len)
            if (get_bits1(&gb))
                skip_bits(&gb, sl->degr_prior_len);
        if (ocr_flag)
            skip_bits_long(&gb, sl->ocr_len);
        if (au_start_flag) {
            if (sl->use_rand_acc_pt)
                get_bits1(&gb);
            if (sl->au_seq_num_len > 0)
                skip_bits_long(&gb, sl->au_seq_num_len);
            if (sl->use_timestamps) {
                dts_flag = get_bits1(&gb);
                cts_flag = get_bits1(&gb);
            }
        }
        if (sl->inst_bitrate_len)
            inst_bitrate_flag = get_bits1(&gb);
        if (dts_flag == 1)
            dts = get_ts64(&gb, sl->timestamp_len);
        if (cts_flag == 1)
            cts = get_ts64(&gb, sl->timestamp_len);
        if (sl->au_len > 0)
            skip_bits_long(&gb, sl->au_len);
        if (inst_bitrate_flag)
            skip_bits_long(&gb, sl->inst_bitrate_len);
    }

    if (dts != AV_NOPTS_VALUE)
        pes->dts = dts;
    if (cts != AV_NOPTS_VALUE)
        pes->pts = cts;

    if (sl->timestamp_len && sl->timestamp_res)
        avpriv_set_pts_info(pes->st, sl->timestamp_len, 1, sl->timestamp_res);

    return (get_bits_count(&gb) + 7) >> 3;
}

/* return non zero if a packet could be constructed */
static int mpegts_push_data(MpegTSFilter *filter,
                            const uint8_t *buf, int buf_size, int is_start,
                            int64_t pos)
{
    PESContext *pes   = filter->u.pes_filter.opaque;
    MpegTSContext *ts = pes->ts;
    const uint8_t *p;
    int len, code;

    if (!ts->pkt)
        return 0;

    if (is_start) {
        if (pes->state == MPEGTS_PAYLOAD && pes->data_index > 0) {
            new_pes_packet(pes, ts->pkt);
            ts->stop_parse = 1;
        } else {
            reset_pes_packet_state(pes);
        }
        pes->state         = MPEGTS_HEADER;
        pes->ts_packet_pos = pos;
    }
    p = buf;
    while (buf_size > 0) {
        switch (pes->state) {
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
                 * decide */
                if (pes->header[0] == 0x00 && pes->header[1] == 0x00 &&
                    pes->header[2] == 0x01) {
                    /* it must be an mpeg2 PES stream */
                    code = pes->header[3] | 0x100;
                    av_dlog(pes->stream, "pid=%x pes_code=%#x\n", pes->pid,
                            code);

                    if ((pes->st && pes->st->discard == AVDISCARD_ALL &&
                         (!pes->sub_st ||
                          pes->sub_st->discard == AVDISCARD_ALL)) ||
                        code == 0x1be) /* padding_stream */
                        goto skip;

                    /* stream not present in PMT */
                    if (!pes->st) {
                        if (ts->skip_changes)
                            goto skip;

                        pes->st = avformat_new_stream(ts->stream, NULL);
                        if (!pes->st)
                            return AVERROR(ENOMEM);
                        pes->st->id = pes->pid;
                        mpegts_set_stream_info(pes->st, pes, 0, 0);
                    }

                    pes->total_size = AV_RB16(pes->header + 4);
                    /* NOTE: a zero total size means the PES size is
                     * unbounded */
                    if (!pes->total_size)
                        pes->total_size = MAX_PES_PAYLOAD;

                    /* allocate pes buffer */
                    pes->buffer = av_buffer_alloc(pes->total_size +
                                                  FF_INPUT_BUFFER_PADDING_SIZE);
                    if (!pes->buffer)
                        return AVERROR(ENOMEM);

                    if (code != 0x1bc && code != 0x1bf && /* program_stream_map, private_stream_2 */
                        code != 0x1f0 && code != 0x1f1 && /* ECM, EMM */
                        code != 0x1ff && code != 0x1f2 && /* program_stream_directory, DSMCC_stream */
                        code != 0x1f8) {                  /* ITU-T Rec. H.222.1 type E stream */
                        pes->state = MPEGTS_PESHEADER;
                        if (pes->st->codec->codec_id == AV_CODEC_ID_NONE && !pes->st->request_probe) {
                            av_dlog(pes->stream,
                                    "pid=%x stream_type=%x probing\n",
                                    pes->pid,
                                    pes->stream_type);
                            pes->st->request_probe = 1;
                        }
                    } else {
                        pes->state      = MPEGTS_PAYLOAD;
                        pes->data_index = 0;
                    }
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
        case MPEGTS_PESHEADER:
            len = PES_HEADER_SIZE - pes->data_index;
            if (len < 0)
                return AVERROR_INVALIDDATA;
            if (len > buf_size)
                len = buf_size;
            memcpy(pes->header + pes->data_index, p, len);
            pes->data_index += len;
            p += len;
            buf_size -= len;
            if (pes->data_index == PES_HEADER_SIZE) {
                pes->pes_header_size = pes->header[8] + 9;
                pes->state           = MPEGTS_PESHEADER_FILL;
            }
            break;
        case MPEGTS_PESHEADER_FILL:
            len = pes->pes_header_size - pes->data_index;
            if (len < 0)
                return AVERROR_INVALIDDATA;
            if (len > buf_size)
                len = buf_size;
            memcpy(pes->header + pes->data_index, p, len);
            pes->data_index += len;
            p += len;
            buf_size -= len;
            if (pes->data_index == pes->pes_header_size) {
                const uint8_t *r;
                unsigned int flags, pes_ext, skip;

                flags = pes->header[7];
                r = pes->header + 9;
                pes->pts = AV_NOPTS_VALUE;
                pes->dts = AV_NOPTS_VALUE;
                if ((flags & 0xc0) == 0x80) {
                    pes->dts = pes->pts = ff_parse_pes_pts(r);
                    r += 5;
                } else if ((flags & 0xc0) == 0xc0) {
                    pes->pts = ff_parse_pes_pts(r);
                    r += 5;
                    pes->dts = ff_parse_pes_pts(r);
                    r += 5;
                }
                pes->extended_stream_id = -1;
                if (flags & 0x01) { /* PES extension */
                    pes_ext = *r++;
                    /* Skip PES private data, program packet sequence counter and P-STD buffer */
                    skip  = (pes_ext >> 4) & 0xb;
                    skip += skip & 0x9;
                    r    += skip;
                    if ((pes_ext & 0x41) == 0x01 &&
                        (r + 2) <= (pes->header + pes->pes_header_size)) {
                        /* PES extension 2 */
                        if ((r[0] & 0x7f) > 0 && (r[1] & 0x80) == 0)
                            pes->extended_stream_id = r[1];
                    }
                }

                /* we got the full header. We parse it and get the payload */
                pes->state = MPEGTS_PAYLOAD;
                pes->data_index = 0;
                if (pes->stream_type == 0x12 && buf_size > 0) {
                    int sl_header_bytes = read_sl_header(pes, &pes->sl, p,
                                                         buf_size);
                    pes->pes_header_size += sl_header_bytes;
                    p += sl_header_bytes;
                    buf_size -= sl_header_bytes;
                }
                if (pes->stream_type == 0x15 && buf_size >= 5) {
                    /* skip metadata access unit header */
                    pes->pes_header_size += 5;
                    p += 5;
                    buf_size -= 5;
                }
                if (pes->ts->fix_teletext_pts && pes->st->codec->codec_id == AV_CODEC_ID_DVB_TELETEXT) {
                    AVProgram *p = NULL;
                    while ((p = av_find_program_from_stream(pes->stream, p, pes->st->index))) {
                        if (p->pcr_pid != -1 && p->discard != AVDISCARD_ALL) {
                            MpegTSFilter *f = pes->ts->pids[p->pcr_pid];
                            if (f) {
                                AVStream *st = NULL;
                                if (f->type == MPEGTS_PES) {
                                    PESContext *pcrpes = f->u.pes_filter.opaque;
                                    if (pcrpes)
                                        st = pcrpes->st;
                                } else if (f->type == MPEGTS_PCR) {
                                    int i;
                                    for (i = 0; i < p->nb_stream_indexes; i++) {
                                        AVStream *pst = pes->stream->streams[p->stream_index[i]];
                                        if (pst->codec->codec_type == AVMEDIA_TYPE_VIDEO)
                                            st = pst;
                                    }
                                }
                                if (f->last_pcr != -1 && st && st->discard != AVDISCARD_ALL) {
                                    // teletext packets do not always have correct timestamps,
                                    // the standard says they should be handled after 40.6 ms at most,
                                    // and the pcr error to this packet should be no more than 100 ms.
                                    // TODO: we should interpolate the PCR, not just use the last one
                                    int64_t pcr = f->last_pcr / 300;
                                    pes->st->pts_wrap_reference = st->pts_wrap_reference;
                                    pes->st->pts_wrap_behavior = st->pts_wrap_behavior;
                                    if (pes->dts == AV_NOPTS_VALUE || pes->dts < pcr) {
                                        pes->pts = pes->dts = pcr;
                                    } else if (pes->dts > pcr + 3654 + 9000) {
                                        pes->pts = pes->dts = pcr + 3654 + 9000;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            break;
        case MPEGTS_PAYLOAD:
            if (pes->buffer) {
                if (pes->data_index > 0 &&
                    pes->data_index + buf_size > pes->total_size) {
                    new_pes_packet(pes, ts->pkt);
                    pes->total_size = MAX_PES_PAYLOAD;
                    pes->buffer = av_buffer_alloc(pes->total_size +
                                                  FF_INPUT_BUFFER_PADDING_SIZE);
                    if (!pes->buffer)
                        return AVERROR(ENOMEM);
                    ts->stop_parse = 1;
                } else if (pes->data_index == 0 &&
                           buf_size > pes->total_size) {
                    // pes packet size is < ts size packet and pes data is padded with 0xff
                    // not sure if this is legal in ts but see issue #2392
                    buf_size = pes->total_size;
                }
                memcpy(pes->buffer->data + pes->data_index, p, buf_size);
                pes->data_index += buf_size;
                /* emit complete packets with known packet size
                 * decreases demuxer delay for infrequent packets like subtitles from
                 * a couple of seconds to milliseconds for properly muxed files.
                 * total_size is the number of bytes following pes_packet_length
                 * in the pes header, i.e. not counting the first PES_START_SIZE bytes */
                if (!ts->stop_parse && pes->total_size < MAX_PES_PAYLOAD &&
                    pes->pes_header_size + pes->data_index == pes->total_size + PES_START_SIZE) {
                    ts->stop_parse = 1;
                    new_pes_packet(pes, ts->pkt);
                }
            }
            buf_size = 0;
            break;
        case MPEGTS_SKIP:
            buf_size = 0;
            break;
        }
    }

    return 0;
}

static PESContext *add_pes_stream(MpegTSContext *ts, int pid, int pcr_pid)
{
    MpegTSFilter *tss;
    PESContext *pes;

    /* if no pid found, then add a pid context */
    pes = av_mallocz(sizeof(PESContext));
    if (!pes)
        return 0;
    pes->ts      = ts;
    pes->stream  = ts->stream;
    pes->pid     = pid;
    pes->pcr_pid = pcr_pid;
    pes->state   = MPEGTS_SKIP;
    pes->pts     = AV_NOPTS_VALUE;
    pes->dts     = AV_NOPTS_VALUE;
    tss          = mpegts_open_pes_filter(ts, pid, mpegts_push_data, pes);
    if (!tss) {
        av_free(pes);
        return 0;
    }
    return pes;
}

#define MAX_LEVEL 4
typedef struct {
    AVFormatContext *s;
    AVIOContext pb;
    Mp4Descr *descr;
    Mp4Descr *active_descr;
    int descr_count;
    int max_descr_count;
    int level;
    int predefined_SLConfigDescriptor_seen;
} MP4DescrParseContext;

static int init_MP4DescrParseContext(MP4DescrParseContext *d, AVFormatContext *s,
                                     const uint8_t *buf, unsigned size,
                                     Mp4Descr *descr, int max_descr_count)
{
    int ret;
    if (size > (1 << 30))
        return AVERROR_INVALIDDATA;

    if ((ret = ffio_init_context(&d->pb, (unsigned char *)buf, size, 0,
                                 NULL, NULL, NULL, NULL)) < 0)
        return ret;

    d->s               = s;
    d->level           = 0;
    d->descr_count     = 0;
    d->descr           = descr;
    d->active_descr    = NULL;
    d->max_descr_count = max_descr_count;

    return 0;
}

static void update_offsets(AVIOContext *pb, int64_t *off, int *len)
{
    int64_t new_off = avio_tell(pb);
    (*len) -= new_off - *off;
    *off    = new_off;
}

static int parse_mp4_descr(MP4DescrParseContext *d, int64_t off, int len,
                           int target_tag);

static int parse_mp4_descr_arr(MP4DescrParseContext *d, int64_t off, int len)
{
    while (len > 0) {
        int ret = parse_mp4_descr(d, off, len, 0);
        if (ret < 0)
            return ret;
        update_offsets(&d->pb, &off, &len);
    }
    return 0;
}

static int parse_MP4IODescrTag(MP4DescrParseContext *d, int64_t off, int len)
{
    avio_rb16(&d->pb); // ID
    avio_r8(&d->pb);
    avio_r8(&d->pb);
    avio_r8(&d->pb);
    avio_r8(&d->pb);
    avio_r8(&d->pb);
    update_offsets(&d->pb, &off, &len);
    return parse_mp4_descr_arr(d, off, len);
}

static int parse_MP4ODescrTag(MP4DescrParseContext *d, int64_t off, int len)
{
    int id_flags;
    if (len < 2)
        return 0;
    id_flags = avio_rb16(&d->pb);
    if (!(id_flags & 0x0020)) { // URL_Flag
        update_offsets(&d->pb, &off, &len);
        return parse_mp4_descr_arr(d, off, len); // ES_Descriptor[]
    } else {
        return 0;
    }
}

static int parse_MP4ESDescrTag(MP4DescrParseContext *d, int64_t off, int len)
{
    int es_id = 0;
    if (d->descr_count >= d->max_descr_count)
        return AVERROR_INVALIDDATA;
    ff_mp4_parse_es_descr(&d->pb, &es_id);
    d->active_descr = d->descr + (d->descr_count++);

    d->active_descr->es_id = es_id;
    update_offsets(&d->pb, &off, &len);
    parse_mp4_descr(d, off, len, MP4DecConfigDescrTag);
    update_offsets(&d->pb, &off, &len);
    if (len > 0)
        parse_mp4_descr(d, off, len, MP4SLDescrTag);
    d->active_descr = NULL;
    return 0;
}

static int parse_MP4DecConfigDescrTag(MP4DescrParseContext *d, int64_t off,
                                      int len)
{
    Mp4Descr *descr = d->active_descr;
    if (!descr)
        return AVERROR_INVALIDDATA;
    d->active_descr->dec_config_descr = av_malloc(len);
    if (!descr->dec_config_descr)
        return AVERROR(ENOMEM);
    descr->dec_config_descr_len = len;
    avio_read(&d->pb, descr->dec_config_descr, len);
    return 0;
}

static int parse_MP4SLDescrTag(MP4DescrParseContext *d, int64_t off, int len)
{
    Mp4Descr *descr = d->active_descr;
    int predefined;
    if (!descr)
        return AVERROR_INVALIDDATA;

    predefined = avio_r8(&d->pb);
    if (!predefined) {
        int lengths;
        int flags = avio_r8(&d->pb);
        descr->sl.use_au_start    = !!(flags & 0x80);
        descr->sl.use_au_end      = !!(flags & 0x40);
        descr->sl.use_rand_acc_pt = !!(flags & 0x20);
        descr->sl.use_padding     = !!(flags & 0x08);
        descr->sl.use_timestamps  = !!(flags & 0x04);
        descr->sl.use_idle        = !!(flags & 0x02);
        descr->sl.timestamp_res   = avio_rb32(&d->pb);
        avio_rb32(&d->pb);
        descr->sl.timestamp_len      = avio_r8(&d->pb);
        if (descr->sl.timestamp_len > 64) {
            avpriv_request_sample(NULL, "timestamp_len > 64");
            descr->sl.timestamp_len = 64;
            return AVERROR_PATCHWELCOME;
        }
        descr->sl.ocr_len            = avio_r8(&d->pb);
        descr->sl.au_len             = avio_r8(&d->pb);
        descr->sl.inst_bitrate_len   = avio_r8(&d->pb);
        lengths                      = avio_rb16(&d->pb);
        descr->sl.degr_prior_len     = lengths >> 12;
        descr->sl.au_seq_num_len     = (lengths >> 7) & 0x1f;
        descr->sl.packet_seq_num_len = (lengths >> 2) & 0x1f;
    } else if (!d->predefined_SLConfigDescriptor_seen){
        avpriv_report_missing_feature(d->s, "Predefined SLConfigDescriptor");
        d->predefined_SLConfigDescriptor_seen = 1;
    }
    return 0;
}

static int parse_mp4_descr(MP4DescrParseContext *d, int64_t off, int len,
                           int target_tag)
{
    int tag;
    int len1 = ff_mp4_read_descr(d->s, &d->pb, &tag);
    update_offsets(&d->pb, &off, &len);
    if (len < 0 || len1 > len || len1 <= 0) {
        av_log(d->s, AV_LOG_ERROR,
               "Tag %x length violation new length %d bytes remaining %d\n",
               tag, len1, len);
        return AVERROR_INVALIDDATA;
    }

    if (d->level++ >= MAX_LEVEL) {
        av_log(d->s, AV_LOG_ERROR, "Maximum MP4 descriptor level exceeded\n");
        goto done;
    }

    if (target_tag && tag != target_tag) {
        av_log(d->s, AV_LOG_ERROR, "Found tag %x expected %x\n", tag,
               target_tag);
        goto done;
    }

    switch (tag) {
    case MP4IODescrTag:
        parse_MP4IODescrTag(d, off, len1);
        break;
    case MP4ODescrTag:
        parse_MP4ODescrTag(d, off, len1);
        break;
    case MP4ESDescrTag:
        parse_MP4ESDescrTag(d, off, len1);
        break;
    case MP4DecConfigDescrTag:
        parse_MP4DecConfigDescrTag(d, off, len1);
        break;
    case MP4SLDescrTag:
        parse_MP4SLDescrTag(d, off, len1);
        break;
    }


done:
    d->level--;
    avio_seek(&d->pb, off + len1, SEEK_SET);
    return 0;
}

static int mp4_read_iods(AVFormatContext *s, const uint8_t *buf, unsigned size,
                         Mp4Descr *descr, int *descr_count, int max_descr_count)
{
    MP4DescrParseContext d;
    int ret;

    ret = init_MP4DescrParseContext(&d, s, buf, size, descr, max_descr_count);
    if (ret < 0)
        return ret;

    ret = parse_mp4_descr(&d, avio_tell(&d.pb), size, MP4IODescrTag);

    *descr_count = d.descr_count;
    return ret;
}

static int mp4_read_od(AVFormatContext *s, const uint8_t *buf, unsigned size,
                       Mp4Descr *descr, int *descr_count, int max_descr_count)
{
    MP4DescrParseContext d;
    int ret;

    ret = init_MP4DescrParseContext(&d, s, buf, size, descr, max_descr_count);
    if (ret < 0)
        return ret;

    ret = parse_mp4_descr_arr(&d, avio_tell(&d.pb), size);

    *descr_count = d.descr_count;
    return ret;
}

static void m4sl_cb(MpegTSFilter *filter, const uint8_t *section,
                    int section_len)
{
    MpegTSContext *ts = filter->u.section_filter.opaque;
    SectionHeader h;
    const uint8_t *p, *p_end;
    AVIOContext pb;
    int mp4_descr_count = 0;
    Mp4Descr mp4_descr[MAX_MP4_DESCR_COUNT] = { { 0 } };
    int i, pid;
    AVFormatContext *s = ts->stream;

    p_end = section + section_len - 4;
    p = section;
    if (parse_section_header(&h, &p, p_end) < 0)
        return;
    if (h.tid != M4OD_TID)
        return;

    mp4_read_od(s, p, (unsigned) (p_end - p), mp4_descr, &mp4_descr_count,
                MAX_MP4_DESCR_COUNT);

    for (pid = 0; pid < NB_PID_MAX; pid++) {
        if (!ts->pids[pid])
            continue;
        for (i = 0; i < mp4_descr_count; i++) {
            PESContext *pes;
            AVStream *st;
            if (ts->pids[pid]->es_id != mp4_descr[i].es_id)
                continue;
            if (ts->pids[pid]->type != MPEGTS_PES) {
                av_log(s, AV_LOG_ERROR, "pid %x is not PES\n", pid);
                continue;
            }
            pes = ts->pids[pid]->u.pes_filter.opaque;
            st  = pes->st;
            if (!st)
                continue;

            pes->sl = mp4_descr[i].sl;

            ffio_init_context(&pb, mp4_descr[i].dec_config_descr,
                              mp4_descr[i].dec_config_descr_len, 0,
                              NULL, NULL, NULL, NULL);
            ff_mp4_read_dec_config_descr(s, st, &pb);
            if (st->codec->codec_id == AV_CODEC_ID_AAC &&
                st->codec->extradata_size > 0)
                st->need_parsing = 0;
            if (st->codec->codec_id == AV_CODEC_ID_H264 &&
                st->codec->extradata_size > 0)
                st->need_parsing = 0;

            if (st->codec->codec_id <= AV_CODEC_ID_NONE) {
                // do nothing
            } else if (st->codec->codec_id < AV_CODEC_ID_FIRST_AUDIO)
                st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            else if (st->codec->codec_id < AV_CODEC_ID_FIRST_SUBTITLE)
                st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
            else if (st->codec->codec_id < AV_CODEC_ID_FIRST_UNKNOWN)
                st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
        }
    }
    for (i = 0; i < mp4_descr_count; i++)
        av_free(mp4_descr[i].dec_config_descr);
}

int ff_parse_mpeg2_descriptor(AVFormatContext *fc, AVStream *st, int stream_type,
                              const uint8_t **pp, const uint8_t *desc_list_end,
                              Mp4Descr *mp4_descr, int mp4_descr_count, int pid,
                              MpegTSContext *ts)
{
    const uint8_t *desc_end;
    int desc_len, desc_tag, desc_es_id;
    char language[252];
    int i;

    desc_tag = get8(pp, desc_list_end);
    if (desc_tag < 0)
        return AVERROR_INVALIDDATA;
    desc_len = get8(pp, desc_list_end);
    if (desc_len < 0)
        return AVERROR_INVALIDDATA;
    desc_end = *pp + desc_len;
    if (desc_end > desc_list_end)
        return AVERROR_INVALIDDATA;

    av_dlog(fc, "tag: 0x%02x len=%d\n", desc_tag, desc_len);

    if (st->codec->codec_id == AV_CODEC_ID_NONE &&
        stream_type == STREAM_TYPE_PRIVATE_DATA)
        mpegts_find_stream_type(st, desc_tag, DESC_types);

    switch (desc_tag) {
    case 0x1E: /* SL descriptor */
        desc_es_id = get16(pp, desc_end);
        if (ts && ts->pids[pid])
            ts->pids[pid]->es_id = desc_es_id;
        for (i = 0; i < mp4_descr_count; i++)
            if (mp4_descr[i].dec_config_descr_len &&
                mp4_descr[i].es_id == desc_es_id) {
                AVIOContext pb;
                ffio_init_context(&pb, mp4_descr[i].dec_config_descr,
                                  mp4_descr[i].dec_config_descr_len, 0,
                                  NULL, NULL, NULL, NULL);
                ff_mp4_read_dec_config_descr(fc, st, &pb);
                if (st->codec->codec_id == AV_CODEC_ID_AAC &&
                    st->codec->extradata_size > 0)
                    st->need_parsing = 0;
                if (st->codec->codec_id == AV_CODEC_ID_MPEG4SYSTEMS)
                    mpegts_open_section_filter(ts, pid, m4sl_cb, ts, 1);
            }
        break;
    case 0x1F: /* FMC descriptor */
        get16(pp, desc_end);
        if (mp4_descr_count > 0 &&
            (st->codec->codec_id == AV_CODEC_ID_AAC_LATM || st->request_probe > 0) &&
            mp4_descr->dec_config_descr_len && mp4_descr->es_id == pid) {
            AVIOContext pb;
            ffio_init_context(&pb, mp4_descr->dec_config_descr,
                              mp4_descr->dec_config_descr_len, 0,
                              NULL, NULL, NULL, NULL);
            ff_mp4_read_dec_config_descr(fc, st, &pb);
            if (st->codec->codec_id == AV_CODEC_ID_AAC &&
                st->codec->extradata_size > 0) {
                st->request_probe = st->need_parsing = 0;
                st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
            }
        }
        break;
    case 0x56: /* DVB teletext descriptor */
        {
            uint8_t *extradata = NULL;
            int language_count = desc_len / 5;

            if (desc_len > 0 && desc_len % 5 != 0)
                return AVERROR_INVALIDDATA;

            if (language_count > 0) {
                /* 4 bytes per language code (3 bytes) with comma or NUL byte should fit language buffer */
                if (language_count > sizeof(language) / 4) {
                    language_count = sizeof(language) / 4;
                }

                if (st->codec->extradata == NULL) {
                    if (ff_alloc_extradata(st->codec, language_count * 2)) {
                        return AVERROR(ENOMEM);
                    }
                }

               if (st->codec->extradata_size < language_count * 2)
                   return AVERROR_INVALIDDATA;

               extradata = st->codec->extradata;

                for (i = 0; i < language_count; i++) {
                    language[i * 4 + 0] = get8(pp, desc_end);
                    language[i * 4 + 1] = get8(pp, desc_end);
                    language[i * 4 + 2] = get8(pp, desc_end);
                    language[i * 4 + 3] = ',';

                    memcpy(extradata, *pp, 2);
                    extradata += 2;

                    *pp += 2;
                }

                language[i * 4 - 1] = 0;
                av_dict_set(&st->metadata, "language", language, 0);
            }
        }
        break;
    case 0x59: /* subtitling descriptor */
        {
            /* 8 bytes per DVB subtitle substream data:
             * ISO_639_language_code (3 bytes),
             * subtitling_type (1 byte),
             * composition_page_id (2 bytes),
             * ancillary_page_id (2 bytes) */
            int language_count = desc_len / 8;

            if (desc_len > 0 && desc_len % 8 != 0)
                return AVERROR_INVALIDDATA;

            if (language_count > 1) {
                avpriv_request_sample(fc, "DVB subtitles with multiple languages");
            }

            if (language_count > 0) {
                uint8_t *extradata;

                /* 4 bytes per language code (3 bytes) with comma or NUL byte should fit language buffer */
                if (language_count > sizeof(language) / 4) {
                    language_count = sizeof(language) / 4;
                }

                if (st->codec->extradata == NULL) {
                    if (ff_alloc_extradata(st->codec, language_count * 5)) {
                        return AVERROR(ENOMEM);
                    }
                }

                if (st->codec->extradata_size < language_count * 5)
                    return AVERROR_INVALIDDATA;

                extradata = st->codec->extradata;

                for (i = 0; i < language_count; i++) {
                    language[i * 4 + 0] = get8(pp, desc_end);
                    language[i * 4 + 1] = get8(pp, desc_end);
                    language[i * 4 + 2] = get8(pp, desc_end);
                    language[i * 4 + 3] = ',';

                    /* hearing impaired subtitles detection using subtitling_type */
                    switch (*pp[0]) {
                    case 0x20: /* DVB subtitles (for the hard of hearing) with no monitor aspect ratio criticality */
                    case 0x21: /* DVB subtitles (for the hard of hearing) for display on 4:3 aspect ratio monitor */
                    case 0x22: /* DVB subtitles (for the hard of hearing) for display on 16:9 aspect ratio monitor */
                    case 0x23: /* DVB subtitles (for the hard of hearing) for display on 2.21:1 aspect ratio monitor */
                    case 0x24: /* DVB subtitles (for the hard of hearing) for display on a high definition monitor */
                    case 0x25: /* DVB subtitles (for the hard of hearing) with plano-stereoscopic disparity for display on a high definition monitor */
                        st->disposition |= AV_DISPOSITION_HEARING_IMPAIRED;
                        break;
                    }

                    extradata[4] = get8(pp, desc_end); /* subtitling_type */
                    memcpy(extradata, *pp, 4); /* composition_page_id and ancillary_page_id */
                    extradata += 5;

                    *pp += 4;
                }

                language[i * 4 - 1] = 0;
                av_dict_set(&st->metadata, "language", language, 0);
            }
        }
        break;
    case 0x0a: /* ISO 639 language descriptor */
        for (i = 0; i + 4 <= desc_len; i += 4) {
            language[i + 0] = get8(pp, desc_end);
            language[i + 1] = get8(pp, desc_end);
            language[i + 2] = get8(pp, desc_end);
            language[i + 3] = ',';
            switch (get8(pp, desc_end)) {
            case 0x01:
                st->disposition |= AV_DISPOSITION_CLEAN_EFFECTS;
                break;
            case 0x02:
                st->disposition |= AV_DISPOSITION_HEARING_IMPAIRED;
                break;
            case 0x03:
                st->disposition |= AV_DISPOSITION_VISUAL_IMPAIRED;
                break;
            }
        }
        if (i) {
            language[i - 1] = 0;
            av_dict_set(&st->metadata, "language", language, 0);
        }
        break;
    case 0x05: /* registration descriptor */
        st->codec->codec_tag = bytestream_get_le32(pp);
        av_dlog(fc, "reg_desc=%.4s\n", (char *)&st->codec->codec_tag);
        if (st->codec->codec_id == AV_CODEC_ID_NONE)
            mpegts_find_stream_type(st, st->codec->codec_tag, REGD_types);
        break;
    case 0x52: /* stream identifier descriptor */
        st->stream_identifier = 1 + get8(pp, desc_end);
        break;
    case 0x26: /* metadata descriptor */
        if (get16(pp, desc_end) == 0xFFFF)
            *pp += 4;
        if (get8(pp, desc_end) == 0xFF) {
            st->codec->codec_tag = bytestream_get_le32(pp);
            if (st->codec->codec_id == AV_CODEC_ID_NONE)
                mpegts_find_stream_type(st, st->codec->codec_tag, METADATA_types);
        }
        break;
    default:
        break;
    }
    *pp = desc_end;
    return 0;
}

static void pmt_cb(MpegTSFilter *filter, const uint8_t *section, int section_len)
{
    MpegTSContext *ts = filter->u.section_filter.opaque;
    SectionHeader h1, *h = &h1;
    PESContext *pes;
    AVStream *st;
    const uint8_t *p, *p_end, *desc_list_end;
    int program_info_length, pcr_pid, pid, stream_type;
    int desc_list_len;
    uint32_t prog_reg_desc = 0; /* registration descriptor */

    int mp4_descr_count = 0;
    Mp4Descr mp4_descr[MAX_MP4_DESCR_COUNT] = { { 0 } };
    int i;

    av_dlog(ts->stream, "PMT: len %i\n", section_len);
    hex_dump_debug(ts->stream, section, section_len);

    p_end = section + section_len - 4;
    p = section;
    if (parse_section_header(h, &p, p_end) < 0)
        return;

    av_dlog(ts->stream, "sid=0x%x sec_num=%d/%d\n",
            h->id, h->sec_num, h->last_sec_num);

    if (h->tid != PMT_TID)
        return;
    if (ts->skip_changes)
        return;

    clear_program(ts, h->id);
    pcr_pid = get16(&p, p_end);
    if (pcr_pid < 0)
        return;
    pcr_pid &= 0x1fff;
    add_pid_to_pmt(ts, h->id, pcr_pid);
    set_pcr_pid(ts->stream, h->id, pcr_pid);

    av_dlog(ts->stream, "pcr_pid=0x%x\n", pcr_pid);

    program_info_length = get16(&p, p_end);
    if (program_info_length < 0)
        return;
    program_info_length &= 0xfff;
    while (program_info_length >= 2) {
        uint8_t tag, len;
        tag = get8(&p, p_end);
        len = get8(&p, p_end);

        av_dlog(ts->stream, "program tag: 0x%02x len=%d\n", tag, len);

        if (len > program_info_length - 2)
            // something else is broken, exit the program_descriptors_loop
            break;
        program_info_length -= len + 2;
        if (tag == 0x1d) { // IOD descriptor
            get8(&p, p_end); // scope
            get8(&p, p_end); // label
            len -= 2;
            mp4_read_iods(ts->stream, p, len, mp4_descr + mp4_descr_count,
                          &mp4_descr_count, MAX_MP4_DESCR_COUNT);
        } else if (tag == 0x05 && len >= 4) { // registration descriptor
            prog_reg_desc = bytestream_get_le32(&p);
            len -= 4;
        }
        p += len;
    }
    p += program_info_length;
    if (p >= p_end)
        goto out;

    // stop parsing after pmt, we found header
    if (!ts->stream->nb_streams)
        ts->stop_parse = 2;

    set_pmt_found(ts, h->id);


    for (;;) {
        st = 0;
        pes = NULL;
        stream_type = get8(&p, p_end);
        if (stream_type < 0)
            break;
        pid = get16(&p, p_end);
        if (pid < 0)
            goto out;
        pid &= 0x1fff;
        if (pid == ts->current_pid)
            goto out;

        /* now create stream */
        if (ts->pids[pid] && ts->pids[pid]->type == MPEGTS_PES) {
            pes = ts->pids[pid]->u.pes_filter.opaque;
            if (!pes->st) {
                pes->st     = avformat_new_stream(pes->stream, NULL);
                if (!pes->st)
                    goto out;
                pes->st->id = pes->pid;
            }
            st = pes->st;
        } else if (stream_type != 0x13) {
            if (ts->pids[pid])
                mpegts_close_filter(ts, ts->pids[pid]); // wrongly added sdt filter probably
            pes = add_pes_stream(ts, pid, pcr_pid);
            if (pes) {
                st = avformat_new_stream(pes->stream, NULL);
                if (!st)
                    goto out;
                st->id = pes->pid;
            }
        } else {
            int idx = ff_find_stream_index(ts->stream, pid);
            if (idx >= 0) {
                st = ts->stream->streams[idx];
            } else {
                st = avformat_new_stream(ts->stream, NULL);
                if (!st)
                    goto out;
                st->id = pid;
                st->codec->codec_type = AVMEDIA_TYPE_DATA;
            }
        }

        if (!st)
            goto out;

        if (pes && !pes->stream_type)
            mpegts_set_stream_info(st, pes, stream_type, prog_reg_desc);

        add_pid_to_pmt(ts, h->id, pid);

        ff_program_add_stream_index(ts->stream, h->id, st->index);

        desc_list_len = get16(&p, p_end);
        if (desc_list_len < 0)
            goto out;
        desc_list_len &= 0xfff;
        desc_list_end  = p + desc_list_len;
        if (desc_list_end > p_end)
            goto out;
        for (;;) {
            if (ff_parse_mpeg2_descriptor(ts->stream, st, stream_type, &p,
                                          desc_list_end, mp4_descr,
                                          mp4_descr_count, pid, ts) < 0)
                break;

            if (pes && prog_reg_desc == AV_RL32("HDMV") &&
                stream_type == 0x83 && pes->sub_st) {
                ff_program_add_stream_index(ts->stream, h->id,
                                            pes->sub_st->index);
                pes->sub_st->codec->codec_tag = st->codec->codec_tag;
            }
        }
        p = desc_list_end;
    }

    if (!ts->pids[pcr_pid])
        mpegts_open_pcr_filter(ts, pcr_pid);

out:
    for (i = 0; i < mp4_descr_count; i++)
        av_free(mp4_descr[i].dec_config_descr);
}

static void pat_cb(MpegTSFilter *filter, const uint8_t *section, int section_len)
{
    MpegTSContext *ts = filter->u.section_filter.opaque;
    SectionHeader h1, *h = &h1;
    const uint8_t *p, *p_end;
    int sid, pmt_pid;
    AVProgram *program;

    av_dlog(ts->stream, "PAT:\n");
    hex_dump_debug(ts->stream, section, section_len);

    p_end = section + section_len - 4;
    p     = section;
    if (parse_section_header(h, &p, p_end) < 0)
        return;
    if (h->tid != PAT_TID)
        return;
    if (ts->skip_changes)
        return;

    ts->stream->ts_id = h->id;

    clear_programs(ts);
    for (;;) {
        sid = get16(&p, p_end);
        if (sid < 0)
            break;
        pmt_pid = get16(&p, p_end);
        if (pmt_pid < 0)
            break;
        pmt_pid &= 0x1fff;

        if (pmt_pid == ts->current_pid)
            break;

        av_dlog(ts->stream, "sid=0x%x pid=0x%x\n", sid, pmt_pid);

        if (sid == 0x0000) {
            /* NIT info */
        } else {
            MpegTSFilter *fil = ts->pids[pmt_pid];
            program = av_new_program(ts->stream, sid);
            program->program_num = sid;
            program->pmt_pid = pmt_pid;
            if (fil)
                if (   fil->type != MPEGTS_SECTION
                    || fil->pid != pmt_pid
                    || fil->u.section_filter.section_cb != pmt_cb)
                    mpegts_close_filter(ts, ts->pids[pmt_pid]);

            if (!ts->pids[pmt_pid])
                mpegts_open_section_filter(ts, pmt_pid, pmt_cb, ts, 1);
            add_pat_entry(ts, sid);
            add_pid_to_pmt(ts, sid, 0); // add pat pid to program
            add_pid_to_pmt(ts, sid, pmt_pid);
        }
    }

    if (sid < 0) {
        int i,j;
        for (j=0; j<ts->stream->nb_programs; j++) {
            for (i = 0; i < ts->nb_prg; i++)
                if (ts->prg[i].id == ts->stream->programs[j]->id)
                    break;
            if (i==ts->nb_prg && !ts->skip_clear)
                clear_avprogram(ts, ts->stream->programs[j]->id);
        }
    }
}

static void sdt_cb(MpegTSFilter *filter, const uint8_t *section, int section_len)
{
    MpegTSContext *ts = filter->u.section_filter.opaque;
    SectionHeader h1, *h = &h1;
    const uint8_t *p, *p_end, *desc_list_end, *desc_end;
    int onid, val, sid, desc_list_len, desc_tag, desc_len, service_type;
    char *name, *provider_name;

    av_dlog(ts->stream, "SDT:\n");
    hex_dump_debug(ts->stream, section, section_len);

    p_end = section + section_len - 4;
    p     = section;
    if (parse_section_header(h, &p, p_end) < 0)
        return;
    if (h->tid != SDT_TID)
        return;
    if (ts->skip_changes)
        return;
    onid = get16(&p, p_end);
    if (onid < 0)
        return;
    val = get8(&p, p_end);
    if (val < 0)
        return;
    for (;;) {
        sid = get16(&p, p_end);
        if (sid < 0)
            break;
        val = get8(&p, p_end);
        if (val < 0)
            break;
        desc_list_len = get16(&p, p_end);
        if (desc_list_len < 0)
            break;
        desc_list_len &= 0xfff;
        desc_list_end  = p + desc_list_len;
        if (desc_list_end > p_end)
            break;
        for (;;) {
            desc_tag = get8(&p, desc_list_end);
            if (desc_tag < 0)
                break;
            desc_len = get8(&p, desc_list_end);
            desc_end = p + desc_len;
            if (desc_end > desc_list_end)
                break;

            av_dlog(ts->stream, "tag: 0x%02x len=%d\n",
                    desc_tag, desc_len);

            switch (desc_tag) {
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
                    if (program) {
                        av_dict_set(&program->metadata, "service_name", name, 0);
                        av_dict_set(&program->metadata, "service_provider",
                                    provider_name, 0);
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

static int parse_pcr(int64_t *ppcr_high, int *ppcr_low,
                     const uint8_t *packet);

/* handle one TS packet */
static int handle_packet(MpegTSContext *ts, const uint8_t *packet)
{
    MpegTSFilter *tss;
    int len, pid, cc, expected_cc, cc_ok, afc, is_start, is_discontinuity,
        has_adaptation, has_payload;
    const uint8_t *p, *p_end;
    int64_t pos;

    pid = AV_RB16(packet + 1) & 0x1fff;
    if (pid && discard_pid(ts, pid))
        return 0;
    is_start = packet[1] & 0x40;
    tss = ts->pids[pid];
    if (ts->auto_guess && tss == NULL && is_start) {
        add_pes_stream(ts, pid, -1);
        tss = ts->pids[pid];
    }
    if (!tss)
        return 0;
    ts->current_pid = pid;

    afc = (packet[3] >> 4) & 3;
    if (afc == 0) /* reserved value */
        return 0;
    has_adaptation   = afc & 2;
    has_payload      = afc & 1;
    is_discontinuity = has_adaptation &&
                       packet[4] != 0 && /* with length > 0 */
                       (packet[5] & 0x80); /* and discontinuity indicated */

    /* continuity check (currently not used) */
    cc = (packet[3] & 0xf);
    expected_cc = has_payload ? (tss->last_cc + 1) & 0x0f : tss->last_cc;
    cc_ok = pid == 0x1FFF || // null packet PID
            is_discontinuity ||
            tss->last_cc < 0 ||
            expected_cc == cc;

    tss->last_cc = cc;
    if (!cc_ok) {
        av_log(ts->stream, AV_LOG_DEBUG,
               "Continuity check failed for pid %d expected %d got %d\n",
               pid, expected_cc, cc);
        if (tss->type == MPEGTS_PES) {
            PESContext *pc = tss->u.pes_filter.opaque;
            pc->flags |= AV_PKT_FLAG_CORRUPT;
        }
    }

    if (!has_payload && tss->type != MPEGTS_PCR)
        return 0;
    p = packet + 4;
    if (has_adaptation) {
        /* skip adaptation field */
        p += p[0] + 1;
    }
    /* if past the end of packet, ignore */
    p_end = packet + TS_PACKET_SIZE;
    if (p > p_end || (p == p_end && tss->type != MPEGTS_PCR))
        return 0;

    pos = avio_tell(ts->stream->pb);
    if (pos >= 0) {
        av_assert0(pos >= TS_PACKET_SIZE);
        ts->pos47_full = pos - TS_PACKET_SIZE;
    }

    if (tss->type == MPEGTS_SECTION) {
        if (is_start) {
            /* pointer field present */
            len = *p++;
            if (p + len > p_end)
                return 0;
            if (len && cc_ok) {
                /* write remaining section bytes */
                write_section_data(ts, tss,
                                   p, len, 0);
                /* check whether filter has been closed */
                if (!ts->pids[pid])
                    return 0;
            }
            p += len;
            if (p < p_end) {
                write_section_data(ts, tss,
                                   p, p_end - p, 1);
            }
        } else {
            if (cc_ok) {
                write_section_data(ts, tss,
                                   p, p_end - p, 0);
            }
        }

        // stop find_stream_info from waiting for more streams
        // when all programs have received a PMT
        if (ts->stream->ctx_flags & AVFMTCTX_NOHEADER) {
            int i;
            for (i = 0; i < ts->nb_prg; i++) {
                if (!ts->prg[i].pmt_found)
                    break;
            }
            if (i == ts->nb_prg && ts->nb_prg > 0) {
                if (ts->stream->nb_streams > 1 || pos > 100000) {
                    av_log(ts->stream, AV_LOG_DEBUG, "All programs have pmt, headers found\n");
                    ts->stream->ctx_flags &= ~AVFMTCTX_NOHEADER;
                }
            }
        }

    } else {
        int ret;
        int64_t pcr_h;
        int pcr_l;
        if (parse_pcr(&pcr_h, &pcr_l, packet) == 0)
            tss->last_pcr = pcr_h * 300 + pcr_l;
        // Note: The position here points actually behind the current packet.
        if (tss->type == MPEGTS_PES) {
            if ((ret = tss->u.pes_filter.pes_cb(tss, p, p_end - p, is_start,
                                                pos - ts->raw_packet_size)) < 0)
                return ret;
        }
    }

    return 0;
}

static void reanalyze(MpegTSContext *ts) {
    AVIOContext *pb = ts->stream->pb;
    int64_t pos = avio_tell(pb);
    if (pos < 0)
        return;
    pos -= ts->pos47_full;
    if (pos == TS_PACKET_SIZE) {
        ts->size_stat[0] ++;
    } else if (pos == TS_DVHS_PACKET_SIZE) {
        ts->size_stat[1] ++;
    } else if (pos == TS_FEC_PACKET_SIZE) {
        ts->size_stat[2] ++;
    }

    ts->size_stat_count ++;
    if (ts->size_stat_count > SIZE_STAT_THRESHOLD) {
        int newsize = 0;
        if (ts->size_stat[0] > SIZE_STAT_THRESHOLD) {
            newsize = TS_PACKET_SIZE;
        } else if (ts->size_stat[1] > SIZE_STAT_THRESHOLD) {
            newsize = TS_DVHS_PACKET_SIZE;
        } else if (ts->size_stat[2] > SIZE_STAT_THRESHOLD) {
            newsize = TS_FEC_PACKET_SIZE;
        }
        if (newsize && newsize != ts->raw_packet_size) {
            av_log(ts->stream, AV_LOG_WARNING, "changing packet size to %d\n", newsize);
            ts->raw_packet_size = newsize;
        }
        ts->size_stat_count = 0;
        memset(ts->size_stat, 0, sizeof(ts->size_stat));
    }
}

/* XXX: try to find a better synchro over several packets (use
 * get_packet_size() ?) */
static int mpegts_resync(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    int c, i;

    for (i = 0; i < MAX_RESYNC_SIZE; i++) {
        c = avio_r8(pb);
        if (url_feof(pb))
            return AVERROR_EOF;
        if (c == 0x47) {
            avio_seek(pb, -1, SEEK_CUR);
            reanalyze(s->priv_data);
            return 0;
        }
    }
    av_log(s, AV_LOG_ERROR,
           "max resync size reached, could not find sync byte\n");
    /* no sync found */
    return AVERROR_INVALIDDATA;
}

/* return AVERROR_something if error or EOF. Return 0 if OK. */
static int read_packet(AVFormatContext *s, uint8_t *buf, int raw_packet_size,
                       const uint8_t **data)
{
    AVIOContext *pb = s->pb;
    int len;

    for (;;) {
        len = ffio_read_indirect(pb, buf, TS_PACKET_SIZE, data);
        if (len != TS_PACKET_SIZE)
            return len < 0 ? len : AVERROR_EOF;
        /* check packet sync byte */
        if ((*data)[0] != 0x47) {
            /* find a new packet start */
            uint64_t pos = avio_tell(pb);
            avio_seek(pb, -FFMIN(raw_packet_size, pos), SEEK_CUR);

            if (mpegts_resync(s) < 0)
                return AVERROR(EAGAIN);
            else
                continue;
        } else {
            break;
        }
    }
    return 0;
}

static void finished_reading_packet(AVFormatContext *s, int raw_packet_size)
{
    AVIOContext *pb = s->pb;
    int skip = raw_packet_size - TS_PACKET_SIZE;
    if (skip > 0)
        avio_skip(pb, skip);
}

static int handle_packets(MpegTSContext *ts, int nb_packets)
{
    AVFormatContext *s = ts->stream;
    uint8_t packet[TS_PACKET_SIZE + FF_INPUT_BUFFER_PADDING_SIZE];
    const uint8_t *data;
    int packet_num, ret = 0;

    if (avio_tell(s->pb) != ts->last_pos) {
        int i;
        av_dlog(ts->stream, "Skipping after seek\n");
        /* seek detected, flush pes buffer */
        for (i = 0; i < NB_PID_MAX; i++) {
            if (ts->pids[i]) {
                if (ts->pids[i]->type == MPEGTS_PES) {
                    PESContext *pes = ts->pids[i]->u.pes_filter.opaque;
                    av_buffer_unref(&pes->buffer);
                    pes->data_index = 0;
                    pes->state = MPEGTS_SKIP; /* skip until pes header */
                }
                ts->pids[i]->last_cc = -1;
                ts->pids[i]->last_pcr = -1;
            }
        }
    }

    ts->stop_parse = 0;
    packet_num = 0;
    memset(packet + TS_PACKET_SIZE, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    for (;;) {
        packet_num++;
        if (nb_packets != 0 && packet_num >= nb_packets ||
            ts->stop_parse > 1) {
            ret = AVERROR(EAGAIN);
            break;
        }
        if (ts->stop_parse > 0)
            break;

        ret = read_packet(s, packet, ts->raw_packet_size, &data);
        if (ret != 0)
            break;
        ret = handle_packet(ts, data);
        finished_reading_packet(s, ts->raw_packet_size);
        if (ret != 0)
            break;
    }
    ts->last_pos = avio_tell(s->pb);
    return ret;
}

static int mpegts_probe(AVProbeData *p)
{
    const int size = p->buf_size;
    int maxscore = 0;
    int sumscore = 0;
    int i;
    int check_count = size / TS_FEC_PACKET_SIZE;
#define CHECK_COUNT 10
#define CHECK_BLOCK 100

    if (check_count < CHECK_COUNT)
        return AVERROR_INVALIDDATA;

    for (i = 0; i<check_count; i+=CHECK_BLOCK) {
        int left = FFMIN(check_count - i, CHECK_BLOCK);
        int score      = analyze(p->buf + TS_PACKET_SIZE     *i, TS_PACKET_SIZE     *left, TS_PACKET_SIZE     , NULL);
        int dvhs_score = analyze(p->buf + TS_DVHS_PACKET_SIZE*i, TS_DVHS_PACKET_SIZE*left, TS_DVHS_PACKET_SIZE, NULL);
        int fec_score  = analyze(p->buf + TS_FEC_PACKET_SIZE *i, TS_FEC_PACKET_SIZE *left, TS_FEC_PACKET_SIZE , NULL);
        score = FFMAX3(score, dvhs_score, fec_score);
        sumscore += score;
        maxscore = FFMAX(maxscore, score);
    }

    sumscore = sumscore * CHECK_COUNT / check_count;
    maxscore = maxscore * CHECK_COUNT / CHECK_BLOCK;

    av_dlog(0, "TS score: %d %d\n", sumscore, maxscore);

    if      (sumscore > 6) return AVPROBE_SCORE_MAX   + sumscore - CHECK_COUNT;
    else if (maxscore > 6) return AVPROBE_SCORE_MAX/2 + sumscore - CHECK_COUNT;
    else
        return AVERROR_INVALIDDATA;
}

/* return the 90kHz PCR and the extension for the 27MHz PCR. return
 * (-1) if not available */
static int parse_pcr(int64_t *ppcr_high, int *ppcr_low, const uint8_t *packet)
{
    int afc, len, flags;
    const uint8_t *p;
    unsigned int v;

    afc = (packet[3] >> 4) & 3;
    if (afc <= 1)
        return AVERROR_INVALIDDATA;
    p   = packet + 4;
    len = p[0];
    p++;
    if (len == 0)
        return AVERROR_INVALIDDATA;
    flags = *p++;
    len--;
    if (!(flags & 0x10))
        return AVERROR_INVALIDDATA;
    if (len < 6)
        return AVERROR_INVALIDDATA;
    v          = AV_RB32(p);
    *ppcr_high = ((int64_t) v << 1) | (p[4] >> 7);
    *ppcr_low  = ((p[4] & 1) << 8) | p[5];
    return 0;
}

static void seek_back(AVFormatContext *s, AVIOContext *pb, int64_t pos) {

    /* NOTE: We attempt to seek on non-seekable files as well, as the
     * probe buffer usually is big enough. Only warn if the seek failed
     * on files where the seek should work. */
    if (avio_seek(pb, pos, SEEK_SET) < 0)
        av_log(s, pb->seekable ? AV_LOG_ERROR : AV_LOG_INFO, "Unable to seek back to the start\n");
}

static int mpegts_read_header(AVFormatContext *s)
{
    MpegTSContext *ts = s->priv_data;
    AVIOContext *pb   = s->pb;
    uint8_t buf[8 * 1024] = {0};
    int len;
    int64_t pos;

    ffio_ensure_seekback(pb, s->probesize);

    /* read the first 8192 bytes to get packet size */
    pos = avio_tell(pb);
    len = avio_read(pb, buf, sizeof(buf));
    ts->raw_packet_size = get_packet_size(buf, len);
    if (ts->raw_packet_size <= 0) {
        av_log(s, AV_LOG_WARNING, "Could not detect TS packet size, defaulting to non-FEC/DVHS\n");
        ts->raw_packet_size = TS_PACKET_SIZE;
    }
    ts->stream     = s;
    ts->auto_guess = 0;

    if (s->iformat == &ff_mpegts_demuxer) {
        /* normal demux */

        /* first do a scan to get all the services */
        seek_back(s, pb, pos);

        mpegts_open_section_filter(ts, SDT_PID, sdt_cb, ts, 1);

        mpegts_open_section_filter(ts, PAT_PID, pat_cb, ts, 1);

        handle_packets(ts, s->probesize / ts->raw_packet_size);
        /* if could not find service, enable auto_guess */

        ts->auto_guess = 1;

        av_dlog(ts->stream, "tuning done\n");

        s->ctx_flags |= AVFMTCTX_NOHEADER;
    } else {
        AVStream *st;
        int pcr_pid, pid, nb_packets, nb_pcrs, ret, pcr_l;
        int64_t pcrs[2], pcr_h;
        int packet_count[2];
        uint8_t packet[TS_PACKET_SIZE];
        const uint8_t *data;

        /* only read packets */

        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        avpriv_set_pts_info(st, 60, 1, 27000000);
        st->codec->codec_type = AVMEDIA_TYPE_DATA;
        st->codec->codec_id   = AV_CODEC_ID_MPEG2TS;

        /* we iterate until we find two PCRs to estimate the bitrate */
        pcr_pid    = -1;
        nb_pcrs    = 0;
        nb_packets = 0;
        for (;;) {
            ret = read_packet(s, packet, ts->raw_packet_size, &data);
            if (ret < 0)
                return ret;
            pid = AV_RB16(data + 1) & 0x1fff;
            if ((pcr_pid == -1 || pcr_pid == pid) &&
                parse_pcr(&pcr_h, &pcr_l, data) == 0) {
                finished_reading_packet(s, ts->raw_packet_size);
                pcr_pid = pid;
                packet_count[nb_pcrs] = nb_packets;
                pcrs[nb_pcrs] = pcr_h * 300 + pcr_l;
                nb_pcrs++;
                if (nb_pcrs >= 2)
                    break;
            } else {
                finished_reading_packet(s, ts->raw_packet_size);
            }
            nb_packets++;
        }

        /* NOTE1: the bitrate is computed without the FEC */
        /* NOTE2: it is only the bitrate of the start of the stream */
        ts->pcr_incr = (pcrs[1] - pcrs[0]) / (packet_count[1] - packet_count[0]);
        ts->cur_pcr  = pcrs[0] - ts->pcr_incr * packet_count[0];
        s->bit_rate  = TS_PACKET_SIZE * 8 * 27e6 / ts->pcr_incr;
        st->codec->bit_rate = s->bit_rate;
        st->start_time      = ts->cur_pcr;
        av_dlog(ts->stream, "start=%0.3f pcr=%0.3f incr=%d\n",
                st->start_time / 1000000.0, pcrs[0] / 27e6, ts->pcr_incr);
    }

    seek_back(s, pb, pos);
    return 0;
}

#define MAX_PACKET_READAHEAD ((128 * 1024) / 188)

static int mpegts_raw_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MpegTSContext *ts = s->priv_data;
    int ret, i;
    int64_t pcr_h, next_pcr_h, pos;
    int pcr_l, next_pcr_l;
    uint8_t pcr_buf[12];
    const uint8_t *data;

    if (av_new_packet(pkt, TS_PACKET_SIZE) < 0)
        return AVERROR(ENOMEM);
    ret = read_packet(s, pkt->data, ts->raw_packet_size, &data);
    pkt->pos = avio_tell(s->pb);
    if (ret < 0) {
        av_free_packet(pkt);
        return ret;
    }
    if (data != pkt->data)
        memcpy(pkt->data, data, ts->raw_packet_size);
    finished_reading_packet(s, ts->raw_packet_size);
    if (ts->mpeg2ts_compute_pcr) {
        /* compute exact PCR for each packet */
        if (parse_pcr(&pcr_h, &pcr_l, pkt->data) == 0) {
            /* we read the next PCR (XXX: optimize it by using a bigger buffer */
            pos = avio_tell(s->pb);
            for (i = 0; i < MAX_PACKET_READAHEAD; i++) {
                avio_seek(s->pb, pos + i * ts->raw_packet_size, SEEK_SET);
                avio_read(s->pb, pcr_buf, 12);
                if (parse_pcr(&next_pcr_h, &next_pcr_l, pcr_buf) == 0) {
                    /* XXX: not precise enough */
                    ts->pcr_incr =
                        ((next_pcr_h - pcr_h) * 300 + (next_pcr_l - pcr_l)) /
                        (i + 1);
                    break;
                }
            }
            avio_seek(s->pb, pos, SEEK_SET);
            /* no next PCR found: we use previous increment */
            ts->cur_pcr = pcr_h * 300 + pcr_l;
        }
        pkt->pts      = ts->cur_pcr;
        pkt->duration = ts->pcr_incr;
        ts->cur_pcr  += ts->pcr_incr;
    }
    pkt->stream_index = 0;
    return 0;
}

static int mpegts_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MpegTSContext *ts = s->priv_data;
    int ret, i;

    pkt->size = -1;
    ts->pkt = pkt;
    ret = handle_packets(ts, 0);
    if (ret < 0) {
        av_free_packet(ts->pkt);
        /* flush pes data left */
        for (i = 0; i < NB_PID_MAX; i++)
            if (ts->pids[i] && ts->pids[i]->type == MPEGTS_PES) {
                PESContext *pes = ts->pids[i]->u.pes_filter.opaque;
                if (pes->state == MPEGTS_PAYLOAD && pes->data_index > 0) {
                    new_pes_packet(pes, pkt);
                    pes->state = MPEGTS_SKIP;
                    ret = 0;
                    break;
                }
            }
    }

    if (!ret && pkt->size < 0)
        ret = AVERROR(EINTR);
    return ret;
}

static void mpegts_free(MpegTSContext *ts)
{
    int i;

    clear_programs(ts);

    for (i = 0; i < NB_PID_MAX; i++)
        if (ts->pids[i])
            mpegts_close_filter(ts, ts->pids[i]);
}

static int mpegts_read_close(AVFormatContext *s)
{
    MpegTSContext *ts = s->priv_data;
    mpegts_free(ts);
    return 0;
}

static av_unused int64_t mpegts_get_pcr(AVFormatContext *s, int stream_index,
                              int64_t *ppos, int64_t pos_limit)
{
    MpegTSContext *ts = s->priv_data;
    int64_t pos, timestamp;
    uint8_t buf[TS_PACKET_SIZE];
    int pcr_l, pcr_pid =
        ((PESContext *)s->streams[stream_index]->priv_data)->pcr_pid;
    int pos47 = ts->pos47_full % ts->raw_packet_size;
    pos =
        ((*ppos + ts->raw_packet_size - 1 - pos47) / ts->raw_packet_size) *
        ts->raw_packet_size + pos47;
    while(pos < pos_limit) {
        if (avio_seek(s->pb, pos, SEEK_SET) < 0)
            return AV_NOPTS_VALUE;
        if (avio_read(s->pb, buf, TS_PACKET_SIZE) != TS_PACKET_SIZE)
            return AV_NOPTS_VALUE;
        if (buf[0] != 0x47) {
            avio_seek(s->pb, -TS_PACKET_SIZE, SEEK_CUR);
            if (mpegts_resync(s) < 0)
                return AV_NOPTS_VALUE;
            pos = avio_tell(s->pb);
            continue;
        }
        if ((pcr_pid < 0 || (AV_RB16(buf + 1) & 0x1fff) == pcr_pid) &&
            parse_pcr(&timestamp, &pcr_l, buf) == 0) {
            *ppos = pos;
            return timestamp;
        }
        pos += ts->raw_packet_size;
    }

    return AV_NOPTS_VALUE;
}

static int64_t mpegts_get_dts(AVFormatContext *s, int stream_index,
                              int64_t *ppos, int64_t pos_limit)
{
    MpegTSContext *ts = s->priv_data;
    int64_t pos;
    int pos47 = ts->pos47_full % ts->raw_packet_size;
    pos = ((*ppos  + ts->raw_packet_size - 1 - pos47) / ts->raw_packet_size) * ts->raw_packet_size + pos47;
    ff_read_frame_flush(s);
    if (avio_seek(s->pb, pos, SEEK_SET) < 0)
        return AV_NOPTS_VALUE;
    while(pos < pos_limit) {
        int ret;
        AVPacket pkt;
        av_init_packet(&pkt);
        ret = av_read_frame(s, &pkt);
        if (ret < 0)
            return AV_NOPTS_VALUE;
        av_free_packet(&pkt);
        if (pkt.dts != AV_NOPTS_VALUE && pkt.pos >= 0) {
            ff_reduce_index(s, pkt.stream_index);
            av_add_index_entry(s->streams[pkt.stream_index], pkt.pos, pkt.dts, 0, 0, AVINDEX_KEYFRAME /* FIXME keyframe? */);
            if (pkt.stream_index == stream_index && pkt.pos >= *ppos) {
                *ppos = pkt.pos;
                return pkt.dts;
            }
        }
        pos = pkt.pos;
    }

    return AV_NOPTS_VALUE;
}

/**************************************************************/
/* parsing functions - called from other demuxers such as RTP */

MpegTSContext *ff_mpegts_parse_open(AVFormatContext *s)
{
    MpegTSContext *ts;

    ts = av_mallocz(sizeof(MpegTSContext));
    if (!ts)
        return NULL;
    /* no stream case, currently used by RTP */
    ts->raw_packet_size = TS_PACKET_SIZE;
    ts->stream = s;
    ts->auto_guess = 1;
    mpegts_open_section_filter(ts, SDT_PID, sdt_cb, ts, 1);
    mpegts_open_section_filter(ts, PAT_PID, pat_cb, ts, 1);

    return ts;
}

/* return the consumed length if a packet was output, or -1 if no
 * packet is output */
int ff_mpegts_parse_packet(MpegTSContext *ts, AVPacket *pkt,
                           const uint8_t *buf, int len)
{
    int len1;

    len1 = len;
    ts->pkt = pkt;
    for (;;) {
        ts->stop_parse = 0;
        if (len < TS_PACKET_SIZE)
            return AVERROR_INVALIDDATA;
        if (buf[0] != 0x47) {
            buf++;
            len--;
        } else {
            handle_packet(ts, buf);
            buf += TS_PACKET_SIZE;
            len -= TS_PACKET_SIZE;
            if (ts->stop_parse == 1)
                break;
        }
    }
    return len1 - len;
}

void ff_mpegts_parse_close(MpegTSContext *ts)
{
    mpegts_free(ts);
    av_free(ts);
}

AVInputFormat ff_mpegts_demuxer = {
    .name           = "mpegts",
    .long_name      = NULL_IF_CONFIG_SMALL("MPEG-TS (MPEG-2 Transport Stream)"),
    .priv_data_size = sizeof(MpegTSContext),
    .read_probe     = mpegts_probe,
    .read_header    = mpegts_read_header,
    .read_packet    = mpegts_read_packet,
    .read_close     = mpegts_read_close,
    .read_timestamp = mpegts_get_dts,
    .flags          = AVFMT_SHOW_IDS | AVFMT_TS_DISCONT,
    .priv_class     = &mpegts_class,
};

AVInputFormat ff_mpegtsraw_demuxer = {
    .name           = "mpegtsraw",
    .long_name      = NULL_IF_CONFIG_SMALL("raw MPEG-TS (MPEG-2 Transport Stream)"),
    .priv_data_size = sizeof(MpegTSContext),
    .read_header    = mpegts_read_header,
    .read_packet    = mpegts_raw_read_packet,
    .read_close     = mpegts_read_close,
    .read_timestamp = mpegts_get_dts,
    .flags          = AVFMT_SHOW_IDS | AVFMT_TS_DISCONT,
    .priv_class     = &mpegtsraw_class,
};
