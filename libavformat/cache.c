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
#include "libavutil/opt.h"
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

#define BUFFER_SIZE 8192

typedef struct Segment {
  int64_t begin;
  int64_t end;
  struct Segment *next;
} Segment;

typedef struct CacheContext {
  const AVClass *class;
  URLContext *inner;
  Segment *segs;
  Segment *seg;
  int64_t *segs_flat;
  int num_segs;
  int8_t cache_fill;
  char *cache_path;
  int fdw, fdr, fdi;
  int64_t position;
  int64_t length;
  pthread_t thread;
  pthread_mutex_t mutex; // protect fdw and inner
  void (*callback)(int64_t *segs, int len);
} CacheContext;

static Segment *segments_select(CacheContext *c, Segment **segs, int64_t position)
{
  Segment *next = *segs;

  if (next == NULL) {
    *segs = av_mallocz(sizeof(Segment));
    (*segs)->begin = (*segs)->end = position;
    c->num_segs = 1;
    c->segs_flat = av_mallocz(sizeof(int64_t) * 2 * c->num_segs);
    return *segs;
  }

  while (next) {
    if (position < next->begin) {
      Segment *seg = av_mallocz(sizeof(Segment));
      seg->begin = seg->end = position;
      seg->next = next->next;
      *segs = seg;
      c->num_segs++;
      c->segs_flat = av_realloc(c->segs_flat, sizeof(int64_t) * 2 * c->num_segs);
    } else if (next->begin <= position && position <= next->end) {
      return next;
    } else if (next->end < position && (!next->next || position < next->next->begin)) {
      Segment *seg = av_mallocz(sizeof(Segment));
      seg->begin = seg->end = position;
      seg->next = next->next;
      next->next = seg;
      c->num_segs++;
      c->segs_flat = av_realloc(c->segs_flat, sizeof(int64_t) * 2 * c->num_segs);
    }
    next = next->next;
  }

  return NULL;
}

static void segments_balance(CacheContext *c, Segment *seg)
{
  Segment *next = seg->next;
  int64_t pos = -1;
  if (next && seg->end >= next->begin) {
    seg->end = FFMAX(next->end, seg->end);
    pos = ffurl_seek(c->inner, seg->end, SEEK_SET);
    if (pos > 0) {
      seg->end = lseek(c->fdw, pos, SEEK_SET); // should be protected
    } else {
      av_log(NULL, AV_LOG_ERROR, "balance: %"PRId64", %"PRId64", %"PRId64"\n", seg->begin, seg->end, pos);
    }
    seg->next = next->next;
    av_free(next);
    c->num_segs--;
    c->segs_flat = av_realloc(c->segs_flat, sizeof(int64_t) * 2 * c->num_segs);
  }
}

static void segments_dump(CacheContext *c, Segment *segs, int fd)
{
  lseek(fd, 0, SEEK_SET);
  ftruncate64(fd, 0);
  for (int i = 0; segs; i += 2) {
    write(fd, &segs->begin, sizeof(int64_t));
    write(fd, &segs->end, sizeof(int64_t));
    c->segs_flat[i] = segs->begin;
    c->segs_flat[i+1] = segs->end;
    segs = segs->next;
  }
}

static Segment *segments_load(CacheContext *c, int fd)
{
  Segment *seg = NULL, **next = &seg;
  int64_t begin = 0, end = 0;
  lseek(fd, 0, SEEK_SET);
  c->num_segs = 0;
  while (sizeof(int64_t) == read(fd, &begin, sizeof(int64_t)) &&
      sizeof(int64_t) == read(fd, &end, sizeof(int64_t))) {
    *next = av_mallocz(sizeof(Segment));
    (*next)->begin = begin;
    (*next)->end = end;
    av_log(NULL, AV_LOG_INFO, "[%"PRId64"-%"PRId64"]\t", begin, end);
    c->num_segs++;
    c->segs_flat = av_realloc(c->segs_flat, sizeof(int64_t) * 2 * c->num_segs);
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
  uint8_t buf[BUFFER_SIZE];
  int r, r_, len = 0;
  CacheContext *c = (CacheContext *)arg;

  while (c->cache_fill && c->seg->end - c->seg->begin < c->length) {
    if (c->seg->end < c->length) {
      pthread_mutex_lock(&c->mutex);
      r = ffurl_read(c->inner, buf, BUFFER_SIZE);
      if(r > 0){
        r_ = write(c->fdw, buf, r);
        av_assert0(r_ == r);
        c->seg->end += r_;
        len += r_;
        segments_balance(c, c->seg);
        if (len > 102400 && c->segs_flat && c->num_segs > 0 && c->callback) {
          segments_dump(c, c->segs, c->fdi);
          c->callback(c->segs_flat, c->num_segs);
          len = 0;
        }
        pthread_mutex_unlock(&c->mutex);
      } else {
        pthread_mutex_unlock(&c->mutex);
        usleep(100 * 1000);
      }
    } else {
      usleep(100 * 1000);
    }
  }

  if (len > 0 && c->segs_flat && c->num_segs > 0 && c->callback) {
    segments_dump(c, c->segs, c->fdi);
    c->callback(c->segs_flat, c->num_segs);
  }

  return NULL;
}

static int cache_open(URLContext *h, const char *arg, int flags)
{
  char *url;
  int dlen, opened;
  CacheContext *c= h->priv_data;

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
  if (!c->inner->is_streamed && c->length > 1048576) {
    c->fdw = open(c->cache_path, O_RDWR | O_BINARY | O_CREAT, 0600);
    c->fdr = open(c->cache_path, O_RDWR | O_BINARY, 0600);
    if (c->fdw != -1 && c->fdr != -1 && ftruncate64(c->fdw, c->length) == 0) {
      char index_path[dlen + 4];
      snprintf(index_path, dlen + 4, "%s.ssi", c->cache_path);
      c->fdi = open(index_path, O_RDWR | O_BINARY | O_CREAT, 0600);
      c->segs = segments_load(c, c->fdi);
      if (c->segs) {
        c->seg = c->segs;
        if (c->segs_flat && c->num_segs > 0 && c->callback) {
          segments_dump(c, c->segs, c->fdi);
          c->callback(c->segs_flat, c->num_segs);
        }
      } else {
        c->seg = segments_select(c, &c->segs, 0);
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
  CacheContext *c= h->priv_data;
  Segment* seg = c->seg;
  int len = 0;

  if (c->cache_fill) {
    while (seg->begin > c->position || c->position >= seg->end) {
      usleep(100 * 1000);
      seg = c->seg;
      continue;
    }

    len = read(c->fdr, buf, FFMIN(size, seg->end - c->position));
    if (len > 0) {
      c->position += len;
    }
    return (-1 == len) ? AVERROR(errno) : len;
  } else {
    return ffurl_read(c->inner, buf, size);
  }
}

static int64_t cache_seek(URLContext *h, int64_t position, int whence)
{
  CacheContext *c= h->priv_data;
  Segment *candi = NULL;

  if (!c->cache_fill || whence == AVSEEK_SIZE || position < 0) {
    return ffurl_seek(c->inner, position, whence);
  }

  candi = segments_select(c, &c->segs, position);
  if (candi == c->seg) {
    c->position = lseek(c->fdr, position, whence);
  } else {
    pthread_mutex_lock(&c->mutex);
    int64_t pos = ffurl_seek(c->inner, candi->end, whence);
    if (pos >= candi->begin) {
      candi->end = lseek(c->fdw, pos, SEEK_SET);
      c->position = lseek(c->fdr, position, SEEK_SET);
      c->seg = candi;
    } else {
      av_log(NULL, AV_LOG_ERROR, "cache_seek: %"PRId64", %"PRId64", %"PRId64"\n", position, candi->end, pos);
    }
    pthread_mutex_unlock(&c->mutex);
  }

  return c->position;
}

static int cache_close(URLContext *h)
{
  CacheContext *c= h->priv_data;

  if (c->cache_fill) {
    c->cache_fill = 0;
    pthread_join(c->thread, NULL);
    segments_dump(c, c->segs, c->fdi);
    pthread_mutex_destroy(&c->mutex);
    segments_free(c->segs);
    av_free(c->segs_flat);
    close(c->fdw);
    close(c->fdr);
    close(c->fdi);
  }

  ffurl_close(c->inner);
  av_free(c->cache_path);

  return 0;
}

static const AVOption options[] = {
  {"cache_clk", "callback when cache updated", offsetof(CacheContext, callback), AV_OPT_TYPE_INT, {0}, 0, INT_MAX},
  {NULL}
};

static const AVClass cache_context_class = {
  .class_name     = "cache",
  .item_name      = av_default_item_name,
  .option         = options,
  .version        = LIBAVUTIL_VERSION_INT,
};

URLProtocol ff_cache_protocol = {
  .name                = "cache",
  .url_open            = cache_open,
  .url_read            = cache_read,
  .url_seek            = cache_seek,
  .url_close           = cache_close,
  .priv_data_size      = sizeof(CacheContext),
  .priv_data_class     = &cache_context_class,
};
