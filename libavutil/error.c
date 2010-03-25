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

#include "avutil.h"
#include "avstring.h"

int av_strerror(int errnum, char *errbuf, size_t errbuf_size)
{
    int ret = 0;
    const char *errstr = NULL;

    switch (errnum) {
    case AVERROR_EOF:               errstr = "End of file"; break;
    case AVERROR_INVALIDDATA:       errstr = "Invalid data found when processing input"; break;
    case AVERROR_NOTSUPP:           errstr = "Operation not supported"; break;
    case AVERROR_NUMEXPECTED:       errstr = "Number syntax expected in filename"; break;
    case AVERROR_PATCHWELCOME:      errstr = "Not yet implemented in FFmpeg, patches welcome"; break;
    }

    if (errstr) {
        av_strlcpy(errbuf, errstr, errbuf_size);
    } else {
#if HAVE_STRERROR_R
        ret = strerror_r(AVUNERROR(errnum), errbuf, errbuf_size);
#else
        snprintf(errbuf, errbuf_size, "Error number %d occurred", errnum);
#endif
    }

    return ret;
}
