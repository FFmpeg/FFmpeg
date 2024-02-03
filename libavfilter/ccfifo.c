/*
 * CEA-708 Closed Captioning FIFO
 * Copyright (c) 2023 LTN Global Communications
 *
 * Author: Devin Heitmueller <dheitmueller@ltnglobal.com>
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

#include "ccfifo.h"
#include "libavutil/fifo.h"

#define MAX_CC_ELEMENTS 128

struct cc_lookup {
    int num;
    int den;
    int cc_count;
    int num_608;
};

const static struct cc_lookup cc_lookup_vals[] = {
    { 15, 1, 40, 4 },
    { 24, 1, 25, 3 },
    { 24000, 1001, 25, 3 },
    { 30, 1, 20, 2 },
    { 30000, 1001, 20, 2},
    { 60, 1, 10, 1 },
    { 60000, 1001, 10, 1},
};

void ff_ccfifo_uninit(CCFifo *ccf)
{
    av_fifo_freep2(&ccf->cc_608_fifo);
    av_fifo_freep2(&ccf->cc_708_fifo);
    memset(ccf, 0, sizeof(*ccf));
}

int ff_ccfifo_init(CCFifo *ccf, AVRational framerate, void *log_ctx)
{
    int i;

    memset(ccf, 0, sizeof(*ccf));
    ccf->log_ctx = log_ctx;
    ccf->framerate = framerate;

    if (!(ccf->cc_708_fifo = av_fifo_alloc2(MAX_CC_ELEMENTS, CC_BYTES_PER_ENTRY, 0)))
        goto error;

    if (!(ccf->cc_608_fifo = av_fifo_alloc2(MAX_CC_ELEMENTS, CC_BYTES_PER_ENTRY, 0)))
        goto error;

    /* Based on the target FPS, figure out the expected cc_count and number of
       608 tuples per packet.  See ANSI/CTA-708-E Sec 4.3.6.1. */
    for (i = 0; i < FF_ARRAY_ELEMS(cc_lookup_vals); i++) {
        if (framerate.num == cc_lookup_vals[i].num &&
            framerate.den == cc_lookup_vals[i].den) {
            ccf->expected_cc_count = cc_lookup_vals[i].cc_count;
            ccf->expected_608 = cc_lookup_vals[i].num_608;
            break;
        }
    }

    if (ccf->expected_608 == 0) {
        /* We didn't find an output frame we support.  We'll let the call succeed
           and the FIFO to be allocated, but the extract/inject functions will simply
           leave everything the way it is */
        ccf->passthrough = 1;
    }

    return 0;

error:
    ff_ccfifo_uninit(ccf);
    return AVERROR(ENOMEM);
}

int ff_ccfifo_injectbytes(CCFifo *ccf, uint8_t *cc_data, size_t len)
{
    int cc_608_tuples = 0;
    int cc_708_tuples = 0;
    int cc_filled = 0;

    if (ccf->passthrough) {
        return 0;
    }

    if (len < ff_ccfifo_getoutputsize(ccf)) {
        return AVERROR(EINVAL);
    }

    /* Insert any available data from the 608 FIFO */
    if (ccf->expected_608 <= av_fifo_can_read(ccf->cc_608_fifo))
        cc_608_tuples = ccf->expected_608;
    else
        cc_608_tuples = av_fifo_can_read(ccf->cc_608_fifo);
    av_fifo_read(ccf->cc_608_fifo, cc_data, cc_608_tuples);
    cc_filled += cc_608_tuples;

    /* Insert any available data from the 708 FIFO */
    if ((ccf->expected_cc_count - cc_filled) <= av_fifo_can_read(ccf->cc_708_fifo))
        cc_708_tuples = ccf->expected_cc_count - cc_filled;
    else
        cc_708_tuples = av_fifo_can_read(ccf->cc_708_fifo);
    av_fifo_read(ccf->cc_708_fifo, &cc_data[cc_filled * CC_BYTES_PER_ENTRY], cc_708_tuples);
    cc_filled += cc_708_tuples;

    /* Insert 708 padding into any remaining fields */
    while (cc_filled < ccf->expected_cc_count) {
        cc_data[cc_filled * CC_BYTES_PER_ENTRY]     = 0xfa;
        cc_data[cc_filled * CC_BYTES_PER_ENTRY + 1] = 0x00;
        cc_data[cc_filled * CC_BYTES_PER_ENTRY + 2] = 0x00;
        cc_filled++;
    }

    return 0;
}

int ff_ccfifo_inject(CCFifo *ccf, AVFrame *frame)
{
    AVFrameSideData *sd;
    int ret;

    if (ccf->passthrough == 1 || ccf->cc_detected == 0)
        return 0;

    sd = av_frame_new_side_data(frame, AV_FRAME_DATA_A53_CC,
                                ff_ccfifo_getoutputsize(ccf));
    if (sd) {
        ret = ff_ccfifo_injectbytes(ccf, sd->data, sd->size);
        if (ret < 0) {
            av_frame_remove_side_data(frame, AV_FRAME_DATA_A53_CC);
            return ret;
        }
    }

    return 0;
}

int ff_ccfifo_extractbytes(CCFifo *ccf, uint8_t *cc_bytes, size_t len)
{
    int cc_count = len / CC_BYTES_PER_ENTRY;

    if (ccf->passthrough == 1) {
        av_log_once(ccf->log_ctx, AV_LOG_WARNING, AV_LOG_DEBUG, &ccf->passthrough_warning,
                    "cc_fifo cannot transcode captions fps=%d/%d\n",
                    ccf->framerate.num, ccf->framerate.den);
        return 0;
    }

    ccf->cc_detected = 1;

    for (int i = 0; i < cc_count; i++) {
        /* See ANSI/CTA-708-E Sec 4.3, Table 3 */
        uint8_t cc_valid = (cc_bytes[CC_BYTES_PER_ENTRY*i] & 0x04) >> 2;
        uint8_t cc_type = cc_bytes[CC_BYTES_PER_ENTRY*i] & 0x03;
        if (cc_type == 0x00 || cc_type == 0x01) {
            av_fifo_write(ccf->cc_608_fifo, &cc_bytes[CC_BYTES_PER_ENTRY*i], 1);
        } else if (cc_valid && (cc_type == 0x02 || cc_type == 0x03)) {
            av_fifo_write(ccf->cc_708_fifo, &cc_bytes[CC_BYTES_PER_ENTRY*i], 1);
        }
    }
    return 0;
}

/* Read the A53 side data, discard padding, and put 608/708 into
   queues so we can ensure they get into the output frames at
   the correct rate... */
int ff_ccfifo_extract(CCFifo *ccf, AVFrame *frame)
{
    AVFrameSideData *side_data = av_frame_get_side_data(frame, AV_FRAME_DATA_A53_CC);
    if (side_data) {
        ff_ccfifo_extractbytes(ccf, side_data->data, side_data->size);

        /* Remove the side data, as we will re-create it on the
           output as needed */
        if (!ccf->passthrough)
            av_frame_remove_side_data(frame, AV_FRAME_DATA_A53_CC);
    }
    return 0;
}
