/*
 * Copyright (c) 2003-2013 Loren Merritt
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110 USA
 */
/*
 * tiny_ssim.c
 * Computes the Structural Similarity Metric between two rawYV12 video files.
 * original algorithm:
 * Z. Wang, A. C. Bovik, H. R. Sheikh and E. P. Simoncelli,
 *   "Image quality assessment: From error visibility to structural similarity,"
 *   IEEE Transactions on Image Processing, vol. 13, no. 4, pp. 600-612, Apr. 2004.
 *
 * To improve speed, this implementation uses the standard approximation of
 * overlapped 8x8 block sums, rather than the original gaussian weights.
 */

#include "config.h"
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define FFSWAP(type,a,b) do{type SWAP_tmp= b; b= a; a= SWAP_tmp;}while(0)
#define FFMIN(a,b) ((a) > (b) ? (b) : (a))

#define BIT_DEPTH 8
#define PIXEL_MAX ((1 << BIT_DEPTH)-1)
typedef uint8_t  pixel;

/****************************************************************************
 * structural similarity metric
 ****************************************************************************/
static void ssim_4x4x2_core( const pixel *pix1, intptr_t stride1,
                             const pixel *pix2, intptr_t stride2,
                             int sums[2][4] )
{
    int x,y,z;

    for( z = 0; z < 2; z++ )
    {
        uint32_t s1 = 0, s2 = 0, ss = 0, s12 = 0;
        for( y = 0; y < 4; y++ )
            for( x = 0; x < 4; x++ )
            {
                int a = pix1[x+y*stride1];
                int b = pix2[x+y*stride2];
                s1  += a;
                s2  += b;
                ss  += a*a;
                ss  += b*b;
                s12 += a*b;
            }
        sums[z][0] = s1;
        sums[z][1] = s2;
        sums[z][2] = ss;
        sums[z][3] = s12;
        pix1 += 4;
        pix2 += 4;
    }
}

static float ssim_end1( int s1, int s2, int ss, int s12 )
{
/* Maximum value for 10-bit is: ss*64 = (2^10-1)^2*16*4*64 = 4286582784, which will overflow in some cases.
 * s1*s1, s2*s2, and s1*s2 also obtain this value for edge cases: ((2^10-1)*16*4)^2 = 4286582784.
 * Maximum value for 9-bit is: ss*64 = (2^9-1)^2*16*4*64 = 1069551616, which will not overflow. */
#if BIT_DEPTH > 9
    typedef float type;
    static const float ssim_c1 = .01*.01*PIXEL_MAX*PIXEL_MAX*64;
    static const float ssim_c2 = .03*.03*PIXEL_MAX*PIXEL_MAX*64*63;
#else
    typedef int type;
    static const int ssim_c1 = (int)(.01*.01*PIXEL_MAX*PIXEL_MAX*64 + .5);
    static const int ssim_c2 = (int)(.03*.03*PIXEL_MAX*PIXEL_MAX*64*63 + .5);
#endif
    type fs1 = s1;
    type fs2 = s2;
    type fss = ss;
    type fs12 = s12;
    type vars = fss*64 - fs1*fs1 - fs2*fs2;
    type covar = fs12*64 - fs1*fs2;
    return (float)(2*fs1*fs2 + ssim_c1) * (float)(2*covar + ssim_c2)
         / ((float)(fs1*fs1 + fs2*fs2 + ssim_c1) * (float)(vars + ssim_c2));
}

static float ssim_end4( int sum0[5][4], int sum1[5][4], int width )
{
    float ssim = 0.0;
    int i;

    for( i = 0; i < width; i++ )
        ssim += ssim_end1( sum0[i][0] + sum0[i+1][0] + sum1[i][0] + sum1[i+1][0],
                           sum0[i][1] + sum0[i+1][1] + sum1[i][1] + sum1[i+1][1],
                           sum0[i][2] + sum0[i+1][2] + sum1[i][2] + sum1[i+1][2],
                           sum0[i][3] + sum0[i+1][3] + sum1[i][3] + sum1[i+1][3] );
    return ssim;
}

float ssim_plane(
                           pixel *pix1, intptr_t stride1,
                           pixel *pix2, intptr_t stride2,
                           int width, int height, void *buf, int *cnt )
{
    int z = 0;
    int x, y;
    float ssim = 0.0;
    int (*sum0)[4] = buf;
    int (*sum1)[4] = sum0 + (width >> 2) + 3;
    width >>= 2;
    height >>= 2;
    for( y = 1; y < height; y++ )
    {
        for( ; z <= y; z++ )
        {
            FFSWAP( void*, sum0, sum1 );
            for( x = 0; x < width; x+=2 )
                ssim_4x4x2_core( &pix1[4*(x+z*stride1)], stride1, &pix2[4*(x+z*stride2)], stride2, &sum0[x] );
        }
        for( x = 0; x < width-1; x += 4 )
            ssim += ssim_end4( sum0+x, sum1+x, FFMIN(4,width-x-1) );
    }
//     *cnt = (height-1) * (width-1);
    return ssim / ((height-1) * (width-1));
}


uint64_t ssd_plane( const uint8_t *pix1, const uint8_t *pix2, int size )
{
    uint64_t ssd = 0;
    int i;
    for( i=0; i<size; i++ )
    {
        int d = pix1[i] - pix2[i];
        ssd += d*d;
    }
    return ssd;
}

static double ssd_to_psnr( uint64_t ssd, uint64_t denom )
{
    return -10*log((double)ssd/(denom*255*255))/log(10);
}

static double ssim_db( double ssim, double weight )
{
    return 10*(log(weight)/log(10)-log(weight-ssim)/log(10));
}

static void print_results(uint64_t ssd[3], double ssim[3], int frames, int w, int h)
{
    printf( "PSNR Y:%.3f  U:%.3f  V:%.3f  All:%.3f | ",
            ssd_to_psnr( ssd[0], (uint64_t)frames*w*h ),
            ssd_to_psnr( ssd[1], (uint64_t)frames*w*h/4 ),
            ssd_to_psnr( ssd[2], (uint64_t)frames*w*h/4 ),
            ssd_to_psnr( ssd[0] + ssd[1] + ssd[2], (uint64_t)frames*w*h*3/2 ) );
    printf( "SSIM Y:%.5f U:%.5f V:%.5f All:%.5f (%.5f)",
            ssim[0] / frames,
            ssim[1] / frames,
            ssim[2] / frames,
            (ssim[0]*4 + ssim[1] + ssim[2]) / (frames*6),
            ssim_db(ssim[0] * 4 + ssim[1] + ssim[2], frames*6));
}

int main(int argc, char* argv[])
{
    FILE *f[2];
    uint8_t *buf[2], *plane[2][3];
    int *temp;
    uint64_t ssd[3] = {0,0,0};
    double ssim[3] = {0,0,0};
    int frame_size, w, h;
    int frames, seek;
    int i;

    if( argc<4 || 2 != sscanf(argv[3], "%dx%d", &w, &h) )
    {
        printf("tiny_ssim <file1.yuv> <file2.yuv> <width>x<height> [<seek>]\n");
        return -1;
    }

    f[0] = fopen(argv[1], "rb");
    f[1] = fopen(argv[2], "rb");
    sscanf(argv[3], "%dx%d", &w, &h);

    if (w<=0 || h<=0 || w*(int64_t)h >= INT_MAX/3 || 2LL*w+12 >= INT_MAX / sizeof(*temp)) {
        fprintf(stderr, "Dimensions are too large, or invalid\n");
        return -2;
    }

    frame_size = w*h*3LL/2;
    for( i=0; i<2; i++ )
    {
        buf[i] = malloc(frame_size);
        plane[i][0] = buf[i];
        plane[i][1] = plane[i][0] + w*h;
        plane[i][2] = plane[i][1] + w*h/4;
    }
    temp = malloc((2*w+12)*sizeof(*temp));
    seek = argc<5 ? 0 : atoi(argv[4]);
    fseek(f[seek<0], seek < 0 ? -seek : seek, SEEK_SET);

    for( frames=0;; frames++ )
    {
        uint64_t ssd_one[3];
        double ssim_one[3];
        if( fread(buf[0], frame_size, 1, f[0]) != 1) break;
        if( fread(buf[1], frame_size, 1, f[1]) != 1) break;
        for( i=0; i<3; i++ )
        {
            ssd_one[i]  = ssd_plane ( plane[0][i], plane[1][i], w*h>>2*!!i );
            ssim_one[i] = ssim_plane( plane[0][i], w>>!!i,
                                     plane[1][i], w>>!!i,
                                     w>>!!i, h>>!!i, temp, NULL );
            ssd[i] += ssd_one[i];
            ssim[i] += ssim_one[i];
        }

        printf("Frame %d | ", frames);
        print_results(ssd_one, ssim_one, 1, w, h);
        printf("                \r");
        fflush(stdout);
    }

    if( !frames ) return 0;

    printf("Total %d frames | ", frames);
    print_results(ssd, ssim, frames, w, h);
    printf("\n");

    return 0;
}
