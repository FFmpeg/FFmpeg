/*
 * MMX optimized DSP utils
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
 *
 */

#define TESTCPU_MAIN
#include "dsputil.h"
//#include "../libavcodec/dsputil.c"
#include "../libavcodec/i386/cputest.c"
#include "../libavcodec/i386/dsputil_mmx.c"
#undef TESTCPU_MAIN

#define PAD 0x10000
/*
 * for testing speed of various routine - should be probably extended
 * for a general purpose regression test later
 *
 * currently only for i386 - FIXME
 */

#define PIX_FUNC_C(a) \
    { #a "_c", a ## _c, 0 }, \
    { #a "_mmx", a ## _mmx, MM_MMX }, \
    { #a "_mmx2", a ## _mmx2, MM_MMXEXT | PAD }

#define PIX_FUNC(a) \
    { #a "_mmx", a ## _mmx, MM_MMX }, \
    { #a "_3dnow", a ## _3dnow, MM_3DNOW }, \
    { #a "_mmx2", a ## _mmx2, MM_MMXEXT | PAD }

#define PIX_FUNC_MMX(a) \
    { #a "_mmx", a ## _mmx, MM_MMX | PAD }

/*
    PIX_FUNC_C(pix_abs16x16),
    PIX_FUNC_C(pix_abs16x16_x2),
    PIX_FUNC_C(pix_abs16x16_y2),
    PIX_FUNC_C(pix_abs16x16_xy2),
    PIX_FUNC_C(pix_abs8x8),
    PIX_FUNC_C(pix_abs8x8_x2),
    PIX_FUNC_C(pix_abs8x8_y2),
    PIX_FUNC_C(pix_abs8x8_xy2),
*/

static const struct pix_func {
    char* name;
    op_pixels_func func;
    int mm_flags;
} pix_func[] = {

    PIX_FUNC_MMX(put_pixels),
#if 1
    PIX_FUNC(put_pixels_x2),
    PIX_FUNC(put_pixels_y2),
    PIX_FUNC_MMX(put_pixels_xy2),

    PIX_FUNC(put_no_rnd_pixels_x2),
    PIX_FUNC(put_no_rnd_pixels_y2),
    PIX_FUNC_MMX(put_no_rnd_pixels_xy2),

    PIX_FUNC(avg_pixels),
    PIX_FUNC(avg_pixels_x2),
    PIX_FUNC(avg_pixels_y2),
    PIX_FUNC(avg_pixels_xy2),
#endif
    { 0, 0 }
};

static inline long long rdtsc()
{
    long long l;
    asm volatile(   "rdtsc\n\t"
		    : "=A" (l)
		);
    return l;
}

static test_speed(int step)
{
    const struct pix_func* pix = pix_func;
    const int linesize = 720;
    char empty[32768];
    char* bu =(char*)(((long)empty + 32) & ~0xf);

    int sum = 0;

    while (pix->name)
    {
	int i;
        uint64_t te, ts;
        op_pixels_func func = pix->func;
	char* im = bu;

	if (!(pix->mm_flags & mm_flags))
            continue;

	printf("%30s... ", pix->name);
        fflush(stdout);
	ts = rdtsc();
	for(i=0; i<100000; i++){
	    func(im, im + 1000, linesize, 16);
	    im += step;
	    if (im > bu + 20000)
		im = bu;
	}
	te = rdtsc();
        emms();
	printf("% 9d\n", (int)(te - ts));
        sum += (te - ts) / 100000;
	if (pix->mm_flags & PAD)
            puts("");
        pix++;
    }

    printf("Total sum: %d\n", sum);
}

int main(int argc, char* argv[])
{
    int step = 16;

    if (argc > 1)
    {
        // something simple for now
	if (argc > 2 && (strcmp("-s", argv[1]) == 0
			 || strcmp("-step", argv[1]) == 0))
            step = atoi(argv[2]);
    }

    mm_flags = mm_support();
    printf("%s: detected CPU flags:", argv[0]);
    if (mm_flags & MM_MMX)
        printf(" mmx");
    if (mm_flags & MM_MMXEXT)
        printf(" mmxext");
    if (mm_flags & MM_3DNOW)
        printf(" 3dnow");
    if (mm_flags & MM_SSE)
        printf(" sse");
    if (mm_flags & MM_SSE2)
        printf(" sse2");
    printf("\n");

    printf("Using step: %d\n", step);
    test_speed(step);
}
