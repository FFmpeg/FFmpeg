/*
    Copyright (C) 2003 Michael Niedermayer <michaelni@gmx.at>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>

#include "swscale.h"
#include "../libvo/img_format.h"
#include "../cpudetect.h"

static int testFormat[]={
IMGFMT_YVU9,
IMGFMT_YV12,
//IMGFMT_IYUV,
IMGFMT_I420,
IMGFMT_BGR15,
IMGFMT_BGR16,
IMGFMT_BGR24,
IMGFMT_BGR32,
IMGFMT_RGB24,
IMGFMT_RGB32,
//IMGFMT_Y8,
IMGFMT_Y800,
//IMGFMT_YUY2,
0
};

static uint64_t getSSD(uint8_t *src1, uint8_t *src2, int stride1, int stride2, int w, int h){
	int x,y;
	uint64_t ssd=0;

//printf("%d %d\n", w, h);
        
	for(y=0; y<h; y++){
		for(x=0; x<w; x++){
			int d= src1[x + y*stride1] - src2[x + y*stride2];
			ssd+= d*d;
//printf("%d", abs(src1[x + y*stride1] - src2[x + y*stride2])/26 );
		}
//printf("\n");
	}
	return ssd;
}

// test by ref -> src -> dst -> out & compare out against ref
// ref & out are YV12
static void doTest(uint8_t *ref[3], int refStride[3], int w, int h, int srcFormat, int dstFormat, 
                   int srcW, int srcH, int dstW, int dstH, int flags){
	uint8_t *src[3];
	uint8_t *dst[3];
	uint8_t *out[3];
	int srcStride[3], dstStride[3];
	int i;
	uint64_t ssdY, ssdU, ssdV;
	struct SwsContext *srcContext, *dstContext, *outContext;
	
	for(i=0; i<3; i++){
		// avoid stride % bpp != 0
		if(srcFormat==IMGFMT_RGB24 || srcFormat==IMGFMT_BGR24)
			srcStride[i]= srcW*3;
		else
			srcStride[i]= srcW*4;
		
		if(dstFormat==IMGFMT_RGB24 || dstFormat==IMGFMT_BGR24)
			dstStride[i]= dstW*3;
		else
			dstStride[i]= dstW*4;
	
		src[i]= (uint8_t*) malloc(srcStride[i]*srcH);
		dst[i]= (uint8_t*) malloc(dstStride[i]*dstH);
		out[i]= (uint8_t*) malloc(refStride[i]*h);
	}

	srcContext= sws_getContext(w, h, IMGFMT_YV12, srcW, srcH, srcFormat, flags, NULL, NULL);
	dstContext= sws_getContext(srcW, srcH, srcFormat, dstW, dstH, dstFormat, flags, NULL, NULL);
	outContext= sws_getContext(dstW, dstH, dstFormat, w, h, IMGFMT_YV12, flags, NULL, NULL);
	if(srcContext==NULL ||dstContext==NULL ||outContext==NULL){
		printf("Failed allocating swsContext\n");
		goto end;
	}
//	printf("test %X %X %X -> %X %X %X\n", (int)ref[0], (int)ref[1], (int)ref[2],
//		(int)src[0], (int)src[1], (int)src[2]);

	sws_scale(srcContext, ref, refStride, 0, h   , src, srcStride);
	sws_scale(dstContext, src, srcStride, 0, srcH, dst, dstStride);
	sws_scale(outContext, dst, dstStride, 0, dstH, out, refStride);
asm volatile ("emms\n\t");
	     
	ssdY= getSSD(ref[0], out[0], refStride[0], refStride[0], w, h);
	ssdU= getSSD(ref[1], out[1], refStride[1], refStride[1], (w+1)>>1, (h+1)>>1);
	ssdV= getSSD(ref[2], out[2], refStride[2], refStride[2], (w+1)>>1, (h+1)>>1);
	
	if(srcFormat == IMGFMT_Y800 || dstFormat==IMGFMT_Y800) ssdU=ssdV=0; //FIXME check that output is really gray
	
	ssdY/= w*h;
	ssdU/= w*h/4;
	ssdV/= w*h/4;
	
	if(ssdY>100 || ssdU>100 || ssdV>100){
		printf(" %s %dx%d -> %s %4dx%4d flags=%2d SSD=%5lld,%5lld,%5lld\n", 
			vo_format_name(srcFormat), srcW, srcH, 
			vo_format_name(dstFormat), dstW, dstH,
			flags,
			ssdY, ssdU, ssdV);
	}

	end:
	
	sws_freeContext(srcContext);
	sws_freeContext(dstContext);
	sws_freeContext(outContext);

	for(i=0; i<3; i++){
		free(src[i]);
		free(dst[i]);
		free(out[i]);
	}
}

void mp_msg_c( int x, const char *format, ... ){
    va_list va;
    va_start(va, format);
    vfprintf(stderr, format, va);
    va_end(va);
}

int verbose=0; //FIXME
void fast_memcpy(void *a, void *b, int s){ //FIXME
    memcpy(a, b, s);
}

static void selfTest(uint8_t *src[3], int stride[3], int w, int h){
	int srcFormat, dstFormat, srcFormatIndex, dstFormatIndex;
	int srcW, srcH, dstW, dstH;
	int flags;

	for(srcFormatIndex=0; ;srcFormatIndex++){
		srcFormat= testFormat[srcFormatIndex];
		if(!srcFormat) break;
		for(dstFormatIndex=0; ;dstFormatIndex++){
			dstFormat= testFormat[dstFormatIndex];
			if(!dstFormat) break;
//			if(!isSupportedOut(dstFormat)) continue;
printf("%s -> %s\n", 
	vo_format_name(srcFormat),
	vo_format_name(dstFormat));
 
			srcW= w;
			srcH= h;
			for(dstW=w - w/3; dstW<= 4*w/3; dstW+= w/3){
				for(dstH=h - h/3; dstH<= 4*h/3; dstH+= h/3){
					for(flags=1; flags<33; flags*=2)
						doTest(src, stride, w, h, srcFormat, dstFormat,
							srcW, srcH, dstW, dstH, flags);
				}
			}
		}
	}
}

#define W 96
#define H 96

int main(int argc, char **argv){
	uint8_t rgb_data[W*H*4];
	uint8_t *rgb_src[3]= {rgb_data, NULL, NULL};
	int rgb_stride[3]={4*W, 0, 0};
	uint8_t data[3][W*H];
	uint8_t *src[3]= {data[0], data[1], data[2]};
	int stride[3]={W, W, W};
	int x, y;
	struct SwsContext *sws;
	GetCpuCaps(&gCpuCaps);

	sws= sws_getContext(W/12, H/12, IMGFMT_BGR32, W, H, IMGFMT_YV12, 2, NULL, NULL);
        
	for(y=0; y<H; y++){
		for(x=0; x<W*4; x++){
			rgb_data[ x + y*4*W]= random();
		}
	}

	sws_scale(sws, rgb_src, rgb_stride, 0, H   , src, stride);
asm volatile ("emms\n\t");
	selfTest(src,  stride, W, H);
}
