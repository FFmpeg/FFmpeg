/** Available items from parser list
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
static const AVCodecParser *parser_list[] = {
    &ff_aac_parser,
    &ff_aac_latm_parser,
    &ff_ac3_parser,
    &ff_adx_parser,
    &ff_bmp_parser,
    &ff_cavsvideo_parser,
    &ff_cook_parser,
    &ff_dca_parser,
    &ff_dirac_parser,
    &ff_dnxhd_parser,
    &ff_dpx_parser,
    &ff_dvaudio_parser,
    &ff_dvbsub_parser,
    &ff_dvdsub_parser,
    &ff_dvd_nav_parser,
    &ff_flac_parser,
    &ff_g729_parser,
    &ff_gsm_parser,
    &ff_h261_parser,
    &ff_h263_parser,
    &ff_h264_parser,
    &ff_hevc_parser,
    &ff_mjpeg_parser,
    &ff_mlp_parser,
    &ff_mpeg4video_parser,
    &ff_mpegaudio_parser,
    &ff_mpegvideo_parser,
    &ff_opus_parser,
    &ff_png_parser,
    &ff_pnm_parser,
    &ff_rv30_parser,
    &ff_rv40_parser,
    &ff_sbc_parser,
    &ff_sipr_parser,
    &ff_tak_parser,
    &ff_vc1_parser,
    &ff_vorbis_parser,
    &ff_vp3_parser,
    &ff_vp8_parser,
    &ff_vp9_parser,
    &ff_xma_parser,
    NULL };