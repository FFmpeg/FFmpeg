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

#include "libavutil/error.c"

static const char *const tag_list[] = {
#define ERROR_TAG(CODE, DESC) #CODE,
#define ERROR_TAG2(CODE, CODE2, DESC) #CODE,
    AVERROR_LIST(ERROR_TAG, ERROR_TAG2)
#if !HAVE_STRERROR_R
    STRERROR_LIST(ERROR_TAG)
#endif
};

int main(void)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(error_entries); i++) {
        const struct ErrorEntry *entry = &error_entries[i];
        printf("%d: %s [%s]\n", entry->num, av_err2str(entry->num), tag_list[i]);
    }

    for (i = 0; i < 256; i++) {
        printf("%d: %s\n", -i, av_err2str(-i));
    }

    return 0;
}
