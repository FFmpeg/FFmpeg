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

#ifndef ftruncate64
#define ftruncate64 ftruncate
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
  int8_t cache_fill;
  char *cache_path;
  int fdw, fdr, fdi;
  int64_t position;
  int64_t length;
  pthread_t thread;
  pthread_mutex_t mutex;
} Context;

static Segment *segments_select(Segment **segs, int64_t position)
{
  Segment *next = *segs;

  if (next == NULL) {
    *segs = av_mallocz(sizeof(Segment));
    (*segs)->begin = (*segs)->end = position;
    return *segs;
  }

  while (next) {
    if (position < next->begin) {
      Segment *seg = av_mallocz(sizeof(Segment));
      seg->begin = seg->end = position;
      seg->next = next->next;
      *segs = seg;
    } else if (next->begin <= position && position <= next->end) {
      return next;
    } else if (next->end < position && (!next->next || position < next->next->begin)) {
      Segment *seg = av_mallocz(sizeof(Segment));
      seg->begin = seg->end = position;
      seg->next = next->next;
      next->next = seg;
    }
    next = next->next;
  }

  return NULL;
}

static void segments_balance(Context *c, Segment *seg)
{
  Segment *next = seg->next;
  int64_t pos = -1;
  if (next && seg->end >= next->begin) {
    seg->end = FFMAX(next->end, seg->end);
    pos = ffurl_seek(c->inner, seg->end, SEEK_SET);
    if (pos > 0) {
      seg->end = lseek(c->fdw, pos, SEEK_SET);
    } else {
      av_log(NULL, AV_LOG_ERROR, "balance: %"PRId64", %"PRId64", %"PRId64"\n", seg->begin, seg->end, pos);
    }
    seg->next = next->next;
    av_free(next);
  }
}

static void segments_dump(Segment *segs, int fd)
{
  lseek(fd, 0, SEEK_SET);
  ftruncate64(fd, 0);
  while (segs) {
    write(fd, &segs->begin, sizeof(int64_t));
    write(fd, &segs->end, sizeof(int64_t));
    segs = segs->next;
  }
}

static Segment *segments_load(int fd)
{
  Segment *seg = NULL, **next = &seg;
  int64_t begin = 0, end = 0;
  lseek(fd, 0, SEEK_SET);
  while (sizeof(int64_t) == read(fd, &begin, sizeof(int64_t)) &&
      sizeof(int64_t) == read(fd, &end, sizeof(int64_t))) {
    *next = av_mallocz(sizeof(Segment));
    (*next)->begin = begin;
    (*next)->end = end;
    av_log(NULL, AV_LOG_INFO, "[%"PRId64"-%"PRId64"]\t", begin, end);
    next = &((*next)->next);
  }
  av_log(NULL, AV_LOG_INFO, "\n");
  return seg;
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

  while (c->cache_fill && c->seg->end - c->seg->begin < c->length) {
    pthread_mutex_lock(&c->mutex);
    if (c->seg->end < c->length) {
      r = ffurl_read(c->inner, buf, 1024);
      if(r > 0){
        r_ = write(c->fdw, buf, r);
        av_assert0(r_ == r);
        c->seg->end += r_;
        segments_balance(c, c->seg);
      } else {
        usleep(200);
      }
    } else {
      usleep(200);
    }
    pthread_mutex_unlock(&c->mutex);
  }

  return NULL;
}

static int cache_open(URLContext *h, const char *arg, int flags)
{
  char *url;
  int dlen, opened;
  Context *c= h->priv_data;

  arg = strchr(arg, ':') + 1;
  url = strchr(arg, ':') + 1;
  dlen = strlen(arg) - strlen(url);
  c->cache_path = av_mallocz(sizeof(char) * dlen);
  av_strlcpy(c->cache_path, arg, dlen);
  av_log(NULL, AV_LOG_INFO, "cache_open: %s, %s\n", c->cache_path, url);

  opened = ffurl_open(&c->inner, url, flags, &h->interrupt_callback, NULL);
  if (opened != 0) {
    return opened;
  }

  c->length = ffurl_size(c->inner);
  if (c->length > 1048576) {
    c->fdw = open(c->cache_path, O_RDWR | O_BINARY | O_CREAT, 0600);
    c->fdr = open(c->cache_path, O_RDWR | O_BINARY, 0600);
    if (!c->inner->is_streamed && ftruncate64(c->fdw, c->length) == 0) {
      char index_path[dlen + 4];
      snprintf(index_path, dlen + 4, "%s.ssi", c->cache_path);
      c->fdi = open(index_path, O_RDWR | O_BINARY | O_CREAT, 0600);
      c->segs = segments_load(c->fdi);
      if (c->segs) {
        c->seg = c->segs;
      } else {
        c->seg = segments_select(&c->segs, 0);
      }
      pthread_mutex_init(&c->mutex, NULL);
      pthread_create(&c->thread, NULL, cache_fill_thread, c);
      c->cache_fill = 1;
      av_log(NULL, AV_LOG_INFO, "cache_open: %s, %s, %d, %"PRId64"\n", index_path, url, opened, c->length);
    } else {
      close(c->fdw);
      close(c->fdr);
    }
  }

  return 0;
}

static int cache_read(URLContext *h, unsigned char *buf, int size)
{
  Context *c= h->priv_data;
  Segment* seg = c->seg;
  int len = 0;

  if (c->cache_fill) {
    while (seg->begin > c->position || c->position >= seg->end) {
      usleep(200);
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
  candi = segments_select(&c->segs, position);
  if (candi == c->seg) {
    c->position = lseek(c->fdr, position, whence);
  } else {
    int64_t pos = ffurl_seek(c->inner, candi->end, whence);
    if (pos > 0) {
      candi->end = lseek(c->fdw, pos, SEEK_SET);
    } else {
      av_log(NULL, AV_LOG_ERROR, "cache_seek: %"PRId64", %"PRId64", %"PRId64"\n", position, candi->end, pos);
    }
    c->position = lseek(c->fdr, position, SEEK_SET);
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
    segments_dump(c->segs, c->fdi);
    pthread_mutex_destroy(&c->mutex);
    segments_free(c->segs);
    close(c->fdw);
    close(c->fdr);
    close(c->fdi);
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
