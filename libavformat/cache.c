/*
 * Input cache protocol.
 * Copyright (c) 2011 Michael Niedermayer
 * Copyright (c) 2013 Cedric Fung
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
 *
 * Based on file.c by Fabrice Bellard
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <pthread.h>
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/file.h"
#include "os_support.h"
#include "avformat.h"
#include "url.h"

#if HAVE_SETMODE
#include <io.h>
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_EXCL
#define O_EXCL 0
#endif

typedef struct Segment {
  int64_t begin;
  int64_t end;
  struct Segment *next;
} Segment;

typedef struct Context {
  URLContext *inner;
  Segment *segs;
  Segment *seg;
  char *cache_path;
  int fdw, fdr;
  int64_t position;
  int8_t cache_fill;
  pthread_t thread;
  pthread_mutex_t mutex;
} Context;


static Segment *segments_contains(Segment *segs, int64_t position)
{
  Segment *seg = segs;
  while (seg) {
    if (seg->begin <= position && seg->end >= position) {
      return seg;
    }
    seg = seg->next;
  }
  return NULL;
}

static void segments_insert(Segment **segs, Segment *seg)
{
  Segment *root = *segs;
  if (root == NULL) {
    *segs = seg;
  }
  while (root) {
    Segment *next = root->next;
    if (root->begin < seg->begin) {
      seg->next = root;
      *segs = seg;
    } else if (root->end < seg->begin && (!next || seg->begin < next->begin)) {
      root->next = seg;
      seg->next = next;
    }
    root = next;
  }
}

static void segments_balance(Context *c, Segment *seg)
{
  Segment *next = seg->next;
  if (next && seg->end >= next->begin) {
    seg->end = ffurl_seek(c->inner, FFMAX(next->end, seg->end), SEEK_SET);
    lseek(c->fdw, seg->end, SEEK_SET);
    seg->next = next->next;
    av_free(next);
  }
}

static void segments_free(Segment *segs)
{
  while (segs) {
    Segment *next = segs->next;
    av_free(segs);
    segs = next;
  }
}

static void* cache_fill_thread(void* arg)
{
  uint8_t buf[1024];
  int r, r_;
  Context *c = (Context *)arg;

  while (c->cache_fill) {
    pthread_mutex_lock(&c->mutex);
    r = ffurl_read(c->inner, buf, 1024);
    if(r > 0){
      r_ = write(c->fdw, buf, r);
      av_assert0(r_ == r);
      c->seg->end += r;
      segments_balance(c, c->seg);
    } else {
      usleep(500);
    }
    pthread_mutex_unlock(&c->mutex);
  }

  return NULL;
}

static Segment *segment_alloc(void)
{
  Segment *seg = (Segment *)av_mallocz(sizeof(Segment));

  seg->begin = 0;
  seg->end = 0;
  return seg;
}

static int cache_open(URLContext *h, const char *arg, int flags)
{
  char *url;
  int dlen, opened;
  int64_t vlen = 0;
  Context *c= h->priv_data;

  arg = strchr(arg, ':') + 1;
  url = strchr(arg, ':') + 1;
  dlen = strlen(arg) - strlen(url);
  c->cache_path = av_mallocz(sizeof(char) * dlen);
  av_strlcpy(c->cache_path, arg, dlen);

  av_log(NULL, AV_LOG_INFO, "cache_open: %s, %s\n", c->cache_path, url);

  opened = ffurl_open(&c->inner, url, flags, &h->interrupt_callback, NULL);
  if (opened == 0) {
    vlen = ffurl_size(c->inner);
    if (vlen > 0) {
      c->fdw = open(c->cache_path, O_RDWR | O_BINARY | O_CREAT, 0600);
      c->fdr = open(c->cache_path, O_RDWR | O_BINARY, 0600);
      if (ftruncate64(c->fdw, vlen) == 0) {
        c->seg = segment_alloc();
        segments_insert(&c->segs, c->seg);
        pthread_mutex_init(&c->mutex, NULL);
        pthread_create(&c->thread, NULL, cache_fill_thread, c);
        c->cache_fill = 1;
        av_log(NULL, AV_LOG_INFO, "cache_open: %s, %s, %d, %lld\n", c->cache_path, url, opened, (long long)vlen);
      } else {
        close(c->fdw);
        close(c->fdr);
      }
    }
  }

  return opened;
}

static int cache_read(URLContext *h, unsigned char *buf, int size)
{
  Context *c= h->priv_data;
  Segment* seg = c->seg;
  int len = 0;

  if (c->cache_fill) {
    while (seg->begin > c->position || c->position >= seg->end) {
      usleep(500);
      seg = c->seg;
      continue;
    }

    len = read(c->fdr, buf, FFMIN(size, seg->end - c->position));
    if (len > 0) {
      c->position += len;
    }
  } else {
    len = ffurl_read(c->inner, buf, size);
  }

  return (-1 == len) ? AVERROR(errno) : len;
}

static int64_t cache_seek(URLContext *h, int64_t position, int whence)
{
  Context *c= h->priv_data;
  Segment *candi = NULL;

  if (!c->cache_fill || whence == AVSEEK_SIZE) {
    return ffurl_seek(c->inner, position, whence);
  }

  pthread_mutex_lock(&c->mutex);
  candi = segments_contains(c->segs, position);
  if (!candi) {
    candi = segment_alloc();
    segments_insert(&c->segs, candi);
    candi->begin = ffurl_seek(c->inner, position, whence);
    candi->end = candi->begin;
    c->position = candi->begin;
    lseek(c->fdw, c->position, SEEK_SET);
    lseek(c->fdr, c->position, SEEK_SET);
    c->seg = candi;
  } else if (candi == c->seg) {
    c->position = lseek(c->fdr, position, whence);
  } else {
    int64_t wp = ffurl_seek(c->inner, candi->end, whence);
    lseek(c->fdw, wp, SEEK_SET);
    c->position = lseek(c->fdr, position, whence);
    c->seg = candi;
  }
  pthread_mutex_unlock(&c->mutex);

  return c->position;
}

static int cache_close(URLContext *h)
{
  Context *c= h->priv_data;

  if (c->cache_fill) {
    c->cache_fill = 0;
    pthread_join(c->thread, NULL);
    pthread_mutex_destroy(&c->mutex);
    segments_free(c->segs);
  }

  ffurl_close(c->inner);
  av_free(c->cache_path);

  return 0;
}

URLProtocol ff_cache_protocol = {
  .name                = "cache",
  .url_open            = cache_open,
  .url_read            = cache_read,
  .url_seek            = cache_seek,
  .url_close           = cache_close,
  .priv_data_size      = sizeof(Context),
};
