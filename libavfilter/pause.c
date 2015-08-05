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

#include <float.h>
#include <math.h>
#include <stdint.h>

#include "config.h"

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#include "audio.h"
#include "avfilter.h"
#include "internal.h"

// global variables for pause/apause sync. TODO configurable id's
static int64_t video_first_pts = AV_NOPTS_VALUE;
static int64_t video_skipped_pts_duration = AV_NOPTS_VALUE;
static int paused;

typedef struct PauseContext {
    const AVClass *class;

    int start_paused;
    int64_t last_pts, first_pts, skipped_pts_duration;
    AVRational tb;
    double position;
} PauseContext;

static av_cold int init(AVFilterContext *ctx)
{
    PauseContext *s = ctx->priv;

    s->skipped_pts_duration = 0;
    s->last_pts = AV_NOPTS_VALUE;
    s->first_pts = AV_NOPTS_VALUE;
    s->position = 0;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
	PauseContext *s = inlink->dst->priv;

	s->tb = (inlink->type == AVMEDIA_TYPE_VIDEO) ?
			inlink->time_base : (AVRational){ 1, inlink->sample_rate };

	if (inlink->type == AVMEDIA_TYPE_VIDEO)
		video_skipped_pts_duration = 0;

	paused = s->start_paused;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    outlink->flags |= FF_LINK_FLAG_REQUEST_LOOP;
    return 0;
}

static int command(AVFilterContext *ctx, const char *cmd, const char *arg, char *res, int res_len, int flags)
{
	if (!strcmp(cmd, "play")) {
    	paused = 0;
        return 0;
    } else if (!strcmp(cmd, "pause")) {
    	paused = 1;
        return 0;
    }

    return AVERROR(ENOSYS);
}


#define OFFSET(x) offsetof(PauseContext, x)

#if CONFIG_PAUSE_FILTER
static int pause_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
	int64_t progress;
	AVFilterContext *ctx = inlink->dst;
    PauseContext       *s = ctx->priv;


    /* init last pts */
    if (s->last_pts == AV_NOPTS_VALUE)
    	s->last_pts = frame->pts;

    /* init first pts */
    if (s->first_pts == AV_NOPTS_VALUE) {
        s->first_pts = frame->pts;
        video_first_pts = av_rescale_q(s->first_pts, s->tb, AV_TIME_BASE_Q);
    }

    /* drop everything if paused */
    if (paused) {
    	if (frame->pts != AV_NOPTS_VALUE) {
    		s->skipped_pts_duration += frame->pts - s->last_pts;
    		video_skipped_pts_duration = av_rescale_q(s->skipped_pts_duration, s->tb, AV_TIME_BASE_Q);
    		s->last_pts = frame->pts;
    	}
        av_frame_free(&frame);
        return 0;
    }

    /* substract first pts + skipped pts duration if playing */
    if (frame->pts != AV_NOPTS_VALUE) {
    	s->last_pts = frame->pts;
    	frame->pts -= s->first_pts + s->skipped_pts_duration;
    	s->position = ((double) av_rescale_q(frame->pts, AV_TIME_BASE_Q, s->tb)) / ((double)1000000);
    } else {
    	s->position += ((double)s->tb.num) / ((double)s->tb.den);
    }

    fprintf(stderr,"position:%0.2f\n",s->position);

    return ff_filter_frame(ctx->outputs[0], frame);
}

#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption pause_options[] = {
    { "start_paused", "Initial state ", OFFSET(start_paused), AV_OPT_TYPE_INT,  { .i64 = 1 },       0, 1, FLAGS },
    { NULL }
};
#undef FLAGS

AVFILTER_DEFINE_CLASS(pause);

static const AVFilterPad pause_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = pause_filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad pause_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_pause = {
    .name        = "pause",
    .description = NULL_IF_CONFIG_SMALL("Pick one continuous section from the input, drop the rest."),
    .init        = init,
    .priv_size   = sizeof(PauseContext),
    .priv_class  = &pause_class,
    .inputs      = pause_inputs,
    .outputs     = pause_outputs,
	.process_command = command,
};
#endif // CONFIG_PAUSE_FILTER

#if CONFIG_APAUSE_FILTER
static int apause_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    PauseContext       *s = ctx->priv;

    /* init last pts */
    if (s->last_pts == AV_NOPTS_VALUE)
    	s->last_pts = frame->pts;

    /* init first pts */
    if (s->first_pts == AV_NOPTS_VALUE) {
    	if (video_first_pts != AV_NOPTS_VALUE) { // sync by video
    		s->first_pts = av_rescale_q(video_first_pts, AV_TIME_BASE_Q, s->tb);
    	} else {
    		s->first_pts = frame->pts;
    	}

    }

    /* drop everything if paused */
    if (paused) {
    	if (frame->pts != AV_NOPTS_VALUE) {
    		s->skipped_pts_duration += frame->pts - s->last_pts;
    		s->last_pts = frame->pts;
    	}
        av_frame_free(&frame);
        return 0;
    }

    /* substract first pts + skipped pts duration if playing */
    if (frame->pts != AV_NOPTS_VALUE) {
    	s->last_pts = frame->pts;
    	if (video_skipped_pts_duration != AV_NOPTS_VALUE) { // sync by video
    		frame->pts -= av_rescale_q(video_skipped_pts_duration, AV_TIME_BASE_Q, s->tb);
    	} else {
    		frame->pts -= s->skipped_pts_duration;
    	}
    	frame->pts -= s->first_pts;
    	if (frame->pts < 0) {
    		av_frame_free(&frame);
    		return 0;

    	}
    }

    return ff_filter_frame(ctx->outputs[0], frame);
}

#define FLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption apause_options[] = {
    { "start_paused", "Initial state ", OFFSET(start_paused), AV_OPT_TYPE_INT,  { .i64 = 1 },       0, 1, FLAGS },
    { NULL }
};
#undef FLAGS

AVFILTER_DEFINE_CLASS(apause);

static const AVFilterPad apause_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = apause_filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad apause_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_af_apause = {
    .name        = "apause",
    .description = NULL_IF_CONFIG_SMALL("Pick one continuous section from the input, drop the rest."),
    .init        = init,
    .priv_size   = sizeof(PauseContext),
    .priv_class  = &apause_class,
    .inputs      = apause_inputs,
    .outputs     = apause_outputs,
	.process_command = command,
};
#endif // CONFIG_APAUSE_FILTER
