/*
 * Ogg Theora muxer mapping
 * Copyright (c) 2007 Baptiste Coudurier <baptiste dot coudurier at free dot fr>
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

#include "libavcodec/xiph.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/rational.h"
#include "libavformat/internal.h"
#include "libavformat/ogg/muxer.h"

typedef struct TheoraMuxerContext {
    int vrev;     ///< bitstream revision
    int64_t last_kf_pts;
} TheoraMuxerContext;

static int theora_init(AVFormatContext *s, AVStream *st)
{
    OGGStreamContext *oggstream = st->priv_data;
    TheoraMuxerContext *theora = oggstream->priv_data;
    int den, num;
    int err = ff_ogg_build_xiph_headers(s, st, 42, 0x81, "theora", 0);

    if (err < 0)
        return err;

    den = AV_RB32(oggstream->header[0] + 22);
    num = AV_RB32(oggstream->header[0] + 26);
    /* Make sure to use time base stored in the Theora stream header to write
       correct timestamps */
    if (st->time_base.num != num || st->time_base.den != den) {
        av_log(s, AV_LOG_DEBUG, "Changing time base from %d/%d to %d/%d\n",
               st->time_base.num, st->time_base.den, num, den);
        avpriv_set_pts_info(st, 64, num, den);
    }
    /** KFGSHIFT is the width of the less significant section of the granule position
        The less significant section is the frame count since the last keyframe */
    oggstream->granule_shift = ((oggstream->header[0][40]&3)<<3)|(oggstream->header[0][41]>>5);
    oggstream->granule_delta_mask = (INT64_C(1) << oggstream->granule_shift) - 1;
    oggstream->granule_key_mask = oggstream->granule_delta_mask;
    theora->vrev = oggstream->header[0][9];
    av_log(s, AV_LOG_DEBUG, "theora kfgshift %u, vrev %d\n",
           oggstream->granule_shift, theora->vrev);

    return 0;
}

/**
 * Whether Theora headers set a frame rate other than the stream time base,
 * which packets keep across links.
 */
static int theora_rate_changed(const AVStream *st, const uint8_t *extradata, size_t size)
{
    const uint8_t *header[3];
    int header_len[3];

    if (size > INT_MAX ||
        avpriv_split_xiph_headers(extradata, size, 42, header, header_len) < 0 ||
        header_len[0] < 42)
        return 0;
    return av_cmp_q(st->time_base,
                    (AVRational){ AV_RB32(header[0] + 26), AV_RB32(header[0] + 22) }) != 0;
}

static const char *theora_new_link_refused(const AVStream *st, const AVPacket *pkt,
                                           const uint8_t *new_extradata, size_t size)
{
    if (!new_extradata && !(pkt->flags & AV_PKT_FLAG_KEY))
        return "New Theora links must start on a keyframe";
    if (new_extradata && theora_rate_changed(st, new_extradata, size))
        return "New Theora links cannot change the frame rate";
    return NULL;
}

static int64_t theora_packet_granule(AVStream *st, const AVPacket *pkt)
{
    OGGStreamContext *oggstream = st->priv_data;
    TheoraMuxerContext *theora = oggstream->priv_data;
    int64_t pts = theora->vrev < 1 ? pkt->pts : pkt->pts + pkt->duration;
    int pframe_count;

    if (pkt->flags & AV_PKT_FLAG_KEY)
        theora->last_kf_pts = pts;
    pframe_count = pts - theora->last_kf_pts;
    // prevent frame count from overflow if key frame flag is not set
    if (pframe_count >= (INT64_C(1)<<oggstream->granule_shift)) {
        theora->last_kf_pts += pframe_count;
        pframe_count = 0;
    }
    return (theora->last_kf_pts<<oggstream->granule_shift) | pframe_count;
}

const OGGMuxerMapping ff_ogg_theora_muxer_mapping = {
    .codec_id         = AV_CODEC_ID_THEORA,
    .priv_data_size   = sizeof(TheoraMuxerContext),
    .new_links        = 1,
    .init             = theora_init,
    .new_link_refused = theora_new_link_refused,
    .packet_granule   = theora_packet_granule,
};
