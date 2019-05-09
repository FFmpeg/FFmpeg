/*
 * FITS implementation of common functions
 * Copyright (c) 2017 Paras Chadha
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

#include "avcodec.h"
#include "libavutil/dict.h"
#include "fits.h"

int avpriv_fits_header_init(FITSHeader *header, FITSHeaderState state)
{
    header->state = state;
    header->naxis_index = 0;
    header->blank_found = 0;
    header->pcount = 0;
    header->gcount = 1;
    header->groups = 0;
    header->rgb = 0;
    header->image_extension = 0;
    header->bscale = 1.0;
    header->bzero = 0;
    header->data_min_found = 0;
    header->data_max_found = 0;
    return 0;
}

static int dict_set_if_not_null(AVDictionary ***metadata, char *keyword, char *value)
{
    if (metadata)
        av_dict_set(*metadata, keyword, value, 0);
    return 0;
}

/**
 * Extract keyword and value from a header line (80 bytes) and store them in keyword and value strings respectively
 * @param ptr8 pointer to the data
 * @param keyword pointer to the char array in which keyword is to be stored
 * @param value pointer to the char array in which value is to be stored
 * @return 0 if calculated successfully otherwise AVERROR_INVALIDDATA
 */
static int read_keyword_value(const uint8_t *ptr8, char *keyword, char *value)
{
    int i;

    for (i = 0; i < 8 && ptr8[i] != ' '; i++) {
        keyword[i] = ptr8[i];
    }
    keyword[i] = '\0';

    if (ptr8[8] == '=') {
        i = 10;
        while (i < 80 && ptr8[i] == ' ') {
            i++;
        }

        if (i < 80) {
            *value++ = ptr8[i];
            i++;
            if (ptr8[i-1] == '\'') {
                for (; i < 80 && ptr8[i] != '\''; i++) {
                    *value++ = ptr8[i];
                }
                *value++ = '\'';
            } else if (ptr8[i-1] == '(') {
                for (; i < 80 && ptr8[i] != ')'; i++) {
                    *value++ = ptr8[i];
                }
                *value++ = ')';
            } else {
                for (; i < 80 && ptr8[i] != ' ' && ptr8[i] != '/'; i++) {
                    *value++ = ptr8[i];
                }
            }
        }
    }
    *value = '\0';
    return 0;
}

#define CHECK_KEYWORD(key) \
    if (strcmp(keyword, key)) { \
        av_log(avcl, AV_LOG_ERROR, "expected %s keyword, found %s = %s\n", key, keyword, value); \
        return AVERROR_INVALIDDATA; \
    }

#define CHECK_VALUE(key, val) \
    if (sscanf(value, "%d", &header->val) != 1) { \
        av_log(avcl, AV_LOG_ERROR, "invalid value of %s keyword, %s = %s\n", key, keyword, value); \
        return AVERROR_INVALIDDATA; \
    }

int avpriv_fits_header_parse_line(void *avcl, FITSHeader *header, const uint8_t line[80], AVDictionary ***metadata)
{
    int dim_no, ret;
    int64_t t;
    double d;
    char keyword[10], value[72], c;

    read_keyword_value(line, keyword, value);
    switch (header->state) {
    case STATE_SIMPLE:
        CHECK_KEYWORD("SIMPLE");

        if (value[0] == 'F') {
            av_log(avcl, AV_LOG_WARNING, "not a standard FITS file\n");
        } else if (value[0] != 'T') {
            av_log(avcl, AV_LOG_ERROR, "invalid value of SIMPLE keyword, SIMPLE = %c\n", value[0]);
            return AVERROR_INVALIDDATA;
        }

        header->state = STATE_BITPIX;
        break;
    case STATE_XTENSION:
        CHECK_KEYWORD("XTENSION");

        if (!strcmp(value, "'IMAGE   '")) {
            header->image_extension = 1;
        }

        header->state = STATE_BITPIX;
        break;
    case STATE_BITPIX:
        CHECK_KEYWORD("BITPIX");
        CHECK_VALUE("BITPIX", bitpix);

        switch(header->bitpix) {
        case   8:
        case  16:
        case  32: case -32:
        case  64: case -64: break;
        default:
            av_log(avcl, AV_LOG_ERROR, "invalid value of BITPIX %d\n", header->bitpix); \
            return AVERROR_INVALIDDATA;
        }

        dict_set_if_not_null(metadata, keyword, value);

        header->state = STATE_NAXIS;
        break;
    case STATE_NAXIS:
        CHECK_KEYWORD("NAXIS");
        CHECK_VALUE("NAXIS", naxis);
        dict_set_if_not_null(metadata, keyword, value);

        if (header->naxis) {
            header->state = STATE_NAXIS_N;
        } else {
            header->state = STATE_REST;
        }
        break;
    case STATE_NAXIS_N:
        ret = sscanf(keyword, "NAXIS%d", &dim_no);
        if (ret != 1 || dim_no != header->naxis_index + 1) {
            av_log(avcl, AV_LOG_ERROR, "expected NAXIS%d keyword, found %s = %s\n", header->naxis_index + 1, keyword, value);
            return AVERROR_INVALIDDATA;
        }

        if (sscanf(value, "%d", &header->naxisn[header->naxis_index]) != 1) {
            av_log(avcl, AV_LOG_ERROR, "invalid value of NAXIS%d keyword, %s = %s\n", header->naxis_index + 1, keyword, value);
            return AVERROR_INVALIDDATA;
        }

        dict_set_if_not_null(metadata, keyword, value);
        header->naxis_index++;
        if (header->naxis_index == header->naxis) {
            header->state = STATE_REST;
        }
        break;
    case STATE_REST:
        if (!strcmp(keyword, "BLANK") && sscanf(value, "%"SCNd64"", &t) == 1) {
            header->blank = t;
            header->blank_found = 1;
        } else if (!strcmp(keyword, "BSCALE") && sscanf(value, "%lf", &d) == 1) {
            header->bscale = d;
        } else if (!strcmp(keyword, "BZERO") && sscanf(value, "%lf", &d) == 1) {
            header->bzero = d;
        } else if (!strcmp(keyword, "CTYPE3") && !strncmp(value, "'RGB", 4)) {
            header->rgb = 1;
        } else if (!strcmp(keyword, "DATAMAX") && sscanf(value, "%lf", &d) == 1) {
            header->data_max_found = 1;
            header->data_max = d;
        } else if (!strcmp(keyword, "DATAMIN") && sscanf(value, "%lf", &d) == 1) {
            header->data_min_found = 1;
            header->data_min = d;
        } else if (!strcmp(keyword, "END")) {
            return 1;
        } else if (!strcmp(keyword, "GROUPS") && sscanf(value, "%c", &c) == 1) {
            header->groups = (c == 'T');
        } else if (!strcmp(keyword, "GCOUNT") && sscanf(value, "%"SCNd64"", &t) == 1) {
            header->gcount = t;
        } else if (!strcmp(keyword, "PCOUNT") && sscanf(value, "%"SCNd64"", &t) == 1) {
            header->pcount = t;
        }
        dict_set_if_not_null(metadata, keyword, value);
        break;
    }
    return 0;
}
