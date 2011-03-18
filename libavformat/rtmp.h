/*
 * RTMP definitions
 * Copyright (c) 2009 Kostya Shishkov
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVFORMAT_RTMP_H
#define AVFORMAT_RTMP_H

#include "avformat.h"

#define RTMP_DEFAULT_PORT 1935

#define RTMP_HANDSHAKE_PACKET_SIZE 1536

/**
 * emulated Flash client version - 9.0.124.2 on Linux
 * @{
 */
#define RTMP_CLIENT_PLATFORM "LNX"
#define RTMP_CLIENT_VER1    9
#define RTMP_CLIENT_VER2    0
#define RTMP_CLIENT_VER3  124
#define RTMP_CLIENT_VER4    2
/** @} */ //version defines

#endif /* AVFORMAT_RTMP_H */
