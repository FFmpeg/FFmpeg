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

static int FUNC(sei_user_data_registered)
    (CodedBitstreamContext *ctx, RWContext *rw,
     SEIRawUserDataRegistered *current, uint32_t *payload_size)
{
    int err, i, j;

    HEADER("User Data Registered ITU-T T.35");

    u(8, itu_t_t35_country_code, 0x00, 0xff);
    if (current->itu_t_t35_country_code != 0xff)
        i = 1;
    else {
        u(8, itu_t_t35_country_code_extension_byte, 0x00, 0xff);
        i = 2;
    }

#ifdef READ
    if (*payload_size < i) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "Invalid SEI user data registered payload.\n");
        return AVERROR_INVALIDDATA;
    }
    current->data_length = *payload_size - i;
#else
    *payload_size = i + current->data_length;
#endif

    allocate(current->data, current->data_length);
    for (j = 0; j < current->data_length; j++)
        xu(8, itu_t_t35_payload_byte[], current->data[j], 0x00, 0xff, 1, i + j);

    return 0;
}

static int FUNC(sei_user_data_unregistered)
    (CodedBitstreamContext *ctx, RWContext *rw,
     SEIRawUserDataUnregistered *current, uint32_t *payload_size)
{
    int err, i;

    HEADER("User Data Unregistered");

#ifdef READ
    if (*payload_size < 16) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "Invalid SEI user data unregistered payload.\n");
        return AVERROR_INVALIDDATA;
    }
    current->data_length = *payload_size - 16;
#else
    *payload_size = 16 + current->data_length;
#endif

    for (i = 0; i < 16; i++)
        us(8, uuid_iso_iec_11578[i], 0x00, 0xff, 1, i);

    allocate(current->data, current->data_length);

    for (i = 0; i < current->data_length; i++)
        xu(8, user_data_payload_byte[i], current->data[i], 0x00, 0xff, 1, i);

    return 0;
}

static int FUNC(sei_mastering_display_colour_volume)
    (CodedBitstreamContext *ctx, RWContext *rw,
     SEIRawMasteringDisplayColourVolume *current)
{
    int err, c;

    HEADER("Mastering Display Colour Volume");

    for (c = 0; c < 3; c++) {
        us(16, display_primaries_x[c], 0, 50000, 1, c);
        us(16, display_primaries_y[c], 0, 50000, 1, c);
    }

    u(16, white_point_x, 0, 50000);
    u(16, white_point_y, 0, 50000);

    u(32, max_display_mastering_luminance,
      1, MAX_UINT_BITS(32));
    u(32, min_display_mastering_luminance,
      0, current->max_display_mastering_luminance - 1);

    return 0;
}

static int FUNC(sei_content_light_level)
    (CodedBitstreamContext *ctx, RWContext *rw,
     SEIRawContentLightLevelInfo *current)
{
    int err;

    HEADER("Content Light Level");

    ub(16, max_content_light_level);
    ub(16, max_pic_average_light_level);

    return 0;
}

static int FUNC(sei_alternative_transfer_characteristics)
    (CodedBitstreamContext *ctx, RWContext *rw,
     SEIRawAlternativeTransferCharacteristics *current)
{
    int err;

    HEADER("Alternative Transfer Characteristics");

    ub(8, preferred_transfer_characteristics);

    return 0;
}
