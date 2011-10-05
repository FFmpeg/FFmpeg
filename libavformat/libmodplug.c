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

#include <libmodplug/modplug.h>
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "avformat.h"

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
} ModPlugContext;

#define FF_MODPLUG_MAX_FILE_SIZE (100 * 1<<20) // 100M
#define FF_MODPLUG_DEF_FILE_SIZE (  5 * 1<<20) //   5M

#define OFFSET(x) offsetof(ModPlugContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    {"noise_reduction", "Enable noise reduction 0(off)-1(on)",  OFFSET(noise_reduction), FF_OPT_TYPE_INT, {.dbl = 0}, 0,       1, D},
    {"reverb_depth",    "Reverb level 0(quiet)-100(loud)",      OFFSET(reverb_depth),    FF_OPT_TYPE_INT, {.dbl = 0}, 0,     100, D},
    {"reverb_delay",    "Reverb delay in ms, usually 40-200ms", OFFSET(reverb_delay),    FF_OPT_TYPE_INT, {.dbl = 0}, 0, INT_MAX, D},
    {"bass_amount",     "XBass level 0(quiet)-100(loud)",       OFFSET(bass_amount),     FF_OPT_TYPE_INT, {.dbl = 0}, 0,     100, D},
    {"bass_range",      "XBass cutoff in Hz 10-100",            OFFSET(bass_range),      FF_OPT_TYPE_INT, {.dbl = 0}, 0,     100, D},
    {"surround_depth",  "Surround level 0(quiet)-100(heavy)",   OFFSET(surround_depth),  FF_OPT_TYPE_INT, {.dbl = 0}, 0,     100, D},
    {"surround_delay",  "Surround delay in ms, usually 5-40ms", OFFSET(surround_delay),  FF_OPT_TYPE_INT, {.dbl = 0}, 0, INT_MAX, D},
    {"max_size",        "Max file size supported (in bytes). Default is 5MB. Set to 0 for no limit (not recommended)",
     OFFSET(max_size), FF_OPT_TYPE_INT, {.dbl = FF_MODPLUG_DEF_FILE_SIZE}, 0, FF_MODPLUG_MAX_FILE_SIZE, D},
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

static int modplug_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVStream *st;
    AVIOContext *pb = s->pb;
    ModPlug_Settings settings;
    ModPlugContext *modplug = s->priv_data;
    int sz = avio_size(pb);

    if (sz < 0) {
        av_log(s, AV_LOG_WARNING, "Could not determine file size\n");
        sz = modplug->max_size;
    } else if (modplug->max_size && sz > modplug->max_size) {
        sz = modplug->max_size;
        av_log(s, AV_LOG_WARNING, "Max file size reach%s, allocating %dB "
               "but demuxing is likely to fail due to incomplete buffer\n",
               sz == FF_MODPLUG_DEF_FILE_SIZE ? " (see -max_size)" : "", sz);
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

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    av_set_pts_info(st, 64, 1, 1000);
    st->duration = ModPlug_GetLength(modplug->f);
    st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id    = CODEC_ID_PCM_S16LE;
    st->codec->channels    = settings.mChannels;
    st->codec->sample_rate = settings.mFrequency;

    return modplug_load_metadata(s);
}

static int modplug_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ModPlugContext *modplug = s->priv_data;

    if (av_new_packet(pkt, 512) < 0)
        return AVERROR(ENOMEM);

    pkt->size = ModPlug_Read(modplug->f, pkt->data, 512);
    if (pkt->size <= 0) {
        av_free_packet(pkt);
        return pkt->size == 0 ? AVERROR_EOF : AVERROR(EIO);
    }
    return 0;
}

static int modplug_read_close(AVFormatContext *s)
{
    ModPlugContext *modplug = s->priv_data;
    ModPlug_Unload(modplug->f);
    return 0;
}

static int modplug_read_seek(AVFormatContext *s, int stream_idx, int64_t ts, int flags)
{
    const ModPlugContext *modplug = s->priv_data;
    ModPlug_Seek(modplug->f, (int)ts);
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
    .read_header    = modplug_read_header,
    .read_packet    = modplug_read_packet,
    .read_close     = modplug_read_close,
    .read_seek      = modplug_read_seek,
    .extensions     = "669,abc,amf,ams,dbm,dmf,dsm,far,it,mdl,med,mid,mod,mt2,mtm,okt,psm,ptm,s3m,stm,ult,umx,xm"
                      ",itgz,itr,itz,mdgz,mdr,mdz,s3gz,s3r,s3z,xmgz,xmr,xmz", // compressed mods
    .priv_class     = &modplug_class,
};
