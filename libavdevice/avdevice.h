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

#ifndef AVDEVICE_AVDEVICE_H
#define AVDEVICE_AVDEVICE_H

#include "version.h"

/**
 * @file
 * @ingroup lavd
 * Main libavdevice API header
 */

/**
 * @defgroup lavd Special devices muxing/demuxing library
 * @{
 * Libavdevice is a complementary library to @ref libavf "libavformat". It
 * provides various "special" platform-specific muxers and demuxers, e.g. for
 * grabbing devices, audio capture and playback etc. As a consequence, the
 * (de)muxers in libavdevice are of the AVFMT_NOFILE type (they use their own
 * I/O functions). The filename passed to avformat_open_input() often does not
 * refer to an actually existing file, but has some special device-specific
 * meaning - e.g. for x11grab it is the display name.
 *
 * To use libavdevice, simply call avdevice_register_all() to register all
 * compiled muxers and demuxers. They all use standard libavformat API.
 * @}
 */

#include "libavformat/avformat.h"

/**
 * Return the LIBAVDEVICE_VERSION_INT constant.
 */
unsigned avdevice_version(void);

/**
 * Return the libavdevice build-time configuration.
 */
const char *avdevice_configuration(void);

/**
 * Return the libavdevice license.
 */
const char *avdevice_license(void);

/**
 * Initialize libavdevice and register all the input and output devices.
 * @warning This function is not thread safe.
 */
void avdevice_register_all(void);

typedef struct AVDeviceRect {
    int x;      /**< x coordinate of top left corner */
    int y;      /**< y coordinate of top left corner */
    int width;  /**< width */
    int height; /**< height */
} AVDeviceRect;

/**
 * Message types used by avdevice_app_to_dev_control_message().
 */
enum AVAppToDevMessageType {
    /**
     * Dummy message.
     */
    AV_APP_TO_DEV_NONE = MKBETAG('N','O','N','E'),

    /**
     * Window size change message.
     *
     * Message is sent to the device every time the application changes the size
     * of the window device renders to.
     * Message should also be sent right after window is created.
     *
     * data: AVDeviceRect: new window size.
     */
    AV_APP_TO_DEV_WINDOW_SIZE = MKBETAG('G','E','O','M'),

    /**
     * Repaint request message.
     *
     * Message is sent to the device when window have to be rapainted.
     *
     * data: AVDeviceRect: area required to be repainted.
     *       NULL: whole area is required to be repainted.
     */
    AV_APP_TO_DEV_WINDOW_REPAINT = MKBETAG('R','E','P','A')
};

/**
 * Send control message from application to device.
 *
 * @param s         device context.
 * @param type      message type.
 * @param data      message data. Exact type depends on message type.
 * @param data_size size of message data.
 * @return >= 0 on success, negative on error.
 *         AVERROR(ENOSYS) when device doesn't implement handler of the message.
 */
int avdevice_app_to_dev_control_message(struct AVFormatContext *s,
                                        enum AVAppToDevMessageType type,
                                        void *data, size_t data_size);

#endif /* AVDEVICE_AVDEVICE_H */
