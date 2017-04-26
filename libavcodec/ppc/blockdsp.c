/*
 * Copyright (c) 2002 Brian Foley
 * Copyright (c) 2002 Dieter Shirley
 * Copyright (c) 2003-2004 Romain Dolbeau <romain@dolbeau.org>
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

#include "config.h"

#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/ppc/cpu.h"
#include "libavutil/ppc/util_altivec.h"

#include "libavcodec/blockdsp.h"

/* ***** WARNING ***** WARNING ***** WARNING ***** */
/*
 * clear_blocks_dcbz32_ppc will not work properly on PowerPC processors with
 * a cache line size not equal to 32 bytes. Fortunately all processors used
 * by Apple up to at least the 7450 (AKA second generation G4) use 32-byte
 * cache lines. This is due to the use of the 'dcbz' instruction. It simply
 * clears a single cache line to zero, so you need to know the cache line
 * size to use it! It's absurd, but it's fast...
 *
 * update 24/06/2003: Apple released the G5 yesterday, with a PPC970.
 * cache line size: 128 bytes. Oups.
 * The semantics of dcbz was changed, it always clears 32 bytes. So the function
 * below will work, but will be slow. So I fixed check_dcbz_effect to use dcbzl,
 * which is defined to clear a cache line (as dcbz before). So we can still
 * distinguish, and use dcbz (32 bytes) or dcbzl (one cache line) as required.
 *
 * see <http://developer.apple.com/technotes/tn/tn2087.html>
 * and <http://developer.apple.com/technotes/tn/tn2086.html>
 */
static void clear_blocks_dcbz32_ppc(int16_t *blocks)
{
    register int misal = (unsigned long) blocks & 0x00000010, i = 0;

    if (misal) {
        ((unsigned long *) blocks)[0] = 0L;
        ((unsigned long *) blocks)[1] = 0L;
        ((unsigned long *) blocks)[2] = 0L;
        ((unsigned long *) blocks)[3] = 0L;
        i += 16;
    }
    for (; i < sizeof(int16_t) * 6 * 64 - 31; i += 32)
        __asm__ volatile ("dcbz %0,%1" :: "b" (blocks), "r" (i) : "memory");
    if (misal) {
        ((unsigned long *) blocks)[188] = 0L;
        ((unsigned long *) blocks)[189] = 0L;
        ((unsigned long *) blocks)[190] = 0L;
        ((unsigned long *) blocks)[191] = 0L;
        i += 16;
    }
}

/* Same as above, when dcbzl clears a whole 128 bytes cache line
 * i.e. the PPC970 AKA G5. */
static void clear_blocks_dcbz128_ppc(int16_t *blocks)
{
#if HAVE_DCBZL
    register int misal = (unsigned long) blocks & 0x0000007f, i = 0;

    if (misal) {
        /* We could probably also optimize this case,
         * but there's not much point as the machines
         * aren't available yet (2003-06-26). */
        memset(blocks, 0, sizeof(int16_t) * 6 * 64);
    } else {
        for (; i < sizeof(int16_t) * 6 * 64; i += 128)
            __asm__ volatile ("dcbzl %0,%1" :: "b" (blocks), "r" (i) : "memory");
    }
#else
    memset(blocks, 0, sizeof(int16_t) * 6 * 64);
#endif
}

/* Check dcbz report how many bytes are set to 0 by dcbz. */
/* update 24/06/2003: Replace dcbz by dcbzl to get the intended effect
 * (Apple "fixed" dcbz). Unfortunately this cannot be used unless the
 * assembler knows about dcbzl ... */
static long check_dcbzl_effect(void)
{
    long count = 0;
#if HAVE_DCBZL
    register char *fakedata = av_malloc(1024);
    register char *fakedata_middle;
    register long zero = 0, i = 0;

    if (!fakedata)
        return 0L;

    fakedata_middle = fakedata + 512;

    memset(fakedata, 0xFF, 1024);

    /* Below the constraint "b" seems to mean "address base register"
     * in gcc-3.3 / RS/6000 speaks. Seems to avoid using r0, so.... */
    __asm__ volatile ("dcbzl %0, %1" :: "b" (fakedata_middle), "r" (zero));

    for (i = 0; i < 1024; i++)
        if (fakedata[i] == (char) 0)
            count++;

    av_free(fakedata);
#endif

    return count;
}

#if HAVE_ALTIVEC
static void clear_block_altivec(int16_t *block)
{
    LOAD_ZERO;
    vec_st(zero_s16v,   0, block);
    vec_st(zero_s16v,  16, block);
    vec_st(zero_s16v,  32, block);
    vec_st(zero_s16v,  48, block);
    vec_st(zero_s16v,  64, block);
    vec_st(zero_s16v,  80, block);
    vec_st(zero_s16v,  96, block);
    vec_st(zero_s16v, 112, block);
}
#endif /* HAVE_ALTIVEC */

av_cold void ff_blockdsp_init_ppc(BlockDSPContext *c)
{
    // common optimizations whether AltiVec is available or not
    switch (check_dcbzl_effect()) {
    case 32:
        c->clear_blocks = clear_blocks_dcbz32_ppc;
        break;
    case 128:
        c->clear_blocks = clear_blocks_dcbz128_ppc;
        break;
    default:
        break;
    }

#if HAVE_ALTIVEC
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    c->clear_block = clear_block_altivec;
#endif /* HAVE_ALTIVEC */
}
