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
#include <string.h>

#include "libavcodec/packet.h"
#include "libavcodec/packet_internal.h"

#define HANDLE_CHARS "abcdefghijklmnopqrstuvwxyz0123456789_"

#define SET_HANDLE(type, handle)                                          \
    if (handles[AV_PKT_DATA_ ## type]) {                                  \
        printf("%s: second handle %s\n", #type, handle);                  \
        ret = 1;                                                          \
    }                                                                     \
    handles[AV_PKT_DATA_ ## type] = handle;

/*
 * Every side data type needs exactly one handle, and no two types may share
 * one. A handle is typed in options, where '+' and '-' have a meaning, so it
 * is made of [a-z0-9_] only. The handles are printed and compared with the
 * reference, since a released handle must not change.
 */
int main(void)
{
    const char *handles[AV_PKT_DATA_NB] = { NULL };
    int ret = 0;

    FF_PACKET_SIDE_DATA_HANDLES(SET_HANDLE)

    for (int type = 0; type < AV_PKT_DATA_NB; type++) {
        printf("%2d: %s\n", type, handles[type] ? handles[type] : "(none)");
        if (!handles[type])
            ret = 1;
        else if (strspn(handles[type], HANDLE_CHARS) != strlen(handles[type])) {
            printf("%2d: character outside of [a-z0-9_]\n", type);
            ret = 1;
        }

        for (int other = 0; handles[type] && other < type; other++) {
            if (handles[other] && !strcmp(handles[type], handles[other])) {
                printf("%2d: handle of type %d\n", type, other);
                ret = 1;
            }
        }
    }

    return ret;
}
