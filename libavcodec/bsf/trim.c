/*
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

#include "libavutil/fifo.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/timestamp.h"

#include "libavcodec/bsf.h"
#include "libavcodec/bsf_internal.h"

enum TrimType {
    TRIM_PTS,
    TRIM_DTS,
    TRIM_PKT_INDEX,
    TRIM_MSEC_PT,
    TRIM_MSEC_DT,
    TRIM_DUR_TS,
    TRIM_DUR_T_MSEC,
};

/* The packet discriminant a bound is compared against; the indexes count consumed
 * and exported packets. */
enum TrimAxis {
    AXIS_PTS,
    AXIS_DTS,
    AXIS_INDEX,
    AXIS_OUT_INDEX,
};

/* An axis, a unit, and whether the value is relative; the duration types are
 * the relative forms of pts and msec_pt. */
static const struct {
    enum TrimAxis axis;
    int msec;
    int relative;
} trim_types[] = {
    [TRIM_PTS]        = { AXIS_PTS,   0, 0 },
    [TRIM_DTS]        = { AXIS_DTS,   0, 0 },
    [TRIM_PKT_INDEX]  = { AXIS_INDEX, 0, 0 },
    [TRIM_MSEC_PT]    = { AXIS_PTS,   1, 0 },
    [TRIM_MSEC_DT]    = { AXIS_DTS,   1, 0 },
    [TRIM_DUR_TS]     = { AXIS_PTS,   0, 1 },
    [TRIM_DUR_T_MSEC] = { AXIS_PTS,   1, 1 },
};

typedef struct TrimBound {
    /* Resolved from the options, immutable once init returns. */
    int active;         ///< the option was set at all
    enum TrimAxis axis;
    int relative;       ///< offset is measured from the reference packet
    int64_t offset;     ///< option value converted to axis units

    /* Rebuilt from the fields above by every reset. */
    int64_t value;      ///< resolved bound, meaningful once pending is clear
    int pending;        ///< the reference packet has not been seen yet
} TrimBound;

typedef struct TrimContext {
    const AVClass *class;

    int64_t start;
    int64_t end;
    int start_type;
    int end_type;
    int start_rel;
    int end_rel;
    int trim_packets;

    TrimBound start_bound;
    TrimBound end_bound;

    int audio;          ///< trim through skip samples side data
    int64_t pkt_idx;    ///< packets consumed so far
    int64_t out_idx;    ///< packets exported so far
    int64_t skip_carry;  ///< samples of head skip left over by dropped packets
    uint8_t skip_reason; ///< reason that head skip came with

    int preroll;
    int preroll_size;

    AVFifo *preroll_fifo;  ///< packets held since the last keyframe, NULL when off
    AVPacket *pending;     ///< in-range packet waiting for the preroll to drain
    int preroll_full;      ///< the group overflowed, so hold nothing until the next one

    int64_t nb_exported;   ///< packets that left the filter
    int64_t last_pts;      ///< the last of them, kept for the closing summary
    int64_t last_dts;
    int last_key;
} TrimContext;

/* Bounds mix option values with stream values; either can overflow. */
static int add_checked(AVBSFContext *ctx, int64_t *acc, int64_t delta,
                       const char *what)
{
    if ((delta > 0 && *acc > INT64_MAX - delta) ||
        (delta < 0 && *acc < INT64_MIN - delta)) {
        av_log(ctx, AV_LOG_ERROR, "%s is not representable\n", what);
        return AVERROR(ERANGE);
    }

    *acc += delta;

    return 0;
}

static int64_t packet_axis(const TrimContext *s, const AVPacket *pkt,
                           enum TrimAxis axis)
{
    switch (axis) {
    case AXIS_PTS:       return pkt->pts;
    case AXIS_DTS:       return pkt->dts;
    case AXIS_OUT_INDEX: return s->out_idx;
    default:             return s->pkt_idx;
    }
}

/* The dts stands in for the pts, being monotonic and never above it. */
static enum TrimAxis monotonic_axis(enum TrimAxis axis)
{
    return axis == AXIS_PTS ? AXIS_DTS : axis;
}

/* An index counts whole packets, so only time places a bound inside one. */
static int axis_is_time(enum TrimAxis axis)
{
    return axis == AXIS_PTS || axis == AXIS_DTS;
}

/* Nothing converted here is ever negative, so a negative result is the
 * INT64_MIN av_rescale_q reports for one it cannot represent. */
static int convert_checked(AVBSFContext *ctx, int64_t *value, AVRational from,
                           AVRational to, const char *what)
{
    int64_t converted = av_rescale_q(*value, from, to);

    if (converted < 0) {
        av_log(ctx, AV_LOG_ERROR, "%s is not representable\n", what);
        return AVERROR(ERANGE);
    }

    *value = converted;

    return 0;
}

static int time_base_to_samples(AVBSFContext *ctx, int64_t *value,
                                const char *what)
{
    return convert_checked(ctx, value, ctx->time_base_in,
                           (AVRational){ 1, ctx->par_in->sample_rate }, what);
}

static int samples_to_time_base(AVBSFContext *ctx, int64_t *value,
                                const char *what)
{
    return convert_checked(ctx, value, (AVRational){ 1, ctx->par_in->sample_rate },
                           ctx->time_base_in, what);
}

/* The samples a packet decodes to, zero where the codec does not give them. */
static int64_t packet_samples(AVBSFContext *ctx, const AVPacket *pkt)
{
    return FFMAX(av_get_audio_frame_duration2(ctx->par_in, pkt->size), 0);
}

/* A skip reaching past a dropped packet only needs how much of itself that
 * packet used up, which a duration still estimates closely enough. */
static int64_t packet_skip_consumed(AVBSFContext *ctx, const AVPacket *pkt)
{
    int64_t nb_samples = packet_samples(ctx, pkt);

    if (nb_samples > 0)
        return nb_samples;

    nb_samples = av_rescale_q(pkt->duration, ctx->time_base_in,
                              (AVRational){ 1, ctx->par_in->sample_rate });

    return FFMAX(nb_samples, 0);
}

/* How far a packet reaches past its axis value. Audio reaches as far as the
 * samples it decodes to, a duration also covering any gap after them; without
 * them it reaches nowhere, so no bound can fall inside it. */
static int packet_span(AVBSFContext *ctx, const AVPacket *pkt,
                       enum TrimAxis axis, int64_t *span)
{
    TrimContext *s = ctx->priv_data;

    *span = 0;

    if (!axis_is_time(axis))
        return 0;

    if (s->audio) {
        *span = packet_samples(ctx, pkt);
        return samples_to_time_base(ctx, span, "packet span");
    }

    *span = FFMAX(pkt->duration, 0);

    return 0;
}

static int bound_init(AVBSFContext *ctx, TrimBound *b, int active,
                      int64_t value, int type, int rel, const char *name)
{
    b->active = active;
    if (!active)
        return 0;

    b->axis     = trim_types[type].axis;
    b->relative = rel || trim_types[type].relative;

    if (trim_types[type].relative && value < 0) {
        av_log(ctx, AV_LOG_ERROR, "%s duration must not be negative\n", name);
        return AVERROR(EINVAL);
    }

    if (trim_types[type].msec) {
        int64_t ts = av_rescale_q(value, (AVRational){ 1, 1000 },
                                  ctx->time_base_in);
        if (ts == INT64_MIN) {
            av_log(ctx, AV_LOG_ERROR, "%s of %"PRId64" ms is not representable "
                   "in the stream time base\n", name, value);
            return AVERROR(ERANGE);
        }
        value = ts;
    }

    b->offset = value;

    return 0;
}

static void bound_reset(TrimBound *b)
{
    b->value   = b->offset;
    b->pending = b->relative;
}

static int bound_resolve(AVBSFContext *ctx, TrimBound *b, int64_t ref,
                         const char *name)
{
    int ret = add_checked(ctx, &b->value, ref, name);

    if (ret < 0)
        return ret;

    b->pending = 0;

    return 0;
}

/* How much of the packet lies before the start bound, EAGAIN when all of it
 * does. */
static int trim_head(AVBSFContext *ctx, const AVPacket *pkt, int64_t *head)
{
    TrimContext *s = ctx->priv_data;
    int64_t v, span;
    int ret;

    if (!s->start_bound.active)
        return 0;

    /* A packet without the discriminant a bound uses cannot be placed. */
    v = packet_axis(s, pkt, s->start_bound.axis);
    if (v == AV_NOPTS_VALUE)
        return AVERROR(EAGAIN);

    /* The start is measured from the first packet of the stream. */
    if (s->start_bound.pending) {
        ret = bound_resolve(ctx, &s->start_bound, v, "start bound");
        if (ret < 0)
            return ret;
    }

    if (v >= s->start_bound.value)
        return 0;

    ret = packet_span(ctx, pkt, s->start_bound.axis, &span);
    if (ret < 0)
        return ret;

    if (!s->trim_packets || av_sat_add64(v, span) <= s->start_bound.value)
        return AVERROR(EAGAIN);
    *head = s->start_bound.value - v;

    return 0;
}

/* How much of the packet lies past the end bound, measured from head, where it
 * starts contributing. EAGAIN when none of it is inside, EOF once nothing later
 * can be. */
static int trim_tail(AVBSFContext *ctx, const AVPacket *pkt, int64_t head,
                     int64_t *tail)
{
    TrimContext *s = ctx->priv_data;
    int64_t v;
    int ret;

    if (!s->end_bound.active)
        return 0;

    /* The end is measured from the first exported packet, at the point it is
     * trimmed to. */
    if (s->end_bound.pending) {
        int64_t ref = packet_axis(s, pkt, s->end_bound.axis);

        if (ref == AV_NOPTS_VALUE)
            return AVERROR(EAGAIN);
        if (axis_is_time(s->end_bound.axis)) {
            ret = add_checked(ctx, &ref, head, "end anchor");
            if (ret < 0)
                return ret;
        }
        ret = bound_resolve(ctx, &s->end_bound, ref, "end bound");
        if (ret < 0)
            return ret;
    }

    /* Only a discriminant that cannot come back into range may end the stream. */
    v = packet_axis(s, pkt, monotonic_axis(s->end_bound.axis));
    if (v != AV_NOPTS_VALUE && v >= s->end_bound.value)
        return AVERROR_EOF;

    v = packet_axis(s, pkt, s->end_bound.axis);
    if (v == AV_NOPTS_VALUE)
        return AVERROR(EAGAIN);

    /* Reordering can still bring in-range packets after this one. */
    if (v >= s->end_bound.value)
        return AVERROR(EAGAIN);

    if (s->trim_packets) {
        int64_t span, past;

        ret = packet_span(ctx, pkt, s->end_bound.axis, &span);
        if (ret < 0)
            return ret;

        past = av_sat_add64(v, span);
        if (past > s->end_bound.value)
            *tail = past - s->end_bound.value;
    }

    return 0;
}

/* Audio is trimmed in sample space through skip samples side data, leaving the
 * timestamps untouched. */
static int trim_audio(AVBSFContext *ctx, AVPacket *pkt, int64_t head, int keep)
{
    TrimContext *s = ctx->priv_data;
    int64_t nb_samples = packet_samples(ctx, pkt);
    int64_t tail = 0, anchor, input_head;
    int trimming = head != 0;
    uint8_t head_reason = 0, tail_reason = 0, input_reason;
    size_t size;
    uint8_t *side = av_packet_get_side_data(pkt, AV_PKT_DATA_SKIP_SAMPLES,
                                            &size);
    int ret;

    ret = time_base_to_samples(ctx, &head, "head trim");
    if (ret < 0)
        return ret;

    /* A decoder replaces the skip it carries with any nonzero one a packet
     * brings, whether larger or smaller, so resolve the incoming skip first. */
    input_head   = s->skip_carry;
    input_reason = s->skip_reason;

    if (side && size >= 8 && AV_RL32(side)) {
        input_head   = AV_RL32(side);
        input_reason = size >= 10 ? AV_RL8(side + 8) : 0;
    }

    /* That skip and this filter's trim are offsets from the same boundary, so
     * the larger wins and brings its own reason; the trim has none. */
    if (input_head >= head) {
        head        = input_head;
        head_reason = input_reason;
    }

    /* Only a trim of this filter collapses a packet; one already covered by its
     * own skip is passed through. */
    if (!keep || (trimming && nb_samples > 0 && head >= nb_samples))
        goto drop;

    /* A bound measured from this packet is anchored past its composed skip. */
    anchor = head;
    ret = samples_to_time_base(ctx, &anchor, "end anchor");
    if (ret < 0)
        return ret;

    ret = trim_tail(ctx, pkt, anchor, &tail);
    if (ret < 0) {
        if (ret != AVERROR(EAGAIN))
            return ret;
        goto drop;
    }

    trimming |= tail != 0;
    ret = time_base_to_samples(ctx, &tail, "tail trim");
    if (ret < 0)
        return ret;
    if (side && size >= 8 && AV_RL32(side + 4) >= tail) {
        tail        = AV_RL32(side + 4);
        tail_reason = size >= 10 ? AV_RL8(side + 9) : 0;
    }

    if (trimming && nb_samples > 0 && head >= nb_samples - tail)
        goto drop;

    /* The side data below carries any excess from here on. */
    s->skip_carry  = 0;
    s->skip_reason = 0;

    if (!head && !tail)
        return 0;

    if (head > UINT32_MAX || tail > UINT32_MAX) {
        av_log(ctx, AV_LOG_ERROR, "skip samples count is not representable\n");
        return AVERROR(ERANGE);
    }

    if (!side || size < 10) {
        side = av_packet_new_side_data(pkt, AV_PKT_DATA_SKIP_SAMPLES, 10);
        if (!side)
            return AVERROR(ENOMEM);
    }

    AV_WL32(side,     head);
    AV_WL32(side + 4, tail);
    AV_WL8 (side + 8, head_reason);
    AV_WL8 (side + 9, tail_reason);

    return 0;

drop:
    /* A decoder subtracts the samples of every frame it discards and carries
     * the rest, so do the same across dropped packets. */
    if (head > 0) {
        int64_t consumed = packet_skip_consumed(ctx, pkt);

        if (consumed <= 0) {
            av_log(ctx, AV_LOG_ERROR, "cannot carry a %"PRId64" sample skip past "
                   "a dropped packet of unknown sample count\n", head);
            return AVERROR(EINVAL);
        }
        s->skip_carry  = FFMAX(head - consumed, 0);
        s->skip_reason = s->skip_carry ? head_reason : 0;
    }

    return AVERROR(EAGAIN);
}

/* Everything but audio is trimmed on the timeline itself, which no side data
 * can express. */
static int trim_shift(AVBSFContext *ctx, AVPacket *pkt, int64_t head)
{
    int64_t tail = 0;
    int ret;

    ret = trim_tail(ctx, pkt, head, &tail);
    if (ret < 0)
        return ret;

    if (!head && !tail)
        return 0;

    /* Bounds on different axes can select disjoint parts of one packet, and
     * each trim is below the duration, so the subtraction cannot overflow. */
    if (head >= pkt->duration - tail)
        return AVERROR(EAGAIN);

    if (pkt->pts != AV_NOPTS_VALUE) {
        ret = add_checked(ctx, &pkt->pts, head, "packet pts");
        if (ret < 0)
            return ret;
    }
    if (pkt->dts != AV_NOPTS_VALUE) {
        ret = add_checked(ctx, &pkt->dts, head, "packet dts");
        if (ret < 0)
            return ret;
    }
    pkt->duration -= head + tail;

    return 0;
}

static void preroll_clear(AVBSFContext *ctx)
{
    TrimContext *s = ctx->priv_data;
    AVPacket *held;

    while (s->preroll_fifo && av_fifo_read(s->preroll_fifo, &held, 1) >= 0)
        av_packet_free(&held);
}

/* A held packet is decoded for the frames after it to reference and dropped
 * before output, so the exported stream still begins at the bound. */
static void preroll_export(AVBSFContext *ctx, AVPacket *pkt)
{
    TrimContext *s = ctx->priv_data;
    AVPacket *held;

    if (av_fifo_read(s->preroll_fifo, &held, 1) >= 0) {
        av_packet_move_ref(pkt, held);
        av_packet_free(&held);
        pkt->flags |= AV_PKT_FLAG_DISCARD;
        return;
    }

    av_packet_move_ref(pkt, s->pending);
    av_packet_free(&s->pending);
}

static int preroll_hold(AVBSFContext *ctx, AVPacket *pkt)
{
    TrimContext *s = ctx->priv_data;
    AVPacket *held;
    int ret;

    if (s->preroll_full)
        return AVERROR(EAGAIN);

    held = av_packet_alloc();
    if (!held)
        return AVERROR(ENOMEM);

    av_packet_move_ref(held, pkt);
    ret = av_fifo_write(s->preroll_fifo, &held, 1);
    if (ret < 0) {
        av_packet_move_ref(pkt, held);
        av_packet_free(&held);
        if (ret != AVERROR(ENOSPC))
            return ret;

        /* A group too long to hold is dropped whole rather than exported as a
         * fragment no decoder can start from. */
        av_log(ctx, AV_LOG_WARNING, "preroll exceeds %d packets, the first "
               "packets in range will not be decodable\n", s->preroll_size);
        preroll_clear(ctx);
        s->preroll_full = 1;
    }

    return AVERROR(EAGAIN);
}

static void log_exported_packet(AVBSFContext *ctx, const char *which,
                                int64_t idx, int64_t pts, int64_t dts, int key)
{
    av_log(ctx, AV_LOG_VERBOSE, "%s exported packet: output index %"PRId64", "
           "pts %s, dts %s, keyframe %d\n", which, idx, av_ts2str(pts),
           av_ts2str(dts), key);
}

static void packet_exported(AVBSFContext *ctx, const AVPacket *pkt)
{
    TrimContext *s = ctx->priv_data;

    s->last_pts = pkt->pts;
    s->last_dts = pkt->dts;
    s->last_key = !!(pkt->flags & AV_PKT_FLAG_KEY);

    if (!s->nb_exported++)
        log_exported_packet(ctx, "first", 0, s->last_pts, s->last_dts,
                            s->last_key);
}

/* Only the flush or close ending the stream can tell which packet was last. */
static void trim_report(AVBSFContext *ctx)
{
    TrimContext *s = ctx->priv_data;

    if (s->nb_exported)
        log_exported_packet(ctx, "last", s->nb_exported - 1, s->last_pts,
                            s->last_dts, s->last_key);
    else if (s->pkt_idx)
        av_log(ctx, AV_LOG_WARNING, "no packet was in range, the output is "
               "empty\n");
}

static int trim_filter(AVBSFContext *ctx, AVPacket *pkt)
{
    TrimContext *s = ctx->priv_data;
    int64_t head = 0;
    int ret;

    /* The preroll an in-range packet needs is exported ahead of it. */
    if (s->pending) {
        preroll_export(ctx, pkt);
        packet_exported(ctx, pkt);
        return 0;
    }

    ret = ff_bsf_get_packet_ref(ctx, pkt);
    if (ret < 0)
        return ret;

    /* Only the group the start bound falls in is worth keeping, so the window
     * starts over at every keyframe. */
    if (s->preroll_fifo && pkt->flags & AV_PKT_FLAG_KEY) {
        preroll_clear(ctx);
        s->preroll_full = 0;
    }

    ret = trim_head(ctx, pkt, &head);

    /* A packet before the start still composes with the skip it carries, so the
     * audio path runs on it too and is told the verdict instead. */
    if (s->audio && (!ret || ret == AVERROR(EAGAIN)))
        ret = trim_audio(ctx, pkt, head, !ret);
    else if (!ret)
        ret = trim_shift(ctx, pkt, head);
    else if (ret == AVERROR(EAGAIN) && s->preroll_fifo)
        ret = preroll_hold(ctx, pkt);

    s->pkt_idx++;
    if (ret < 0) {
        av_packet_unref(pkt);
        return ret;
    }

    s->out_idx++;
    if (s->preroll_fifo && av_fifo_can_read(s->preroll_fifo)) {
        s->pending = av_packet_alloc();
        if (!s->pending) {
            av_packet_unref(pkt);
            return AVERROR(ENOMEM);
        }
        av_packet_move_ref(s->pending, pkt);
        preroll_export(ctx, pkt);
    }

    packet_exported(ctx, pkt);

    return 0;
}

/* Runtime state is derived from the options, so a flush restores it exactly. */
static void trim_reset(AVBSFContext *ctx)
{
    TrimContext *s = ctx->priv_data;

    bound_reset(&s->start_bound);
    bound_reset(&s->end_bound);

    s->pkt_idx     = 0;
    s->out_idx     = 0;
    s->nb_exported = 0;
    s->skip_carry  = 0;
    s->skip_reason = 0;

    preroll_clear(ctx);
    av_packet_free(&s->pending);
    s->preroll_full = 0;
}

static void trim_flush(AVBSFContext *ctx)
{
    trim_report(ctx);
    trim_reset(ctx);
}

static void trim_close(AVBSFContext *ctx)
{
    TrimContext *s = ctx->priv_data;

    trim_report(ctx);
    preroll_clear(ctx);
    av_packet_free(&s->pending);
    av_fifo_freep2(&s->preroll_fifo);
}

static int trim_init(AVBSFContext *ctx)
{
    TrimContext *s = ctx->priv_data;
    int ret;

    if (s->start == INT64_MIN && s->end == INT64_MAX) {
        av_log(ctx, AV_LOG_ERROR, "At least one of start or end must be set\n");
        return AVERROR(EINVAL);
    }

    ret = bound_init(ctx, &s->start_bound, s->start != INT64_MIN, s->start,
                     s->start_type, s->start_rel, "start");
    if (ret < 0)
        return ret;

    ret = bound_init(ctx, &s->end_bound, s->end != INT64_MAX, s->end,
                     s->end_type, s->end_rel, "end");
    if (ret < 0)
        return ret;

    /* A packet count measured from the first exported packet is a promise about
     * the exported stream, so it is counted there. */
    if (s->end_bound.axis == AXIS_INDEX && s->end_bound.relative)
        s->end_bound.axis = AXIS_OUT_INDEX;

    /* Shifting the timeline leaves the samples where they were, so audio with
     * no sample space to be trimmed in cannot be trimmed at all. */
    if (s->trim_packets && ctx->par_in->codec_type == AVMEDIA_TYPE_AUDIO) {
        if (ctx->par_in->sample_rate <= 0 || ctx->time_base_in.num <= 0 ||
            ctx->time_base_in.den <= 0) {
            av_log(ctx, AV_LOG_ERROR, "audio is trimmed through skip samples "
                   "side data, which needs a sample rate and a time base\n");
            return AVERROR(EINVAL);
        }
        s->audio = 1;
    }

    /* The window is bounded by a keyframe, which only video has. Audio needs a
     * preroll counted in samples, which is not the one this would give. */
    if (s->preroll && s->start_bound.active &&
        ctx->par_in->codec_type == AVMEDIA_TYPE_VIDEO) {
        s->preroll_fifo = av_fifo_alloc2(1, sizeof(AVPacket *),
                                         AV_FIFO_FLAG_AUTO_GROW);
        if (!s->preroll_fifo)
            return AVERROR(ENOMEM);
        av_fifo_auto_grow_limit(s->preroll_fifo, s->preroll_size);
    }

    trim_reset(ctx);

    return 0;
}

#define OFFSET(x) offsetof(TrimContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption trim_options[] = {
    { "start", "time or index marking the start of the accepted range", OFFSET(start),
        AV_OPT_TYPE_INT64, { .i64 = INT64_MIN }, INT64_MIN, INT64_MAX, FLAGS },
    { "start_type", "how to interpret start", OFFSET(start_type),
        AV_OPT_TYPE_INT, { .i64 = TRIM_PTS }, 0, TRIM_MSEC_DT, FLAGS, .unit = "start_type" },
        { "pts",       "stream time base pts",              0, AV_OPT_TYPE_CONST, { .i64 = TRIM_PTS },       0, 0, FLAGS, .unit = "start_type" },
        { "dts",       "stream time base dts",              0, AV_OPT_TYPE_CONST, { .i64 = TRIM_DTS },       0, 0, FLAGS, .unit = "start_type" },
        { "pkt_index", "packet index",                      0, AV_OPT_TYPE_CONST, { .i64 = TRIM_PKT_INDEX }, 0, 0, FLAGS, .unit = "start_type" },
        { "msec_pt",   "milliseconds, matched against pts", 0, AV_OPT_TYPE_CONST, { .i64 = TRIM_MSEC_PT },   0, 0, FLAGS, .unit = "start_type" },
        { "msec_dt",   "milliseconds, matched against dts", 0, AV_OPT_TYPE_CONST, { .i64 = TRIM_MSEC_DT },   0, 0, FLAGS, .unit = "start_type" },
    { "start_rel", "interpret start relative to the first packet of the stream", OFFSET(start_rel),
        AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "end", "time or index marking the end of the accepted range", OFFSET(end),
        AV_OPT_TYPE_INT64, { .i64 = INT64_MAX }, INT64_MIN, INT64_MAX, FLAGS },
    { "end_type", "how to interpret end", OFFSET(end_type),
        AV_OPT_TYPE_INT, { .i64 = TRIM_PTS }, 0, TRIM_DUR_T_MSEC, FLAGS, .unit = "end_type" },
        { "pts",        "stream time base pts",              0, AV_OPT_TYPE_CONST, { .i64 = TRIM_PTS },         0, 0, FLAGS, .unit = "end_type" },
        { "dts",        "stream time base dts",              0, AV_OPT_TYPE_CONST, { .i64 = TRIM_DTS },         0, 0, FLAGS, .unit = "end_type" },
        { "pkt_index",  "packet index",                      0, AV_OPT_TYPE_CONST, { .i64 = TRIM_PKT_INDEX },   0, 0, FLAGS, .unit = "end_type" },
        { "msec_pt",    "milliseconds, matched against pts", 0, AV_OPT_TYPE_CONST, { .i64 = TRIM_MSEC_PT },     0, 0, FLAGS, .unit = "end_type" },
        { "msec_dt",    "milliseconds, matched against dts", 0, AV_OPT_TYPE_CONST, { .i64 = TRIM_MSEC_DT },     0, 0, FLAGS, .unit = "end_type" },
        { "dur_ts",     "duration in stream time base",      0, AV_OPT_TYPE_CONST, { .i64 = TRIM_DUR_TS },      0, 0, FLAGS, .unit = "end_type" },
        { "dur_t_msec", "duration in milliseconds",          0, AV_OPT_TYPE_CONST, { .i64 = TRIM_DUR_T_MSEC },  0, 0, FLAGS, .unit = "end_type" },
    { "end_rel", "interpret end relative to the first exported packet", OFFSET(end_rel),
        AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "trim_packets", "trim packets straddling a boundary instead of exporting them "
                      "untouched or dropping them", OFFSET(trim_packets),
        AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
    { "preroll", "export the packets the first packet in range needs to be decodable, "
                 "flagged for the decoder to drop them after decoding", OFFSET(preroll),
        AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "preroll_size", "maximum number of packets held for preroll", OFFSET(preroll_size),
        AV_OPT_TYPE_INT, { .i64 = 300 }, 1, INT_MAX, FLAGS },
    { NULL },
};

static const AVClass trim_class = {
    .class_name = "trim",
    .item_name  = av_default_item_name,
    .option     = trim_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFBitStreamFilter ff_trim_bsf = {
    .p.name         = "trim",
    .p.priv_class   = &trim_class,
    .priv_data_size = sizeof(TrimContext),
    .init           = trim_init,
    .close          = trim_close,
    .flush          = trim_flush,
    .filter         = trim_filter,
};
