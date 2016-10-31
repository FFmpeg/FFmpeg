/*
 * Copyright (c) 2016 Google Inc.
 * Copyright (c) 2016 KongQun Yang (kqyang@google.com)
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

/**
 * @file
 * internal header for VPx codec configuration utilities.
 */

#ifndef AVFORMAT_VPCC_H
#define AVFORMAT_VPCC_H

#include <stdint.h>
#include "avio.h"
#include "avformat.h"
#include "libavcodec/avcodec.h"

/**
 * Writes VP codec configuration to the provided AVIOContext.
 *
 * @param s address of the AVFormatContext for the logging context.
 * @param pb address of the AVIOContext where the vpcC shall be written.
 * @param par address of the AVCodecParameters which contains codec information.
 * @return >=0 in case of success, a negative value corresponding to an AVERROR
 *         code in case of failure
 */
int ff_isom_write_vpcc(AVFormatContext *s, AVIOContext *pb,
                       AVCodecParameters *par);

#endif /* AVFORMAT_VPCC_H */
