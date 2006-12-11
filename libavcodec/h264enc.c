/*
 * H.264 encoder
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


#include "common.h"
#include "bitstream.h"
#include "mpegvideo.h"
#include "h264data.h"

/**
 * Write out the provided data into a NAL unit.
 * @param nal_ref_idc NAL reference IDC
 * @param nal_unit_type NAL unit payload type
 * @param dest the target buffer, dst+1 == src is allowed as a special case
 * @param destsize the length of the dst array
 * @param b2 the data which should be escaped
 * @returns pointer to current position in the output buffer or NULL if an error occured
 */
static uint8_t *h264_write_nal_unit(int nal_ref_idc, int nal_unit_type, uint8_t *dest, int *destsize,
                          PutBitContext *b2)
{
    PutBitContext b;
    int i, destpos, rbsplen, escape_count;
    uint8_t *rbsp;

    if (nal_unit_type != NAL_END_STREAM)
        put_bits(b2,1,1); // rbsp_stop_bit

    // Align b2 on a byte boundary
    align_put_bits(b2);
    rbsplen = put_bits_count(b2)/8;
    flush_put_bits(b2);
    rbsp = b2->buf;

    init_put_bits(&b,dest,*destsize);

    put_bits(&b,16,0);
    put_bits(&b,16,0x01);

    put_bits(&b,1,0); // forbidden zero bit
    put_bits(&b,2,nal_ref_idc); // nal_ref_idc
    put_bits(&b,5,nal_unit_type); // nal_unit_type

    flush_put_bits(&b);

    destpos = 5;
    escape_count= 0;

    for (i=0; i<rbsplen; i+=2)
    {
        if (rbsp[i]) continue;
        if (i>0 && rbsp[i-1]==0)
            i--;
        if (i+2<rbsplen && rbsp[i+1]==0 && rbsp[i+2]<=3)
        {
            escape_count++;
            i+=2;
        }
    }

    if(escape_count==0)
    {
        if(dest+destpos != rbsp)
        {
            memcpy(dest+destpos, rbsp, rbsplen);
            *destsize -= (rbsplen+destpos);
        }
        return dest+rbsplen+destpos;
    }

    if(rbsplen + escape_count + 1> *destsize)
    {
        av_log(NULL, AV_LOG_ERROR, "Destination buffer too small!\n");
        return NULL;
    }

    // this should be damn rare (hopefully)
    for (i = 0 ; i < rbsplen ; i++)
    {
        if (i + 2 < rbsplen && (rbsp[i] == 0 && rbsp[i+1] == 0 && rbsp[i+2] < 4))
        {
            dest[destpos++] = rbsp[i++];
            dest[destpos++] = rbsp[i];
            dest[destpos++] = 0x03; // emulation prevention byte
        }
        else
            dest[destpos++] = rbsp[i];
    }
    *destsize -= destpos;
    return dest+destpos;
}

