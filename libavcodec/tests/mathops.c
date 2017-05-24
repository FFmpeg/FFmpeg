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

#include "libavcodec/mathops.h"

#include <stdlib.h>

int main(void)
{
    unsigned u;

    for(u=0; u<65536; u++) {
        unsigned s = u*u;
        unsigned root = ff_sqrt(s);
        unsigned root_m1 = ff_sqrt(s-1);
        if (s && root != u) {
            fprintf(stderr, "ff_sqrt failed at %u with %u\n", s, root);
            return 1;
        }
        if (u && root_m1 != u - 1) {
            fprintf(stderr, "ff_sqrt failed at %u with %u\n", s, root);
            return 1;
        }
    }
    return 0;
}
