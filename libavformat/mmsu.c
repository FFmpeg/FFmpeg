/*
 * Just combine mmsh and mmst
 * Copyright (c) Cedric Fung <wolfplanet@gmail.com>
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
 */

#include <fcntl.h>
#if HAVE_SETMODE
#include <io.h>
#endif
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "mms.h"
#include "asf.h"
#include "libavutil/intreadwrite.h"

int mmst = 0;
int mmsh = 0;

static int mmsu_open(URLContext *h, const char *uri, int flags) {
  extern URLProtocol ff_mmst_protocol;
  extern URLProtocol ff_mmsh_protocol;
  int ret;

  if ((ret = ff_mmst_protocol.url_open(h, uri, flags)) == 0)
    mmst = 1;
  else if ((ret = ff_mmsh_protocol.url_open(h, uri, flags)) == 0)
    mmsh = 1;

  return ret;
}

static int mmsu_read(URLContext *h, uint8_t *buf, int size) {
  extern URLProtocol ff_mmst_protocol;
  extern URLProtocol ff_mmsh_protocol;
  if (mmst)
    return ff_mmst_protocol.url_read(h, buf, size);
  else if (mmsh)
    return ff_mmsh_protocol.url_read(h, buf, size);

  return 0;
}

static int mmsu_close(URLContext *h) {
  extern URLProtocol ff_mmst_protocol;
  extern URLProtocol ff_mmsh_protocol;
  if (mmst)
    return ff_mmst_protocol.url_close(h);
  else if (mmsh)
    return ff_mmsh_protocol.url_close(h);

  return 0;
}


URLProtocol ff_mmsu_protocol = {
  .name      = "mms",
  .url_open  = mmsu_open,
  .url_read  = mmsu_read,
  .url_close = mmsu_close,
};
