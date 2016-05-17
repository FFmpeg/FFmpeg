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

#undef _GNU_SOURCE
#define _XOPEN_SOURCE 600 /* XSI-compliant version of strerror_r */
#include "avutil.h"
#include "avstring.h"
#include "common.h"

struct error_entry {
    int num;
    const char *tag;
    const char *str;
};

#define ERROR_TAG(tag) AVERROR_##tag, #tag
#define EERROR_TAG(tag) AVERROR(tag), #tag
#define AVERROR_INPUT_AND_OUTPUT_CHANGED (AVERROR_INPUT_CHANGED | AVERROR_OUTPUT_CHANGED)
static const struct error_entry error_entries[] = {
    { ERROR_TAG(BSF_NOT_FOUND),      "Bitstream filter not found"                     },
    { ERROR_TAG(BUG),                "Internal bug, should not have happened"         },
    { ERROR_TAG(BUG2),               "Internal bug, should not have happened"         },
    { ERROR_TAG(BUFFER_TOO_SMALL),   "Buffer too small"                               },
    { ERROR_TAG(DECODER_NOT_FOUND),  "Decoder not found"                              },
    { ERROR_TAG(DEMUXER_NOT_FOUND),  "Demuxer not found"                              },
    { ERROR_TAG(ENCODER_NOT_FOUND),  "Encoder not found"                              },
    { ERROR_TAG(EOF),                "End of file"                                    },
    { ERROR_TAG(EXIT),               "Immediate exit requested"                       },
    { ERROR_TAG(EXTERNAL),           "Generic error in an external library"           },
    { ERROR_TAG(FILTER_NOT_FOUND),   "Filter not found"                               },
    { ERROR_TAG(INPUT_CHANGED),      "Input changed"                                  },
    { ERROR_TAG(INVALIDDATA),        "Invalid data found when processing input"       },
    { ERROR_TAG(MUXER_NOT_FOUND),    "Muxer not found"                                },
    { ERROR_TAG(OPTION_NOT_FOUND),   "Option not found"                               },
    { ERROR_TAG(OUTPUT_CHANGED),     "Output changed"                                 },
    { ERROR_TAG(PATCHWELCOME),       "Not yet implemented in FFmpeg, patches welcome" },
    { ERROR_TAG(PROTOCOL_NOT_FOUND), "Protocol not found"                             },
    { ERROR_TAG(STREAM_NOT_FOUND),   "Stream not found"                               },
    { ERROR_TAG(UNKNOWN),            "Unknown error occurred"                         },
    { ERROR_TAG(EXPERIMENTAL),       "Experimental feature"                           },
    { ERROR_TAG(INPUT_AND_OUTPUT_CHANGED), "Input and output changed"                 },
    { ERROR_TAG(HTTP_BAD_REQUEST),   "Server returned 400 Bad Request"         },
    { ERROR_TAG(HTTP_UNAUTHORIZED),  "Server returned 401 Unauthorized (authorization failed)" },
    { ERROR_TAG(HTTP_FORBIDDEN),     "Server returned 403 Forbidden (access denied)" },
    { ERROR_TAG(HTTP_NOT_FOUND),     "Server returned 404 Not Found"           },
    { ERROR_TAG(HTTP_OTHER_4XX),     "Server returned 4XX Client Error, but not one of 40{0,1,3,4}" },
    { ERROR_TAG(HTTP_SERVER_ERROR),  "Server returned 5XX Server Error reply" },
#if !HAVE_STRERROR_R
    { EERROR_TAG(E2BIG),             "Argument list too long" },
    { EERROR_TAG(EACCES),            "Permission denied" },
    { EERROR_TAG(EAGAIN),            "Resource temporarily unavailable" },
    { EERROR_TAG(EBADF),             "Bad file descriptor" },
    { EERROR_TAG(EBUSY),             "Device or resource busy" },
    { EERROR_TAG(ECHILD),            "No child processes" },
    { EERROR_TAG(EDEADLK),           "Resource deadlock avoided" },
    { EERROR_TAG(EDOM),              "Numerical argument out of domain" },
    { EERROR_TAG(EEXIST),            "File exists" },
    { EERROR_TAG(EFAULT),            "Bad address" },
    { EERROR_TAG(EFBIG),             "File too large" },
    { EERROR_TAG(EILSEQ),            "Illegal byte sequence" },
    { EERROR_TAG(EINTR),             "Interrupted system call" },
    { EERROR_TAG(EINVAL),            "Invalid argument" },
    { EERROR_TAG(EIO),               "I/O error" },
    { EERROR_TAG(EISDIR),            "Is a directory" },
    { EERROR_TAG(EMFILE),            "Too many open files" },
    { EERROR_TAG(EMLINK),            "Too many links" },
    { EERROR_TAG(ENAMETOOLONG),      "File name too long" },
    { EERROR_TAG(ENFILE),            "Too many open files in system" },
    { EERROR_TAG(ENODEV),            "No such device" },
    { EERROR_TAG(ENOENT),            "No such file or directory" },
    { EERROR_TAG(ENOEXEC),           "Exec format error" },
    { EERROR_TAG(ENOLCK),            "No locks available" },
    { EERROR_TAG(ENOMEM),            "Cannot allocate memory" },
    { EERROR_TAG(ENOSPC),            "No space left on device" },
    { EERROR_TAG(ENOSYS),            "Function not implemented" },
    { EERROR_TAG(ENOTDIR),           "Not a directory" },
    { EERROR_TAG(ENOTEMPTY),         "Directory not empty" },
    { EERROR_TAG(ENOTTY),            "Inappropriate I/O control operation" },
    { EERROR_TAG(ENXIO),             "No such device or address" },
    { EERROR_TAG(EPERM),             "Operation not permitted" },
    { EERROR_TAG(EPIPE),             "Broken pipe" },
    { EERROR_TAG(ERANGE),            "Result too large" },
    { EERROR_TAG(EROFS),             "Read-only file system" },
    { EERROR_TAG(ESPIPE),            "Illegal seek" },
    { EERROR_TAG(ESRCH),             "No such process" },
    { EERROR_TAG(EXDEV),             "Cross-device link" },
#endif
};

int av_strerror(int errnum, char *errbuf, size_t errbuf_size)
{
    int ret = 0, i;
    const struct error_entry *entry = NULL;

    for (i = 0; i < FF_ARRAY_ELEMS(error_entries); i++) {
        if (errnum == error_entries[i].num) {
            entry = &error_entries[i];
            break;
        }
    }
    if (entry) {
        av_strlcpy(errbuf, entry->str, errbuf_size);
    } else {
#if HAVE_STRERROR_R
        ret = AVERROR(strerror_r(AVUNERROR(errnum), errbuf, errbuf_size));
#else
        ret = -1;
#endif
        if (ret < 0)
            snprintf(errbuf, errbuf_size, "Error number %d occurred", errnum);
    }

    return ret;
}
