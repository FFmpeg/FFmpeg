/* 
 * General DV muxer/demuxer 
 * Copyright (c) 2003 Roman Shaposhnick
 *
 * Many thanks to Dan Dennedy <dan@dennedy.org> for providing wealth
 * of DV technical info.
 *
 * Raw DV format
 * Copyright (c) 2002 Fabrice Bellard.
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
#include <time.h>
#include "avformat.h"
#include "dvdata.h"
#include "dv.h"

struct DVDemuxContext {
    AVFormatContext* fctx;
    AVStream*        vst;
    AVStream*        ast[2];       
    AVPacket         audio_pkt[2];
    int              ach;
    int              frames;
    uint64_t         abytes;
};

struct DVMuxContext {
    const DVprofile*  sys;    /* Current DV profile. E.g.: 525/60, 625/50 */
    uint8_t     frame_buf[144000]; /* frame under contruction */
    FifoBuffer  audio_data;   /* Fifo for storing excessive amounts of PCM */
    int         frames;       /* Number of a current frame */
    time_t      start_time;   /* Start time of recording */
    uint8_t     aspect;       /* Aspect ID 0 - 4:3, 7 - 16:9 */
    int         has_audio;    /* frame under contruction has audio */
    int         has_video;    /* frame under contruction has video */
};

enum dv_section_type {
     dv_sect_header  = 0x1f,
     dv_sect_subcode = 0x3f,
     dv_sect_vaux    = 0x56,
     dv_sect_audio   = 0x76,
     dv_sect_video   = 0x96,
};

enum dv_pack_type {
     dv_header525     = 0x3f, /* see dv_write_pack for important details on */ 
     dv_header625     = 0xbf, /* these two packs */
     dv_timecode      = 0x13,
     dv_audio_source  = 0x50,
     dv_audio_control = 0x51,
     dv_audio_recdate = 0x52,
     dv_audio_rectime = 0x53,
     dv_video_source  = 0x60,
     dv_video_control = 0x61,
     dv_viedo_recdate = 0x62,
     dv_video_rectime = 0x63,
     dv_unknown_pack  = 0xff,
};



/*
 * The reason why the following three big ugly looking tables are
 * here is my lack of DV spec IEC 61834. The tables were basically 
 * constructed to make code that places packs in SSYB, VAUX and 
 * AAUX blocks very simple and table-driven. They conform to the
 * SMPTE 314M and the output of my personal DV camcorder, neither
 * of which is sufficient for a reliable DV stream producing. Thus
 * while code is still in development I'll be gathering input from
 * people with different DV equipment and modifying the tables to
 * accommodate all the quirks. Later on, if possible, some of them
 * will be folded into smaller tables and/or switch-if logic. For 
 * now, my only excuse is -- they don't eat up that much of a space.
 */

static const int dv_ssyb_packs_dist[12][6] = {
    { 0x13, 0x13, 0x13, 0x13, 0x13, 0x13 },
    { 0x13, 0x13, 0x13, 0x13, 0x13, 0x13 },
    { 0x13, 0x13, 0x13, 0x13, 0x13, 0x13 },
    { 0x13, 0x13, 0x13, 0x13, 0x13, 0x13 },
    { 0x13, 0x13, 0x13, 0x13, 0x13, 0x13 },
    { 0x13, 0x13, 0x13, 0x13, 0x13, 0x13 },
    { 0x13, 0x62, 0x63, 0x13, 0x62, 0x63 },
    { 0x13, 0x62, 0x63, 0x13, 0x62, 0x63 },
    { 0x13, 0x62, 0x63, 0x13, 0x62, 0x63 },
    { 0x13, 0x62, 0x63, 0x13, 0x62, 0x63 },
    { 0x13, 0x62, 0x63, 0x13, 0x62, 0x63 },
    { 0x13, 0x62, 0x63, 0x13, 0x62, 0x63 },
};

static const int dv_vaux_packs_dist[12][15] = {
    { 0x60, 0x61, 0x62, 0x63, 0xff, 0xff, 0xff, 0xff, 0xff, 
      0x60, 0x61, 0x62, 0x63, 0xff, 0xff },
    { 0x60, 0x61, 0x62, 0x63, 0xff, 0xff, 0xff, 0xff, 0xff, 
      0x60, 0x61, 0x62, 0x63, 0xff, 0xff },
    { 0x60, 0x61, 0x62, 0x63, 0xff, 0xff, 0xff, 0xff, 0xff, 
      0x60, 0x61, 0x62, 0x63, 0xff, 0xff },
    { 0x60, 0x61, 0x62, 0x63, 0xff, 0xff, 0xff, 0xff, 0xff, 
      0x60, 0x61, 0x62, 0x63, 0xff, 0xff },
    { 0x60, 0x61, 0x62, 0x63, 0xff, 0xff, 0xff, 0xff, 0xff, 
      0x60, 0x61, 0x62, 0x63, 0xff, 0xff },
    { 0x60, 0x61, 0x62, 0x63, 0xff, 0xff, 0xff, 0xff, 0xff, 
      0x60, 0x61, 0x62, 0x63, 0xff, 0xff },
    { 0x60, 0x61, 0x62, 0x63, 0xff, 0xff, 0xff, 0xff, 0xff, 
      0x60, 0x61, 0x62, 0x63, 0xff, 0xff },
    { 0x60, 0x61, 0x62, 0x63, 0xff, 0xff, 0xff, 0xff, 0xff, 
      0x60, 0x61, 0x62, 0x63, 0xff, 0xff },
    { 0x60, 0x61, 0x62, 0x63, 0xff, 0xff, 0xff, 0xff, 0xff, 
      0x60, 0x61, 0x62, 0x63, 0xff, 0xff },
    { 0x60, 0x61, 0x62, 0x63, 0xff, 0xff, 0xff, 0xff, 0xff, 
      0x60, 0x61, 0x62, 0x63, 0xff, 0xff },
    { 0x60, 0x61, 0x62, 0x63, 0xff, 0xff, 0xff, 0xff, 0xff, 
      0x60, 0x61, 0x62, 0x63, 0xff, 0xff },
    { 0x60, 0x61, 0x62, 0x63, 0xff, 0xff, 0xff, 0xff, 0xff, 
      0x60, 0x61, 0x62, 0x63, 0xff, 0xff },
};

static const int dv_aaux_packs_dist[12][9] = {
    { 0xff, 0xff, 0xff, 0x50, 0x51, 0x52, 0x53, 0xff, 0xff },
    { 0x50, 0x51, 0x52, 0x53, 0xff, 0xff, 0xff, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0x50, 0x51, 0x52, 0x53, 0xff, 0xff },
    { 0x50, 0x51, 0x52, 0x53, 0xff, 0xff, 0xff, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0x50, 0x51, 0x52, 0x53, 0xff, 0xff },
    { 0x50, 0x51, 0x52, 0x53, 0xff, 0xff, 0xff, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0x50, 0x51, 0x52, 0x53, 0xff, 0xff },
    { 0x50, 0x51, 0x52, 0x53, 0xff, 0xff, 0xff, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0x50, 0x51, 0x52, 0x53, 0xff, 0xff },
    { 0x50, 0x51, 0x52, 0x53, 0xff, 0xff, 0xff, 0xff, 0xff },
    { 0xff, 0xff, 0xff, 0x50, 0x51, 0x52, 0x53, 0xff, 0xff },
    { 0x50, 0x51, 0x52, 0x53, 0xff, 0xff, 0xff, 0xff, 0xff },
};

static inline uint16_t dv_audio_12to16(uint16_t sample)
{
    uint16_t shift, result;
    
    sample = (sample < 0x800) ? sample : sample | 0xf000;
    shift = (sample & 0xf00) >> 8;

    if (shift < 0x2 || shift > 0xd) {
	result = sample;
    } else if (shift < 0x8) {
        shift--;
	result = (sample - (256 * shift)) << shift;
    } else {
	shift = 0xe - shift;
	result = ((sample + ((256 * shift) + 1)) << shift) - 1;
    }

    return result;
}

static int dv_audio_frame_size(const DVprofile* sys, int frame)
{
    return sys->audio_samples_dist[frame % (sizeof(sys->audio_samples_dist)/
		                            sizeof(sys->audio_samples_dist[0]))];
}

static int dv_write_pack(enum dv_pack_type pack_id, DVMuxContext *c, uint8_t* buf)
{
    struct tm tc;
    time_t ct;
    int ltc_frame;

    buf[0] = (uint8_t)pack_id;
    switch (pack_id) {
    case dv_header525: /* I can't imagine why these two weren't defined as real */
    case dv_header625: /* packs in SMPTE314M -- they definitely look like ones */
          buf[1] = 0xf8 |               /* reserved -- always 1 */
	           (0 & 0x07);          /* APT: Track application ID */
          buf[2] = (0 << 7)    | /* TF1: audio data is 0 - valid; 1 - invalid */
	           (0x0f << 3) | /* reserved -- always 1 */
		   (0 & 0x07);   /* AP1: Audio application ID */
          buf[3] = (0 << 7)    | /* TF2: video data is 0 - valid; 1 - invalid */  
	           (0x0f << 3) | /* reserved -- always 1 */
		   (0 & 0x07);   /* AP2: Video application ID */
          buf[4] = (0 << 7)    | /* TF3: subcode(SSYB) is 0 - valid; 1 - invalid */ 
	           (0x0f << 3) | /* reserved -- always 1 */
		   (0 & 0x07);   /* AP3: Subcode application ID */
	  break;
    case dv_timecode:
          ct = (time_t)(c->frames / ((float)c->sys->frame_rate / 
                                     (float)c->sys->frame_rate_base));
          gmtime_r(&ct, &tc);
          /* 
           * LTC drop-frame frame counter drops two frames (0 and 1) every 
           * minute, unless it is exactly divisible by 10
           */
          ltc_frame = (c->frames + 2*ct/60 - 2*ct/600) % c->sys->ltc_divisor;
	  buf[1] = (0 << 7) | /* Color fame: 0 - unsync; 1 - sync mode */
		   (1 << 6) | /* Drop frame timecode: 0 - nondrop; 1 - drop */
		   ((ltc_frame / 10) << 4) | /* Tens of frames */
		   (ltc_frame % 10);         /* Units of frames */
	  buf[2] = (1 << 7) | /* Biphase mark polarity correction: 0 - even; 1 - odd */
		   ((tc.tm_sec / 10) << 4) | /* Tens of seconds */
		   (tc.tm_sec % 10);         /* Units of seconds */
	  buf[3] = (1 << 7) | /* Binary group flag BGF0 */
		   ((tc.tm_min / 10) << 4) | /* Tens of minutes */
		   (tc.tm_min % 10);         /* Units of minutes */
	  buf[4] = (1 << 7) | /* Binary group flag BGF2 */
		   (1 << 6) | /* Binary group flag BGF1 */
	           ((tc.tm_hour / 10) << 4) | /* Tens of hours */
		   (tc.tm_hour % 10);         /* Units of hours */
          break;
    case dv_audio_source:  /* AAUX source pack */
          buf[1] = (0 << 7) | /* locked mode       */
                   (1 << 6) | /* reserved -- always 1 */
	           (dv_audio_frame_size(c->sys, c->frames) -
		    c->sys->audio_min_samples[0]);
	                      /* # of samples      */
          buf[2] = (0 << 7) | /* multi-stereo      */
                   (0 << 5) | /* #of audio channels per block: 0 -- 1 channel */
                   (0 << 4) | /* pair bit: 0 -- one pair of channels */
	            0;        /* audio mode        */
          buf[3] = (1 << 7) | /* res               */
                   (1 << 6) | /* multi-language flag */
	           (c->sys->dsf << 5) | /*  system: 60fields/50fields */
	            0;        /* definition: 0 -- SD (525/625) */
          buf[4] = (1 << 7) | /* emphasis: 1 -- off */
                   (0 << 6) | /* emphasis time constant: 0 -- reserved */
	           (0 << 3) | /* frequency: 0 -- 48Khz, 1 -- 44,1Khz, 2 -- 32Khz */
                    0;        /* quantization: 0 -- 16bit linear, 1 -- 12bit nonlinear */			    
          break;
    case dv_audio_control:
          buf[1] = (0 << 6) | /* copy protection: 0 -- unrestricted */
                   (1 << 4) | /* input source: 1 -- digital input */
	           (3 << 2) | /* compression: 3 -- no information */
	            0;        /* misc. info/SMPTE emphasis off */
          buf[2] = (1 << 7) | /* recording start point: 1 -- no */
                   (1 << 6) | /* recording end point: 1 -- no */
	           (1 << 3) | /* recording mode: 1 -- original */
	            7;         
          buf[3] = (1 << 7) | /* direction: 1 -- forward */
                    0x20;     /* speed */
          buf[4] = (1 << 7) | /* reserved -- always 1 */
                    0x7f;     /* genre category */
	  break;
    case dv_audio_recdate:
    case dv_viedo_recdate:  /* VAUX recording date */
          ct = c->start_time + (time_t)(c->frames / 
	       ((float)c->sys->frame_rate / (float)c->sys->frame_rate_base));
          gmtime_r(&ct, &tc);
	  buf[1] = 0xff; /* ds, tm, tens of time zone, units of time zone */
	                 /* 0xff is very likely to be "unknown" */
	  buf[2] = (3 << 6) | /* reserved -- always 1 */
		   ((tc.tm_mday / 10) << 4) | /* Tens of day */
		   (tc.tm_mday % 10);         /* Units of day */
	  buf[3] = /* we set high 4 bits to 0, shouldn't we set them to week? */
	           (((tc.tm_mon + 1) / 10) << 4) |    /* Tens of month */
		   ((tc.tm_mon + 1) % 10);            /* Units of month */
	  buf[4] = (((tc.tm_year % 100) / 10) << 4) | /* Tens of year */
		   (tc.tm_year % 10);                 /* Units of year */
          break;
    case dv_audio_rectime:  /* AAUX recording time */
    case dv_video_rectime:  /* VAUX recording time */
          ct = c->start_time + (time_t)(c->frames / 
	       ((float)c->sys->frame_rate / (float)c->sys->frame_rate_base));
          gmtime_r(&ct, &tc);
	  buf[1] = (3 << 6) | /* reserved -- always 1 */
		   0x3f; /* tens of frame, units of frame: 0x3f - "unknown" ? */
	  buf[2] = (1 << 7) | /* reserved -- always 1 */ 
		   ((tc.tm_sec / 10) << 4) | /* Tens of seconds */
		   (tc.tm_sec % 10);         /* Units of seconds */
	  buf[3] = (1 << 7) | /* reserved -- always 1 */
		   ((tc.tm_min / 10) << 4) | /* Tens of minutes */
		   (tc.tm_min % 10);         /* Units of minutes */
	  buf[4] = (3 << 6) | /* reserved -- always 1 */ 
	           ((tc.tm_hour / 10) << 4) | /* Tens of hours */
		   (tc.tm_hour % 10);         /* Units of hours */
	  break;
    case dv_video_source:
	  buf[1] = 0xff; /* reserved -- always 1 */
	  buf[2] = (1 << 7) | /* B/W: 0 - b/w, 1 - color */
		   (1 << 6) | /* following CLF is valid - 0, invalid - 1 */
		   (3 << 4) | /* CLF: color frames id (see ITU-R BT.470-4) */
		   0xf; /* reserved -- always 1 */
	  buf[3] = (3 << 6) | /* reserved -- always 1 */
		   (c->sys->dsf << 5) | /*  system: 60fields/50fields */
		   0; /* signal type video compression */
	  buf[4] = 0xff; /* VISC: 0xff -- no information */
          break;
    case dv_video_control:
	  buf[1] = (0 << 6) | /* Copy generation management (CGMS) 0 -- free */
		   0x3f; /* reserved -- always 1 */
	  buf[2] = 0xc8 | /* reserved -- always b11001xxx */
		   c->aspect;
	  buf[3] = (1 << 7) | /* Frame/field flag 1 -- frame, 0 -- field */
		   (1 << 6) | /* First/second field flag 0 -- field 2, 1 -- field 1 */
		   (1 << 5) | /* Frame change flag 0 -- same picture as before, 1 -- different */
		   (1 << 4) | /* 1 - interlaced, 0 - noninterlaced */
		   0xc; /* reserved -- always b1100 */
	  buf[4] = 0xff; /* reserved -- always 1 */
          break;
    default:
          buf[1] = buf[2] = buf[3] = buf[4] = 0xff;
    }
    return 5;
}

static inline int dv_write_dif_id(enum dv_section_type t, uint8_t seq_num, 
                                  uint8_t dif_num, uint8_t* buf)
{
    buf[0] = (uint8_t)t;    /* Section type */
    buf[1] = (seq_num<<4) | /* DIF seq number 0-9 for 525/60; 0-11 for 625/50 */
	     (0 << 3) |     /* FSC: for 50Mb/s 0 - first channel; 1 - second */
	     7;             /* reserved -- always 1 */
    buf[2] = dif_num;       /* DIF block number Video: 0-134, Audio: 0-8 */
    return 3;
}

static inline int dv_write_ssyb_id(uint8_t syb_num, uint8_t fr, uint8_t* buf)
{
    if (syb_num == 0 || syb_num == 6) {
	buf[0] = (fr<<7) | /* FR ID 1 - first half of each channel; 0 - second */
		 (0<<4)  | /* AP3 (Subcode application ID) */
		 0x0f;     /* reserved -- always 1 */
    } 
    else if (syb_num == 11) {
	buf[0] = (fr<<7) | /* FR ID 1 - first half of each channel; 0 - second */
                 0x7f;     /* reserved -- always 1 */
    }
    else {
	buf[0] = (fr<<7) | /* FR ID 1 - first half of each channel; 0 - second */
                 (0<<4)  | /* APT (Track application ID) */
		 0x0f;     /* reserved -- always 1 */
    }
    buf[1] = 0xf0 |            /* reserved -- always 1 */
             (syb_num & 0x0f); /* SSYB number 0 - 11   */
    buf[2] = 0xff;             /* reserved -- always 1 */
    return 3;
}

static void dv_format_frame(DVMuxContext *c, uint8_t* buf)
{
    int i, j, k;
    
    for (i = 0; i < c->sys->difseg_size; i++) {
       memset(buf, 0xff, 80 * 6); /* First 6 DIF blocks are for control data */
       
       /* DV header: 1DIF */
       buf += dv_write_dif_id(dv_sect_header, i, 0, buf);
       buf += dv_write_pack((c->sys->dsf ? dv_header625 : dv_header525), c, buf);
       buf += 72; /* unused bytes */
				   
       /* DV subcode: 2DIFs */
       for (j = 0; j < 2; j++) {
          buf += dv_write_dif_id( dv_sect_subcode, i, j, buf);
	  for (k = 0; k < 6; k++) {
	     buf += dv_write_ssyb_id(k, (i < c->sys->difseg_size/2), buf);
	     buf += dv_write_pack(dv_ssyb_packs_dist[i][k], c, buf);
	  }
	  buf += 29; /* unused bytes */
       }
       
       /* DV VAUX: 3DIFs */
       for (j = 0; j < 3; j++) {
	  buf += dv_write_dif_id(dv_sect_vaux, i, j, buf);
	  for (k = 0; k < 15 ; k++)
	     buf += dv_write_pack(dv_vaux_packs_dist[i][k], c, buf);
	  buf += 2; /* unused bytes */
       } 
       
       /* DV Audio/Video: 135 Video DIFs + 9 Audio DIFs */
       for (j = 0; j < 135; j++) {
            if (j%15 == 0) {
	        buf += dv_write_dif_id(dv_sect_audio, i, j/15, buf);
	        buf += dv_write_pack(dv_aaux_packs_dist[i][j/15], c, buf);
		buf += 72; /* shuffled PCM audio */
	    }
	    buf += dv_write_dif_id(dv_sect_video, i, j, buf);
	    buf += 77; /* 1 video macro block: 1 bytes control
			                       4 * 14 bytes Y 8x8 data
			                           10 bytes Cr 8x8 data
						   10 bytes Cb 8x8 data */
       }
    }
}

static void dv_inject_audio(DVMuxContext *c, const uint8_t* pcm, uint8_t* frame_ptr)
{
    int i, j, d, of, size;
    size = 4 * dv_audio_frame_size(c->sys, c->frames);
    for (i = 0; i < c->sys->difseg_size; i++) {
       frame_ptr += 6 * 80; /* skip DIF segment header */
       for (j = 0; j < 9; j++) {
          for (d = 8; d < 80; d+=2) {
	     of = c->sys->audio_shuffle[i][j] + (d - 8)/2 * c->sys->audio_stride;
	     if (of*2 >= size)
	         continue;
	     
	     frame_ptr[d] = pcm[of*2+1]; // FIXME: may be we have to admit
	     frame_ptr[d+1] = pcm[of*2]; //        that DV is a big endian PCM       
          }
          frame_ptr += 16 * 80; /* 15 Video DIFs + 1 Audio DIF */
       }
    }
}

static void dv_inject_video(DVMuxContext *c, const uint8_t* video_data, uint8_t* frame_ptr)
{
    int i, j;
    int ptr = 0;

    for (i = 0; i < c->sys->difseg_size; i++) {
       ptr += 6 * 80; /* skip DIF segment header */
       for (j = 0; j < 135; j++) {
            if (j%15 == 0)
	        ptr += 80; /* skip Audio DIF */
	    ptr += 3;
	    memcpy(frame_ptr + ptr, video_data + ptr, 77);
	    ptr += 77;
       }
    }
}

/* 
 * This is the dumbest implementation of all -- it simply looks at
 * a fixed offset and if pack isn't there -- fails. We might want
 * to have a fallback mechanism for complete search of missing packs.
 */
static const uint8_t* dv_extract_pack(uint8_t* frame, enum dv_pack_type t)
{
    int offs;
    
    switch (t) {
    case dv_audio_source:
          offs = (80*6 + 80*16*3 + 3);
	  break;
    case dv_audio_control:
          offs = (80*6 + 80*16*4 + 3);
	  break;
    case dv_video_control:
          offs = (80*5 + 48 + 5);
          break;
    default:
          return NULL;
    }   

    return (frame[offs] == t ? &frame[offs] : NULL);
}

/* 
 * There's a couple of assumptions being made here:
 * 1. By default we silence erroneous (0x8000/16bit 0x800/12bit) audio samples.
 *    We can pass them upwards when ffmpeg will be ready to deal with them.
 * 2. We don't do software emphasis.
 * 3. Audio is always returned as 16bit linear samples: 12bit nonlinear samples
 *    are converted into 16bit linear ones.
 */
static int dv_extract_audio(uint8_t* frame, uint8_t* pcm, uint8_t* pcm2)
{
    int size, i, j, d, of, smpls, freq, quant, half_ch;
    uint16_t lc, rc;
    const DVprofile* sys;
    const uint8_t* as_pack;
    
    as_pack = dv_extract_pack(frame, dv_audio_source);
    if (!as_pack)    /* No audio ? */
	return 0;
   
    sys = dv_frame_profile(frame);
    smpls = as_pack[1] & 0x3f; /* samples in this frame - min. samples */
    freq = (as_pack[4] >> 3) & 0x07; /* 0 - 48KHz, 1 - 44,1kHz, 2 - 32 kHz */
    quant = as_pack[4] & 0x07; /* 0 - 16bit linear, 1 - 12bit nonlinear */
    
    if (quant > 1)
	return -1; /* Unsupported quantization */

    size = (sys->audio_min_samples[freq] + smpls) * 4; /* 2ch, 2bytes */
    half_ch = sys->difseg_size/2;

    /* for each DIF segment */
    for (i = 0; i < sys->difseg_size; i++) {
       frame += 6 * 80; /* skip DIF segment header */
       if (quant == 1 && i == half_ch) {
           if (!pcm2)
               break;
           else
               pcm = pcm2;
       }

       for (j = 0; j < 9; j++) {
          for (d = 8; d < 80; d += 2) {
	     if (quant == 0) {  /* 16bit quantization */
		 of = sys->audio_shuffle[i][j] + (d - 8)/2 * sys->audio_stride;
                 if (of*2 >= size)
		     continue;
		 
		 pcm[of*2] = frame[d+1]; // FIXME: may be we have to admit
		 pcm[of*2+1] = frame[d]; //        that DV is a big endian PCM
		 if (pcm[of*2+1] == 0x80 && pcm[of*2] == 0x00)
		     pcm[of*2+1] = 0;
	      } else {           /* 12bit quantization */
		 lc = ((uint16_t)frame[d] << 4) | 
		      ((uint16_t)frame[d+2] >> 4);
		 rc = ((uint16_t)frame[d+1] << 4) |
	              ((uint16_t)frame[d+2] & 0x0f);
		 lc = (lc == 0x800 ? 0 : dv_audio_12to16(lc));
		 rc = (rc == 0x800 ? 0 : dv_audio_12to16(rc));

		 of = sys->audio_shuffle[i%half_ch][j] + (d - 8)/3 * sys->audio_stride;
                 if (of*2 >= size)
		     continue;

		 pcm[of*2] = lc & 0xff; // FIXME: may be we have to admit
		 pcm[of*2+1] = lc >> 8; //        that DV is a big endian PCM
		 of = sys->audio_shuffle[i%half_ch+half_ch][j] + 
		      (d - 8)/3 * sys->audio_stride;
		 pcm[of*2] = rc & 0xff; // FIXME: may be we have to admit
		 pcm[of*2+1] = rc >> 8; //        that DV is a big endian PCM
		 ++d;
	      }
	  }
		
	  frame += 16 * 80; /* 15 Video DIFs + 1 Audio DIF */
        }
    }

    return size;
}

static int dv_extract_audio_info(DVDemuxContext* c, uint8_t* frame)
{
    const uint8_t* as_pack;
    const DVprofile* sys;
    int freq, smpls, quant, i;

    sys = dv_frame_profile(frame);
    as_pack = dv_extract_pack(frame, dv_audio_source);
    if (!as_pack || !sys) {    /* No audio ? */
        c->ach = 0;
	return -1;
    }
    
    smpls = as_pack[1] & 0x3f; /* samples in this frame - min. samples */
    freq = (as_pack[4] >> 3) & 0x07; /* 0 - 48KHz, 1 - 44,1kHz, 2 - 32 kHz */
    quant = as_pack[4] & 0x07; /* 0 - 16bit linear, 1 - 12bit nonlinear */
    c->ach = (quant && freq == 2) ? 2 : 1;

    /* The second stereo channel could appear in IEC 61834 stream only */
    if (c->ach == 2 && !c->ast[1]) {
        c->ast[1] = av_new_stream(c->fctx, 0);
	if (c->ast[1]) {
            av_set_pts_info(c->ast[1], 64, 1, 30000);
	    c->ast[1]->codec.codec_type = CODEC_TYPE_AUDIO;
	    c->ast[1]->codec.codec_id = CODEC_ID_PCM_S16LE;
	} else
	    c->ach = 1;
    }
    for (i=0; i<c->ach; i++) {
       c->ast[i]->codec.sample_rate = dv_audio_frequency[freq];
       c->ast[i]->codec.channels = 2;
       c->ast[i]->codec.bit_rate = 2 * dv_audio_frequency[freq] * 16;
    }
    
    return (sys->audio_min_samples[freq] + smpls) * 4; /* 2ch, 2bytes */;
}

static int dv_extract_video_info(DVDemuxContext *c, uint8_t* frame)
{
    const DVprofile* sys; 
    const uint8_t* vsc_pack;
    AVCodecContext* avctx;
    int apt, is16_9;
    int size = 0;
    
    sys = dv_frame_profile(frame);
    if (sys) {
        avctx = &c->vst->codec;
	
	avctx->frame_rate = sys->frame_rate;
        avctx->frame_rate_base = sys->frame_rate_base;
        avctx->width = sys->width;
        avctx->height = sys->height;
        avctx->pix_fmt = sys->pix_fmt;
        
	/* finding out SAR is a little bit messy */
	vsc_pack = dv_extract_pack(frame, dv_video_control);
        apt = frame[4] & 0x07;
	is16_9 = (vsc_pack && ((vsc_pack[2] & 0x07) == 0x02 ||
	                       (!apt && (vsc_pack[2] & 0x07) == 0x07)));
	avctx->sample_aspect_ratio = sys->sar[is16_9];
	
	size = sys->frame_size;
    }
    return size;
}

/* 
 * The following 6 functions constitute our interface to the world
 */

int dv_assemble_frame(DVMuxContext *c, AVStream* st,
                      const uint8_t* data, int data_size, uint8_t** frame)
{
    uint8_t pcm[8192];
    int fsize, reqasize;
   
    *frame = &c->frame_buf[0];
    if (c->has_audio && c->has_video) {  /* must be a stale frame */
        dv_format_frame(c, *frame);
	c->frames++;
	c->has_audio = c->has_video = 0;
    }
    
    if (st->codec.codec_type == CODEC_TYPE_VIDEO) {
        /* FIXME: we have to have more sensible approach than this one */
	if (c->has_video)
	    av_log(&st->codec, AV_LOG_ERROR, "Can't process DV frame #%d. Insufficient audio data or severe sync problem.\n", c->frames);
	    
        dv_inject_video(c, data, *frame);
	c->has_video = 1;
	data_size = 0;
    } 
    
    reqasize = 4 * dv_audio_frame_size(c->sys, c->frames);
    fsize = fifo_size(&c->audio_data, c->audio_data.rptr);
    if (st->codec.codec_type == CODEC_TYPE_AUDIO || (c->has_video && fsize >= reqasize)) { 
	if (fsize + data_size >= reqasize && !c->has_audio) {
            if (fsize >= reqasize) {
	        fifo_read(&c->audio_data, &pcm[0], reqasize, &c->audio_data.rptr);
            } else {
	        fifo_read(&c->audio_data, &pcm[0], fsize, &c->audio_data.rptr);
                memcpy(&pcm[fsize], &data[0], reqasize - fsize);
		data += reqasize - fsize;
		data_size -= reqasize - fsize;
	    }
	    dv_inject_audio(c, &pcm[0], *frame);
	    c->has_audio = 1;
	}
    
        /* FIXME: we have to have more sensible approach than this one */
        if (fifo_size(&c->audio_data, c->audio_data.rptr) + data_size >= 100*AVCODEC_MAX_AUDIO_FRAME_SIZE)
	    av_log(&st->codec, AV_LOG_ERROR, "Can't process DV frame #%d. Insufficient video data or severe sync problem.\n", c->frames);
	fifo_write(&c->audio_data, (uint8_t *)data, data_size, &c->audio_data.wptr);
    }

    return (c->has_audio && c->has_video) ? c->sys->frame_size : 0;
}

DVMuxContext* dv_init_mux(AVFormatContext* s)
{
    DVMuxContext *c;
    AVStream *vst;
    AVStream *ast;

    c = av_mallocz(sizeof(DVMuxContext));
    if (!c)
        return NULL;

    if (s->nb_streams != 2)
        goto bail_out;

    /* We have to sort out where audio and where video stream is */
    if (s->streams[0]->codec.codec_type == CODEC_TYPE_VIDEO &&
        s->streams[1]->codec.codec_type == CODEC_TYPE_AUDIO) {
       vst = s->streams[0];
       ast = s->streams[1];
    }
    else if (s->streams[1]->codec.codec_type == CODEC_TYPE_VIDEO &&
             s->streams[0]->codec.codec_type == CODEC_TYPE_AUDIO) {
           vst = s->streams[1];
           ast = s->streams[0];
    } else
        goto bail_out;
  
    /* Some checks -- DV format is very picky about its incoming streams */
    if (vst->codec.codec_id != CODEC_ID_DVVIDEO ||
	ast->codec.codec_id != CODEC_ID_PCM_S16LE)
       goto bail_out;
    if (ast->codec.sample_rate != 48000 ||
	ast->codec.channels != 2)
       goto bail_out;

    c->sys = dv_codec_profile(&vst->codec);
    if (!c->sys)
	goto bail_out;
    
    /* Ok, everything seems to be in working order */
    c->frames = 0;
    c->has_audio = c->has_video = 0;
    c->start_time = (time_t)s->timestamp;
    c->aspect = 0; /* 4:3 is the default */
    if ((int)(av_q2d(vst->codec.sample_aspect_ratio) * vst->codec.width / vst->codec.height * 10) == 17) /* 16:9 */ 
        c->aspect = 0x07;

    if (fifo_init(&c->audio_data, 100*AVCODEC_MAX_AUDIO_FRAME_SIZE) < 0)
        goto bail_out;

    dv_format_frame(c, &c->frame_buf[0]);

    return c;
    
bail_out:
    av_free(c);
    return NULL;
}

void dv_delete_mux(DVMuxContext *c)
{    
    fifo_free(&c->audio_data);
}

DVDemuxContext* dv_init_demux(AVFormatContext *s)
{
    DVDemuxContext *c;

    c = av_mallocz(sizeof(DVDemuxContext));
    if (!c)
        return NULL;

    c->vst = av_new_stream(s, 0);
    c->ast[0] = av_new_stream(s, 0);
    if (!c->vst || !c->ast[0])
        goto fail;
    av_set_pts_info(c->vst, 64, 1, 30000);
    av_set_pts_info(c->ast[0], 64, 1, 30000);

    c->fctx = s;
    c->ast[1] = NULL;
    c->ach = 0;
    c->frames = 0;
    c->abytes = 0;
    c->audio_pkt[0].size = 0;
    c->audio_pkt[1].size = 0;
    
    c->vst->codec.codec_type = CODEC_TYPE_VIDEO;
    c->vst->codec.codec_id = CODEC_ID_DVVIDEO;
    c->vst->codec.bit_rate = 25000000;
    
    c->ast[0]->codec.codec_type = CODEC_TYPE_AUDIO;
    c->ast[0]->codec.codec_id = CODEC_ID_PCM_S16LE;
   
    s->ctx_flags |= AVFMTCTX_NOHEADER; 
    
    return c;
    
fail:
    if (c->vst)
        av_free(c->vst);
    if (c->ast[0])
        av_free(c->ast[0]);
    av_free(c);
    return NULL;
}

static void __destruct_pkt(struct AVPacket *pkt)
{
    pkt->data = NULL; pkt->size = 0;
    return;
}

int dv_get_packet(DVDemuxContext *c, AVPacket *pkt)
{
    int size = -1;
    int i;

    for (i=0; i<c->ach; i++) {
       if (c->ast[i] && c->audio_pkt[i].size) {
           *pkt = c->audio_pkt[i];
	   c->audio_pkt[i].size = 0;
	   size = pkt->size;
	   break;
       }
    }

    return size;
}

int dv_produce_packet(DVDemuxContext *c, AVPacket *pkt, 
                      uint8_t* buf, int buf_size)
{
    int size, i;
    const DVprofile* sys = dv_frame_profile(buf);
   
    if (buf_size < 4 || buf_size < sys->frame_size)
        return -1;   /* Broken frame, or not enough data */

    /* Queueing audio packet */
    /* FIXME: in case of no audio/bad audio we have to do something */
    size = dv_extract_audio_info(c, buf);
    c->audio_pkt[0].data = c->audio_pkt[1].data = NULL;
    for (i=0; i<c->ach; i++) {
       if (av_new_packet(&c->audio_pkt[i], size) < 0)
           return AVERROR_NOMEM;
       c->audio_pkt[i].stream_index = c->ast[i]->index;
       c->audio_pkt[i].pts = c->abytes * 30000*8 / c->ast[i]->codec.bit_rate;
       c->audio_pkt[i].flags |= PKT_FLAG_KEY;
    }
    dv_extract_audio(buf, c->audio_pkt[0].data, c->audio_pkt[1].data);
    c->abytes += size;
    
    /* Now it's time to return video packet */
    size = dv_extract_video_info(c, buf);
    av_init_packet(pkt);
    pkt->destruct = __destruct_pkt;
    pkt->data     = buf;
    pkt->size     = size; 
    pkt->flags   |= PKT_FLAG_KEY;
    pkt->stream_index = c->vst->id;
    pkt->pts      = c->frames * sys->frame_rate_base * (30000/sys->frame_rate);
    
    c->frames++;

    return size;
}
                           
int64_t dv_frame_offset(DVDemuxContext *c, int64_t timestamp)
{
    const DVprofile* sys = dv_codec_profile(&c->vst->codec);

    return sys->frame_size * ((timestamp * sys->frame_rate) / 
	                      (AV_TIME_BASE * sys->frame_rate_base));
}

/************************************************************
 * Implementation of the easiest DV storage of all -- raw DV.
 ************************************************************/
 
typedef struct RawDVContext {
    uint8_t         buf[144000];
    DVDemuxContext* dv_demux;
} RawDVContext;

static int dv_read_header(AVFormatContext *s,
                          AVFormatParameters *ap)
{
    RawDVContext *c = s->priv_data;
    c->dv_demux = dv_init_demux(s);
   
    return c->dv_demux ? 0 : -1;
}


static int dv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int size;
    RawDVContext *c = s->priv_data; 
  
    size = dv_get_packet(c->dv_demux, pkt);
    
    if (size < 0) {
        if (get_buffer(&s->pb, c->buf, 4) <= 0) 
            return AVERROR_IO;
    
        size = dv_frame_profile(c->buf)->frame_size;
        if (get_buffer(&s->pb, c->buf + 4, size - 4) <= 0)
	    return AVERROR_IO;

	size = dv_produce_packet(c->dv_demux, pkt, c->buf, size);
    } 
    
    return size;
}

static int dv_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp)
{
    RawDVContext *c = s->priv_data;

    return url_fseek(&s->pb, dv_frame_offset(c->dv_demux, timestamp), SEEK_SET);
}

static int dv_read_close(AVFormatContext *s)
{
    RawDVContext *c = s->priv_data;
    av_free(c->dv_demux);
    return 0;
}

static int dv_write_header(AVFormatContext *s)
{
    s->priv_data = dv_init_mux(s);
    if (!s->priv_data) {
        av_log(s, AV_LOG_ERROR, "Can't initialize DV format!\n"
		    "Make sure that you supply exactly two streams:\n"
		    "     video: 25fps or 29.97fps, audio: 2ch/48Khz/PCM\n");
	return -1;
    }
    return 0;
}

static int dv_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    uint8_t* frame;
    int fsize;
   
    fsize = dv_assemble_frame((DVMuxContext *)s->priv_data, s->streams[pkt->stream_index],
                              pkt->data, pkt->size, &frame);
    if (fsize > 0) {
        put_buffer(&s->pb, frame, fsize); 
        put_flush_packet(&s->pb);
    } 
    return 0;
}

/* 
 * We might end up with some extra A/V data without matching counterpart.
 * E.g. video data without enough audio to write the complete frame.
 * Currently we simply drop the last frame. I don't know whether this 
 * is the best strategy of all
 */
static int dv_write_trailer(struct AVFormatContext *s)
{
    dv_delete_mux((DVMuxContext *)s->priv_data);
    return 0;
}

static AVInputFormat dv_iformat = {
    "dv",
    "DV video format",
    sizeof(RawDVContext),
    NULL,
    dv_read_header,
    dv_read_packet,
    dv_read_close,
    dv_read_seek,
    .extensions = "dv,dif",
};

static AVOutputFormat dv_oformat = {
    "dv",
    "DV video format",
    NULL,
    "dv",
    sizeof(DVMuxContext),
    CODEC_ID_PCM_S16LE,
    CODEC_ID_DVVIDEO,
    dv_write_header,
    dv_write_packet,
    dv_write_trailer,
};

int ff_dv_init(void)
{
    av_register_input_format(&dv_iformat);
    av_register_output_format(&dv_oformat);
    return 0;
}
