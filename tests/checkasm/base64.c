/*
 * Copyright (c) 2026 Shreesh Adiga <16567adigashreesh@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "checkasm.h"
#include "libavutil/base64.h"
#include "libavutil/base64_internal.h"
#include "libavutil/mem_internal.h"
#if ARCH_AARCH64
#include "libavutil/aarch64/cpu.h"
#include "libavutil/aarch64/base64.h"
#endif
#include <string.h>

#define BUF_SIZE (1024)

static char *(*base64_encode_get_fn(void))(char *, const uint8_t *, int, const char *) {
#if ARCH_AARCH64
    int cpu_flags = av_get_cpu_flags();
    if (have_neon(cpu_flags))
        return ff_base64_encode_neon;
    else
#endif
        return ff_base64_encode_c;
}

static void check_base64_encode(void)
{
    LOCAL_ALIGNED_32(uint8_t, buf, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, out1, [AV_BASE64_SIZE(BUF_SIZE)]);
    LOCAL_ALIGNED_32(uint8_t, out2, [AV_BASE64_SIZE(BUF_SIZE)]);
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    for (int i = 0; i < BUF_SIZE; i++)
        buf[i] = rnd();

    declare_func(char *, char *out, const uint8_t *in, int in_size, const char *b64);

    func_type *fn = base64_encode_get_fn();

    if (check_func(fn, "av_base64_encode")) {
        for (int i = 0; i < 48 * 5; i++) {
            size_t offset = rnd() % (BUF_SIZE - i);
            call_new(out1, i ? buf + offset : NULL, i, b64);
            call_ref(out2, i ? buf + offset : NULL, i, b64);
            if (memcmp(out1, out2, AV_BASE64_SIZE(i)))
                fail();
        }

        bench_new(out1, buf, BUF_SIZE, b64);
    }
}

void checkasm_check_base64(void)
{
    check_base64_encode();
    report("base64");
}
