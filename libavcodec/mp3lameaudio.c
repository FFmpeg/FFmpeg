/*
 * Interface to libmp3lame for mp3 encoding
 * Copyright (c) 2002 Lennert Buytenhek <buytenh@gnu.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
 
/**
 * @file mp3lameaudio.c
 * Interface to libmp3lame for mp3 encoding.
 */

#include "avcodec.h"
#include "mpegaudio.h"
#include <lame/lame.h>

typedef struct Mp3AudioContext {
	lame_global_flags *gfp;
	int stereo;
} Mp3AudioContext;


static int MP3lame_encode_init(AVCodecContext *avctx)
{
	Mp3AudioContext *s = avctx->priv_data;

	if (avctx->channels > 2)
		return -1;

	s->stereo = avctx->channels > 1 ? 1 : 0;

	if ((s->gfp = lame_init()) == NULL)
		goto err;
	lame_set_in_samplerate(s->gfp, avctx->sample_rate);
	lame_set_out_samplerate(s->gfp, avctx->sample_rate);
	lame_set_num_channels(s->gfp, avctx->channels);
	/* lame 3.91 dies on quality != 5 */
	lame_set_quality(s->gfp, 5);
	/* lame 3.91 doesn't work in mono */
	lame_set_mode(s->gfp, JOINT_STEREO);
	lame_set_brate(s->gfp, avctx->bit_rate/1000);
	if (lame_init_params(s->gfp) < 0)
		goto err_close;

	avctx->frame_size = MPA_FRAME_SIZE;
    
        avctx->coded_frame= avcodec_alloc_frame();
        avctx->coded_frame->key_frame= 1;

	return 0;

err_close:
	lame_close(s->gfp);
err:
	return -1;
}

int MP3lame_encode_frame(AVCodecContext *avctx,
                     unsigned char *frame, int buf_size, void *data)
{
	Mp3AudioContext *s = avctx->priv_data;
	int num;

	/* lame 3.91 dies on '1-channel interleaved' data */
	if (s->stereo) {
		num = lame_encode_buffer_interleaved(s->gfp, data,
			MPA_FRAME_SIZE, frame, buf_size);
	} else {
		num = lame_encode_buffer(s->gfp, data, data, MPA_FRAME_SIZE,
			frame, buf_size);
	}

	return num;
}

int MP3lame_encode_close(AVCodecContext *avctx)
{
	Mp3AudioContext *s = avctx->priv_data;
    
        av_freep(&avctx->coded_frame);

	lame_close(s->gfp);
	return 0;
}


AVCodec mp3lame_encoder = {
    "mp3",
    CODEC_TYPE_AUDIO,
    CODEC_ID_MP3,
    sizeof(Mp3AudioContext),
    MP3lame_encode_init,
    MP3lame_encode_frame,
    MP3lame_encode_close
};
