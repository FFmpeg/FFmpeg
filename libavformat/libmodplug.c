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
* @todo ModPlug options (noise reduction, reverb, bass boost, ...)
* @todo metadata
*/

#include <libmodplug/modplug.h>
#include "avformat.h"

typedef struct ModPlugContext {
    ModPlugFile *f;
    uint8_t buf[5 * 1<<20]; ///< input file content, 5M max
} ModPlugContext;

static int modplug_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVStream *st;
    AVIOContext *pb = s->pb;
    ModPlug_Settings settings;
    ModPlugContext *modplug = s->priv_data;

    int sz = avio_read(pb, modplug->buf, sizeof(modplug->buf));

    ModPlug_GetSettings(&settings);
    settings.mChannels       = 2;
    settings.mBits           = 16;
    settings.mFrequency      = 44100;
    settings.mResamplingMode = MODPLUG_RESAMPLE_FIR; // best quality
    settings.mLoopCount      = 0; // prevents looping forever
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
    return 0;
}

static int modplug_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, n;
    ModPlugContext *modplug = s->priv_data;
    uint8_t buf[512];

    n = ModPlug_Read(modplug->f, buf, sizeof(buf));
    if (n <= 0)
        return AVERROR(EIO);

    ret = av_new_packet(pkt, n);
    if (ret)
        return ret;
    pkt->pts  = pkt->dts = AV_NOPTS_VALUE;
    pkt->size = n;
    memcpy(pkt->data, buf, n);
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
};
