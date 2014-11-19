/*
 * Copyright (C) 2010-2011 FFmpeg for WinRT ARM project
 *
 * Authors: Jesse Jiang <qyljcy@163.com>
 *          PPLive Inc. <http://www.pptv.com/>
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
 * WinRT API convert
 */

#ifndef FFMPEG_COMPAT_WINRTAPICONVERT_H
#define FFMPEG_COMPAT_WINRTAPICONVERT_H
#include <Synchapi.h>
#include <MemoryApi.h>

#define Sleep(x) WaitForSingleObjectEx(GetCurrentProcess(),x,FALSE)

#define _beginthreadex(a, b, c, d, e, f) CreateThread(a, b, c, d, e, f)

// d is dwMaximumSizeHigh ,e is dwMaximumSizeLow as theay are all 0 in codes so CreateFileMappingFromApp should be 0
#define CreateFileMapping(a, b, c, d, e, f) CreateFileMappingFromApp(a, b, c, e, f) 
// same as CreateFileMapping
#define MapViewOfFile(a, b, c, d, e) MapViewOfFileFromApp(a, b, d, e) 

#endif /*FFMPEG_COMPAT_WINRTAPICONVERT_H*/