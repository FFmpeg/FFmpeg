/*
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
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include "libavcodec/avcodec.h"
#include "libavutil/error.h"



static int setup_side_data_entry(AVPacket* avpkt)
{
    const uint8_t *data_name = NULL;
    int ret = 0, bytes;
    uint8_t *extra_data = NULL;


    /* get side_data_name string */
    data_name = av_packet_side_data_name(AV_PKT_DATA_NEW_EXTRADATA);

    /* Allocate a memory bloc */
    bytes = strlen(data_name);

    if(!(extra_data = av_malloc(bytes))){
        ret = AVERROR(ENOMEM);
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        exit(1);
    }
    /* copy side_data_name to extra_data array */
    memcpy(extra_data, data_name, bytes);

    /* create side data for AVPacket */
    ret = av_packet_add_side_data(avpkt, AV_PKT_DATA_NEW_EXTRADATA,
                                        extra_data, bytes);
    if(ret < 0){
        fprintf(stderr,
                "Error occurred in av_packet_add_side_data: %s\n",
                av_err2str(ret));
    }

    return ret;
}

static int initializations(AVPacket* avpkt)
{
    const static uint8_t* data = "selftest for av_packet_clone(...)";
    int ret = 0;

    /* initialize avpkt */
    av_init_packet(avpkt);

    /* set values for avpkt */
    avpkt->pts = 17;
    avpkt->dts = 2;
    avpkt->data = (uint8_t*)data;
    avpkt->size = strlen(data);
    avpkt->flags = AV_PKT_FLAG_DISCARD;
    avpkt->duration = 100;
    avpkt->pos = 3;

    ret = setup_side_data_entry(avpkt);

    return ret;
}

int main(void)
{
    AVPacket avpkt;
    AVPacket *avpkt_clone = NULL;
    int ret = 0;

    if(initializations(&avpkt) < 0){
        printf("failed to initialize variables\n");
        return 1;
    }
    /* test av_packet_clone*/
    avpkt_clone = av_packet_clone(&avpkt);

    if(!avpkt_clone) {
        av_log(NULL, AV_LOG_ERROR,"av_packet_clone failed to clone AVPacket\n");
        return 1;
    }
    /*test av_grow_packet*/
    if(av_grow_packet(avpkt_clone, 20) < 0){
        av_log(NULL, AV_LOG_ERROR, "av_grow_packet failed\n");
        return 1;
    }
    if(av_grow_packet(avpkt_clone, INT_MAX) == 0){
        printf( "av_grow_packet failed to return error "
                "when \"grow_by\" parameter is too large.\n" );
        ret = 1;
    }
    /* test size error check in av_new_packet*/
    if(av_new_packet(avpkt_clone, INT_MAX) == 0){
        printf( "av_new_packet failed to return error "
                "when \"size\" parameter is too large.\n" );
        ret = 1;
    }
    /*test size error check in av_packet_from_data*/
    if(av_packet_from_data(avpkt_clone, avpkt_clone->data, INT_MAX) == 0){
        printf("av_packet_from_data failed to return error "
                "when \"size\" parameter is too large.\n" );
        ret = 1;
    }
    /*clean up*/
    av_packet_free(&avpkt_clone);
    av_packet_unref(&avpkt);


    return ret;
}
