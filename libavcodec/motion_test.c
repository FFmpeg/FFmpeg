/* motion test. (c) 2001 Fabrice Bellard. */

/**
 * @file motion_test.c
 * motion test.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "dsputil.h"

#include "i386/mmx.h"

int pix_abs16x16_mmx(uint8_t *blk1, uint8_t *blk2, int lx);
int pix_abs16x16_mmx1(uint8_t *blk1, uint8_t *blk2, int lx);
int pix_abs16x16_x2_mmx(uint8_t *blk1, uint8_t *blk2, int lx);
int pix_abs16x16_x2_mmx1(uint8_t *blk1, uint8_t *blk2, int lx);
int pix_abs16x16_x2_c(uint8_t *blk1, uint8_t *blk2, int lx);
int pix_abs16x16_y2_mmx(uint8_t *blk1, uint8_t *blk2, int lx);
int pix_abs16x16_y2_mmx1(uint8_t *blk1, uint8_t *blk2, int lx);
int pix_abs16x16_y2_c(uint8_t *blk1, uint8_t *blk2, int lx);
int pix_abs16x16_xy2_mmx(uint8_t *blk1, uint8_t *blk2, int lx);
int pix_abs16x16_xy2_mmx1(uint8_t *blk1, uint8_t *blk2, int lx);
int pix_abs16x16_xy2_c(uint8_t *blk1, uint8_t *blk2, int lx);

typedef int motion_func(uint8_t *blk1, uint8_t *blk2, int lx);

#define WIDTH 64
#define HEIGHT 64

uint8_t img1[WIDTH * HEIGHT];
uint8_t img2[WIDTH * HEIGHT];

void fill_random(uint8_t *tab, int size)
{
    int i;
    for(i=0;i<size;i++) {
#if 1
        tab[i] = random() % 256;
#else
        tab[i] = i;
#endif
    }
}

void help(void)
{
    printf("motion-test [-h]\n"
           "test motion implementations\n");
    exit(1);
}

int64_t gettime(void)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

#define NB_ITS 500

int dummy;

void test_motion(const char *name,
                 motion_func *test_func, motion_func *ref_func)
{
    int x, y, d1, d2, it;
    uint8_t *ptr;
    int64_t ti;
    printf("testing '%s'\n", name);

    /* test correctness */
    for(it=0;it<20;it++) {

        fill_random(img1, WIDTH * HEIGHT);
        fill_random(img2, WIDTH * HEIGHT);
        
        for(y=0;y<HEIGHT-17;y++) {
            for(x=0;x<WIDTH-17;x++) {
                ptr = img2 + y * WIDTH + x; 
                d1 = test_func(img1, ptr, WIDTH);
                d2 = ref_func(img1, ptr, WIDTH);
                if (d1 != d2) {
                    printf("error: mmx=%d c=%d\n", d1, d2);
                }
            }
        }
    }
    emms();
    
    /* speed test */
    ti = gettime();
    d1 = 0;
    for(it=0;it<NB_ITS;it++) {
        for(y=0;y<HEIGHT-17;y++) {
            for(x=0;x<WIDTH-17;x++) {
                ptr = img2 + y * WIDTH + x; 
                d1 += test_func(img1, ptr, WIDTH);
            }
        }
    }
    emms();
    dummy = d1; /* avoid optimisation */
    ti = gettime() - ti;
    
    printf("  %0.0f kop/s\n", 
           (double)NB_ITS * (WIDTH - 16) * (HEIGHT - 16) / 
           (double)(ti / 1000.0));
}


int main(int argc, char **argv)
{
    int c;
    
    for(;;) {
        c = getopt(argc, argv, "h");
        if (c == -1)
            break;
        switch(c) {
        case 'h':
            help();
            break;
        }
    }
               
    printf("ffmpeg motion test\n");

    test_motion("mmx", pix_abs16x16_mmx, pix_abs16x16_c);
    test_motion("mmx_x2", pix_abs16x16_x2_mmx, pix_abs16x16_x2_c);
    test_motion("mmx_y2", pix_abs16x16_y2_mmx, pix_abs16x16_y2_c);
    test_motion("mmx_xy2", pix_abs16x16_xy2_mmx, pix_abs16x16_xy2_c);
    return 0;
}
