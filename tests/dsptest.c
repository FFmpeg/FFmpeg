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
static const struct pix_func {
    char* name;
    op_pixels_func func;
    int mm_flags;
} pix_func[] = {
    { "put_pixels_x2_mmx", put_pixels_y2_mmx, MM_MMX },
    { "put_pixels_x2_3dnow", put_pixels_y2_3dnow, MM_3DNOW },
    { "put_pixels_x2_mmx2", put_pixels_y2_mmx2, MM_MMXEXT | PAD },

    { "put_no_rnd_pixels_x2_mmx", put_no_rnd_pixels_x2_mmx, MM_MMX },
    { "put_no_rnd_pixels_x2_3dnow", put_no_rnd_pixels_x2_3dnow, MM_3DNOW },
    { "put_no_rnd_pixels_x2_mmx2", put_no_rnd_pixels_x2_mmx2, MM_MMXEXT | PAD },

    { "put_pixels_y2_mmx", put_pixels_y2_mmx, MM_MMX },
    { "put_pixels_y2_3dnow", put_pixels_y2_3dnow, MM_3DNOW },
    { "put_pixels_y2_mmx2", put_pixels_y2_mmx2, MM_MMXEXT | PAD },
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
	if (pix->mm_flags & PAD)
            puts("");
        pix++;
    }
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
    printf("dsptest: CPU flags:");
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
