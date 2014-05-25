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

/**
 * @file
 * error code definitions
 */

#ifndef AVUTIL_ERROR_H
#define AVUTIL_ERROR_H

#include <errno.h>
#include <stddef.h>

/**
 * @addtogroup lavu_error
 *
 * @{
 */


/* error handling */
#if EDOM > 0
#define AVERROR(e) (-(e))   ///< Returns a negative error code from a POSIX error code, to return from library functions.
#define AVUNERROR(e) (-(e)) ///< Returns a POSIX error code from a library function error return value.
#else
/* Some platforms have E* and errno already negated. */
#define AVERROR(e) (e)
#define AVUNERROR(e) (e)
#endif

#define AVERROR_BSF_NOT_FOUND      (-0x39acbd08) ///< Bitstream filter not found
#define AVERROR_DECODER_NOT_FOUND  (-0x3cbabb08) ///< Decoder not found
#define AVERROR_DEMUXER_NOT_FOUND  (-0x32babb08) ///< Demuxer not found
#define AVERROR_ENCODER_NOT_FOUND  (-0x3cb1ba08) ///< Encoder not found
#define AVERROR_EOF                (-0x5fb9b0bb) ///< End of file
#define AVERROR_EXIT               (-0x2bb6a7bb) ///< Immediate exit was requested; the called function should not be restarted
#define AVERROR_FILTER_NOT_FOUND   (-0x33b6b908) ///< Filter not found
#define AVERROR_INVALIDDATA        (-0x3ebbb1b7) ///< Invalid data found when processing input
#define AVERROR_MUXER_NOT_FOUND    (-0x27aab208) ///< Muxer not found
#define AVERROR_OPTION_NOT_FOUND   (-0x2bafb008) ///< Option not found
#define AVERROR_PATCHWELCOME       (-0x3aa8beb0) ///< Not yet implemented in Libav, patches welcome
#define AVERROR_PROTOCOL_NOT_FOUND (-0x30adaf08) ///< Protocol not found
#define AVERROR_STREAM_NOT_FOUND   (-0x2dabac08) ///< Stream not found
#define AVERROR_BUG                (-0x5fb8aabe) ///< Bug detected, please report the issue
#define AVERROR_UNKNOWN            (-0x31b4b1ab) ///< Unknown error, typically from an external library
#define AVERROR_EXPERIMENTAL       (-0x2bb2afa8) ///< Requested feature is flagged experimental. Set strict_std_compliance if you really want to use it.
#define AVERROR_INPUT_CHANGED      (-0x636e6701) ///< Input changed between calls. Reconfiguration is required. (can be OR-ed with AVERROR_OUTPUT_CHANGED)
#define AVERROR_OUTPUT_CHANGED     (-0x636e6702) ///< Output changed between calls. Reconfiguration is required. (can be OR-ed with AVERROR_INPUT_CHANGED)

/**
 * Put a description of the AVERROR code errnum in errbuf.
 * In case of failure the global variable errno is set to indicate the
 * error. Even in case of failure av_strerror() will print a generic
 * error message indicating the errnum provided to errbuf.
 *
 * @param errnum      error code to describe
 * @param errbuf      buffer to which description is written
 * @param errbuf_size the size in bytes of errbuf
 * @return 0 on success, a negative value if a description for errnum
 * cannot be found
 */
int av_strerror(int errnum, char *errbuf, size_t errbuf_size);

/**
 * @}
 */

#endif /* AVUTIL_ERROR_H */
