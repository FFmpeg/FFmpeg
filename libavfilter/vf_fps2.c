
// V 1.0.0

#include "libavutil/dict.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/eval.h"
#include "libavutil/pixdesc.h"
#include "libavutil/motion_vector.h"
#include "libavutil/video_enc_params.h"
#include "libavformat/url.h"
#include "libavformat/http.h"
#include "libavformat/avio.h"
#include "avfilter.h"
#include "formats.h"
#include "drawutils.h"
#include "internal.h"
#include "video.h"

struct FPS2Context;
typedef int (*PixelBelongsToRegion)(struct FPS2Context *s, int x, int y);

typedef struct FPS2Context {
    double input_fps;
    double fps;
    double frames_sent;
    double frames_arrived;

} FPS2Context;

#define OFFSET(x) offsetof(FPS2Context, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM|AVFILTER_FLAG_DYNAMIC_OUTPUTS


static const AVOption fps2_options[] = {
        {"fps", "turn the tripwire on or off", OFFSET(fps), AV_OPT_TYPE_DOUBLE, {.dbl = 20}, 0, 9999, FLAGS },
        { NULL } };

AVFILTER_DEFINE_CLASS(fps2);

static int filter_frame(AVFilterLink *inlink, AVFrame *frame) {
    AVFilterContext *ctx = inlink->dst;
    FPS2Context *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    s->frames_arrived++;
    if ((s->frames_sent/s->frames_arrived > s->fps)){
        av_frame_free(&frame);
        return 0;
    }
    s->frames_sent++;
    if (s->frames_arrived >= 10000){  // dont let it grow to high.
        s->frames_arrived = 0;
        s->frames_sent = 0;
    }
    return ff_filter_frame(outlink, frame);
}


static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    FPS2Context *s = ctx->priv;
    s->input_fps = inlink->frame_rate.num;
    if (s->input_fps < s->fps)
        av_log(ctx, AV_LOG_WARNING, "Input fps lower than get set for fps2.\n");
    s->fps = s->fps/s->input_fps;
    s->frames_sent = 0;
    s->frames_arrived = 0;
    return 0;
}

static const AVFilterPad fps2_inputs[] = {
        {
                .name = "default",
                .type = AVMEDIA_TYPE_VIDEO,
                .filter_frame = filter_frame,
                .config_props = config_input,
                .flags = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        },
};

static const AVFilterPad fps2_outputs[] = { { .name = "default", .type = AVMEDIA_TYPE_VIDEO, }, };

const AVFilter ff_vf_fps2 = { 
        .name           = "fps2", 
        .description    = NULL_IF_CONFIG_SMALL("Tracking object based on motion vectors from video encoding."), 
        .priv_size      = sizeof(FPS2Context), 
        .priv_class     = &fps2_class,
        .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
        FILTER_INPUTS(fps2_inputs),
        FILTER_OUTPUTS(fps2_outputs),
};
