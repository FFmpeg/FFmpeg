/**
 * @file oggvorbis.h
 * oggvorbis.
 */

#ifndef AVCODEC_OGGVORBIS_H
#define AVCODEC_OGGVORBIS_H

#include <vorbis/vorbisenc.h>

#include "avcodec.h"

int oggvorbis_init_encoder(vorbis_info *vi, AVCodecContext *avccontext) ;

#endif
