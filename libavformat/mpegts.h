/*
 * MPEG-2 transport stream defines
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

#ifndef AVFORMAT_MPEGTS_H
#define AVFORMAT_MPEGTS_H

#include "avformat.h"

#define TS_FEC_PACKET_SIZE 204
#define TS_DVHS_PACKET_SIZE 192
#define TS_PACKET_SIZE 188
#define TS_MAX_PACKET_SIZE 204

#define NB_PID_MAX 8192
#define USUAL_SECTION_SIZE 1024 /* except EIT which is limited to 4096 */
#define MAX_SECTION_SIZE 4096

/* pids */
#define PAT_PID         0x0000 /* Program Association Table */
#define CAT_PID         0x0001 /* Conditional Access Table */
#define TSDT_PID        0x0002 /* Transport Stream Description Table */
#define IPMP_PID        0x0003
/* PID from 0x0004 to 0x000F are reserved */
#define NIT_PID         0x0010 /* Network Information Table */
#define SDT_PID         0x0011 /* Service Description Table */
#define BAT_PID         0x0011 /* Bouquet Association Table */
#define EIT_PID         0x0012 /* Event Information Table */
#define RST_PID         0x0013 /* Running Status Table */
#define TDT_PID         0x0014 /* Time and Date Table */
#define TOT_PID         0x0014
#define NET_SYNC_PID    0x0015
#define RNT_PID         0x0016 /* RAR Notification Table */
/* PID from 0x0017 to 0x001B are reserved for future use */
/* PID value 0x001C allocated to link-local inband signalling shall not be
 * used on any broadcast signals. It shall only be used between devices in a
 * controlled environment. */
#define LINK_LOCAL_PID  0x001C
#define MEASUREMENT_PID 0x001D
#define DIT_PID         0x001E /* Discontinuity Information Table */
#define SIT_PID         0x001F /* Selection Information Table */
/* PID from 0x0020 to 0x1FFA may be assigned as needed to PMT, elementary
 * streams and other data tables */
#define FIRST_OTHER_PID 0x0020
#define  LAST_OTHER_PID 0x1FFA
/* PID 0x1FFB is used by DigiCipher 2/ATSC MGT metadata */
/* PID from 0x1FFC to 0x1FFE may be assigned as needed to PMT, elementary
 * streams and other data tables */
#define NULL_PID        0x1FFF /* Null packet (used for fixed bandwidth padding) */

/* table ids */
#define PAT_TID         0x00 /* Program Association section */
#define CAT_TID         0x01 /* Conditional Access section */
#define PMT_TID         0x02 /* Program Map section */
#define TSDT_TID        0x03 /* Transport Stream Description section */
/* TID from 0x04 to 0x3F are reserved */
#define M4OD_TID        0x05
#define NIT_TID         0x40 /* Network Information section - actual network */
#define ONIT_TID        0x41 /* Network Information section - other network */
#define SDT_TID         0x42 /* Service Description section - actual TS */
/* TID from 0x43 to 0x45 are reserved for future use */
#define OSDT_TID        0x46 /* Service Descrition section - other TS */
/* TID from 0x47 to 0x49 are reserved for future use */
#define BAT_TID         0x4A /* Bouquet Association section */
#define UNT_TID         0x4B /* Update Notification Table section */
#define DFI_TID         0x4C /* Downloadable Font Info section */
/* TID 0x4D is reserved for future use */
#define EIT_TID         0x4E /* Event Information section - actual TS */
#define OEIT_TID        0x4F /* Event Information section - other TS */
#define EITS_START_TID  0x50 /* Event Information section schedule - actual TS */
#define EITS_END_TID    0x5F /* Event Information section schedule - actual TS */
#define OEITS_START_TID 0x60 /* Event Information section schedule - other TS */
#define OEITS_END_TID   0x6F /* Event Information section schedule - other TS */
#define TDT_TID         0x70 /* Time Date section */
#define RST_TID         0x71 /* Running Status section */
#define ST_TID          0x72 /* Stuffing section */
#define TOT_TID         0x73 /* Time Offset section */
#define AIT_TID         0x74 /* Application Inforamtion section */
#define CT_TID          0x75 /* Container section */
#define RCT_TID         0x76 /* Related Content section */
#define CIT_TID         0x77 /* Content Identifier section */
#define MPE_FEC_TID     0x78 /* MPE-FEC section */
#define RPNT_TID        0x79 /* Resolution Provider Notification section */
#define MPE_IFEC_TID    0x7A /* MPE-IFEC section */
#define PROTMT_TID      0x7B /* Protection Message section */
/* TID from 0x7C to 0x7D are reserved for future use */
#define DIT_TID         0x7E /* Discontinuity Information section */
#define SIT_TID         0x7F /* Selection Information section */
/* TID from 0x80 to 0xFE are user defined */
/* TID 0xFF is reserved */

#define STREAM_TYPE_VIDEO_MPEG1     0x01
#define STREAM_TYPE_VIDEO_MPEG2     0x02
#define STREAM_TYPE_AUDIO_MPEG1     0x03
#define STREAM_TYPE_AUDIO_MPEG2     0x04
#define STREAM_TYPE_PRIVATE_SECTION 0x05
#define STREAM_TYPE_PRIVATE_DATA    0x06
#define STREAM_TYPE_AUDIO_AAC       0x0f
#define STREAM_TYPE_AUDIO_AAC_LATM  0x11
#define STREAM_TYPE_VIDEO_MPEG4     0x10
#define STREAM_TYPE_METADATA        0x15
#define STREAM_TYPE_VIDEO_H264      0x1b
#define STREAM_TYPE_VIDEO_HEVC      0x24
#define STREAM_TYPE_VIDEO_CAVS      0x42
#define STREAM_TYPE_VIDEO_VC1       0xea
#define STREAM_TYPE_VIDEO_DIRAC     0xd1

#define STREAM_TYPE_AUDIO_AC3       0x81
#define STREAM_TYPE_AUDIO_DTS       0x82
#define STREAM_TYPE_AUDIO_TRUEHD    0x83
#define STREAM_TYPE_AUDIO_EAC3      0x87

typedef struct MpegTSContext MpegTSContext;

MpegTSContext *avpriv_mpegts_parse_open(AVFormatContext *s);
int avpriv_mpegts_parse_packet(MpegTSContext *ts, AVPacket *pkt,
                               const uint8_t *buf, int len);
void avpriv_mpegts_parse_close(MpegTSContext *ts);

typedef struct SLConfigDescr {
    int use_au_start;
    int use_au_end;
    int use_rand_acc_pt;
    int use_padding;
    int use_timestamps;
    int use_idle;
    int timestamp_res;
    int timestamp_len;
    int ocr_len;
    int au_len;
    int inst_bitrate_len;
    int degr_prior_len;
    int au_seq_num_len;
    int packet_seq_num_len;
} SLConfigDescr;

typedef struct Mp4Descr {
    int es_id;
    int dec_config_descr_len;
    uint8_t *dec_config_descr;
    SLConfigDescr sl;
} Mp4Descr;

/**
 * Parse an MPEG-2 descriptor
 * @param[in] fc                    Format context (used for logging only)
 * @param st                        Stream
 * @param stream_type               STREAM_TYPE_xxx
 * @param pp                        Descriptor buffer pointer
 * @param desc_list_end             End of buffer
 * @return <0 to stop processing
 */
int ff_parse_mpeg2_descriptor(AVFormatContext *fc, AVStream *st, int stream_type,
                              const uint8_t **pp, const uint8_t *desc_list_end,
                              Mp4Descr *mp4_descr, int mp4_descr_count, int pid,
                              MpegTSContext *ts);

/**
 * Check presence of H264 startcode
 * @return <0 to stop processing
 */
int ff_check_h264_startcode(AVFormatContext *s, const AVStream *st, const AVPacket *pkt);

#endif /* AVFORMAT_MPEGTS_H */
