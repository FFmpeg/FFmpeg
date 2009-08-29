/*
 * Copyright (C) 2002 Michael Niedermayer <michaelni@gmx.at>
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
#include <string.h>              /* for memset() */
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>

#include "swscale.h"
#include "rgb2rgb.h"

#define SIZE 1000
#define srcByte 0x55
#define dstByte 0xBB

#define FUNC(s,d,n) {s,d,#n,n}

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

    if (!srcBuffer || !dstBuffer)
        return -1;

    av_log(NULL, AV_LOG_INFO, "memory corruption test ...\n");
    args_parse(argc, argv);
    av_log(NULL, AV_LOG_INFO, "CPU capabilities forced to %x\n", cpu_caps);
    sws_rgb2rgb_init(cpu_caps);

    for(funcNum=0; ; funcNum++) {
        struct func_info_s {
            int src_bpp;
            int dst_bpp;
            const char *name;
            void (*func)(const uint8_t *src, uint8_t *dst, long src_size);
        } func_info[] = {
            FUNC(2, 2, rgb15to16),
            FUNC(2, 3, rgb15to24),
            FUNC(2, 4, rgb15to32),
            FUNC(2, 3, rgb16to24),
            FUNC(2, 4, rgb16to32),
            FUNC(3, 2, rgb24to15),
            FUNC(3, 2, rgb24to16),
            FUNC(3, 4, rgb24to32),
            FUNC(4, 2, rgb32to15),
            FUNC(4, 2, rgb32to16),
            FUNC(4, 3, rgb32to24),
            FUNC(2, 2, rgb16to15),
            FUNC(2, 2, rgb15tobgr15),
            FUNC(2, 2, rgb15tobgr16),
            FUNC(2, 3, rgb15tobgr24),
            FUNC(2, 4, rgb15tobgr32),
            FUNC(2, 2, rgb16tobgr15),
            FUNC(2, 2, rgb16tobgr16),
            FUNC(2, 3, rgb16tobgr24),
            FUNC(2, 4, rgb16tobgr32),
            FUNC(3, 2, rgb24tobgr15),
            FUNC(3, 2, rgb24tobgr16),
            FUNC(3, 3, rgb24tobgr24),
            FUNC(3, 4, rgb24tobgr32),
            FUNC(4, 2, rgb32tobgr15),
            FUNC(4, 2, rgb32tobgr16),
            FUNC(4, 3, rgb32tobgr24),
            FUNC(4, 4, rgb32tobgr32),
            FUNC(0, 0, NULL)
        };
        int width;
        int failed=0;
        int srcBpp=0;
        int dstBpp=0;

        if (!func_info[funcNum].func) break;

        av_log(NULL, AV_LOG_INFO,".");
        memset(srcBuffer, srcByte, SIZE);

        for(width=63; width>0; width--) {
            int dstOffset;
            for(dstOffset=128; dstOffset<196; dstOffset+=4) {
                int srcOffset;
                memset(dstBuffer, dstByte, SIZE);

                for(srcOffset=128; srcOffset<196; srcOffset+=4) {
                    uint8_t *src= srcBuffer+srcOffset;
                    uint8_t *dst= dstBuffer+dstOffset;
                    const char *name=NULL;

                    if(failed) break; //don't fill the screen with shit ...

                    srcBpp = func_info[funcNum].src_bpp;
                    dstBpp = func_info[funcNum].dst_bpp;
                    name   = func_info[funcNum].name;

                    func_info[funcNum].func(src, dst, width*srcBpp);

                    if(!srcBpp) break;

                    for(i=0; i<SIZE; i++) {
                        if(srcBuffer[i]!=srcByte) {
                            av_log(NULL, AV_LOG_INFO, "src damaged at %d w:%d src:%d dst:%d %s\n",
                                   i, width, srcOffset, dstOffset, name);
                            failed=1;
                            break;
                        }
                    }
                    for(i=0; i<dstOffset; i++) {
                        if(dstBuffer[i]!=dstByte) {
                            av_log(NULL, AV_LOG_INFO, "dst damaged at %d w:%d src:%d dst:%d %s\n",
                                   i, width, srcOffset, dstOffset, name);
                            failed=1;
                            break;
                        }
                    }
                    for(i=dstOffset + width*dstBpp; i<SIZE; i++) {
                        if(dstBuffer[i]!=dstByte) {
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

    av_log(NULL, AV_LOG_INFO, "\n%d converters passed, %d converters randomly overwrote memory\n", passedNum, failedNum);
    return failedNum;
}
