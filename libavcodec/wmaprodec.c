/**
 *@brief Uninitialize the decoder and free all resources.
 *@param avctx codec context
 *@return 0 on success, < 0 otherwise
 */
static av_cold int decode_end(AVCodecContext *avctx)
{
    WMA3DecodeContext *s = avctx->priv_data;
    int i;

    av_freep(&s->num_sfb);
    av_freep(&s->sfb_offsets);
    av_freep(&s->subwoofer_cutoffs);
    av_freep(&s->sf_offsets);

    for (i = 0 ; i < WMAPRO_BLOCK_SIZES ; i++)
        ff_mdct_end(&s->mdct_ctx[i]);

    return 0;
}

/**
 *@brief Calculate a decorrelation matrix from the bitstream parameters.
 *@param s codec context
 *@param chgroup channel group for which the matrix needs to be calculated
 */
static void decode_decorrelation_matrix(WMA3DecodeContext *s,
                                        WMA3ChannelGroup *chgroup)
{
    int i;
    int offset = 0;
    int8_t rotation_offset[WMAPRO_MAX_CHANNELS * WMAPRO_MAX_CHANNELS];
    memset(chgroup->decorrelation_matrix,0,
           sizeof(float) *s->num_channels * s->num_channels);

    for (i = 0; i < chgroup->num_channels * (chgroup->num_channels - 1) >> 1; i++)
        rotation_offset[i] = get_bits(&s->gb,6);

    for (i = 0; i < chgroup->num_channels; i++)
        chgroup->decorrelation_matrix[chgroup->num_channels * i + i] =
                                                get_bits1(&s->gb) ? 1.0 : -1.0;

    for (i = 1; i < chgroup->num_channels; i++) {
        int x;
        for (x = 0; x < i; x++) {
            int y;
            for (y = 0; y < i + 1 ; y++) {
                float v1 = chgroup->decorrelation_matrix[x * chgroup->num_channels + y];
                float v2 = chgroup->decorrelation_matrix[i * chgroup->num_channels + y];
                int n = rotation_offset[offset + x];
                float sinv;
                float cosv;

                if (n < 32) {
                    sinv = sin64[n];
                    cosv = sin64[32-n];
                } else {
                    sinv = sin64[64-n];
                    cosv = -sin64[n-32];
                }

                chgroup->decorrelation_matrix[y + x * chgroup->num_channels] =
                                               (v1 * sinv) - (v2 * cosv);
                chgroup->decorrelation_matrix[y + i * chgroup->num_channels] =
                                               (v1 * cosv) + (v2 * sinv);
            }
        }
        offset += i;
    }
}

/**
 *@brief Reconstruct the individual channel data.
 *@param s codec context
 */
static void inverse_channel_transform(WMA3DecodeContext *s)
{
    int i;

    for (i = 0; i < s->num_chgroups; i++) {

        if (s->chgroup[i].transform == 1) {
            /** M/S stereo decoding */
            int16_t* sfb_offsets = s->cur_sfb_offsets;
            float* ch0 = *sfb_offsets + s->channel[0].coeffs;
            float* ch1 = *sfb_offsets++ + s->channel[1].coeffs;
            const char* tb = s->chgroup[i].transform_band;
            const char* tb_end = tb + s->num_bands;

            while (tb < tb_end) {
                const float* ch0_end = s->channel[0].coeffs +
                                       FFMIN(*sfb_offsets,s->subframe_len);
                if (*tb++ == 1) {
                    while (ch0 < ch0_end) {
                        const float v1 = *ch0;
                        const float v2 = *ch1;
                        *ch0++ = v1 - v2;
                        *ch1++ = v1 + v2;
                    }
                } else {
                    while (ch0 < ch0_end) {
                        *ch0++ *= 181.0 / 128;
                        *ch1++ *= 181.0 / 128;
                    }
                }
                ++sfb_offsets;
            }
        } else if (s->chgroup[i].transform) {
            float data[WMAPRO_MAX_CHANNELS];
            const int num_channels = s->chgroup[i].num_channels;
            float** ch_data = s->chgroup[i].channel_data;
            float** ch_end = ch_data + num_channels;
            const int8_t* tb = s->chgroup[i].transform_band;
            int16_t* sfb;

            /** multichannel decorrelation */
            for (sfb = s->cur_sfb_offsets ;
                sfb < s->cur_sfb_offsets + s->num_bands;sfb++) {
                if (*tb++ == 1) {
                    int y;
                    /** multiply values with the decorrelation_matrix */
                    for (y = sfb[0]; y < FFMIN(sfb[1], s->subframe_len); y++) {
                        const float* mat = s->chgroup[i].decorrelation_matrix;
                        const float* data_end = data + num_channels;
                        float* data_ptr = data;
                        float** ch;

                        for (ch = ch_data;ch < ch_end; ch++)
                           *data_ptr++ = (*ch)[y];

                        for (ch = ch_data; ch < ch_end; ch++) {
                            float sum = 0;
                            data_ptr = data;
                            while (data_ptr < data_end)
                                sum += *data_ptr++ * *mat++;

                            (*ch)[y] = sum;
                        }
                    }
                }
            }
        }
    }
}

