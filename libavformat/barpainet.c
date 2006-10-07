/*
 * copyright (c) 2002 Francois Revol
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

#include <stdlib.h>
#include <strings.h>
#include "barpainet.h"

int inet_aton (const char * str, struct in_addr * add) {
        const char * pch = str;
        unsigned int add1 = 0, add2 = 0, add3 = 0, add4 = 0;

        add1 = atoi(pch);
        pch = strpbrk(pch,".");
        if (pch == 0 || ++pch == 0) goto done;
        add2 = atoi(pch);
        pch = strpbrk(pch,".");
        if (pch == 0 || ++pch == 0) goto done;
        add3 = atoi(pch);
        pch = strpbrk(pch,".");
        if (pch == 0 || ++pch == 0) goto done;
        add4 = atoi(pch);

done:
        add->s_addr=(add4<<24)+(add3<<16)+(add2<<8)+add1;

        return 1;
}
