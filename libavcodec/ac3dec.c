/*
 * AC3 decoder
 * Copyright (c) 2001 Fabrice Bellard.
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
 * @file ac3dec.c
 * AC3 decoder.
 */

//#define DEBUG

#include "avcodec.h"
#include "libac3/ac3.h"

/* currently, I use libac3 which is Copyright (C) Aaron Holtzman and
   released under the GPL license. I may reimplement it someday... */
typedef struct AC3DecodeState {
    uint8_t inbuf[4096]; /* input buffer */
    uint8_t *inbuf_ptr;
    int frame_size;
    int flags;
    int channels;
    ac3_state_t state;
} AC3DecodeState;

static int ac3_decode_init(AVCodecContext *avctx)
{
    AC3DecodeState *s = avctx->priv_data;

    ac3_init ();
    s->inbuf_ptr = s->inbuf;
    s->frame_size = 0;
    return 0;
}

stream_samples_t samples;

/**** the following two functions comes from ac3dec */
static inline int blah (int32_t i)
{
    if (i > 0x43c07fff)
	return 32767;
    else if (i < 0x43bf8000)
	return -32768;
    else
	return i - 0x43c00000;
}

static inline void float_to_int (float * _f, int16_t * s16, int nchannels)
{
    int i, j, c;
    int32_t * f = (int32_t *) _f;	// XXX assumes IEEE float format

    j = 0;
    nchannels *= 256;
    for (i = 0; i < 256; i++) {
	for (c = 0; c < nchannels; c += 256)
	    s16[j++] = blah (f[i + c]);
    }
}

/**** end */

#define HEADER_SIZE 7

static int ac3_decode_frame(AVCodecContext *avctx, 
                            void *data, int *data_size,
                            uint8_t *buf, int buf_size)
{
    AC3DecodeState *s = avctx->priv_data;
    uint8_t *buf_ptr;
    int flags, i, len;
    int sample_rate, bit_rate;
    short *out_samples = data;
    float level;
    static const int ac3_channels[8] = {
	2, 1, 2, 3, 3, 4, 4, 5
    };

    buf_ptr = buf;
    while (buf_size > 0) {
        len = s->inbuf_ptr - s->inbuf;
        if (s->frame_size == 0) {
            /* no header seen : find one. We need at least 7 bytes to parse it */
            len = HEADER_SIZE - len;
            if (len > buf_size)
                len = buf_size;
            memcpy(s->inbuf_ptr, buf_ptr, len);
            buf_ptr += len;
            s->inbuf_ptr += len;
            buf_size -= len;
            if ((s->inbuf_ptr - s->inbuf) == HEADER_SIZE) {
                len = ac3_syncinfo (s->inbuf, &s->flags, &sample_rate, &bit_rate);
                if (len == 0) {
                    /* no sync found : move by one byte (inefficient, but simple!) */
                    memcpy(s->inbuf, s->inbuf + 1, HEADER_SIZE - 1);
                    s->inbuf_ptr--;
                } else {
		    s->frame_size = len;
                    /* update codec info */
                    avctx->sample_rate = sample_rate;
                    s->channels = ac3_channels[s->flags & 7];
                    if (s->flags & AC3_LFE)
			s->channels++;
		    if (avctx->channels == 0)
			/* No specific number of channel requested */
			avctx->channels = s->channels;
		    else if (s->channels < avctx->channels) {
			fprintf(stderr, "ac3dec: AC3 Source channels are less than specified: output to %d channels.. (frmsize: %d)\n", s->channels, len);
			avctx->channels = s->channels;
		    }
		    avctx->bit_rate = bit_rate;
                }
            }
        } else if (len < s->frame_size) {
            len = s->frame_size - len;
            if (len > buf_size)
                len = buf_size;

            memcpy(s->inbuf_ptr, buf_ptr, len);
            buf_ptr += len;
            s->inbuf_ptr += len;
            buf_size -= len;
        } else {
            flags = s->flags;
            if (avctx->channels == 1)
                flags = AC3_MONO;
            else if (avctx->channels == 2)
                flags = AC3_STEREO;
            else
                flags |= AC3_ADJUST_LEVEL;
            level = 1;
            if (ac3_frame (&s->state, s->inbuf, &flags, &level, 384)) {
            fail:
                s->inbuf_ptr = s->inbuf;
                s->frame_size = 0;
                continue;
            }
            for (i = 0; i < 6; i++) {
                if (ac3_block (&s->state))
                    goto fail;
                float_to_int (*samples, out_samples + i * 256 * avctx->channels, avctx->channels);
            }
            s->inbuf_ptr = s->inbuf;
            s->frame_size = 0;
            *data_size = 6 * avctx->channels * 256 * sizeof(int16_t);
            break;
        }
    }
    return buf_ptr - buf;
}

static int ac3_decode_end(AVCodecContext *s)
{
    return 0;
}

AVCodec ac3_decoder = {
    "ac3",
    CODEC_TYPE_AUDIO,
    CODEC_ID_AC3,
    sizeof(AC3DecodeState),
    ac3_decode_init,
    NULL,
    ac3_decode_end,
    ac3_decode_frame,
};
