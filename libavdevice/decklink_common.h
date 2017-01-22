/*
 * Blackmagic DeckLink common code
 * Copyright (c) 2013-2014 Ramiro Polla, Luca Barbato, Deti Fliegl
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

#ifndef AVDEVICE_DECKLINK_COMMON_H
#define AVDEVICE_DECKLINK_COMMON_H

#include <DeckLinkAPIVersion.h>

#include "decklink_common_c.h"

class decklink_output_callback;
class decklink_input_callback;

typedef struct AVPacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    unsigned long long size;
    int abort_request;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    AVFormatContext *avctx;
} AVPacketQueue;

struct decklink_ctx {
    /* DeckLink SDK interfaces */
    IDeckLink *dl;
    IDeckLinkOutput *dlo;
    IDeckLinkInput *dli;
    IDeckLinkConfiguration *cfg;
    IDeckLinkAttributes *attr;
    decklink_output_callback *output_callback;
    decklink_input_callback *input_callback;

    /* DeckLink mode information */
    BMDTimeValue bmd_tb_den;
    BMDTimeValue bmd_tb_num;
    BMDDisplayMode bmd_mode;
    BMDVideoConnection video_input;
    BMDAudioConnection audio_input;
    int bmd_width;
    int bmd_height;
    int bmd_field_dominance;

    /* Capture buffer queue */
    AVPacketQueue queue;

    /* Streams present */
    int audio;
    int video;

    /* Status */
    int playback_started;
    int capture_started;
    int64_t last_pts;
    unsigned long frameCount;
    unsigned int dropped;
    AVStream *audio_st;
    AVStream *video_st;
    AVStream *teletext_st;

    /* Options */
    int list_devices;
    int list_formats;
    int64_t teletext_lines;
    double preroll;
    int duplex_mode;
    DecklinkPtsSource audio_pts_source;
    DecklinkPtsSource video_pts_source;
    int draw_bars;

    int frames_preroll;
    int frames_buffer;

    sem_t semaphore;

    int channels;
};

typedef enum { DIRECTION_IN, DIRECTION_OUT} decklink_direction_t;

#ifdef _WIN32
#if BLACKMAGIC_DECKLINK_API_VERSION < 0x0a040000
typedef unsigned long buffercount_type;
#else
typedef unsigned int buffercount_type;
#endif
IDeckLinkIterator *CreateDeckLinkIteratorInstance(void);
#else
typedef uint32_t buffercount_type;
#endif

static const BMDAudioConnection decklink_audio_connection_map[] = {
    (BMDAudioConnection)0,
    bmdAudioConnectionEmbedded,
    bmdAudioConnectionAESEBU,
    bmdAudioConnectionAnalog,
    bmdAudioConnectionAnalogXLR,
    bmdAudioConnectionAnalogRCA,
    bmdAudioConnectionMicrophone,
};

static const BMDVideoConnection decklink_video_connection_map[] = {
    (BMDVideoConnection)0,
    bmdVideoConnectionSDI,
    bmdVideoConnectionHDMI,
    bmdVideoConnectionOpticalSDI,
    bmdVideoConnectionComponent,
    bmdVideoConnectionComposite,
    bmdVideoConnectionSVideo,
};

HRESULT ff_decklink_get_display_name(IDeckLink *This, const char **displayName);
int ff_decklink_set_format(AVFormatContext *avctx, int width, int height, int tb_num, int tb_den, decklink_direction_t direction = DIRECTION_OUT, int num = 0);
int ff_decklink_set_format(AVFormatContext *avctx, decklink_direction_t direction, int num);
int ff_decklink_list_devices(AVFormatContext *avctx);
int ff_decklink_list_formats(AVFormatContext *avctx, decklink_direction_t direction = DIRECTION_OUT);
void ff_decklink_cleanup(AVFormatContext *avctx);
int ff_decklink_init_device(AVFormatContext *avctx, const char* name);

#endif /* AVDEVICE_DECKLINK_COMMON_H */
