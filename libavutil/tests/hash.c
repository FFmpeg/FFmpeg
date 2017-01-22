/*
 * Copyright (C) 2013 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

#include <stdio.h>
#include <string.h>

#include "libavutil/hash.h"

#define SRC_BUF_SIZE 64
#define DST_BUF_SIZE (AV_HASH_MAX_SIZE * 8)

int main(void)
{
   struct AVHashContext *ctx = NULL;
   int i, j, numhashes = 0;
   static const uint8_t src[SRC_BUF_SIZE] = { 0 };
   uint8_t dst[DST_BUF_SIZE];

   while (av_hash_names(numhashes))
       numhashes++;

   for (i = 0; i < numhashes; i++) {
       if (av_hash_alloc(&ctx, av_hash_names(i)) < 0)
           return 1;

       av_hash_init(ctx);
       av_hash_update(ctx, src, SRC_BUF_SIZE);
       memset(dst, 0, DST_BUF_SIZE);
       av_hash_final_hex(ctx, dst, DST_BUF_SIZE);
       printf("%s hex: %s\n", av_hash_get_name(ctx), dst);

       av_hash_init(ctx);
       av_hash_update(ctx, src, SRC_BUF_SIZE);
       av_hash_final_bin(ctx, dst, DST_BUF_SIZE);
       printf("%s bin: ", av_hash_get_name(ctx));
       for (j = 0; j < av_hash_get_size(ctx); j++) {
           printf("%#x ", dst[j]);
       }
       printf("\n");

       av_hash_init(ctx);
       av_hash_update(ctx, src, SRC_BUF_SIZE);
       av_hash_final_b64(ctx, dst, DST_BUF_SIZE);
       printf("%s b64: %s\n", av_hash_get_name(ctx), dst);
       av_hash_freep(&ctx);
   }
   return 0;
}
