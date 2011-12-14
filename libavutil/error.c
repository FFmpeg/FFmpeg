/*
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

#include "avutil.h"
#include "avstring.h"

int av_strerror(int errnum, char *errbuf, size_t errbuf_size)
{
    int ret = 0;
    const char *errstr = NULL;

    switch (errnum) {
    case AVERROR_BSF_NOT_FOUND:     errstr = "Bitstream filter not found"                   ; break;
    case AVERROR_DECODER_NOT_FOUND: errstr = "Decoder not found"                            ; break;
    case AVERROR_DEMUXER_NOT_FOUND: errstr = "Demuxer not found"                            ; break;
    case AVERROR_ENCODER_NOT_FOUND: errstr = "Encoder not found"                            ; break;
    case AVERROR_EOF:               errstr = "End of file"                                  ; break;
    case AVERROR_EXIT:              errstr = "Immediate exit requested"                     ; break;
    case AVERROR_FILTER_NOT_FOUND:  errstr = "Filter not found"                             ; break;
    case AVERROR_INVALIDDATA:       errstr = "Invalid data found when processing input"     ; break;
    case AVERROR_MUXER_NOT_FOUND:   errstr = "Muxer not found"                              ; break;
    case AVERROR_OPTION_NOT_FOUND:  errstr = "Option not found"                             ; break;
    case AVERROR_PATCHWELCOME:      errstr = "Not yet implemented in Libav, patches welcome"; break;
    case AVERROR_PROTOCOL_NOT_FOUND:errstr = "Protocol not found"                           ; break;
    case AVERROR_STREAM_NOT_FOUND:  errstr = "Stream not found"                             ; break;
    case AVERROR_BUG:               errstr = "Bug detected, please report the issue"        ; break;
    }

    if (errstr) {
        av_strlcpy(errbuf, errstr, errbuf_size);
    } else {
#if HAVE_STRERROR_R
        ret = strerror_r(AVUNERROR(errnum), errbuf, errbuf_size);
#else
        ret = -1;
#endif
        if (ret < 0)
            snprintf(errbuf, errbuf_size, "Error number %d occurred", errnum);
    }

    return ret;
}
