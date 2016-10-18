/*
 * SCTE-35 parser
 * Copyright (c) 2016 Carlos Fernandez
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
#ifndef AVFORMAT_SCTE_35_H
#define AVFORMAT_SCTE_35_H

#include "libavutil/bprint.h"

struct scte35_event {
    /* ID given for each separate event */
    int32_t id;
    /* pts specify time when event starts */
    uint64_t in_pts;
    uint64_t nearest_in_pts;
    /* pts specify ehen events end */
    uint64_t out_pts;
    /* duration of the event */
    int64_t duration;
    int64_t start_pos;
    int running;
    int ref_count;
    /* to traverse the list of events */
    struct scte35_event *next;
    struct scte35_event *prev;
};

enum scte35_event_state {
    /* NO event */
    EVENT_NONE,
    /* Commercials need to end */
    EVENT_IN,
    /* Commercials can start from here */
    EVENT_OUT,
    /* commercial can continue */
    EVENT_OUT_CONT,
};

struct scte35_interface {
    /* contain all  the events */
    struct scte35_event *event_list;
    /* state of current event */
    enum scte35_event_state event_state;
    /* time base of pts used in parser */
    AVRational timebase;
    struct scte35_event *current_event;
    /* saved previous state to correctly transition
        the event state */
    int prev_event_state;
    //TODO use AV_BASE64_SIZE to dynamically allocate the array
    char pkt_base64[1024];
    /* keep context of its parent for log */
    void *parent;
    /* general purpose str */
    AVBPrint avbstr;

    void (*update_video_pts)(struct scte35_interface *iface, uint64_t pts);
    struct scte35_event* (*update_event_state)(struct scte35_interface *iface);
    char* (*get_hls_string)(struct scte35_interface *iface, struct scte35_event *event,
               const char *adv_filename, int out_state, int seg_count, int64_t pos);

    void (*unref_scte35_event)(struct scte35_event **event);
    void (*ref_scte35_event)(struct scte35_event *event);
};

int ff_parse_scte35_pkt(struct scte35_interface *iface, const AVPacket *avpkt);

struct scte35_interface* ff_alloc_scte35_parser(void *parent, AVRational timebase);
void ff_delete_scte35_parser(struct scte35_interface* iface);
#endif /* AVFORMAT_SCTE_35_H */
