/*
 * SSA/ASS common functions
 * Copyright (c) 2010  Aurelien Jacobs <aurel@gnuage.org>
 *
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
 * Generate a suitable AVCodecContext.subtitle_header for SUBTITLE_ASS
 * with default style.
 *
 * @param avctx pointer to the AVCodecContext
 * @return >= 0 on success otherwise an error code <0
 */
int ff_ass_subtitle_header_default(AVCodecContext *avctx);

/**
 * Initialize an AVSubtitle structure for use with ff_ass_add_rect().
 *
 * @param sub pointer to the AVSubtitle
 */
void ff_ass_init(AVSubtitle *sub);

/**
 * Add an ASS dialog line to an AVSubtitle as a new AVSubtitleRect.
 *
 * @param sub pointer to the AVSubtitle
 * @param dialog ASS dialog to add to sub
 * @param ts_start start timestamp for this dialog (in 1/100 second unit)
 * @param ts_end end timestamp for this dialog (in 1/100 second unit)
 * @param raw when set to 1, it indicates that dialog contains a whole ASS
 *                           dialog line which should be copied as is.
 *            when set to 0, it indicates that dialog contains only the Text
 *                           part of the ASS dialog line, the rest of the line
 *                           will be generated.
 * @return number of characters read from dialog. It can be less than the whole
 *         length of dialog, if dialog contains several lines of text.
 *         A negative value indicates an error.
 */
int ff_ass_add_rect(AVSubtitle *sub, const char *dialog,
                    int ts_start, int ts_end, int raw);

#endif /* AVCODEC_ASS_H */
