/*
 * Linux audio play and grab interface
 * Copyright (c) 2000, 2001 Gerard Lantau.
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <linux/soundcard.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/time.h>

#include "avformat.h"

const char *audio_device = "/dev/dsp";

typedef struct {
    int fd;
    int rate;
    int channels;
} AudioData;

#define AUDIO_BLOCK_SIZE 4096

/* audio read support */

static int audio_read(URLContext *h, UINT8 *buf, int size)
{
    AudioData *s = h->priv_data;
    int ret;

    ret = read(s->fd, buf, size);
    if (ret < 0)
        return -errno;
    else
        return ret;
}

static int audio_write(URLContext *h, UINT8 *buf, int size)
{
    AudioData *s = h->priv_data;
    int ret;

    ret = write(s->fd, buf, size);
    if (ret < 0)
        return -errno;
    else
        return ret;
}

static int audio_get_format(URLContext *h, URLFormat *f)
{
    AudioData *s = h->priv_data;

    strcpy(f->format_name, "pcm");
    f->sample_rate = s->rate;
    f->channels = s->channels;
    return 0;
}

/* URI syntax: 'audio:[rate[,channels]]' 
   default: rate=44100, channels=2
 */
static int audio_open(URLContext *h, const char *uri, int flags)
{
    AudioData *s;
    const char *p;
    int freq, channels, audio_fd;
    int tmp, err;

    h->is_streamed = 1;
    h->packet_size = AUDIO_BLOCK_SIZE;

    s = malloc(sizeof(AudioData));
    if (!s)
        return -ENOMEM;
    h->priv_data = s;

    /* extract parameters */
    p = uri;
    strstart(p, "audio:", &p);
    freq = strtol(p, (char **)&p, 0);
    if (freq <= 0)
        freq = 44100;
    if (*p == ',')
        p++;
    channels = strtol(p, (char **)&p, 0);
    if (channels <= 0)
            channels = 2;
    s->rate = freq;
    s->channels = channels;
    
    /* open linux audio device */
    if (flags & URL_WRONLY) 
        audio_fd = open(audio_device,O_WRONLY);
    else
        audio_fd = open(audio_device,O_RDONLY);
    if (audio_fd < 0) {
        perror(audio_device);
        return -EIO;
    }

    /* non blocking mode */
    fcntl(audio_fd, F_SETFL, O_NONBLOCK);

#if 0
    tmp=(NB_FRAGMENTS << 16) | FRAGMENT_BITS;
    err=ioctl(audio_fd, SNDCTL_DSP_SETFRAGMENT, &tmp);
    if (err < 0) {
        perror("SNDCTL_DSP_SETFRAGMENT");
    }
#endif

    tmp=AFMT_S16_LE;
    err=ioctl(audio_fd,SNDCTL_DSP_SETFMT,&tmp);
    if (err < 0) {
        perror("SNDCTL_DSP_SETFMT");
        goto fail;
    }
    
    tmp= (channels == 2);
    err=ioctl(audio_fd,SNDCTL_DSP_STEREO,&tmp);
    if (err < 0) {
        perror("SNDCTL_DSP_STEREO");
        goto fail;
    }
    
    tmp = freq;
    err=ioctl(audio_fd, SNDCTL_DSP_SPEED, &tmp);
    if (err < 0) {
        perror("SNDCTL_DSP_SPEED");
        goto fail;
    }

    s->rate = tmp;
    s->fd = audio_fd;

    return 0;
 fail:
    close(audio_fd);
    free(s);
    return -EIO;
}

static int audio_close(URLContext *h)
{
    AudioData *s = h->priv_data;

    close(s->fd);
    free(s);
    return 0;
}

URLProtocol audio_protocol = {
    "audio",
    audio_open,
    audio_read,
    audio_write,
    NULL, /* seek */
    audio_close,
    audio_get_format,
};
