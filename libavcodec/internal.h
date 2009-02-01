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

/**
 * @file libavcodec/internal.h
 * common internal api header.
 */

#ifndef AVCODEC_INTERNAL_H
#define AVCODEC_INTERNAL_H

/**
 * Logs a generic warning message about a missing feature.
 * @param[in] avc a pointer to an arbitrary struct of which the first field is
 * a pointer to an AVClass struct
 * @param[in] feature string containing the name of the missing feature
 * @param[in] want_sample indicates if samples are wanted which exhibit this feature.
 * If \p want_sample is non-zero, additional verbage will be added to the log
 * message which tells the user how to report samples to the development
 * mailing list.
 */
void ff_log_missing_feature(void *avc, const char *feature, int want_sample);

/**
 * Logs a generic warning message asking for a sample.
 * @param[in] avc a pointer to an arbitrary struct of which the first field is
 * a pointer to an AVClass struct
 * @param[in] msg string containing an optional message, or NULL if no message
 */
void ff_log_ask_for_sample(void *avc, const char *msg);

#endif /* AVCODEC_INTERNAL_H */
