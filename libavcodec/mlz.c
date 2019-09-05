/*
 * Copyright (c) 2016 Umair Khan <omerjerk@gmail.com>
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

#include "mlz.h"

av_cold void ff_mlz_init_dict(void* context, MLZ *mlz) {
    mlz->dict = av_mallocz_array(TABLE_SIZE, sizeof(*mlz->dict));

    mlz->flush_code            = FLUSH_CODE;
    mlz->current_dic_index_max = DIC_INDEX_INIT;
    mlz->dic_code_bit          = CODE_BIT_INIT;
    mlz->bump_code             = (DIC_INDEX_INIT - 1);
    mlz->next_code             = FIRST_CODE;
    mlz->freeze_flag           = 0;
    mlz->context               = context;
}

av_cold void ff_mlz_flush_dict(MLZ *mlz) {
    MLZDict *dict = mlz->dict;
    int i;
    for ( i = 0; i < TABLE_SIZE; i++ ) {
        dict[i].string_code = CODE_UNSET;
        dict[i].parent_code = CODE_UNSET;
        dict[i].match_len = 0;
    }
    mlz->current_dic_index_max = DIC_INDEX_INIT;
    mlz->dic_code_bit          = CODE_BIT_INIT;  // DicCodeBitInit;
    mlz->bump_code             = mlz->current_dic_index_max - 1;
    mlz->next_code             = FIRST_CODE;
    mlz->freeze_flag           = 0;
}

static void set_new_entry_dict(MLZDict* dict, int string_code, int parent_code, int char_code) {
    dict[string_code].parent_code = parent_code;
    dict[string_code].string_code = string_code;
    dict[string_code].char_code   = char_code;
    if (parent_code < FIRST_CODE) {
        dict[string_code].match_len = 2;
    } else {
        dict[string_code].match_len = (dict[parent_code].match_len) + 1;
    }
}

static int decode_string(MLZ* mlz, unsigned char *buff, int string_code, int *first_char_code, unsigned long bufsize) {
    MLZDict* dict = mlz->dict;
    unsigned long count, offset;
    int current_code, parent_code, tmp_code;

    count            = 0;
    current_code     = string_code;
    *first_char_code = CODE_UNSET;

    while (count < bufsize) {
        switch (current_code) {
        case CODE_UNSET:
            return count;
            break;
        default:
            if (current_code < FIRST_CODE) {
                *first_char_code = current_code;
                buff[0] = current_code;
                count++;
                return count;
            } else {
                offset  = dict[current_code].match_len - 1;
                tmp_code = dict[current_code].char_code;
                if (offset >= bufsize) {
                    av_log(mlz->context, AV_LOG_ERROR, "MLZ offset error.\n");
                    return count;
                }
                buff[offset] = tmp_code;
                count++;
            }
            current_code = dict[current_code].parent_code;
            if ((current_code < 0) || (current_code > (DIC_INDEX_MAX - 1))) {
                av_log(mlz->context, AV_LOG_ERROR, "MLZ dic index error.\n");
                return count;
            }
            if (current_code > FIRST_CODE) {
                parent_code = dict[current_code].parent_code;
                offset = (dict[current_code].match_len) - 1;
                if (parent_code < 0 || parent_code > DIC_INDEX_MAX-1) {
                    av_log(mlz->context, AV_LOG_ERROR, "MLZ dic index error.\n");
                    return count;
                }
                if (( offset > (DIC_INDEX_MAX - 1))) {
                    av_log(mlz->context, AV_LOG_ERROR, "MLZ dic offset error.\n");
                    return count;
                }
            }
            break;
        }
    }
    return count;
}

static int input_code(GetBitContext* gb, int len) {
    int tmp_code = 0;
    int i;
    for (i = 0; i < len; ++i) {
        tmp_code |= get_bits1(gb) << i;
    }
    return tmp_code;
}

int ff_mlz_decompression(MLZ* mlz, GetBitContext* gb, int size, unsigned char *buff) {
    MLZDict *dict = mlz->dict;
    unsigned long output_chars;
    int string_code, last_string_code, char_code;

    string_code = 0;
    char_code   = -1;
    last_string_code = -1;
    output_chars = 0;

    while (output_chars < size) {
        string_code = input_code(gb, mlz->dic_code_bit);
        switch (string_code) {
            case FLUSH_CODE:
            case MAX_CODE:
                ff_mlz_flush_dict(mlz);
                char_code = -1;
                last_string_code = -1;
                break;
            case FREEZE_CODE:
                mlz->freeze_flag = 1;
                break;
            default:
                if (string_code > mlz->current_dic_index_max) {
                    av_log(mlz->context, AV_LOG_ERROR, "String code %d exceeds maximum value of %d.\n", string_code, mlz->current_dic_index_max);
                    return output_chars;
                }
                if (string_code == (int) mlz->bump_code) {
                    ++mlz->dic_code_bit;
                    mlz->current_dic_index_max *= 2;
                    mlz->bump_code = mlz->current_dic_index_max - 1;
                } else {
                    if (string_code >= mlz->next_code) {
                        int ret = decode_string(mlz, &buff[output_chars], last_string_code, &char_code, size - output_chars);
                        if (ret < 0 || ret > size - output_chars) {
                            av_log(mlz->context, AV_LOG_ERROR, "output chars overflow\n");
                            return output_chars;
                        }
                        output_chars += ret;
                        ret = decode_string(mlz, &buff[output_chars], char_code, &char_code, size - output_chars);
                        if (ret < 0 || ret > size - output_chars) {
                            av_log(mlz->context, AV_LOG_ERROR, "output chars overflow\n");
                            return output_chars;
                        }
                        output_chars += ret;
                        set_new_entry_dict(dict, mlz->next_code, last_string_code, char_code);
                        if (mlz->next_code >= TABLE_SIZE - 1) {
                            av_log(mlz->context, AV_LOG_ERROR, "Too many MLZ codes\n");
                            return output_chars;
                        }
                        mlz->next_code++;
                    } else {
                        int ret = decode_string(mlz, &buff[output_chars], string_code, &char_code, size - output_chars);
                        if (ret < 0 || ret > size - output_chars) {
                            av_log(mlz->context, AV_LOG_ERROR, "output chars overflow\n");
                            return output_chars;
                        }
                        output_chars += ret;
                        if (output_chars <= size && !mlz->freeze_flag) {
                            if (last_string_code != -1) {
                                set_new_entry_dict(dict, mlz->next_code, last_string_code, char_code);
                                if (mlz->next_code >= TABLE_SIZE - 1) {
                                    av_log(mlz->context, AV_LOG_ERROR, "Too many MLZ codes\n");
                                    return output_chars;
                                }
                                mlz->next_code++;
                            }
                        } else {
                            break;
                        }
                    }
                    last_string_code = string_code;
                }
                break;
        }
    }
    return output_chars;
}
