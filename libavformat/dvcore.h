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
#include <time.h>
#include "avformat.h"

/* 
 * DVprofile is used to express the differences between various 
 * DV flavors. For now it's primarily used for differentiating
 * 525/60 and 625/50, but the plans are to use it for various
 * DV specs as well (e.g. SMPTE314M vs. IEC 61834).
 */
typedef struct DVprofile {
    int            dsf;                  /* value of the dsf in the DV header */
    int            frame_size;           /* total size of one frame in bytes */
    int            difseg_size;          /* number of DIF segments */
    int            frame_rate;      
    int            frame_rate_base;
    int            ltc_divisor;          /* FPS from the LTS standpoint */
    int            height;               /* picture height in pixels */
    uint16_t       *video_place;         /* positions of all DV macro blocks */
    
    int            audio_stride;         /* size of audio_shuffle table */
    int            audio_min_samples[3]; /* min ammount of audio samples */
                                         /* for 48Khz, 44.1Khz and 32Khz */
    int            audio_samples_dist[5];/* how many samples are supposed to be */
                                         /* in each frame in a 5 frames window */
    const uint16_t (*audio_shuffle)[9];  /* PCM shuffling table */
} DVprofile;

typedef struct DVMuxContext {
    const DVprofile*  sys;    /* Current DV profile. E.g.: 525/60, 625/50 */
    uint8_t     frame_buf[144000]; /* frame under contruction */
    FifoBuffer  audio_data;   /* Fifo for storing excessive amounts of PCM */
    int         frames;       /* Number of a current frame */
    time_t      start_time;   /* Start time of recording */
    uint8_t     aspect;       /* Aspect ID 0 - 4:3, 7 - 16:9 */
    int         ast, vst;     /* Audio and Video stream indecies */
    int         has_audio;    /* frame under contruction has audio */
    int         has_video;    /* frame under contruction has video */
} DVMuxContext;

void dv_format_frame(DVMuxContext *, uint8_t*);
void dv_inject_audio(DVMuxContext *, uint8_t*, uint8_t*);
void dv_inject_video(DVMuxContext *, uint8_t*, uint8_t*);

int  dv_extract_audio(uint8_t*, uint8_t*, AVCodecContext*);

int  dv_audio_frame_size(const DVprofile*, int);

void dv_assemble_frame(DVMuxContext *, uint8_t*, uint8_t*, int);
int  dv_core_init(DVMuxContext *, AVStream*[]);
void dv_core_delete(DVMuxContext *);

const DVprofile* dv_frame_profile(uint8_t*);
