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

#ifndef FFTOOLS_FFMPEG_FILTER_H
#define FFTOOLS_FFMPEG_FILTER_H

#include "ffmpeg.h"

#include <stdint.h>

#include "ffmpeg_sched.h"
#include "sync_queue.h"

#include "libavfilter/avfilter.h"

#include "libavutil/avutil.h"
#include "libavutil/dict.h"
#include "libavutil/fifo.h"
#include "libavutil/pixfmt.h"
#include "libavutil/rational.h"
#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/downmix_info.h"

typedef struct FilterGraphPriv {
    FilterGraph      fg;

    // name used for logging
    char             log_name[32];

    int              is_simple;
    // true when the filtergraph contains only meta filters
    // that do not modify the frame data
    int              is_meta;
    // source filters are present in the graph
    int              have_sources;
    int              disable_conversions;

    unsigned         nb_outputs_done;

    const char      *graph_desc;

    int              nb_threads;

    // frame for temporarily holding output from the filtergraph
    AVFrame         *frame;
    // frame for sending output to the encoder
    AVFrame         *frame_enc;

    Scheduler       *sch;
    unsigned         sch_idx;

    AVBPrint graph_print_buf;

} FilterGraphPriv;

static inline FilterGraphPriv *fgp_from_fg(FilterGraph *fg)
{
    return (FilterGraphPriv*)fg;
}

static inline const FilterGraphPriv *cfgp_from_cfg(const FilterGraph *fg)
{
    return (const FilterGraphPriv*)fg;
}

typedef struct InputFilterPriv {
    InputFilter         ifilter;

    InputFilterOptions  opts;

    int                 index;

    AVFilterContext    *filter;

    // used to hold submitted input
    AVFrame            *frame;

    /* for filters that are not yet bound to an input stream,
     * this stores the input linklabel, if any */
    uint8_t            *linklabel;

    // filter data type
    enum AVMediaType    type;
    // source data type: AVMEDIA_TYPE_SUBTITLE for sub2video,
    // same as type otherwise
    enum AVMediaType    type_src;

    int                 eof;
    int                 bound;
    int                 drop_warned;
    uint64_t            nb_dropped;

    // parameters configured for this input
    int                 format;

    int                 width, height;
    AVRational          sample_aspect_ratio;
    enum AVColorSpace   color_space;
    enum AVColorRange   color_range;

    int                 sample_rate;
    AVChannelLayout     ch_layout;

    AVRational          time_base;

    AVFrameSideData   **side_data;
    int                 nb_side_data;

    AVFifo             *frame_queue;

    AVBufferRef        *hw_frames_ctx;

    int                 displaymatrix_present;
    int                 displaymatrix_applied;
    int32_t             displaymatrix[9];

    int                 downmixinfo_present;
    AVDownmixInfo       downmixinfo;

    struct {
        AVFrame *frame;

        int64_t last_pts;
        int64_t end_pts;

        /// marks if sub2video_update should force an initialization
        unsigned int initialize;
    } sub2video;
} InputFilterPriv;

static inline InputFilterPriv *ifp_from_ifilter(InputFilter *ifilter)
{
    return (InputFilterPriv*)ifilter;
}

typedef struct FPSConvContext {
    AVFrame          *last_frame;
    /* number of frames emitted by the video-encoding sync code */
    int64_t           frame_number;
    /* history of nb_frames_prev, i.e. the number of times the
     * previous frame was duplicated by vsync code in recent
     * do_video_out() calls */
    int64_t           frames_prev_hist[3];

    uint64_t          dup_warning;

    int               last_dropped;
    int               dropped_keyframe;

    enum VideoSyncMethod vsync_method;

    AVRational        framerate;
    AVRational        framerate_max;
    const AVRational *framerate_supported;
    int               framerate_clip;
} FPSConvContext;


typedef struct OutputFilterPriv {
    OutputFilter            ofilter;

    int                     index;

    void                   *log_parent;
    char                    log_name[32];

    char                   *name;

    AVFilterContext        *filter;

    /* desired output stream properties */
    int                     format;
    int                     width, height;
    int                     sample_rate;
    AVChannelLayout         ch_layout;
    enum AVColorSpace       color_space;
    enum AVColorRange       color_range;

    AVFrameSideData       **side_data;
    int                     nb_side_data;

    // time base in which the output is sent to our downstream
    // does not need to match the filtersink's timebase
    AVRational              tb_out;
    // at least one frame with the above timebase was sent
    // to our downstream, so it cannot change anymore
    int                     tb_out_locked;

    AVRational              sample_aspect_ratio;

    AVDictionary           *sws_opts;
    AVDictionary           *swr_opts;

    // those are only set if no format is specified and the encoder gives us multiple options
    // They point directly to the relevant lists of the encoder.
    const int              *formats;
    const AVChannelLayout  *ch_layouts;
    const int              *sample_rates;
    const enum AVColorSpace *color_spaces;
    const enum AVColorRange *color_ranges;

    AVRational              enc_timebase;
    int64_t                 trim_start_us;
    int64_t                 trim_duration_us;
    // offset for output timestamps, in AV_TIME_BASE_Q
    int64_t                 ts_offset;
    int64_t                 next_pts;
    FPSConvContext          fps;

    unsigned                flags;
} OutputFilterPriv;

static inline OutputFilterPriv *ofp_from_ofilter(OutputFilter *ofilter)
{
    return (OutputFilterPriv*)ofilter;
}

#endif /* FFTOOLS_FFMPEG_FILTER_H */
