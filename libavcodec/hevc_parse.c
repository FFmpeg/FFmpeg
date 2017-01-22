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

#include "bytestream.h"
#include "h2645_parse.h"
#include "hevc.h"
#include "hevc_parse.h"

static int hevc_decode_nal_units(const uint8_t *buf, int buf_size, HEVCParamSets *ps,
                                 int is_nalff, int nal_length_size, void *logctx)
{
    int i;
    int ret = 0;
    H2645Packet pkt = { 0 };

    ret = ff_h2645_packet_split(&pkt, buf, buf_size, logctx, is_nalff, nal_length_size, AV_CODEC_ID_HEVC, 1);
    if (ret < 0) {
        goto done;
    }

    for (i = 0; i < pkt.nb_nals; i++) {
        H2645NAL *nal = &pkt.nals[i];

        /* ignore everything except parameter sets and VCL NALUs */
        switch (nal->type) {
        case NAL_VPS: ff_hevc_decode_nal_vps(&nal->gb, logctx, ps);    break;
        case NAL_SPS: ff_hevc_decode_nal_sps(&nal->gb, logctx, ps, 1); break;
        case NAL_PPS: ff_hevc_decode_nal_pps(&nal->gb, logctx, ps);    break;
        case NAL_TRAIL_R:
        case NAL_TRAIL_N:
        case NAL_TSA_N:
        case NAL_TSA_R:
        case NAL_STSA_N:
        case NAL_STSA_R:
        case NAL_BLA_W_LP:
        case NAL_BLA_W_RADL:
        case NAL_BLA_N_LP:
        case NAL_IDR_W_RADL:
        case NAL_IDR_N_LP:
        case NAL_CRA_NUT:
        case NAL_RADL_N:
        case NAL_RADL_R:
        case NAL_RASL_N:
        case NAL_RASL_R:
            av_log(logctx, AV_LOG_ERROR, "Invalid NAL unit: %d\n", nal->type);
            ret = AVERROR_INVALIDDATA;
            goto done;
            break;
        }
    }

done:
    ff_h2645_packet_uninit(&pkt);
    return ret;
}

int ff_hevc_decode_extradata(const uint8_t *data, int size, HEVCParamSets *ps,
                             int *is_nalff, int *nal_length_size,
                             int err_recognition, void *logctx)
{
    int ret = 0;
    GetByteContext gb;

    bytestream2_init(&gb, data, size);

    if (size > 3 && (data[0] || data[1] || data[2] > 1)) {
        /* It seems the extradata is encoded as hvcC format.
         * Temporarily, we support configurationVersion==0 until 14496-15 3rd
         * is finalized. When finalized, configurationVersion will be 1 and we
         * can recognize hvcC by checking if avctx->extradata[0]==1 or not. */
        int i, j, num_arrays, nal_len_size;

        *is_nalff = 1;

        bytestream2_skip(&gb, 21);
        nal_len_size = (bytestream2_get_byte(&gb) & 3) + 1;
        num_arrays   = bytestream2_get_byte(&gb);

        /* nal units in the hvcC always have length coded with 2 bytes,
         * so put a fake nal_length_size = 2 while parsing them */
        *nal_length_size = 2;

        /* Decode nal units from hvcC. */
        for (i = 0; i < num_arrays; i++) {
            int type = bytestream2_get_byte(&gb) & 0x3f;
            int cnt  = bytestream2_get_be16(&gb);

            for (j = 0; j < cnt; j++) {
                // +2 for the nal size field
                int nalsize = bytestream2_peek_be16(&gb) + 2;
                if (bytestream2_get_bytes_left(&gb) < nalsize) {
                    av_log(logctx, AV_LOG_ERROR,
                           "Invalid NAL unit size in extradata.\n");
                    return AVERROR_INVALIDDATA;
                }

                ret = hevc_decode_nal_units(gb.buffer, nalsize, ps, *is_nalff, *nal_length_size, logctx);
                if (ret < 0) {
                    av_log(logctx, AV_LOG_ERROR,
                           "Decoding nal unit %d %d from hvcC failed\n",
                           type, i);
                    return ret;
                }
                bytestream2_skip(&gb, nalsize);
            }
        }

        /* Now store right nal length size, that will be used to parse
         * all other nals */
        *nal_length_size = nal_len_size;
    } else {
        *is_nalff = 0;
        ret = hevc_decode_nal_units(data, size, ps, *is_nalff, *nal_length_size, logctx);
        if (ret < 0)
            return ret;
    }

    return ret;
}
