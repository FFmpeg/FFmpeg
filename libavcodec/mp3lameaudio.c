/*
 * Interface to libmp3lame for mp3 encoding
 * Copyright (c) 2002 Lennert Buytenhek <buytenh@gnu.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "avcodec.h"
#include <math.h>
#include "mpegaudio.h"
#include <lame/lame.h>

typedef struct Mp3AudioContext {
	lame_global_flags *gfp;
	int first_frame;
	int stereo;
} Mp3AudioContext;


static int MP3lame_encode_init(AVCodecContext *avctx)
{
	Mp3AudioContext *s = avctx->priv_data;

	if (avctx->channels > 2)
		return -1;

	s->first_frame = 1;
	s->stereo = avctx->channels > 1 ? 1 : 0;

	if ((s->gfp = lame_init()) == NULL)
		goto err;
	lame_set_in_samplerate(s->gfp, avctx->sample_rate);
	lame_set_num_channels(s->gfp, avctx->channels);
	/* lame 3.91 dies on quality != 5 */
	lame_set_quality(s->gfp, 5);
	/* lame 3.91 doesn't work in mono */
	lame_set_mode(s->gfp, JOINT_STEREO);
	lame_set_brate(s->gfp, avctx->bit_rate/1000);
	if (lame_init_params(s->gfp) < 0)
		goto err_close;

	avctx->frame_size = MPA_FRAME_SIZE;
	avctx->key_frame = 1;

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

	/* lame 3.91 outputs the first frame as garbage */
	if (s->first_frame)
		s->first_frame = num = 0;

	return num;
}

int MP3lame_encode_close(AVCodecContext *avctx)
{
	Mp3AudioContext *s = avctx->priv_data;

	lame_close(s->gfp);
	return 0;
}


AVCodec mp3lame_encoder = {
    "mp3",
    CODEC_TYPE_AUDIO,
    CODEC_ID_MP3LAME,
    sizeof(Mp3AudioContext),
    MP3lame_encode_init,
    MP3lame_encode_frame,
    MP3lame_encode_close
};
