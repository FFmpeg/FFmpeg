/* 
 * DV format muxer/demuxer
 * Copyright (c) 2003 Roman Shaposhnik
 *
 * Many thanks to Dan Dennedy <dan@dennedy.org> for providing wealth
 * of DV technical info, and SMPTE 314M specification.
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
#include "dvcore.h"

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

static const uint16_t dv_audio_shuffle525[10][9] = {
  {  0, 30, 60, 20, 50, 80, 10, 40, 70 }, /* 1st channel */
  {  6, 36, 66, 26, 56, 86, 16, 46, 76 },
  { 12, 42, 72,  2, 32, 62, 22, 52, 82 },
  { 18, 48, 78,  8, 38, 68, 28, 58, 88 },
  { 24, 54, 84, 14, 44, 74,  4, 34, 64 },
  
  {  1, 31, 61, 21, 51, 81, 11, 41, 71 }, /* 2nd channel */
  {  7, 37, 67, 27, 57, 87, 17, 47, 77 },
  { 13, 43, 73,  3, 33, 63, 23, 53, 83 },
  { 19, 49, 79,  9, 39, 69, 29, 59, 89 },
  { 25, 55, 85, 15, 45, 75,  5, 35, 65 },
};

static const uint16_t dv_audio_shuffle625[12][9] = {
  {   0,  36,  72,  26,  62,  98,  16,  52,  88}, /* 1st channel */
  {   6,  42,  78,  32,  68, 104,  22,  58,  94},
  {  12,  48,  84,   2,  38,  74,  28,  64, 100},
  {  18,  54,  90,   8,  44,  80,  34,  70, 106},
  {  24,  60,  96,  14,  50,  86,   4,  40,  76},  
  {  30,  66, 102,  20,  56,  92,  10,  46,  82},
	
  {   1,  37,  73,  27,  63,  99,  17,  53,  89}, /* 2nd channel */
  {   7,  43,  79,  33,  69, 105,  23,  59,  95},
  {  13,  49,  85,   3,  39,  75,  29,  65, 101},
  {  19,  55,  91,   9,  45,  81,  35,  71, 107},
  {  25,  61,  97,  15,  51,  87,   5,  41,  77},  
  {  31,  67, 103,  21,  57,  93,  11,  47,  83},
};

static const int dv_audio_frequency[3] = {
    48000, 44100, 32000,
};
    
const DVprofile dv_profiles[2] = {
    { .dsf = 0,
      .frame_size = 120000,        /* 525/60 system (NTSC) */
      .difseg_size = 10,
      .frame_rate = 30000,
      .ltc_divisor = 30,
      .frame_rate_base = 1001,
      .height = 480,
/*    .video_place = dv_place_411, */
      .audio_stride = 90,
      .audio_min_samples = { 1580, 1452, 1053 }, /* for 48, 44.1 and 32Khz */
      .audio_samples_dist = { 1602, 1601, 1602, 1601, 1602 },
      .audio_shuffle = dv_audio_shuffle525,
    }, 
    { .dsf = 1,
      .frame_size = 144000,        /* 625/50 system (PAL) */
      .difseg_size = 12,
      .frame_rate = 25,
      .frame_rate_base = 1,
      .ltc_divisor = 25,
      .height = 576,
/*    .video_place = dv_place_420, */
      .audio_stride = 108,
      .audio_min_samples = { 1896, 1742, 1264 }, /* for 48, 44.1 and 32Khz */
      .audio_samples_dist = { 1920, 1920, 1920, 1920, 1920 },
      .audio_shuffle = dv_audio_shuffle525,
     }
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
          localtime_r(&ct, &tc);
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
          localtime_r(&ct, &tc);
	  buf[1] = 0xff; /* ds, tm, tens of time zone, units of time zone */
	                 /* 0xff is very likely to be "unknown" */
	  buf[2] = (3 << 6) | /* reserved -- always 1 */
		   ((tc.tm_mday / 10) << 4) | /* Tens of day */
		   (tc.tm_mday % 10);         /* Units of day */
	  buf[3] = /* we set high 4 bits to 0, shouldn't we set them to week? */
		   (tc.tm_mon % 10);         /* Units of month */
	  buf[4] = (((tc.tm_year % 100) / 10) << 4) | /* Tens of year */
		   (tc.tm_year % 10);                 /* Units of year */
          break;
    case dv_audio_rectime:  /* AAUX recording time */
    case dv_video_rectime:  /* VAUX recording time */
          ct = c->start_time + (time_t)(c->frames / 
	       ((float)c->sys->frame_rate / (float)c->sys->frame_rate_base));
          localtime_r(&ct, &tc);
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

void dv_format_frame(DVMuxContext *c, uint8_t* buf)
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

void dv_inject_audio(DVMuxContext *c, const uint8_t* pcm, uint8_t* frame_ptr)
{
    int i, j, d, of;
    for (i = 0; i < c->sys->difseg_size; i++) {
       frame_ptr += 6 * 80; /* skip DIF segment header */
       for (j = 0; j < 9; j++) {
          for (d = 8; d < 80; d+=2) {
	     of = c->sys->audio_shuffle[i][j] + (d - 8)/2 * c->sys->audio_stride;
	     frame_ptr[d] = pcm[of*2+1]; // FIXME: may be we have to admit
	     frame_ptr[d+1] = pcm[of*2]; //        that DV is a big endian PCM       
          }
          frame_ptr += 16 * 80; /* 15 Video DIFs + 1 Audio DIF */
       }
    }
}

void dv_inject_video(DVMuxContext *c, const uint8_t* video_data, uint8_t* frame_ptr)
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

int dv_audio_frame_size(const DVprofile* sys, int frame)
{
    return sys->audio_samples_dist[frame % (sizeof(sys->audio_samples_dist)/
		                            sizeof(sys->audio_samples_dist[0]))];
}

const DVprofile* dv_frame_profile(uint8_t* frame)
{
    return &dv_profiles[!!(frame[3] & 0x80)]; /* Header, DSF flag */
}

/* 
 * This is the dumbest implementation of all -- it simply looks at
 * a fixed offset and if pack isn't there -- fails. We might want
 * to have a fallback mechanism for complete search of missing packs.
 */
const uint8_t* dv_extract_pack(uint8_t* frame, enum dv_pack_type t)
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
int dv_extract_audio(uint8_t* frame, uint8_t* pcm, AVCodecContext* avctx)
{
    int size, i, j, d, of, smpls, freq, quant;
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

    avctx->sample_rate = dv_audio_frequency[freq];
    avctx->channels = 2;
    avctx->bit_rate = avctx->channels * avctx->sample_rate * 16;
    // What about:
    // avctx->frame_size =
   
    size = (sys->audio_min_samples[freq] + smpls) * 4; /* 2ch, 2bytes */

    /* for each DIF segment */
    for (i = 0; i < sys->difseg_size; i++) {
       frame += 6 * 80; /* skip DIF segment header */
       for (j = 0; j < 9; j++) {
          for (d = 8; d < 80; d += 2) {
	     if (quant == 0) {  /* 16bit quantization */
		 of = sys->audio_shuffle[i][j] + (d - 8)/2 * sys->audio_stride;
		 pcm[of*2] = frame[d+1]; // FIXME: may be we have to admit
		 pcm[of*2+1] = frame[d]; //        that DV is a big endian PCM
		 if (pcm[of*2+1] == 0x80 && pcm[of*2] == 0x00)
		     pcm[of*2+1] = 0;
	      } else {           /* 12bit quantization */
		 if (i >= sys->difseg_size/2)
	             goto out;  /* We're not doing 4ch at this time */
		       
		 lc = ((uint16_t)frame[d] << 4) | 
		      ((uint16_t)frame[d+2] >> 4);
		 rc = ((uint16_t)frame[d+1] << 4) |
	              ((uint16_t)frame[d+2] & 0x0f);
		 lc = (lc == 0x800 ? 0 : dv_audio_12to16(lc));
		 rc = (rc == 0x800 ? 0 : dv_audio_12to16(rc));

		 of = sys->audio_shuffle[i][j] + (d - 8)/3 * sys->audio_stride;
		 pcm[of*2] = lc & 0xff; // FIXME: may be we have to admit
		 pcm[of*2+1] = lc >> 8; //        that DV is a big endian PCM
		 of = sys->audio_shuffle[i+sys->difseg_size/2][j] + 
		      (d - 8)/3 * sys->audio_stride;
		 pcm[of*2] = rc & 0xff; // FIXME: may be we have to admit
		 pcm[of*2+1] = rc >> 8; //        that DV is a big endian PCM
		 ++d;
	      }
	  }
		
	  frame += 16 * 80; /* 15 Video DIFs + 1 Audio DIF */
        }
    }

out:
    return size;
}

/* FIXME: The following three functions could be underengineered ;-) */
void dv_assemble_frame(DVMuxContext *c, const uint8_t* video, const uint8_t* audio, int asize)
{
    uint8_t pcm[8192];
    uint8_t* frame = &c->frame_buf[0];
    int fsize, reqasize;
   
    if (c->has_audio && c->has_video) {  /* must be a stale frame */
        dv_format_frame(c, frame);
	c->frames++;
	c->has_audio = c->has_video = 0;
    }
    
    if (video) {
        /* FIXME: we have to have more sensible approach than this one */
	if (c->has_video)
	    fprintf(stderr, "Can't process DV frame #%d. Insufficient audio data or severe sync problem.\n", c->frames);
	    
        dv_inject_video(c, video, frame);
	c->has_video = 1;
    } 
    if (audio) {
        reqasize = 4 * dv_audio_frame_size(c->sys, c->frames);
        fsize = fifo_size(&c->audio_data, c->audio_data.rptr);
	if (fsize + asize >= reqasize) {
            if (fsize >= reqasize) {
	        fifo_read(&c->audio_data, &pcm[0], reqasize, &c->audio_data.rptr);
            } else {
	        fifo_read(&c->audio_data, &pcm[0], fsize, &c->audio_data.rptr);
                memcpy(&pcm[fsize], &audio[0], reqasize - fsize);
		audio += reqasize - fsize;
		asize -= reqasize - fsize;
	    }
	    dv_inject_audio(c, &pcm[0], frame);
	    c->has_audio = 1;
	}
    
        /* FIXME: we have to have more sensible approach than this one */
        if (fifo_size(&c->audio_data, c->audio_data.rptr) + asize >= AVCODEC_MAX_AUDIO_FRAME_SIZE)
	    fprintf(stderr, "Can't process DV frame #%d. Insufficient video data or severe sync problem.\n", c->frames);
	fifo_write(&c->audio_data, (uint8_t *)audio, asize, &c->audio_data.wptr);
    }
}

int dv_core_init(DVMuxContext *c, AVStream *streams[])
{
    /* We have to sort out where audio and where video stream is */
    if (streams[0]->codec.codec_type == CODEC_TYPE_VIDEO &&
        streams[1]->codec.codec_type == CODEC_TYPE_AUDIO) {
       c->vst = 0;
       c->ast = 1;
    }
    else if (streams[1]->codec.codec_type == CODEC_TYPE_VIDEO &&
             streams[0]->codec.codec_type == CODEC_TYPE_AUDIO) {
	    c->vst = 1;
 	    c->ast = 0;
    } else
        goto bail_out;
  
    /* Some checks -- DV format is very picky about its incoming streams */
    if (streams[c->vst]->codec.codec_id != CODEC_ID_DVVIDEO ||
	streams[c->ast]->codec.codec_id != CODEC_ID_PCM_S16LE)
       goto bail_out;
    if (streams[c->ast]->codec.sample_rate != 48000 ||
	streams[c->ast]->codec.channels != 2)
       goto bail_out;
	
    if (streams[c->vst]->codec.frame_rate == 25 &&
        streams[c->vst]->codec.frame_rate_base == 1) {
        /* May be we have to pick sys for every frame */
	c->sys = &dv_profiles[1];
    }
    else if (streams[c->vst]->codec.frame_rate == 30000 &&
             streams[c->vst]->codec.frame_rate_base == 1001) {
	/* May be we have to pick sys for every frame */
	c->sys = &dv_profiles[0];
    } else
        goto bail_out;
    
    /* Ok, everything seems to be in working order */
    c->frames = 0;
    c->has_audio = c->has_video = 0;
    c->start_time = time(NULL);
    c->aspect = 0; /* 4:3 is the default */
    if (streams[c->vst]->codec.aspect_ratio == 16.0 / 9.0)
        c->aspect = 0x07;

    if (fifo_init(&c->audio_data, AVCODEC_MAX_AUDIO_FRAME_SIZE) < 0)
        goto bail_out;

    dv_format_frame(c, &c->frame_buf[0]);

    return 0;
    
bail_out:
    return -1;
}

void dv_core_delete(DVMuxContext *c)
{    
    fifo_free(&c->audio_data);
}
