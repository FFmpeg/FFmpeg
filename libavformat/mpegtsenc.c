/*
 * MPEG2 transport stream (aka DVB) mux
 * Copyright (c) 2003 Fabrice Bellard.
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

/* write DVB SI sections */

static const uint32_t crc_table[256] = {
	0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
	0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
	0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
	0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
	0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
	0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
	0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
	0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
	0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
	0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
	0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
	0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
	0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
	0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
	0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
	0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
	0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
	0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
	0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
	0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
	0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
	0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
	0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
	0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
	0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
	0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
	0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
	0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
	0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
	0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
	0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
	0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
	0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
	0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
	0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
	0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
	0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
	0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
	0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
	0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
	0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
	0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
	0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

unsigned int mpegts_crc32(const uint8_t *data, int len)
{
    register int i;
    unsigned int crc = 0xffffffff;
    
    for (i=0; i<len; i++)
        crc = (crc << 8) ^ crc_table[((crc >> 24) ^ *data++) & 0xff];
    
    return crc;
}

/*********************************************/
/* mpegts section writer */

typedef struct MpegTSSection {
    int pid;
    int cc;
    void (*write_packet)(struct MpegTSSection *s, const uint8_t *packet);
    void *opaque;
} MpegTSSection;

/* NOTE: 4 bytes must be left at the end for the crc32 */
void mpegts_write_section(MpegTSSection *s, uint8_t *buf, int len)
{
    unsigned int crc;
    unsigned char packet[TS_PACKET_SIZE];
    const unsigned char *buf_ptr;
    unsigned char *q;
    int first, b, len1, left;

    crc = mpegts_crc32(buf, len - 4);
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

int mpegts_write_section1(MpegTSSection *s, int tid, int id, 
                          int version, int sec_num, int last_sec_num,
                          uint8_t *buf, int len)
{
    uint8_t section[1024], *q;
    unsigned int tot_len;
    
    tot_len = 3 + 5 + len + 4;
    /* check if not too big */
    if (tot_len > 1024)
        return -1;

    q = section;
    *q++ = tid;
    put16(&q, 0xb000 | (len + 5 + 4)); /* 5 byte header + 4 byte CRC */
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

#define DEFAULT_PMT_START_PID   0x1000
#define DEFAULT_START_PID       0x0100
#define DEFAULT_PROVIDER_NAME   "FFmpeg"
#define DEFAULT_SERVICE_NAME    "Service01"

/* default network id, transport stream and service identifiers */
#define DEFAULT_ONID            0x0001
#define DEFAULT_TSID            0x0001
#define DEFAULT_SID             0x0001

/* a PES packet header is generated every DEFAULT_PES_HEADER_FREQ packets */
#define DEFAULT_PES_HEADER_FREQ 16
#define DEFAULT_PES_PAYLOAD_SIZE ((DEFAULT_PES_HEADER_FREQ - 1) * 184 + 170)

/* we retransmit the SI info at this rate */
#define SDT_RETRANS_TIME 500
#define PAT_RETRANS_TIME 100

typedef struct MpegTSWriteStream {
    int pid; /* stream associated pid */
    int cc;
    int payload_index;
    int64_t payload_pts;
    uint8_t payload[DEFAULT_PES_PAYLOAD_SIZE];
} MpegTSWriteStream;

typedef struct MpegTSService {
    MpegTSSection pmt; /* MPEG2 pmt table context */
    int pcr_pid;
    int sid;           /* service ID */
    char *name;
    char *provider_name;
} MpegTSService;

typedef struct MpegTSWrite {
    MpegTSSection pat; /* MPEG2 pat table */
    MpegTSSection sdt; /* MPEG2 sdt table context */
    MpegTSService **services;
    int sdt_packet_count;
    int sdt_packet_freq;
    int pat_packet_count;
    int pat_packet_freq;
    int nb_services;
    int onid;
    int tsid;
} MpegTSWrite;

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
    //    MpegTSWrite *ts = s->priv_data;
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
        switch(st->codec.codec_id) {
        case CODEC_ID_MPEG1VIDEO:
        case CODEC_ID_MPEG2VIDEO:
            stream_type = STREAM_TYPE_VIDEO_MPEG2;
            break;
        case CODEC_ID_MPEG4:
            stream_type = STREAM_TYPE_VIDEO_MPEG4;
            break;
        case CODEC_ID_H264:
            stream_type = STREAM_TYPE_VIDEO_H264;
            break;
        case CODEC_ID_MP2:
        case CODEC_ID_MP3:
            stream_type = STREAM_TYPE_AUDIO_MPEG1;
            break;
        case CODEC_ID_AAC:
            stream_type = STREAM_TYPE_AUDIO_AAC;
            break;
        case CODEC_ID_AC3:
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
    service->pmt.pid = DEFAULT_PMT_START_PID + ts->nb_services - 1;
    service->sid = sid;
    service->provider_name = av_strdup(provider_name);
    service->name = av_strdup(name);
    service->pcr_pid = 0x1fff;
    dynarray_add(&ts->services, &ts->nb_services, service);
    return service;
}

static void section_write_packet(MpegTSSection *s, const uint8_t *packet)
{
    AVFormatContext *ctx = s->opaque;
    put_buffer(&ctx->pb, packet, TS_PACKET_SIZE);
}

static int mpegts_write_header(AVFormatContext *s)
{
    MpegTSWrite *ts = s->priv_data;
    MpegTSWriteStream *ts_st;
    MpegTSService *service;
    AVStream *st;
    int i, total_bit_rate;

    ts->tsid = DEFAULT_TSID;
    ts->onid = DEFAULT_ONID;
    /* allocate a single DVB service */
    service = mpegts_add_service(ts, DEFAULT_SID, 
                                 DEFAULT_PROVIDER_NAME, DEFAULT_SERVICE_NAME);
    service->pmt.write_packet = section_write_packet;
    service->pmt.opaque = s;

    ts->pat.pid = PAT_PID;
    ts->pat.cc = 0;
    ts->pat.write_packet = section_write_packet;
    ts->pat.opaque = s;

    ts->sdt.pid = SDT_PID;
    ts->sdt.cc = 0;
    ts->sdt.write_packet = section_write_packet;
    ts->sdt.opaque = s;

    /* assign pids to each stream */
    total_bit_rate = 0;
    for(i = 0;i < s->nb_streams; i++) {
        st = s->streams[i];
        ts_st = av_mallocz(sizeof(MpegTSWriteStream));
        if (!ts_st)
            goto fail;
        st->priv_data = ts_st;
        ts_st->pid = DEFAULT_START_PID + i;
        ts_st->payload_pts = AV_NOPTS_VALUE;
        /* update PCR pid if needed */
        if (st->codec.codec_type == CODEC_TYPE_VIDEO && 
            service->pcr_pid == 0x1fff)
            service->pcr_pid = ts_st->pid;
        total_bit_rate += st->codec.bit_rate;
    }
    if (total_bit_rate <= 8 * 1024)
        total_bit_rate = 8 * 1024;
    ts->sdt_packet_freq = (total_bit_rate * SDT_RETRANS_TIME) / 
        (TS_PACKET_SIZE * 8 * 1000);
    ts->pat_packet_freq = (total_bit_rate * PAT_RETRANS_TIME) / 
        (TS_PACKET_SIZE * 8 * 1000);
#if 0
    printf("%d %d %d\n", 
           total_bit_rate, ts->sdt_packet_freq, ts->pat_packet_freq);
#endif

    /* write info at the start of the file, so that it will be fast to
       find them */
    mpegts_write_sdt(s);
    mpegts_write_pat(s);
    for(i = 0; i < ts->nb_services; i++) {
        mpegts_write_pmt(s, ts->services[i]);
    }
    put_flush_packet(&s->pb);

    return 0;

 fail:
    for(i = 0;i < s->nb_streams; i++) {
        st = s->streams[i];
        av_free(st->priv_data);
    }
    return -1;
}

/* send SDT, PAT and PMT tables regulary */
static void retransmit_si_info(AVFormatContext *s)
{
    MpegTSWrite *ts = s->priv_data;
    int i;

    if (++ts->sdt_packet_count == ts->sdt_packet_freq) {
        ts->sdt_packet_count = 0;
        mpegts_write_sdt(s);
    }
    if (++ts->pat_packet_count == ts->pat_packet_freq) {
        ts->pat_packet_count = 0;
        mpegts_write_pat(s);
        for(i = 0; i < ts->nb_services; i++) {
            mpegts_write_pmt(s, ts->services[i]);
        }
    }
}

/* NOTE: pes_data contains all the PES packet */
static void mpegts_write_pes(AVFormatContext *s, AVStream *st,
                             const uint8_t *payload, int payload_size,
                             int64_t pts)
{
    MpegTSWriteStream *ts_st = st->priv_data;
    uint8_t buf[TS_PACKET_SIZE];
    uint8_t *q;
    int val, is_start, len, ts_len, header_len;

    is_start = 1;
    while (payload_size > 0) {
        retransmit_si_info(s);

        /* prepare packet header */
        q = buf;
        *q++ = 0x47;
        val = (ts_st->pid >> 8);
        if (is_start)
            val |= 0x40;
        *q++ = val;
        *q++ = ts_st->pid;
        *q++ = 0x10 | ts_st->cc;
        ts_st->cc = (ts_st->cc + 1) & 0xf;
        if (is_start) {
            /* write PES header */
            *q++ = 0x00;
            *q++ = 0x00;
            *q++ = 0x01;
            if (st->codec.codec_type == CODEC_TYPE_VIDEO)
                *q++ = 0xe0;
            else
                *q++ = 0xc0;
            if (pts != AV_NOPTS_VALUE)
                header_len = 8;
            else
                header_len = 3;
            len = payload_size + header_len;
            *q++ = len >> 8;
            *q++ = len;
            *q++ = 0x80;
            if (pts != AV_NOPTS_VALUE) {
                *q++ = 0x80; /* PTS only */
                *q++ = 0x05; /* header len */
                val = (0x02 << 4) | 
                    (((pts >> 30) & 0x07) << 1) | 1;
                *q++ = val;
                val = (((pts >> 15) & 0x7fff) << 1) | 1;
                *q++ = val >> 8;
                *q++ = val;
                val = (((pts) & 0x7fff) << 1) | 1;
                *q++ = val >> 8;
                *q++ = val;
            } else {
                *q++ = 0x00;
                *q++ = 0x00;
            }
            is_start = 0;
        }
        /* write header */
        ts_len = q - buf;
        put_buffer(&s->pb, buf, ts_len);
        /* write data */
        len = TS_PACKET_SIZE - ts_len;
        if (len > payload_size)
            len = payload_size;
        put_buffer(&s->pb, payload, len);
        payload += len;
        payload_size -= len;
        ts_len += len;
        /* stuffing */
        len = TS_PACKET_SIZE - ts_len;
        if (len > 0) {
            memset(buf, 0xff, len);
            put_buffer(&s->pb, buf, len);
        }
    }
    put_flush_packet(&s->pb);
}

static int mpegts_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[pkt->stream_index];
    int size= pkt->size;
    uint8_t *buf= pkt->data;
    MpegTSWriteStream *ts_st = st->priv_data;
    int len;

    while (size > 0) {
        len = DEFAULT_PES_PAYLOAD_SIZE - ts_st->payload_index;
        if (len > size)
            len = size;
        memcpy(ts_st->payload + ts_st->payload_index, buf, len);
        buf += len;
        size -= len;
        ts_st->payload_index += len;
        if (ts_st->payload_pts == AV_NOPTS_VALUE)
            ts_st->payload_pts = pkt->pts;
        if (ts_st->payload_index >= DEFAULT_PES_PAYLOAD_SIZE) {
            mpegts_write_pes(s, st, ts_st->payload, ts_st->payload_index,
                             ts_st->payload_pts);
            ts_st->payload_pts = AV_NOPTS_VALUE;
            ts_st->payload_index = 0;
        }
    }
    return 0;
}

static int mpegts_write_end(AVFormatContext *s)
{
    MpegTSWrite *ts = s->priv_data;
    MpegTSWriteStream *ts_st;
    MpegTSService *service;
    AVStream *st;
    int i;

    /* flush current packets */
    for(i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        ts_st = st->priv_data;
        if (ts_st->payload_index > 0) {
            mpegts_write_pes(s, st, ts_st->payload, ts_st->payload_index,
                             ts_st->payload_pts);
        }
    }
    put_flush_packet(&s->pb);
        
    for(i = 0; i < ts->nb_services; i++) {
        service = ts->services[i];
        av_freep(&service->provider_name);
        av_freep(&service->name);
        av_free(service);
    }
    av_free(ts->services);

    for(i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        av_free(st->priv_data);
    }
    return 0;
}

AVOutputFormat mpegts_mux = {
    "mpegts",
    "MPEG2 transport stream format",
    "video/x-mpegts",
    "ts",
    sizeof(MpegTSWrite),
    CODEC_ID_MP2,
    CODEC_ID_MPEG2VIDEO,
    mpegts_write_header,
    mpegts_write_packet,
    mpegts_write_end,
};
