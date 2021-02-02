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

static int FUNC(filler_payload)
    (CodedBitstreamContext *ctx, RWContext *rw,
     SEIRawFillerPayload *current, SEIMessageState *state)
{
    int err, i;

    HEADER("Filler Payload");

#ifdef READ
    current->payload_size = state->payload_size;
#endif

    for (i = 0; i < current->payload_size; i++)
        fixed(8, ff_byte, 0xff);

    return 0;
}

static int FUNC(user_data_registered)
    (CodedBitstreamContext *ctx, RWContext *rw,
     SEIRawUserDataRegistered *current, SEIMessageState *state)
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
    if (state->payload_size < i) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "Invalid SEI user data registered payload.\n");
        return AVERROR_INVALIDDATA;
    }
    current->data_length = state->payload_size - i;
#endif

    allocate(current->data, current->data_length);
    for (j = 0; j < current->data_length; j++)
        xu(8, itu_t_t35_payload_byte[], current->data[j], 0x00, 0xff, 1, i + j);

    return 0;
}

static int FUNC(user_data_unregistered)
    (CodedBitstreamContext *ctx, RWContext *rw,
     SEIRawUserDataUnregistered *current, SEIMessageState *state)
{
    int err, i;

    HEADER("User Data Unregistered");

#ifdef READ
    if (state->payload_size < 16) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "Invalid SEI user data unregistered payload.\n");
        return AVERROR_INVALIDDATA;
    }
    current->data_length = state->payload_size - 16;
#endif

    for (i = 0; i < 16; i++)
        us(8, uuid_iso_iec_11578[i], 0x00, 0xff, 1, i);

    allocate(current->data, current->data_length);

    for (i = 0; i < current->data_length; i++)
        xu(8, user_data_payload_byte[i], current->data[i], 0x00, 0xff, 1, i);

    return 0;
}

static int FUNC(mastering_display_colour_volume)
    (CodedBitstreamContext *ctx, RWContext *rw,
     SEIRawMasteringDisplayColourVolume *current, SEIMessageState *state)
{
    int err, c;

    HEADER("Mastering Display Colour Volume");

    for (c = 0; c < 3; c++) {
        ubs(16, display_primaries_x[c], 1, c);
        ubs(16, display_primaries_y[c], 1, c);
    }

    ub(16, white_point_x);
    ub(16, white_point_y);

    ub(32, max_display_mastering_luminance);
    ub(32, min_display_mastering_luminance);

    return 0;
}

static int FUNC(content_light_level_info)
    (CodedBitstreamContext *ctx, RWContext *rw,
     SEIRawContentLightLevelInfo *current, SEIMessageState *state)
{
    int err;

    HEADER("Content Light Level Information");

    ub(16, max_content_light_level);
    ub(16, max_pic_average_light_level);

    return 0;
}

static int FUNC(alternative_transfer_characteristics)
    (CodedBitstreamContext *ctx, RWContext *rw,
     SEIRawAlternativeTransferCharacteristics *current,
     SEIMessageState *state)
{
    int err;

    HEADER("Alternative Transfer Characteristics");

    ub(8, preferred_transfer_characteristics);

    return 0;
}

static int FUNC(message)(CodedBitstreamContext *ctx, RWContext *rw,
                         SEIRawMessage *current)
{
    const SEIMessageTypeDescriptor *desc;
    int err, i;

    desc = ff_cbs_sei_find_type(ctx, current->payload_type);
    if (desc) {
        SEIMessageState state = {
            .payload_type      = current->payload_type,
            .payload_size      = current->payload_size,
            .extension_present = current->extension_bit_length > 0,
        };
        int start_position, current_position, bits_written;

#ifdef READ
        CHECK(ff_cbs_sei_alloc_message_payload(current, desc));
#endif

        start_position = bit_position(rw);

        CHECK(desc->READWRITE(ctx, rw, current->payload, &state));

        current_position = bit_position(rw);
        bits_written = current_position - start_position;

        if (byte_alignment(rw) || state.extension_present ||
            bits_written < 8 * current->payload_size) {
            size_t bits_left;

#ifdef READ
            GetBitContext tmp = *rw;
            int trailing_bits, trailing_zero_bits;

            bits_left = 8 * current->payload_size - bits_written;
            if (bits_left > 8)
                skip_bits_long(&tmp, bits_left - 8);
            trailing_bits = get_bits(&tmp, FFMIN(bits_left, 8));
            if (trailing_bits == 0) {
                // The trailing bits must contain a bit_equal_to_one, so
                // they can't all be zero.
                return AVERROR_INVALIDDATA;
            }
            trailing_zero_bits = ff_ctz(trailing_bits);
            current->extension_bit_length =
                bits_left - 1 - trailing_zero_bits;
#endif

            if (current->extension_bit_length > 0) {
                allocate(current->extension_data,
                         (current->extension_bit_length + 7) / 8);

                bits_left = current->extension_bit_length;
                for (i = 0; bits_left > 0; i++) {
                    int length = FFMIN(bits_left, 8);
                    xu(length, reserved_payload_extension_data,
                       current->extension_data[i],
                       0, MAX_UINT_BITS(length), 0);
                    bits_left -= length;
                }
            }

            fixed(1, bit_equal_to_one, 1);
            while (byte_alignment(rw))
                fixed(1, bit_equal_to_zero, 0);
        }

#ifdef WRITE
        current->payload_size = (put_bits_count(rw) - start_position) / 8;
#endif
    } else {
        uint8_t *data;

        allocate(current->payload, current->payload_size);
        data = current->payload;

        for (i = 0; i < current->payload_size; i++)
            xu(8, payload_byte[i], data[i], 0, 255, 1, i);
    }

    return 0;
}

static int FUNC(message_list)(CodedBitstreamContext *ctx, RWContext *rw,
                              SEIRawMessageList *current, int prefix)
{
    SEIRawMessage *message;
    int err, k;

#ifdef READ
    for (k = 0;; k++) {
        uint32_t payload_type = 0;
        uint32_t payload_size = 0;
        uint32_t tmp;
        GetBitContext payload_gbc;

        while (show_bits(rw, 8) == 0xff) {
            fixed(8, ff_byte, 0xff);
            payload_type += 255;
        }
        xu(8, last_payload_type_byte, tmp, 0, 254, 0);
        payload_type += tmp;

        while (show_bits(rw, 8) == 0xff) {
            fixed(8, ff_byte, 0xff);
            payload_size += 255;
        }
        xu(8, last_payload_size_byte, tmp, 0, 254, 0);
        payload_size += tmp;

        // There must be space remaining for both the payload and
        // the trailing bits on the SEI NAL unit.
        if (payload_size + 1 > get_bits_left(rw) / 8) {
            av_log(ctx->log_ctx, AV_LOG_ERROR,
                   "Invalid SEI message: payload_size too large "
                   "(%"PRIu32" bytes).\n", payload_size);
            return AVERROR_INVALIDDATA;
        }
        CHECK(init_get_bits(&payload_gbc, rw->buffer,
                            get_bits_count(rw) + 8 * payload_size));
        skip_bits_long(&payload_gbc, get_bits_count(rw));

        CHECK(ff_cbs_sei_list_add(current));
        message = &current->messages[k];

        message->payload_type = payload_type;
        message->payload_size = payload_size;

        CHECK(FUNC(message)(ctx, &payload_gbc, message));

        skip_bits_long(rw, 8 * payload_size);

        if (!cbs_h2645_read_more_rbsp_data(rw))
            break;
    }
#else
    for (k = 0; k < current->nb_messages; k++) {
        PutBitContext start_state;
        uint32_t tmp;
        int trace, i;

        message = &current->messages[k];

        // We write the payload twice in order to find the size.  Trace
        // output is switched off for the first write.
        trace = ctx->trace_enable;
        ctx->trace_enable = 0;

        start_state = *rw;
        for (i = 0; i < 2; i++) {
            *rw = start_state;

            tmp = message->payload_type;
            while (tmp >= 255) {
                fixed(8, ff_byte, 0xff);
                tmp -= 255;
            }
            xu(8, last_payload_type_byte, tmp, 0, 254, 0);

            tmp = message->payload_size;
            while (tmp >= 255) {
                fixed(8, ff_byte, 0xff);
                tmp -= 255;
            }
            xu(8, last_payload_size_byte, tmp, 0, 254, 0);

            err = FUNC(message)(ctx, rw, message);
            ctx->trace_enable = trace;
            if (err < 0)
                return err;
        }
    }
#endif

    return 0;
}
