/*
 * Sample rate convertion for both audio and video
 * Copyright (c) 2000 Fabrice Bellard.
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
 * @file resample.c
 * Sample rate convertion for both audio and video.
 */

#include "avcodec.h"

typedef struct {
    /* fractional resampling */
    uint32_t incr; /* fractional increment */
    uint32_t frac;
    int last_sample;
    /* integer down sample */
    int iratio;  /* integer divison ratio */
    int icount, isum;
    int inv;
} ReSampleChannelContext;

struct ReSampleContext {
    ReSampleChannelContext channel_ctx[2];
    float ratio;
    /* channel convert */
    int input_channels, output_channels, filter_channels;
};


#define FRAC_BITS 16
#define FRAC (1 << FRAC_BITS)

static void init_mono_resample(ReSampleChannelContext *s, float ratio)
{
    ratio = 1.0 / ratio;
    s->iratio = (int)floorf(ratio);
    if (s->iratio == 0)
        s->iratio = 1;
    s->incr = (int)((ratio / s->iratio) * FRAC);
    s->frac = FRAC;
    s->last_sample = 0;
    s->icount = s->iratio;
    s->isum = 0;
    s->inv = (FRAC / s->iratio);
}

/* fractional audio resampling */
static int fractional_resample(ReSampleChannelContext *s, short *output, short *input, int nb_samples)
{
    unsigned int frac, incr;
    int l0, l1;
    short *q, *p, *pend;

    l0 = s->last_sample;
    incr = s->incr;
    frac = s->frac;

    p = input;
    pend = input + nb_samples;
    q = output;

    l1 = *p++;
    for(;;) {
        /* interpolate */
        *q++ = (l0 * (FRAC - frac) + l1 * frac) >> FRAC_BITS;
        frac = frac + s->incr;
        while (frac >= FRAC) {
            frac -= FRAC;
            if (p >= pend)
                goto the_end;
            l0 = l1;
            l1 = *p++;
        }
    }
 the_end:
    s->last_sample = l1;
    s->frac = frac;
    return q - output;
}

static int integer_downsample(ReSampleChannelContext *s, short *output, short *input, int nb_samples)
{
    short *q, *p, *pend;
    int c, sum;

    p = input;
    pend = input + nb_samples;
    q = output;

    c = s->icount;
    sum = s->isum;

    for(;;) {
        sum += *p++;
        if (--c == 0) {
            *q++ = (sum * s->inv) >> FRAC_BITS;
            c = s->iratio;
            sum = 0;
        }
        if (p >= pend)
            break;
    }
    s->isum = sum;
    s->icount = c;
    return q - output;
}

/* n1: number of samples */
static void stereo_to_mono(short *output, short *input, int n1)
{
    short *p, *q;
    int n = n1;

    p = input;
    q = output;
    while (n >= 4) {
        q[0] = (p[0] + p[1]) >> 1;
        q[1] = (p[2] + p[3]) >> 1;
        q[2] = (p[4] + p[5]) >> 1;
        q[3] = (p[6] + p[7]) >> 1;
        q += 4;
        p += 8;
        n -= 4;
    }
    while (n > 0) {
        q[0] = (p[0] + p[1]) >> 1;
        q++;
        p += 2;
        n--;
    }
}

/* n1: number of samples */
static void mono_to_stereo(short *output, short *input, int n1)
{
    short *p, *q;
    int n = n1;
    int v;

    p = input;
    q = output;
    while (n >= 4) {
        v = p[0]; q[0] = v; q[1] = v;
        v = p[1]; q[2] = v; q[3] = v;
        v = p[2]; q[4] = v; q[5] = v;
        v = p[3]; q[6] = v; q[7] = v;
        q += 8;
        p += 4;
        n -= 4;
    }
    while (n > 0) {
        v = p[0]; q[0] = v; q[1] = v;
        q += 2;
        p += 1;
        n--;
    }
}

/* XXX: should use more abstract 'N' channels system */
static void stereo_split(short *output1, short *output2, short *input, int n)
{
    int i;

    for(i=0;i<n;i++) {
        *output1++ = *input++;
        *output2++ = *input++;
    }
}

static void stereo_mux(short *output, short *input1, short *input2, int n)
{
    int i;

    for(i=0;i<n;i++) {
        *output++ = *input1++;
        *output++ = *input2++;
    }
}

static void ac3_5p1_mux(short *output, short *input1, short *input2, int n)
{
    int i;
    short l,r;

    for(i=0;i<n;i++) {
      l=*input1++;
      r=*input2++;
      *output++ = l;           /* left */
      *output++ = (l/2)+(r/2); /* center */
      *output++ = r;           /* right */
      *output++ = 0;           /* left surround */
      *output++ = 0;           /* right surroud */
      *output++ = 0;           /* low freq */
    }
}

static int mono_resample(ReSampleChannelContext *s, short *output, short *input, int nb_samples)
{
    short *buf1;
    short *buftmp;

    buf1= (short*)av_malloc( nb_samples * sizeof(short) );

    /* first downsample by an integer factor with averaging filter */
    if (s->iratio > 1) {
        buftmp = buf1;
        nb_samples = integer_downsample(s, buftmp, input, nb_samples);
    } else {
        buftmp = input;
    }

    /* then do a fractional resampling with linear interpolation */
    if (s->incr != FRAC) {
        nb_samples = fractional_resample(s, output, buftmp, nb_samples);
    } else {
        memcpy(output, buftmp, nb_samples * sizeof(short));
    }
    av_free(buf1);
    return nb_samples;
}

ReSampleContext *audio_resample_init(int output_channels, int input_channels, 
                                      int output_rate, int input_rate)
{
    ReSampleContext *s;
    int i;
    
    if ( input_channels > 2)
      {
	printf("Resampling with input channels greater than 2 unsupported.");
	return NULL;
      }

    s = av_mallocz(sizeof(ReSampleContext));
    if (!s)
      {
	printf("Can't allocate memory for resample context.");
	return NULL;
      }

    s->ratio = (float)output_rate / (float)input_rate;
    
    s->input_channels = input_channels;
    s->output_channels = output_channels;
    
    s->filter_channels = s->input_channels;
    if (s->output_channels < s->filter_channels)
        s->filter_channels = s->output_channels;

/*
 * ac3 output is the only case where filter_channels could be greater than 2.
 * input channels can't be greater than 2, so resample the 2 channels and then
 * expand to 6 channels after the resampling.
 */
    if(s->filter_channels>2)
      s->filter_channels = 2;

    for(i=0;i<s->filter_channels;i++) {
        init_mono_resample(&s->channel_ctx[i], s->ratio);
    }
    return s;
}

/* resample audio. 'nb_samples' is the number of input samples */
/* XXX: optimize it ! */
/* XXX: do it with polyphase filters, since the quality here is
   HORRIBLE. Return the number of samples available in output */
int audio_resample(ReSampleContext *s, short *output, short *input, int nb_samples)
{
    int i, nb_samples1;
    short *bufin[2];
    short *bufout[2];
    short *buftmp2[2], *buftmp3[2];
    int lenout;

    if (s->input_channels == s->output_channels && s->ratio == 1.0) {
        /* nothing to do */
        memcpy(output, input, nb_samples * s->input_channels * sizeof(short));
        return nb_samples;
    }

    /* XXX: move those malloc to resample init code */
    bufin[0]= (short*) av_malloc( nb_samples * sizeof(short) );
    bufin[1]= (short*) av_malloc( nb_samples * sizeof(short) );
    
    /* make some zoom to avoid round pb */
    lenout= (int)(nb_samples * s->ratio) + 16;
    bufout[0]= (short*) av_malloc( lenout * sizeof(short) );
    bufout[1]= (short*) av_malloc( lenout * sizeof(short) );

    if (s->input_channels == 2 &&
        s->output_channels == 1) {
        buftmp2[0] = bufin[0];
        buftmp3[0] = output;
        stereo_to_mono(buftmp2[0], input, nb_samples);
    } else if (s->output_channels >= 2 && s->input_channels == 1) {
        buftmp2[0] = input;
        buftmp3[0] = bufout[0];
    } else if (s->output_channels >= 2) {
        buftmp2[0] = bufin[0];
        buftmp2[1] = bufin[1];
        buftmp3[0] = bufout[0];
        buftmp3[1] = bufout[1];
        stereo_split(buftmp2[0], buftmp2[1], input, nb_samples);
    } else {
        buftmp2[0] = input;
        buftmp3[0] = output;
    }

    /* resample each channel */
    nb_samples1 = 0; /* avoid warning */
    for(i=0;i<s->filter_channels;i++) {
        nb_samples1 = mono_resample(&s->channel_ctx[i], buftmp3[i], buftmp2[i], nb_samples);
    }

    if (s->output_channels == 2 && s->input_channels == 1) {
        mono_to_stereo(output, buftmp3[0], nb_samples1);
    } else if (s->output_channels == 2) {
        stereo_mux(output, buftmp3[0], buftmp3[1], nb_samples1);
    } else if (s->output_channels == 6) {
        ac3_5p1_mux(output, buftmp3[0], buftmp3[1], nb_samples1);
    }

    av_free(bufin[0]);
    av_free(bufin[1]);

    av_free(bufout[0]);
    av_free(bufout[1]);
    return nb_samples1;
}

void audio_resample_close(ReSampleContext *s)
{
    av_free(s);
}
