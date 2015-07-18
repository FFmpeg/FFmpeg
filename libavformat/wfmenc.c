/*
 * WFM JSON
 * Copyright (c) 2014 Andrejus Balciunas, based on md5enc (c) 2009 Reimar Döffinger
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

#include "libavutil/opt.h"
#include "avformat.h"
#include "internal.h"
#include <math.h>

#define MAX(a,b) ((a) > (b) ? a : b)
#define MIN(a,b) ((a) < (b) ? a : b)

typedef struct WFMContext {
	const AVClass *class;
    int moov_commit_period;

    int moov_commit_on_next_keyframe;
    int header_written;
    AVIOContext *pb;
    int64_t duration;
} WFMContext;

static const AVOption wfm_options[] = {
	{ "moov_commit_period", "MOOV commit period (seconds)", offsetof(WFMContext, moov_commit_period), AV_OPT_TYPE_INT, {.dbl = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
    { NULL },
};

static float clamp(const float x, const float min, const float max) {
	return MAX(min, MIN(max, x));
}

static float float2db(float x)
{
	x = fabs(x);
	return (x > 0.0f) ? 20.0f * log10(x) : -9999.9f;
}

static float map2range(float x, float in_min, float in_max, float out_min, float out_max)
{
	return clamp(out_min + (out_max - out_min) * (x - in_min) / (in_max - in_min), out_min, out_max);
}

static int write_header(struct AVFormatContext *s)
{
	int i,found;
	struct WFMContext *c;

    found = 0;
    for (i = 0; i < s->nb_streams; i ++) {
    	AVStream* stream = s->streams[i];
    	avpriv_set_pts_info(stream,64,1,90000);
    	if (stream->codec->codec_type != AVMEDIA_TYPE_AUDIO)
    		continue;
    	if (stream->codec->codec_id != AV_CODEC_ID_PCM_S16LE) {
    		av_log(s, AV_LOG_ERROR, "Only pcm_s16le codec supported\n");
    		return AVERROR(EINVAL);
    	}
    	found = 1;
    }

    if (found == 0) {
    	av_log(s, AV_LOG_ERROR, "No audio stream found\n");
    	return AVERROR(EINVAL);
    }

    c = s->priv_data;

    c->duration = 0;
    c->moov_commit_on_next_keyframe = 0;
    c->header_written = 0;

    return 0;
}

static int write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
	struct WFMContext *c;
	AVCodecContext* codec;
	int i,k,buf_size;
	float pts,dbMin,dbMax;
	AVIOContext *pb;
	uint8_t *buf;
	int64_t pos;

	c = s->priv_data;
	codec = s->streams[pkt->stream_index]->codec;

	dbMin = -48.0f;
	dbMax = 0.0f;

	if (c->moov_commit_period) {
		// try to flush buffer
        if (codec->codec_type == AVMEDIA_TYPE_VIDEO || s->nb_streams == 1) {
         	// check if should commit
            if (c->duration != 0 && (c->duration % (c->moov_commit_period * 1000000)) < 1000000) {
              	c->moov_commit_on_next_keyframe = 1;
              	av_log(s, AV_LOG_DEBUG, "should commit at %ld\n",c->duration);
            } else if (c->moov_commit_on_next_keyframe == 1 && (pkt->flags & AV_PKT_FLAG_KEY) && c->pb) {
            	// commit if should
              	av_log(s, AV_LOG_DEBUG, "commit at %ld\n",c->duration);
               	c->moov_commit_on_next_keyframe = 0;
               	// flush buffer
               	buf_size = avio_close_dyn_buf(c->pb, &buf);
               	c->pb = NULL;
               	avio_write(s->pb, buf, buf_size);
              	av_free(buf);
               	// remember pos
               	pos = avio_tell(s->pb);
               	// write trailer
               	avio_printf(s->pb, "\n]");
            	// flush
               	avio_flush(s->pb);
               	// go back before trailer
                avio_seek(s->pb, pos, SEEK_SET);
            }
        }
        // check/create buffer
        if (!c->pb) {
          	int ret;
            if ((ret = avio_open_dyn_buf(&c->pb)) < 0)
            	return ret;
        }
        // use buffer
        pb = c->pb;
	} else {
		// use direct
		pb = s->pb;
	}

	// update duration
	if (codec->codec_type == AVMEDIA_TYPE_VIDEO || s->nb_streams == 1)
		c->duration += av_rescale_q(pkt->duration,s->streams[pkt->stream_index]->time_base,AV_TIME_BASE_Q);

	// skip non-audio
    if (codec->codec_type != AVMEDIA_TYPE_AUDIO)
    	return 0;

    // write trailer
    if (c->header_written == 1) {
    	avio_printf(pb, ",\n");
    } else {
    	avio_printf(pb, "[\n");
    	c->header_written = 1;
    }

    // write pts
    pts = (float)pkt->pts / (float)codec->sample_rate;
    avio_printf(pb, "[ %.2f,",pts);
    // write stats
    for (i = 0; i < codec->channels; i++) {
    	// find sample sum
    	float rms,result;
    	float sum = 0;
    	for (k = 0; k < pkt->duration; k++) {
    		int16_t sample = *((int16_t*)(pkt->data + (i + k * codec->channels)*2));
    		sum += pow((float) sample, 2);
    	}
    	// find rms
    	rms = sqrt(sum / (float) pkt->duration);
    	result = map2range(float2db(rms / (float) (1 << (sizeof(short) * 8 - 1))), dbMin, dbMax, 0, 1) * 100;
    	// write rms
    	avio_printf(pb, "%2.0f",result);
    	if (i < codec->channels -1)
    		avio_printf(pb, ",");
    }
    // write trailer
    avio_printf(pb, " ]");

    return 0;
}

static int write_trailer(struct AVFormatContext *s)
{
	struct WFMContext *c;
	c = s->priv_data;
	if (c->moov_commit_period && c->pb) {
		uint8_t *buf;
		int buf_size;
		avio_printf(c->pb, "\n]");
		buf_size = avio_close_dyn_buf(c->pb, &buf);
		c->pb = NULL;
		avio_write(s->pb, buf, buf_size);
		av_free(buf);
	} else {
		avio_printf(s->pb, "\n]");
	}
    return 0;
}

static const AVClass wfm_class = {
    .class_name = "wfm json encoder class",
    .item_name  = av_default_item_name,
    .option     = wfm_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_wfm_muxer = {
    .name              = "wfm",
    .long_name         = NULL_IF_CONFIG_SMALL("Per-frame WFM JSON"),
    .priv_data_size    = sizeof(struct WFMContext),
    .audio_codec       = AV_CODEC_ID_PCM_S16LE,
    .video_codec       = AV_CODEC_ID_RAWVIDEO,
    .write_header      = write_header,
    .write_packet      = write_packet,
    .write_trailer     = write_trailer,
    .flags             = AVFMT_VARIABLE_FPS | AVFMT_TS_NONSTRICT,
    .priv_class        = &wfm_class,
};
