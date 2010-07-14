/*
 * QDesign Music 2 (QDM2) payload for RTP
 * Copyright (c) 2010 Ronald S. Bultje
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

#ifndef AVFORMAT_RTPDEC_QDM2_H
#define AVFORMAT_RTPDEC_QDM2_H

#include "rtpdec.h"

/**
 * QMD2 RTP callbacks.
 */
extern RTPDynamicProtocolHandler ff_qdm2_dynamic_handler;

#endif /* AVFORMAT_RTPDEC_QDM2_H */
