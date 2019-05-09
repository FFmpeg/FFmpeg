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

/**
* @file
* ModPlug demuxer
* @todo better probing than extensions matching
*/

#define MODPLUG_STATIC
#include <libmodplug/modplug.h>
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "internal.h"

typedef struct ModPlugContext {
    const AVClass *class;
    ModPlugFile *f;
    uint8_t *buf; ///< input file content

    /* options */
    int noise_reduction;
    int reverb_depth;
    int reverb_delay;
    int bass_amount;
    int bass_range;
    int surround_depth;
    int surround_delay;

    int max_size; ///< max file size to allocate

    /* optional video stream */
    double ts_per_packet; ///< used to define the pts/dts using packet_count;
    int packet_count;     ///< total number of audio packets
    int print_textinfo;   ///< bool flag for printing speed, tempo, order, ...
    int video_stream;     ///< 1 if the user want a video stream, otherwise 0
    int w;                ///< video stream width  in char (one char = 8x8px)
    int h;                ///< video stream height in char (one char = 8x8px)
    int video_switch;     ///< 1 if current packet is video, otherwise 0
    int fsize;            ///< constant frame size
    int linesize;         ///< line size in bytes
    char *color_eval;     ///< color eval user input expression
    AVExpr *expr;         ///< parsed color eval expression
} ModPlugContext;

static const char * const var_names[] = {
    "x", "y",
    "w", "h",
    "t",
    "speed", "tempo", "order", "pattern", "row",
    NULL
};

enum var_name {
    VAR_X, VAR_Y,
    VAR_W, VAR_H,
    VAR_TIME,
    VAR_SPEED, VAR_TEMPO, VAR_ORDER, VAR_PATTERN, VAR_ROW,
    VAR_VARS_NB
};

#define FF_MODPLUG_MAX_FILE_SIZE (100 * 1<<20) // 100M
#define FF_MODPLUG_DEF_FILE_SIZE (  5 * 1<<20) //   5M

#define OFFSET(x) offsetof(ModPlugContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    {"noise_reduction", "Enable noise reduction 0(off)-1(on)",  OFFSET(noise_reduction), AV_OPT_TYPE_INT, {.i64 = 0}, 0,       1, D},
    {"reverb_depth",    "Reverb level 0(quiet)-100(loud)",      OFFSET(reverb_depth),    AV_OPT_TYPE_INT, {.i64 = 0}, 0,     100, D},
    {"reverb_delay",    "Reverb delay in ms, usually 40-200ms", OFFSET(reverb_delay),    AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, D},
    {"bass_amount",     "XBass level 0(quiet)-100(loud)",       OFFSET(bass_amount),     AV_OPT_TYPE_INT, {.i64 = 0}, 0,     100, D},
    {"bass_range",      "XBass cutoff in Hz 10-100",            OFFSET(bass_range),      AV_OPT_TYPE_INT, {.i64 = 0}, 0,     100, D},
    {"surround_depth",  "Surround level 0(quiet)-100(heavy)",   OFFSET(surround_depth),  AV_OPT_TYPE_INT, {.i64 = 0}, 0,     100, D},
    {"surround_delay",  "Surround delay in ms, usually 5-40ms", OFFSET(surround_delay),  AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, D},
    {"max_size",        "Max file size supported (in bytes). Default is 5MB. Set to 0 for no limit (not recommended)",
     OFFSET(max_size), AV_OPT_TYPE_INT, {.i64 = FF_MODPLUG_DEF_FILE_SIZE}, 0, FF_MODPLUG_MAX_FILE_SIZE, D},
    {"video_stream_expr", "Color formula",                                  OFFSET(color_eval),     AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, D},
    {"video_stream",      "Make demuxer output a video stream",             OFFSET(video_stream),   AV_OPT_TYPE_INT, {.i64 = 0},   0,   1, D},
    {"video_stream_w",    "Video stream width in char (one char = 8x8px)",  OFFSET(w),              AV_OPT_TYPE_INT, {.i64 = 30}, 20, 512, D},
    {"video_stream_h",    "Video stream height in char (one char = 8x8px)", OFFSET(h),              AV_OPT_TYPE_INT, {.i64 = 30}, 20, 512, D},
    {"video_stream_ptxt", "Print speed, tempo, order, ... in video stream", OFFSET(print_textinfo), AV_OPT_TYPE_INT, {.i64 = 1},   0,   1, D},
    {NULL},
};

#define SET_OPT_IF_REQUESTED(libopt, opt, flag) do {        \
    if (modplug->opt) {                                     \
        settings.libopt  = modplug->opt;                    \
        settings.mFlags |= flag;                            \
    }                                                       \
} while (0)

#define ADD_META_MULTIPLE_ENTRIES(entry_name, fname) do {                      \
    if (n_## entry_name ##s) {                                                 \
        unsigned i, n = 0;                                                     \
                                                                               \
        for (i = 0; i < n_## entry_name ##s; i++) {                            \
            char item_name[64] = {0};                                          \
            fname(f, i, item_name);                                            \
            if (!*item_name)                                                   \
                continue;                                                      \
            if (n)                                                             \
                av_dict_set(&s->metadata, #entry_name, "\n", AV_DICT_APPEND);  \
            av_dict_set(&s->metadata, #entry_name, item_name, AV_DICT_APPEND); \
            n++;                                                               \
        }                                                                      \
                                                                               \
        extra = av_asprintf(", %u/%u " #entry_name "%s",                       \
                            n, n_## entry_name ##s, n > 1 ? "s" : "");         \
        if (!extra)                                                            \
            return AVERROR(ENOMEM);                                            \
        av_dict_set(&s->metadata, "extra info", extra, AV_DICT_APPEND);        \
        av_free(extra);                                                        \
    }                                                                          \
} while (0)

static int modplug_load_metadata(AVFormatContext *s)
{
    ModPlugContext *modplug = s->priv_data;
    ModPlugFile *f = modplug->f;
    char *extra;
    const char *name = ModPlug_GetName(f);
    const char *msg  = ModPlug_GetMessage(f);

    unsigned n_instruments = ModPlug_NumInstruments(f);
    unsigned n_samples     = ModPlug_NumSamples(f);
    unsigned n_patterns    = ModPlug_NumPatterns(f);
    unsigned n_channels    = ModPlug_NumChannels(f);

    if (name && *name) av_dict_set(&s->metadata, "name",    name, 0);
    if (msg  && *msg)  av_dict_set(&s->metadata, "message", msg,  0);

    extra = av_asprintf("%u pattern%s, %u channel%s",
                        n_patterns, n_patterns > 1 ? "s" : "",
                        n_channels, n_channels > 1 ? "s" : "");
    if (!extra)
        return AVERROR(ENOMEM);
    av_dict_set(&s->metadata, "extra info", extra, AV_DICT_DONT_STRDUP_VAL);

    ADD_META_MULTIPLE_ENTRIES(instrument, ModPlug_InstrumentName);
    ADD_META_MULTIPLE_ENTRIES(sample,     ModPlug_SampleName);

    return 0;
}

#define AUDIO_PKT_SIZE 512

static int modplug_read_header(AVFormatContext *s)
{
    AVStream *st;
    AVIOContext *pb = s->pb;
    ModPlug_Settings settings;
    ModPlugContext *modplug = s->priv_data;
    int64_t sz = avio_size(pb);

    if (sz < 0) {
        av_log(s, AV_LOG_WARNING, "Could not determine file size\n");
        sz = modplug->max_size;
    } else if (modplug->max_size && sz > modplug->max_size) {
        sz = modplug->max_size;
        av_log(s, AV_LOG_WARNING, "Max file size reach%s, allocating %"PRIi64"B "
               "but demuxing is likely to fail due to incomplete buffer\n",
               sz == FF_MODPLUG_DEF_FILE_SIZE ? " (see -max_size)" : "", sz);
    }

    if (modplug->color_eval) {
        int r = av_expr_parse(&modplug->expr, modplug->color_eval, var_names,
                              NULL, NULL, NULL, NULL, 0, s);
        if (r < 0)
            return r;
    }

    modplug->buf = av_malloc(modplug->max_size);
    if (!modplug->buf)
        return AVERROR(ENOMEM);
    sz = avio_read(pb, modplug->buf, sz);

    ModPlug_GetSettings(&settings);
    settings.mChannels       = 2;
    settings.mBits           = 16;
    settings.mFrequency      = 44100;
    settings.mResamplingMode = MODPLUG_RESAMPLE_FIR; // best quality
    settings.mLoopCount      = 0; // prevents looping forever

    if (modplug->noise_reduction) settings.mFlags     |= MODPLUG_ENABLE_NOISE_REDUCTION;
    SET_OPT_IF_REQUESTED(mReverbDepth,   reverb_depth,   MODPLUG_ENABLE_REVERB);
    SET_OPT_IF_REQUESTED(mReverbDelay,   reverb_delay,   MODPLUG_ENABLE_REVERB);
    SET_OPT_IF_REQUESTED(mBassAmount,    bass_amount,    MODPLUG_ENABLE_MEGABASS);
    SET_OPT_IF_REQUESTED(mBassRange,     bass_range,     MODPLUG_ENABLE_MEGABASS);
    SET_OPT_IF_REQUESTED(mSurroundDepth, surround_depth, MODPLUG_ENABLE_SURROUND);
    SET_OPT_IF_REQUESTED(mSurroundDelay, surround_delay, MODPLUG_ENABLE_SURROUND);

    if (modplug->reverb_depth)   settings.mReverbDepth   = modplug->reverb_depth;
    if (modplug->reverb_delay)   settings.mReverbDelay   = modplug->reverb_delay;
    if (modplug->bass_amount)    settings.mBassAmount    = modplug->bass_amount;
    if (modplug->bass_range)     settings.mBassRange     = modplug->bass_range;
    if (modplug->surround_depth) settings.mSurroundDepth = modplug->surround_depth;
    if (modplug->surround_delay) settings.mSurroundDelay = modplug->surround_delay;

    ModPlug_SetSettings(&settings);

    modplug->f = ModPlug_Load(modplug->buf, sz);
    if (!modplug->f)
        return AVERROR_INVALIDDATA;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 1000);
    st->duration = ModPlug_GetLength(modplug->f);
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = AV_CODEC_ID_PCM_S16LE;
    st->codecpar->channels    = settings.mChannels;
    st->codecpar->sample_rate = settings.mFrequency;

    // timebase = 1/1000, 2ch 16bits 44.1kHz-> 2*2*44100
    modplug->ts_per_packet = 1000*AUDIO_PKT_SIZE / (4*44100.);

    if (modplug->video_stream) {
        AVStream *vst = avformat_new_stream(s, NULL);
        if (!vst)
            return AVERROR(ENOMEM);
        avpriv_set_pts_info(vst, 64, 1, 1000);
        vst->duration = st->duration;
        vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        vst->codecpar->codec_id   = AV_CODEC_ID_XBIN;
        vst->codecpar->width      = modplug->w << 3;
        vst->codecpar->height     = modplug->h << 3;
        modplug->linesize = modplug->w * 3;
        modplug->fsize    = modplug->linesize * modplug->h;
    }

    return modplug_load_metadata(s);
}

static void write_text(uint8_t *dst, const char *s, int linesize, int x, int y)
{
    int i;
    dst += y*linesize + x*3;
    for (i = 0; s[i]; i++, dst += 3) {
        dst[0] = 0x0;   // count - 1
        dst[1] = s[i];  // char
        dst[2] = 0x0f;  // background / foreground
    }
}

#define PRINT_INFO(line, name, idvalue) do {                            \
    snprintf(intbuf, sizeof(intbuf), "%.0f", var_values[idvalue]);      \
    write_text(pkt->data, name ":", modplug->linesize,  0+1, line+1);   \
    write_text(pkt->data, intbuf,   modplug->linesize, 10+1, line+1);   \
} while (0)

static int modplug_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ModPlugContext *modplug = s->priv_data;

    if (modplug->video_stream) {
        modplug->video_switch ^= 1; // one video packet for one audio packet
        if (modplug->video_switch) {
            double var_values[VAR_VARS_NB];

            var_values[VAR_W      ] = modplug->w;
            var_values[VAR_H      ] = modplug->h;
            var_values[VAR_TIME   ] = modplug->packet_count * modplug->ts_per_packet;
            var_values[VAR_SPEED  ] = ModPlug_GetCurrentSpeed  (modplug->f);
            var_values[VAR_TEMPO  ] = ModPlug_GetCurrentTempo  (modplug->f);
            var_values[VAR_ORDER  ] = ModPlug_GetCurrentOrder  (modplug->f);
            var_values[VAR_PATTERN] = ModPlug_GetCurrentPattern(modplug->f);
            var_values[VAR_ROW    ] = ModPlug_GetCurrentRow    (modplug->f);

            if (av_new_packet(pkt, modplug->fsize) < 0)
                return AVERROR(ENOMEM);
            pkt->stream_index = 1;
            memset(pkt->data, 0, modplug->fsize);

            if (modplug->print_textinfo) {
                char intbuf[32];
                PRINT_INFO(0, "speed",   VAR_SPEED);
                PRINT_INFO(1, "tempo",   VAR_TEMPO);
                PRINT_INFO(2, "order",   VAR_ORDER);
                PRINT_INFO(3, "pattern", VAR_PATTERN);
                PRINT_INFO(4, "row",     VAR_ROW);
                PRINT_INFO(5, "ts",      VAR_TIME);
            }

            if (modplug->expr) {
                int x, y;
                for (y = 0; y < modplug->h; y++) {
                    for (x = 0; x < modplug->w; x++) {
                        double color;
                        var_values[VAR_X] = x;
                        var_values[VAR_Y] = y;
                        color = av_expr_eval(modplug->expr, var_values, NULL);
                        pkt->data[y*modplug->linesize + x*3 + 2] |= av_clip((int)color, 0, 0xf)<<4;
                    }
                }
            }
            pkt->pts = pkt->dts = var_values[VAR_TIME];
            pkt->flags |= AV_PKT_FLAG_KEY;
            return 0;
        }
    }

    if (av_new_packet(pkt, AUDIO_PKT_SIZE) < 0)
        return AVERROR(ENOMEM);

    if (modplug->video_stream)
        pkt->pts = pkt->dts = modplug->packet_count++ * modplug->ts_per_packet;

    pkt->size = ModPlug_Read(modplug->f, pkt->data, AUDIO_PKT_SIZE);
    if (pkt->size <= 0) {
        av_packet_unref(pkt);
        return pkt->size == 0 ? AVERROR_EOF : AVERROR(EIO);
    }
    return 0;
}

static int modplug_read_close(AVFormatContext *s)
{
    ModPlugContext *modplug = s->priv_data;
    ModPlug_Unload(modplug->f);
    av_freep(&modplug->buf);
    return 0;
}

static int modplug_read_seek(AVFormatContext *s, int stream_idx, int64_t ts, int flags)
{
    ModPlugContext *modplug = s->priv_data;
    ModPlug_Seek(modplug->f, (int)ts);
    if (modplug->video_stream)
        modplug->packet_count = ts / modplug->ts_per_packet;
    return 0;
}

static const char modplug_extensions[] = "669,abc,amf,ams,dbm,dmf,dsm,far,it,mdl,med,mid,mod,mt2,mtm,okt,psm,ptm,s3m,stm,ult,umx,xm,itgz,itr,itz,mdgz,mdr,mdz,s3gz,s3r,s3z,xmgz,xmr,xmz";

static int modplug_probe(const AVProbeData *p)
{
    if (av_match_ext(p->filename, modplug_extensions)) {
        if (p->buf_size < 16384)
            return AVPROBE_SCORE_EXTENSION/2-1;
        else
            return AVPROBE_SCORE_EXTENSION;
    }
    return 0;
}

static const AVClass modplug_class = {
    .class_name = "ModPlug demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_libmodplug_demuxer = {
    .name           = "libmodplug",
    .long_name      = NULL_IF_CONFIG_SMALL("ModPlug demuxer"),
    .priv_data_size = sizeof(ModPlugContext),
    .read_probe     = modplug_probe,
    .read_header    = modplug_read_header,
    .read_packet    = modplug_read_packet,
    .read_close     = modplug_read_close,
    .read_seek      = modplug_read_seek,
    .extensions     = modplug_extensions,
    .priv_class     = &modplug_class,
};
