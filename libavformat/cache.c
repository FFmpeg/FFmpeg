/*
 * Input cache protocol.
 * Copyright (c) 2011 Michael Niedermayer
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
 *      support filling with a background thread
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/file.h"
#include "avformat.h"
#include <fcntl.h>
#if HAVE_SETMODE
#include <io.h>
#endif
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "os_support.h"
#include "url.h"
#include "pthread.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_EXCL
#define O_EXCL 0
#endif

typedef struct Page {
  int64_t pos;
  int64_t begin;
  int64_t len;
  char* name;
  int fdw;
  int fdr;
  struct Page* next;
} Page;

typedef struct Cache {
  Page* page;
  int num_pages;
} Cache;

typedef struct Context {
  URLContext* inner;
  int8_t filling;
  Cache* cache;
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} Context;

#define HEADER (sizeof(uint64_t)*2)

static void* cache_fill_thread(void* arg)
{
  Context* context = (Context*)arg;
  Cache* cache = (Cache*)av_mallocz(sizeof(Cache));
  Page* page = (Page*)av_mallocz(sizeof(Page));

  cache->page = page;
  context->cache = cache;

  char cache_name[128];
  snprintf(cache_name, 128, "/sdcard/VPlayer/page%p", page);
  page->fdw = open(cache_name, O_RDWR | O_BINARY | O_CREAT | O_EXCL, 0600);
  page->fdr = open(cache_name, O_RDWR | O_BINARY, 0600);
  write(page->fdw, &(page->begin), sizeof(uint64_t));
  write(page->fdw, &(page->len), sizeof(uint64_t));
  page->begin = 0;
  page->len = 0;
  page->pos = 0;
  lseek(page->fdr, HEADER, SEEK_SET);

  uint8_t buf[1024];

  while (context->filling) {
    pthread_mutex_lock(&context->mutex);
    int r = ffurl_read(context->inner, buf, 1024);
    if(r > 0){
      int r2= write(page->fdw, buf, r);
      av_assert0(r2==r); // FIXME handle cache failure
      page->len += r;
    }
    pthread_mutex_unlock(&context->mutex);
  }

  av_free(cache);

  while (page) {
    Page* p = page->next;
    close(page->fdw);
    close(page->fdr);
    av_free(page);
    page = p;
  }
  return NULL;
}

static int cache_open(URLContext *h, const char *arg, int flags)
{
  Context *c= h->priv_data;

  av_strstart(arg, "cache:", &arg);
  int opened = ffurl_open(&c->inner, arg, flags, &h->interrupt_callback, NULL);

  c->filling = 1;
  pthread_mutex_init(&c->mutex, NULL);
  pthread_cond_init(&c->cond, NULL);
  pthread_create(&c->thread, NULL, cache_fill_thread, c);
  av_log(NULL, AV_LOG_INFO, "cache_open: %s, %d\n", arg, opened);

  return opened;
}

static int cache_read(URLContext *h, unsigned char *buf, int size)
{
  Context *c= h->priv_data;
  int r;

  Page* page = c->cache->page;

  while (page->len <= page->pos) {
    usleep(100);
    continue;
  }

  r = read(page->fdr, buf, FFMIN(size, page->len - page->pos));
  if (r > 0) {
    page->pos += r;
  }
  return (-1 == r) ? AVERROR(errno) : r;
}

static int64_t cache_seek(URLContext *h, int64_t pos, int whence)
{
  Context *c= h->priv_data;

  if (whence == AVSEEK_SIZE) {
    return ffurl_seek(c->inner, pos, whence);
  }

  Page* page = c->cache->page;

  pthread_mutex_lock(&c->mutex);
  if (pos < page->begin || pos > page->begin + page->len) {
    page->begin = ffurl_seek(c->inner, pos, whence);
    lseek(page->fdw, 0, SEEK_SET);
    write(page->fdw, &(page->begin), sizeof(uint64_t));
    write(page->fdw, &(page->len), sizeof(uint64_t));
    lseek(page->fdr, HEADER, SEEK_SET);
    page->pos = 0;
    page->len = 0;
    pos = page->begin;
  } else {
    page->pos = lseek(page->fdr, pos - page->begin + HEADER, whence) - HEADER;
  }
  pthread_mutex_unlock(&c->mutex);

  return pos;
}

static int cache_close(URLContext *h)
{
  Context *c= h->priv_data;

  c->filling = 0;
  pthread_join(c->thread, NULL);
  pthread_mutex_destroy(&c->mutex);
  pthread_cond_destroy(&c->cond);

  ffurl_close(c->inner);

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
