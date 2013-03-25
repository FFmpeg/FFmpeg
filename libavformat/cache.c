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

/**
 * @TODO
 *      support non continuous caching
 *      support keeping files
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <pthread.h>
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/file.h"
#include "avformat.h"
#include "os_support.h"
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
  char *path;
  int fdw, fdr;
  int64_t begin;
  int64_t length;
  int64_t position;
  struct Segment *next;
} Segment;

typedef struct Context {
  URLContext *inner;
  char *cache_dir;
  Segment *seg;
  int8_t cache_fill;
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} Context;

#define HEADER (sizeof(uint64_t)*2)

static void* cache_fill_thread(void* arg)
{
  char cache_path[128];
  uint8_t buf[1024];
  int r, r_;
  Context *c = (Context *)arg;
  Segment *seg = (Segment *)av_mallocz(sizeof(Segment));

  snprintf(cache_path, 128, "%s/seg%p", c->cache_dir, seg);
  seg->path = av_strdup(cache_path);
  seg->begin = 0;
  seg->length = 0;
  seg->position = 0;
  seg->fdw = open(cache_path, O_RDWR | O_BINARY | O_CREAT | O_EXCL, 0600);
  seg->fdr = open(cache_path, O_RDWR | O_BINARY, 0600);
  write(seg->fdw, &(seg->begin), sizeof(uint64_t));
  write(seg->fdw, &(seg->length), sizeof(uint64_t));
  lseek(seg->fdr, HEADER, SEEK_SET);

  c->seg = seg;

  while (c->cache_fill) {
    pthread_mutex_lock(&c->mutex);
    r = ffurl_read(c->inner, buf, 1024);
    if(r > 0){
      r_ = write(seg->fdw, buf, r);
      av_assert0(r_ == r);
      seg->length += r;
    }
    pthread_mutex_unlock(&c->mutex);
  }

  while (seg) {
    Segment* p = seg->next;
    close(seg->fdw);
    close(seg->fdr);
    unlink(seg->path);
    av_free(seg->path);
    av_free(seg);
    seg = p;
  }
  return NULL;
}

static int cache_open(URLContext *h, const char *arg, int flags)
{
  char *url;
  int dlen, opened;

  Context *c= h->priv_data;

  arg = (strchr(arg, ':')) + 1;
  url = (strchr(arg, ':')) + 1;
  dlen = strlen(arg) - strlen(url);
  c->cache_dir = av_mallocz(sizeof(char) * dlen);
  av_strlcpy(c->cache_dir, arg, dlen);
  av_log(NULL, AV_LOG_INFO, "cache_open: %s, %s\n", c->cache_dir, url);

  opened = ffurl_open(&c->inner, url, flags, &h->interrupt_callback, NULL);
  if (opened == 0) {
    c->cache_fill = 1;
    pthread_mutex_init(&c->mutex, NULL);
    pthread_cond_init(&c->cond, NULL);
    pthread_create(&c->thread, NULL, cache_fill_thread, c);
    av_log(NULL, AV_LOG_INFO, "cache_open: %s, %s, %d\n", c->cache_dir, url, opened);
  }

  return opened;
}

static int cache_read(URLContext *h, unsigned char *buf, int size)
{
  Context *c= h->priv_data;
  Segment* seg = c->seg;
  int len = 0;

  while (seg->length <= seg->position) {
    usleep(100);
    continue;
  }

  len = read(seg->fdr, buf, FFMIN(size, seg->length - seg->position));
  if (len > 0) {
    seg->position += len;
  }

  return (-1 == len) ? AVERROR(errno) : len;
}

static int64_t cache_seek(URLContext *h, int64_t position, int whence)
{
  Context *c= h->priv_data;
  Segment* seg = c->seg;

  if (whence == AVSEEK_SIZE) {
    return ffurl_seek(c->inner, position, whence);
  }

  pthread_mutex_lock(&c->mutex);
  if (position < seg->begin || position > seg->begin + seg->length) {
    seg->begin = ffurl_seek(c->inner, position, whence);
    seg->length = 0;
    seg->position = 0;
    position = seg->begin;
    lseek(seg->fdw, 0, SEEK_SET);
    write(seg->fdw, &(seg->begin), sizeof(uint64_t));
    write(seg->fdw, &(seg->length), sizeof(uint64_t));
    lseek(seg->fdr, HEADER, SEEK_SET);
  } else {
    seg->position = lseek(seg->fdr, position - seg->begin + HEADER, whence) - HEADER;
  }
  pthread_mutex_unlock(&c->mutex);

  return position;
}

static int cache_close(URLContext *h)
{
  Context *c= h->priv_data;

  if (c->cache_fill) {
    c->cache_fill = 0;
    pthread_join(c->thread, NULL);
    pthread_mutex_destroy(&c->mutex);
    pthread_cond_destroy(&c->cond);
  }

  ffurl_close(c->inner);
  av_free(c->cache_dir);

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
