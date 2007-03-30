/*
 * Copyright (C) 2002 Michael Niedermayer <michaelni@gmx.at>
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
 * You should have received a copy of the GNU General Public License
 * along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <string.h>              /* for memset() */
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>

#include "swscale.h"
#include "rgb2rgb.h"

#define SIZE 1000
#define srcByte 0x55
#define dstByte 0xBB


static int cpu_caps;

static char *args_parse(int argc, char *argv[])
{
    int o;

    while ((o = getopt(argc, argv, "m23")) != -1) {
        switch (o) {
            case 'm':
                cpu_caps |= SWS_CPU_CAPS_MMX;
                break;
            case '2':
                cpu_caps |= SWS_CPU_CAPS_MMX2;
                break;
            case '3':
                cpu_caps |= SWS_CPU_CAPS_3DNOW;
                break;
            default:
                av_log(NULL, AV_LOG_ERROR, "Unknown option %c\n", o);
        }
    }

    return argv[optind];
}

int main(int argc, char **argv)
{
	int i, funcNum;
	uint8_t *srcBuffer= (uint8_t*)av_malloc(SIZE);
	uint8_t *dstBuffer= (uint8_t*)av_malloc(SIZE);
	int failedNum=0;
	int passedNum=0;
	
	av_log(NULL, AV_LOG_INFO, "memory corruption test ...\n");
	args_parse(argc, argv);
	av_log(NULL, AV_LOG_INFO, "CPU capabilities forced to %x\n", cpu_caps);
	sws_rgb2rgb_init(cpu_caps);
	
	for(funcNum=0; funcNum<100; funcNum++){
		int width;
		int failed=0;
		int srcBpp=0;
		int dstBpp=0;

		av_log(NULL, AV_LOG_INFO,".");
		memset(srcBuffer, srcByte, SIZE);

		for(width=32; width<64; width++){
			int dstOffset;
			for(dstOffset=128; dstOffset<196; dstOffset+=4){
				int srcOffset;
				memset(dstBuffer, dstByte, SIZE);

				for(srcOffset=128; srcOffset<196; srcOffset+=4){
					uint8_t *src= srcBuffer+srcOffset;
					uint8_t *dst= dstBuffer+dstOffset;
					char *name=NULL;
					
					if(failed) break; //don't fill the screen with shit ...

					switch(funcNum){
					case 0:
						srcBpp=2;
						dstBpp=2;
						name="rgb15to16";
						rgb15to16(src, dst, width*srcBpp);
						break;
					case 1:
						srcBpp=2;
						dstBpp=3;
						name="rgb15to24";
						rgb15to24(src, dst, width*srcBpp);
						break;
					case 2:
						srcBpp=2;
						dstBpp=4;
						name="rgb15to32";
						rgb15to32(src, dst, width*srcBpp);
						break;
					case 3:
						srcBpp=2;
						dstBpp=3;
						name="rgb16to24";
						rgb16to24(src, dst, width*srcBpp);
						break;
					case 4:
						srcBpp=2;
						dstBpp=4;
						name="rgb16to32";
						rgb16to32(src, dst, width*srcBpp);
						break;
					case 5:
						srcBpp=3;
						dstBpp=2;
						name="rgb24to15";
						rgb24to15(src, dst, width*srcBpp);
						break;
					case 6:
						srcBpp=3;
						dstBpp=2;
						name="rgb24to16";
						rgb24to16(src, dst, width*srcBpp);
						break;
					case 7:
						srcBpp=3;
						dstBpp=4;
						name="rgb24to32";
						rgb24to32(src, dst, width*srcBpp);
						break;
					case 8:
						srcBpp=4;
						dstBpp=2;
						name="rgb32to15";
                        //((*s++) << TGA_SHIFT32) | TGA_ALPHA32;
						rgb32to15(src, dst, width*srcBpp);
						break;
					case 9:
						srcBpp=4;
						dstBpp=2;
						name="rgb32to16";
						rgb32to16(src, dst, width*srcBpp);
						break;
					case 10:
						srcBpp=4;
						dstBpp=3;
						name="rgb32to24";
						rgb32to24(src, dst, width*srcBpp);
						break;
					case 11:
						srcBpp=2;
						dstBpp=2;
						name="rgb16to15";
						rgb16to15(src, dst, width*srcBpp);
						break;
					
					case 14:
						srcBpp=2;
						dstBpp=2;
						name="rgb15tobgr15";
						rgb15tobgr15(src, dst, width*srcBpp);
						break;
					case 15:
						srcBpp=2;
						dstBpp=2;
						name="rgb15tobgr16";
						rgb15tobgr16(src, dst, width*srcBpp);
						break;
					case 16:
						srcBpp=2;
						dstBpp=3;
						name="rgb15tobgr24";
						rgb15tobgr24(src, dst, width*srcBpp);
						break;
					case 17:
						srcBpp=2;
						dstBpp=4;
						name="rgb15tobgr32";
						rgb15tobgr32(src, dst, width*srcBpp);
						break;
					case 18:
						srcBpp=2;
						dstBpp=2;
						name="rgb16tobgr15";
						rgb16tobgr15(src, dst, width*srcBpp);
						break;
					case 19:
						srcBpp=2;
						dstBpp=2;
						name="rgb16tobgr16";
						rgb16tobgr16(src, dst, width*srcBpp);
						break;
					case 20:
						srcBpp=2;
						dstBpp=3;
						name="rgb16tobgr24";
						rgb16tobgr24(src, dst, width*srcBpp);
						break;
					case 21:
						srcBpp=2;
						dstBpp=4;
						name="rgb16tobgr32";
						rgb16tobgr32(src, dst, width*srcBpp);
						break;
					case 22:
						srcBpp=3;
						dstBpp=2;
						name="rgb24tobgr15";
						rgb24tobgr15(src, dst, width*srcBpp);
						break;
					case 23:
						srcBpp=3;
						dstBpp=2;
						name="rgb24tobgr16";
						rgb24tobgr16(src, dst, width*srcBpp);
						break;
					case 24:
						srcBpp=3;
						dstBpp=3;
						name="rgb24tobgr24";
						rgb24tobgr24(src, dst, width*srcBpp);
						break;
					case 25:
						srcBpp=3;
						dstBpp=4;
						name="rgb24tobgr32";
						rgb24tobgr32(src, dst, width*srcBpp);
						break;
					case 26:
						srcBpp=4;
						dstBpp=2;
						name="rgb32tobgr15";
						rgb32tobgr15(src, dst, width*srcBpp);
						break;
					case 27:
						srcBpp=4;
						dstBpp=2;
						name="rgb32tobgr16";
						rgb32tobgr16(src, dst, width*srcBpp);
						break;
					case 28:
						srcBpp=4;
						dstBpp=3;
						name="rgb32tobgr24";
						rgb32tobgr24(src, dst, width*srcBpp);
						break;
					case 29:
						srcBpp=4;
						dstBpp=4;
						name="rgb32tobgr32";
						rgb32tobgr32(src, dst, width*srcBpp);
						break;

					}
					if(!srcBpp) break;

					for(i=0; i<SIZE; i++){
						if(srcBuffer[i]!=srcByte){
							av_log(NULL, AV_LOG_INFO, "src damaged at %d w:%d src:%d dst:%d %s\n", 
								i, width, srcOffset, dstOffset, name);
							failed=1;
							break;
						}
					}
					for(i=0; i<dstOffset; i++){
						if(dstBuffer[i]!=dstByte){
							av_log(NULL, AV_LOG_INFO, "dst damaged at %d w:%d src:%d dst:%d %s\n", 
								i, width, srcOffset, dstOffset, name);
							failed=1;
							break;
						}
					}
					for(i=dstOffset + width*dstBpp; i<SIZE; i++){
						if(dstBuffer[i]!=dstByte){
							av_log(NULL, AV_LOG_INFO, "dst damaged at %d w:%d src:%d dst:%d %s\n", 
								i, width, srcOffset, dstOffset, name);
							failed=1;
							break;
						}
					}
				}
			}
		}
		if(failed) failedNum++;
		else if(srcBpp) passedNum++;
	}
	
	av_log(NULL, AV_LOG_INFO, "%d converters passed, %d converters randomly overwrote memory\n", passedNum, failedNum);
	return failedNum;
}
