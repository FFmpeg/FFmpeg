/*
 * SSA/ASS common functions
 * Copyright (c) 2010  Aurelien Jacobs <aurel@gnuage.org>
 *
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

#ifndef AVCODEC_ASS_H
#define AVCODEC_ASS_H

#include "avcodec.h"

/**
 * @name Default values for ASS style
 * @{
 */
#define ASS_DEFAULT_FONT        "Arial"
#define ASS_DEFAULT_FONT_SIZE   16
#define ASS_DEFAULT_COLOR       0xffffff
#define ASS_DEFAULT_BACK_COLOR  0
#define ASS_DEFAULT_BOLD        0
#define ASS_DEFAULT_ITALIC      0
#define ASS_DEFAULT_UNDERLINE   0
#define ASS_DEFAULT_ALIGNMENT   2
/** @} */

/**
 * Generate a suitable AVCodecContext.subtitle_header for SUBTITLE_ASS.
 *
 * @param avctx pointer to the AVCodecContext
 * @param font name of the default font face to use
 * @param font_size default font size to use
 * @param color default text color to use (ABGR)
 * @param back_color default background color to use (ABGR)
 * @param bold 1 for bold text, 0 for normal text
 * @param italic 1 for italic text, 0 for normal text
 * @param underline 1 for underline text, 0 for normal text
 * @param alignment position of the text (left, center, top...), defined after
 *                  the layout of the numpad (1-3 sub, 4-6 mid, 7-9 top)
 * @return >= 0 on success otherwise an error code <0
 */
int ff_ass_subtitle_header(AVCodecContext *avctx,
                           const char *font, int font_size,
                           int color, int back_color,
                           int bold, int italic, int underline,
                           int alignment);

/**
 * Generate a suitable AVCodecContext.subtitle_header for SUBTITLE_ASS
 * with default style.
 *
 * @param avctx pointer to the AVCodecContext
 * @return >= 0 on success otherwise an error code <0
 */
int ff_ass_subtitle_header_default(AVCodecContext *avctx);

/**
 * Add an ASS dialog line to an AVSubtitle as a new AVSubtitleRect.
 *
 * @param sub pointer to the AVSubtitle
 * @param dialog ASS dialog to add to sub
 * @param ts_start start timestamp for this dialog (in 1/100 second unit)
 * @param duration duration for this dialog (in 1/100 second unit), can be -1
 *                 to last until the end of the presentation
 * @param raw when set to 2, it indicates that dialog contains an ASS
 *                           dialog line as muxed in Matroska
 *            when set to 1, it indicates that dialog contains a whole SSA
 *                           dialog line which should be copied as is.
 *            when set to 0, it indicates that dialog contains only the Text
 *                           part of the ASS dialog line, the rest of the line
 *                           will be generated.
 * @return number of characters read from dialog. It can be less than the whole
 *         length of dialog, if dialog contains several lines of text.
 *         A negative value indicates an error.
 */
int ff_ass_add_rect(AVSubtitle *sub, const char *dialog,
                    int ts_start, int duration, int raw);

#endif /* AVCODEC_ASS_H */
