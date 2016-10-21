/*
 * SCTE 35 decoder
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
/*
 * Reference Material Used
 *
 * ANSI/SCTE 35 2013 (Digital Program Insertion Cueing Message for Cable)
 *
 * SCTE 67 2014 (Recommended Practice for SCTE 35
 *          Digital Program Insertion Cueing Message for Cable)
 */



#include "libavcodec/bytestream.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/get_bits.h"
#include "libavutil/buffer_internal.h"
#include "libavutil/base64.h"
#include "scte_35.h"

#define SCTE_CMD_NULL                  0x00
#define SCTE_CMD_SCHEDULE              0x04
#define SCTE_CMD_INSERT                0x05
#define SCTE_CMD_SIGNAL                0x06
#define SCTE_CMD_BANDWIDTH_RESERVATION 0x07


static char* get_hls_string(struct scte35_interface *iface, struct scte35_event *event,
                 const char *filename, int out_state, int seg_count, int64_t pos)
{
    int ret;
    av_bprint_clear(&iface->avbstr);
    if (out_state == EVENT_IN) {
        av_bprintf(&iface->avbstr, "#EXT-OATCLS-SCTE35:%s\n", iface->pkt_base64);
        av_bprintf(&iface->avbstr, "#EXT-X-CUE-IN\n");
        av_bprintf(&iface->avbstr, "#EXT-X-DISCONTINUITY\n");
    } else if (out_state == EVENT_OUT) {
        if (event) {
            av_bprintf(&iface->avbstr, "#EXT-OATCLS-SCTE35:%s\n", iface->pkt_base64);
            if (event->duration != AV_NOPTS_VALUE) {
                int64_t dur = ceil(((double)event->duration * iface->timebase.num) /iface->timebase.den);
                av_bprintf(&iface->avbstr, "#EXT-X-CUE-OUT:%"PRIu64"\n", dur);
            } else {
                av_bprintf(&iface->avbstr, "#EXT-X-CUE-OUT\n");
            }
            av_bprintf(&iface->avbstr, "#EXT-X-DISCONTINUITY\n");
        }
    } else if (out_state == EVENT_OUT_CONT) {
        if (event && event->duration != AV_NOPTS_VALUE) {
            int64_t dur = ceil(((double)event->duration * iface->timebase.num) /iface->timebase.den);
            int64_t elapsed_time = ceil(((double)pos * iface->timebase.num) /iface->timebase.den) - event->out_pts;
            av_bprintf(&iface->avbstr, "#EXT-X-CUE-OUT-CONT:ElapsedTime=%"PRIu64",Duration=%"PRIu64",SCTE35=%s\n",
                elapsed_time,  dur, iface->pkt_base64);
        } else {
            av_bprintf(&iface->avbstr, "#EXT-X-CUE-OUT-CONT:SCTE35=%s\n", iface->pkt_base64);
        }
    }
    if (seg_count >= 0)
        av_bprintf(&iface->avbstr, filename, seg_count);
    else
        av_bprintf(&iface->avbstr, "%s", filename);
    av_bprintf(&iface->avbstr, "\n");

    ret = av_bprint_is_complete(&iface->avbstr);
    if (ret == 0) {
        av_log(iface->parent, AV_LOG_DEBUG, "Out of Memory");
        return NULL;
    }

    av_log(iface->parent, AV_LOG_DEBUG, "%s", iface->avbstr.str);
    return iface->avbstr.str;
}

static struct scte35_event* alloc_scte35_event(int id)
{
    struct scte35_event* event = av_malloc(sizeof(struct scte35_event));
    if (!event)
        return NULL;

    event->id = id;
    event->in_pts = AV_NOPTS_VALUE;
    event->nearest_in_pts = AV_NOPTS_VALUE;
    event->out_pts = AV_NOPTS_VALUE;
    event->running = 0;
    event->next = NULL;
    event->prev = NULL;
    return event;
}

static void ref_scte35_event(struct scte35_event *event)
{
    event->ref_count++;
}

static void unref_scte35_event(struct scte35_event **event)
{
    if (!(*event))
        return;
    if (!(*event)->ref_count) {
        av_freep(event);
    } else {
        (*event)->ref_count--;
    }
}

static void unlink_scte35_event(struct scte35_interface *iface, struct scte35_event *event)
{
    if (!event)
        return;
    if (!event->prev)
        iface->event_list = event->next;
    else
        event->prev->next = event->next;
    if (event->next)
        event->next->prev = event->prev;
    unref_scte35_event(&event);
}

static struct scte35_event* get_event_id(struct scte35_interface *iface, int id)
{
    struct scte35_event *event = iface->event_list;
    struct scte35_event *pevent = NULL;

    while(event) {

        if (event->id == id)
            break;
        pevent = event;
        event = event->next;
    }
    if (!event) {
        event = alloc_scte35_event(id);
        if (pevent)
            pevent->next = event;
        else
            iface->event_list = event;
    }

    return event;
}

/**
 * save the parsed time in ctx pts_time
   @return length of buffer consumed
*/
static int parse_splice_time(struct scte35_interface *iface, const uint8_t *buf, uint64_t *pts, int64_t pts_adjust)
{
    GetBitContext gb;
    int ret;
    init_get_bits(&gb, buf, 40);
    /* is time specified */
    ret =  get_bits(&gb, 1);
    if (ret) {
        skip_bits(&gb, 6);
        *pts = get_bits64(&gb,33) + pts_adjust;
        return 5;
    } else {
        skip_bits(&gb, 7);
        return 1;
    }
}

static int parse_schedule_cmd(struct scte35_interface *iface, const uint8_t *buf)
{
    const uint8_t *sbuf = buf;
    av_log(iface->parent, AV_LOG_DEBUG, "Schedule cmd\n");
    return buf - sbuf;
}
/**
     @return length of buffer used
 */
static int parse_insert_cmd(struct scte35_interface *iface,
    const uint8_t *buf,const int len, int64_t pts_adjust, int64_t current_pts)
{
    GetBitContext gb;
    int ret;
    const uint8_t *sbuf = buf;
    int program_splice_flag;
    int duration_flag;
    int splice_immediate_flag;
    int component_tag;
    int auto_return;
    uint16_t u_program_id;
    uint8_t avail_num;
    uint8_t avail_expect;
    int inout;
    int event_id;
    struct scte35_event *event;
    char buffer[128];
    int cancel;


    av_log(iface->parent, AV_LOG_DEBUG, "%s Insert cmd\n", buffer);
    event_id  = AV_RB32(buf);
    av_log(iface->parent, AV_LOG_DEBUG, "event_id  = %x\n", event_id);
    event = get_event_id(iface, event_id);
    buf +=4;
    cancel = *buf & 0x80;
    av_log(iface->parent, AV_LOG_DEBUG, "splice_event_cancel_indicator  = %d\n", cancel);
    buf++;

    if (!cancel) {
        init_get_bits(&gb, buf, 8);
        inout =  get_bits(&gb, 1);
        program_splice_flag =  get_bits(&gb, 1);
        duration_flag =  get_bits(&gb, 1);
        splice_immediate_flag =  get_bits(&gb, 1);
        skip_bits(&gb, 4);

    } else {
        /*   Delete event only if its not already started */
        if (!event->running) {
            unlink_scte35_event(iface, event);
        }
    }
    buf++;


    av_log(iface->parent, AV_LOG_DEBUG, "out_of_network_indicator  = %d\n", inout);
    av_log(iface->parent, AV_LOG_DEBUG, "program_splice_flag  = %d\n", program_splice_flag);
    av_log(iface->parent, AV_LOG_DEBUG, "duration_flag  = %d\n", duration_flag);
    av_log(iface->parent, AV_LOG_DEBUG, "splice_immediate_flag  = %d\n", splice_immediate_flag);

    if (program_splice_flag &&  !splice_immediate_flag) {
        if (inout) {
            ret = parse_splice_time(iface, buf, &event->out_pts, pts_adjust);
            event->out_pts = event->out_pts * iface->timebase.num / iface->timebase.den;
        } else {
            ret = parse_splice_time(iface, buf, &event->in_pts, pts_adjust);
            event->in_pts = event->in_pts * iface->timebase.num / iface->timebase.den;
        }

        buf += ret;
    } else if (program_splice_flag && splice_immediate_flag) {
        if (inout)
            event->out_pts = current_pts * iface->timebase.num / iface->timebase.den;
        else
            event->in_pts = current_pts * iface->timebase.num / iface->timebase.den;
    }
    if (program_splice_flag == 0) {
        int comp_cnt = *buf++;
        int  i;
        av_log(iface->parent, AV_LOG_DEBUG, "component_count  = %d\n", comp_cnt);
        for (i = 0; i < comp_cnt; i++) {
            component_tag = *buf++;
            av_log(iface->parent, AV_LOG_DEBUG, "component_tag  = %d\n", component_tag);
            if (splice_immediate_flag) {
                if (inout)
                    ret = parse_splice_time(iface, buf, &event->in_pts, pts_adjust);
                else
                    ret = parse_splice_time(iface, buf, &event->out_pts, pts_adjust);
                buf += ret;
            }
        }
    }
    if (duration_flag) {
        init_get_bits(&gb, buf, 40);
        auto_return =  get_bits(&gb, 1);
        av_log(iface->parent, AV_LOG_DEBUG, "autoreturn  = %d\n", auto_return);
        skip_bits(&gb, 6);
        event->duration = get_bits64(&gb,33) + pts_adjust;
        buf += 5;
    }
    u_program_id = AV_RB16(buf);
    av_log(iface->parent, AV_LOG_DEBUG, "u_program_id  = %hd\n", u_program_id);
    buf += 2;
    avail_num = *buf++;
    av_log(iface->parent, AV_LOG_DEBUG, "avail_num  = %hhd\n", avail_num);
    avail_expect = *buf++;
    av_log(iface->parent, AV_LOG_DEBUG, "avail_expect  = %hhd\n", avail_expect);

    return buf - sbuf;
}
static int parse_time_signal_cmd(struct scte35_interface *iface, const uint8_t *buf)
{
    const uint8_t *sbuf = buf;
    av_log(iface->parent, AV_LOG_DEBUG, "Time Signal cmd\n");
    return buf - sbuf;
}
static int parse_bandwidth_reservation_cmd(struct scte35_interface *iface, const uint8_t *buf)
{
    const uint8_t *sbuf = buf;
    av_log(iface->parent, AV_LOG_DEBUG, "Band Width reservation cmd\n");
    return buf - sbuf;
}

int ff_parse_scte35_pkt(struct scte35_interface *iface, const AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int section_length;
    int cmd_length;
    uint8_t cmd_type;
    int16_t tier;
    GetBitContext gb;
    int ret;
    int64_t pts_adjust;

    if (!buf)
        return AVERROR_EOF;


    /* check table id */
    if (*buf != 0xfc)
        av_log(iface->parent, AV_LOG_ERROR, "Invalid SCTE packet\n");


    init_get_bits(&gb, buf + 1, 104);

    /* section_syntax_indicator should be 0 */
    ret = get_bits(&gb,1);
    if (ret)
        av_log(iface->parent, AV_LOG_DEBUG, "Section indicator should be 0, since MPEG short sections are to be used.\n");

    /* private indicator */
    ret = get_bits(&gb,1);
    if (ret)
        av_log(iface->parent, AV_LOG_DEBUG, "corrupt packet\n");

    skip_bits(&gb,2);

    /* section length may be there */
    section_length = get_bits(&gb,12);
    if (section_length > 4093)
    if (ret) {
        av_log(iface->parent, AV_LOG_ERROR, "Invalid length of section\n");
        return AVERROR_INVALIDDATA;
    }

    av_base64_encode(iface->pkt_base64, AV_BASE64_SIZE(section_length + 3), buf, section_length + 3);

    /* protocol version */
    skip_bits(&gb,8);

    ret = get_bits(&gb,1);
    if (ret) {
        av_log(iface->parent, AV_LOG_ERROR, "Encrytion not yet supported\n");
        return AVERROR_PATCHWELCOME;
    }
    /* encryption algo */
    skip_bits(&gb,6);

    pts_adjust =  get_bits64(&gb, 33);

    /* cw_index: used in encryption */
    skip_bits(&gb,8);


    /* tier */
    tier = get_bits(&gb,12);
    if ((tier & 0xfff) == 0xfff)
        tier = -1;

    cmd_length = get_bits(&gb,12);
    if (cmd_length == 0xfff) {
        /* Setting max limit to  cmd_len so it does not cross memory barrier */
        cmd_length = section_length - 17;
    } else if (cmd_length != 0xfff && (cmd_length > (section_length - 17) ) ) {
        av_log(iface->parent, AV_LOG_ERROR, "Command length %d invalid\n", cmd_length);
        return AVERROR_INVALIDDATA;
    }

    cmd_type = get_bits(&gb,8);
    switch(cmd_type) {
    case SCTE_CMD_NULL:
        av_log(iface->parent, AV_LOG_DEBUG, "SCTE-35 Ping Received");
        break;
    case SCTE_CMD_SCHEDULE:
        ret = parse_schedule_cmd(iface, buf + 14);
        break;
    case SCTE_CMD_INSERT:
        ret = parse_insert_cmd(iface, buf + 14, cmd_length, pts_adjust, avpkt->pts);
        break;
    case SCTE_CMD_SIGNAL:
        ret = parse_time_signal_cmd(iface, buf + 14);
        break;
    case SCTE_CMD_BANDWIDTH_RESERVATION:
        ret = parse_bandwidth_reservation_cmd(iface, buf + 14);
        break;
    default:
        break;
    /* reserved yet */
    }
    if (ret < 0)
        goto fail;
    buf += ret;

fail:
    return ret;
}

/*
 * return event, if there is any event whose starting time aka out_pts is less then
 * current pts. This condition also means that event starting time has already been passed.
 * This function will look for event in events list which resides inside iface.
 */
static struct scte35_event* get_event_ciel_out(struct scte35_interface *iface, uint64_t pts)
{
    struct scte35_event *event = iface->event_list;
    while(event) {
        if (!event->running && event->out_pts < pts) {
            iface->event_state = EVENT_OUT;
            break;
        }
        event = event->next;
    }
    return event;
}

/*
 * return event, if current event is in running state
 * and check that in_pts is less then current pts.
 * Event from this function specify commercial ends and
 * mainstream should be coupled in.
 * This event is generally last event to be consumed.
 */
static struct scte35_event* get_event_floor_in(struct scte35_interface *iface, uint64_t pts)
{
    struct scte35_event *event = iface->event_list;
    struct scte35_event *sevent = NULL;
    while(event) {
        if (event->in_pts != AV_NOPTS_VALUE && event->in_pts < pts &&
          (event->nearest_in_pts == AV_NOPTS_VALUE || pts <= event->nearest_in_pts) ) {
            event->nearest_in_pts = pts;
            unlink_scte35_event(iface, event);
            /* send in_event only when that event was in running state */
            if (event->running) {
                iface->event_state = EVENT_IN;
                sevent = event;
            }
        }
        event = event->next;
    }
    return sevent;
}


/*
 *  If there is no running event, then search for an event which have
 *  the pts matching to current pts. Otherwise only give event when
 *  its time to end the commercial.
 *  if we have some event to be presented at this video then cache it
 *  for later use.
 */
static void update_video_pts(struct scte35_interface *iface, uint64_t pts)
{
    struct scte35_event *event = NULL;
    if (iface->event_state == EVENT_NONE) {
        event = get_event_ciel_out(iface, pts);
        if (event)
            event->running = 1;
    } else {
        event = get_event_floor_in(iface, pts);
    }
    if (event)
        iface->current_event = event;
}

/*
 * update the state of scte-35 parser
 * return current event
 */
static struct scte35_event* update_event_state(struct scte35_interface *iface)
{

    struct scte35_event* event = iface->current_event;
    if (iface->prev_event_state == EVENT_IN)
        iface->event_state = EVENT_NONE;
    else if (iface->prev_event_state == EVENT_OUT)
        iface->event_state = EVENT_OUT_CONT;

    if (iface->event_state == EVENT_NONE)
        iface->current_event = NULL;

    iface->prev_event_state = iface->event_state;
    return event;
}


/*
 * Allocate scte35 parser
 * using function pointer so that this module reveals least interface
 * to API uses
 */
struct scte35_interface* ff_alloc_scte35_parser(void *parent, AVRational timebase)
{
    struct scte35_interface* iface = av_mallocz(sizeof(struct scte35_interface));
    if (!iface)
       return NULL;

    iface->parent = parent;
    iface->timebase = timebase;
    iface->update_video_pts = update_video_pts;
    iface->update_event_state = update_event_state;
    av_bprint_init(&iface->avbstr, 0, AV_BPRINT_SIZE_UNLIMITED);
    iface->get_hls_string = get_hls_string;
    iface->unref_scte35_event = unref_scte35_event;
    iface->ref_scte35_event = ref_scte35_event;
    iface->event_state = EVENT_NONE;
    iface->prev_event_state = EVENT_NONE;
    return iface;
}

void ff_delete_scte35_parser(struct scte35_interface* iface)
{
    if (!iface)
        return;
    av_bprint_finalize(&iface->avbstr, NULL);
    av_freep(&iface);
}
