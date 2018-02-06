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

#include "libavutil/common.h"
#include "libavcodec/htmlsubtitles.c"

static const char * const test_cases[] = {
    /* latin guillemets and other < > garbage */
    "<<hello>>",                            // guillemets
    "<<<b>hello</b>>>",                     // guillemets + tags
    "< hello < 2000 > world >",             // unlikely tags due to spaces
    "<h1>TITLE</h1>",                       // likely unhandled tags
    "< font color=red >red</font>",         // invalid format of valid tag
    "Foo <foo@bar.com>",                    // not a tag (not alnum)

    "<b> foo <I> bar </B> bla </i>",        // broken nesting

    "A<br>B<BR/>C<br  / >D<  Br >E<brk><brk/>", // misc line breaks
};

int main(void)
{
    int i;
    AVBPrint dst;

    av_bprint_init(&dst, 0, AV_BPRINT_SIZE_UNLIMITED);
    for (i = 0; i < FF_ARRAY_ELEMS(test_cases); i++) {
        int ret = ff_htmlmarkup_to_ass(NULL, &dst, test_cases[i]);
        if (ret < 0)
            return ret;
        printf("%s --> %s\n", test_cases[i], dst.str);
        av_bprint_clear(&dst);
    }
    av_bprint_finalize(&dst, NULL);
    return 0;
}
