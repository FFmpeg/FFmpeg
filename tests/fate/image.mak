FATE_ALIASPIX += fate-aliaspix-bgr
fate-aliaspix-bgr: CMD = framecrc -i $(TARGET_SAMPLES)/aliaspix/first.pix -pix_fmt bgr24

FATE_ALIASPIX += fate-aliaspix-gray
fate-aliaspix-gray: CMD = framecrc -i $(TARGET_SAMPLES)/aliaspix/firstgray.pix -pix_fmt gray

FATE_ALIASPIX-$(call DEMDEC, IMAGE2, ALIAS_PIX) += $(FATE_ALIASPIX)
FATE_IMAGE += $(FATE_ALIASPIX-yes)
fate-aliaspix: $(FATE_ALIASPIX-yes)

FATE_BRENDERPIX += fate-brenderpix-24
fate-brenderpix-24: CMD = framecrc -c:v brender_pix -i $(TARGET_SAMPLES)/brenderpix/sbwheel.pix

FATE_BRENDERPIX += fate-brenderpix-565
fate-brenderpix-565: CMD = framecrc -c:v brender_pix -i $(TARGET_SAMPLES)/brenderpix/maximafront.pix

FATE_BRENDERPIX += fate-brenderpix-defpal
fate-brenderpix-defpal: CMD = framecrc -c:v brender_pix -i $(TARGET_SAMPLES)/brenderpix/rivrock1.pix -pix_fmt rgb24 -vf scale

FATE_BRENDERPIX += fate-brenderpix-intpal
fate-brenderpix-intpal: CMD = framecrc -c:v brender_pix -i $(TARGET_SAMPLES)/brenderpix/testtex.pix -pix_fmt rgb24 -vf scale

FATE_BRENDERPIX += fate-brenderpix-y400a
fate-brenderpix-y400a: CMD = framecrc -c:v brender_pix -i $(TARGET_SAMPLES)/brenderpix/gears.pix

FATE_BRENDERPIX-$(call DEMDEC, IMAGE2, BRENDER_PIX) += $(FATE_BRENDERPIX)
FATE_IMAGE += $(FATE_BRENDERPIX-yes)
fate-brenderpix: $(FATE_BRENDERPIX-yes)

FATE_IMAGE-$(call PARSERDEMDEC, BMP, IMAGE2PIPE, BMP) += fate-bmpparser
fate-bmpparser: CMD = framecrc -f image2pipe -i $(TARGET_SAMPLES)/bmp/numbers.bmp -pix_fmt rgb24 -vf scale

define FATE_IMGSUITE_DDS
FATE_DDS += fate-dds-$(1)
fate-dds-$(1): CMD = framecrc -i $(TARGET_SAMPLES)/dds/fate_$(1).dds $(DDS_OPTS_$(1)) -vf scale
endef

DDS_OPTS_pal     = -sws_flags +accurate_rnd+bitexact -pix_fmt rgba
DDS_OPTS_pal-ati = -sws_flags +accurate_rnd+bitexact -pix_fmt rgba
DDS_FMT          = alpha8                                               \
                   argb                                                 \
                   argb-aexp                                            \
                   dx10-bc1                                             \
                   dx10-bc1a                                            \
                   dx10-bc2                                             \
                   dx10-bc3                                             \
                   dx10-bc4                                             \
                   dx10-bc5                                             \
                   dxt1                                                 \
                   dxt1a                                                \
                   dxt1-normalmap                                       \
                   dxt2                                                 \
                   dxt3                                                 \
                   dxt4                                                 \
                   dxt5                                                 \
                   dxt5-aexp                                            \
                   dxt5-normalmap                                       \
                   dxt5-normalmap-ati                                   \
                   dxt5-rbxg                                            \
                   dxt5-rgxb                                            \
                   dxt5-rxbg                                            \
                   dxt5-rxgb                                            \
                   dxt5-xgbr                                            \
                   dxt5-xgxr                                            \
                   dxt5-xrbg                                            \
                   dxt5-ycocg                                           \
                   dxt5-ycocg-scaled                                    \
                   monob                                                \
                   pal                                                  \
                   pal-ati                                              \
                   rgb1555                                              \
                   rgb16                                                \
                   rgb24                                                \
                   rgb555                                               \
                   rgba                                                 \
                   rgtc1s                                               \
                   rgtc1u                                               \
                   rgtc2s                                               \
                   rgtc2u                                               \
                   rgtc2u-xy                                            \
                   uyvy                                                 \
                   xbgr                                                 \
                   xrgb                                                 \
                   y                                                    \
                   ya                                                   \
                   ycocg                                                \
                   yuyv
$(foreach FMT,$(DDS_FMT),$(eval $(call FATE_IMGSUITE_DDS,$(FMT))))

FATE_DDS-$(call DEMDEC, IMAGE2, DDS) += $(FATE_DDS)
FATE_IMAGE += $(FATE_DDS-yes)
fate-dds: $(FATE_DDS-yes)

FATE_IMAGE-$(call DEMDEC, IMAGE2, DPX) += fate-dpx
fate-dpx: CMD = framecrc -i $(TARGET_SAMPLES)/dpx/lighthouse_rgb48.dpx

FATE_SAMPLES_AVCONV-$(call PARSERDEMDEC, DPX, IMAGE2PIPE, DPX) += fate-dpxparser
fate-dpxparser: CMD = framecrc -f image2pipe -i $(TARGET_SAMPLES)/dpx/lena_4x_concat.dpx -sws_flags +accurate_rnd+bitexact

FATE_IMAGE_PROBE-$(call DEMDEC, IMAGE2, DPX) += fate-dpx-probe
fate-dpx-probe: CMD = probeframes -show_entries frame=color_transfer,color_range,color_space,color_primaries,sample_aspect_ratio $(TARGET_SAMPLES)/dpx/cyan.dpx

FATE_EXR += fate-exr-slice-raw
fate-exr-slice-raw: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_slice_raw.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-slice-rle
fate-exr-slice-rle: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_slice_rle.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-slice-zip1
fate-exr-slice-zip1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_slice_zip1.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-slice-zip16
fate-exr-slice-zip16: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_slice_zip16.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-slice-pxr24
fate-exr-slice-pxr24: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_slice_pxr24.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-pxr24-float-12x8
fate-exr-rgb-scanline-pxr24-float-12x8: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_pxr24_float_12x8.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgba-multiscanline-half-b44
fate-exr-rgba-multiscanline-half-b44: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_multiscanline_half_b44.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-float-b44
fate-exr-rgb-scanline-float-b44: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_float_b44.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-half-b44-12x8
fate-exr-rgb-scanline-half-b44-12x8: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_b44_12x8.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-half-b44-13x9
fate-exr-rgb-scanline-half-b44-13x9: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_b44_13x9.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-float-raw-12x8
fate-exr-rgb-tile-float-raw-12x8: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_float_raw_12x8.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-float-raw-150x130
fate-exr-rgb-tile-float-raw-150x130: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_float_raw_150x130.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-half-raw-12x8
fate-exr-rgb-tile-half-raw-12x8: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_half_raw_12x8.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44-13x9-l1
fate-exr-rgba-scanline-float-half-b44-13x9-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44_13x9.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44-13x9-l2
fate-exr-rgba-scanline-float-half-b44-13x9-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44_13x9.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44-12x8-l1
fate-exr-rgba-scanline-float-half-b44-12x8-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44_12x8.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44-12x8-l2
fate-exr-rgba-scanline-float-half-b44-12x8-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44_12x8.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44a-12x8-l1
fate-exr-rgba-scanline-float-half-b44a-12x8-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44a_12x8.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44a-12x8-l2
fate-exr-rgba-scanline-float-half-b44a-12x8-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44a_12x8.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44a-13x9-l1
fate-exr-rgba-scanline-float-half-b44a-13x9-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44a_13x9.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44a-13x9-l2
fate-exr-rgba-scanline-float-half-b44a-13x9-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44a_13x9.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-pxr24-float-half-l1
fate-exr-rgb-tile-pxr24-float-half-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_pxr24_float_half.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-pxr24-float-half-l2
fate-exr-rgb-tile-pxr24-float-half-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_tile_pxr24_float_half.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-pxr24-half-float-l1
fate-exr-rgb-tile-pxr24-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_pxr24_half_float.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-pxr24-half-float-l2
fate-exr-rgb-tile-pxr24-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_tile_pxr24_half_float.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-half-float-b44-12x8-l1
fate-exr-rgb-tile-half-float-b44-12x8-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_half_float_b44_12x8.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-half-float-b44-12x8-l2
fate-exr-rgb-tile-half-float-b44-12x8-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_tile_half_float_b44_12x8.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-zip-half-float-l1
fate-exr-rgb-tile-zip-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_zip_half_float.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-zip-half-float-l2
fate-exr-rgb-tile-zip-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_tile_zip_half_float.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-zip1-half-float-l1
fate-exr-rgb-tile-zip1-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_zip1_half_float.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-zip1-half-float-l2
fate-exr-rgb-tile-zip1-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_tile_zip1_half_float.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-rle-half-float-l1
fate-exr-rgb-tile-rle-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_rle_half_float.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-rle-half-float-l2
fate-exr-rgb-tile-rle-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_tile_rle_half_float.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-raw-half-float-l1
fate-exr-rgb-tile-raw-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_raw_half_float.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-raw-half-float-l2
fate-exr-rgb-tile-raw-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_tile_raw_half_float.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-b44-half-float-12x8-l1
fate-exr-rgb-scanline-b44-half-float-12x8-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_b44_half_float_12x8.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-b44-half-float-12x8-l2
fate-exr-rgb-scanline-b44-half-float-12x8-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_b44_half_float_12x8.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-pxr24-half-float-l1
fate-exr-rgb-scanline-pxr24-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_pxr24_half_float.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-pxr24-half-float-l2
fate-exr-rgb-scanline-pxr24-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_pxr24_half_float.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-pxr24-float-half-l1
fate-exr-rgb-scanline-pxr24-float-half-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_pxr24_float_half.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-pxr24-float-half-l2
fate-exr-rgb-scanline-pxr24-float-half-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_pxr24_float_half.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-pxr24-half-uint32-13x9
fate-exr-rgb-scanline-pxr24-half-uint32-13x9: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_pxr24_half_uint32_13x9.exr -pix_fmt rgb48le -vf scale

FATE_EXR += fate-exr-rgb-scanline-zip-half-float-l1
fate-exr-rgb-scanline-zip-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_zip_half_float.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-zip-half-float-l2
fate-exr-rgb-scanline-zip-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_zip_half_float.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-zip1-half-float-l1
fate-exr-rgb-scanline-zip1-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_zip1_half_float.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-zip1-half-float-l2
fate-exr-rgb-scanline-zip1-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_zip1_half_float.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-rle-half-float-l1
fate-exr-rgb-scanline-rle-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_rle_half_float.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-rle-half-float-l2
fate-exr-rgb-scanline-rle-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_rle_half_float.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-raw-half-float-l1
fate-exr-rgb-scanline-raw-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_raw_half_float.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-raw-half-float-l2
fate-exr-rgb-scanline-raw-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_raw_half_float.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-b44-uint32
fate-exr-rgb-scanline-b44-uint32: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_b44_uint32.exr -pix_fmt rgb48le

FATE_EXR += fate-exr-rgb-scanline-pxr24-uint32
fate-exr-rgb-scanline-pxr24-uint32: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_pxr24_uint32.exr -pix_fmt rgb48le

FATE_EXR += fate-exr-rgb-scanline-zip1-half-float-l1-zero-offsets
fate-exr-rgb-scanline-zip1-half-float-l1-zero-offsets: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_zip1_half_float_zero_offsets.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-half-piz-bw
fate-exr-rgb-scanline-half-piz-bw: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_piz_bw.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-half-piz-color
fate-exr-rgb-scanline-half-piz-color: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_piz_color.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-half-piz-dw-t01
fate-exr-rgb-scanline-half-piz-dw-t01: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_piz_dw_t01.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-float-piz-48x32
fate-exr-rgb-scanline-float-piz-48x32: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_float_piz_48x32.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-none-negative-red
fate-exr-rgb-scanline-none-negative-red: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_none_negative_red.exr -pix_fmt gbrpf32le


FATE_EXR += fate-exr-rgb-b44a-half-negative-4x4
fate-exr-rgb-b44a-half-negative-4x4: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_b44a_half_negative_4x4.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-y-tile-zip-half-12x8
fate-exr-y-tile-zip-half-12x8: CMD = framecrc -i $(TARGET_SAMPLES)/exr/y_tile_zip_half_12x8.exr -pix_fmt grayf32le

FATE_EXR += fate-exr-y-scanline-zip-half-12x8
fate-exr-y-scanline-zip-half-12x8: CMD = framecrc -i $(TARGET_SAMPLES)/exr/y_scanline_zip_half_12x8.exr -pix_fmt grayf32le

FATE_EXR += fate-exr-rgb-scanline-half-piz-dw-t08
fate-exr-rgb-scanline-half-piz-dw-t08: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_piz_dw_t08.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgba-zip16-16x32-flag4
fate-exr-rgba-zip16-16x32-flag4: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_zip16_16x32_flag4.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-ya-scanline-zip-half-12x8
fate-exr-ya-scanline-zip-half-12x8: CMD = framecrc -i $(TARGET_SAMPLES)/exr/ya_scanline_zip_half_12x8.exr -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-half-zip
fate-exr-rgb-tile-half-zip: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_half_zip.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-float-zip-dw-large
fate-exr-rgb-scanline-float-zip-dw-large: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_float_zip_dw_large.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-half-piz-dw-large
fate-exr-rgb-scanline-half-piz-dw-large: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_piz_dw_large.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-half-zip-dw-large
fate-exr-rgb-scanline-half-zip-dw-large: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_zip_dw_large.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-uint32-piz-dw-large
fate-exr-rgb-scanline-uint32-piz-dw-large: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_uint32_piz_dw_large.exr -pix_fmt rgb48le

FATE_EXR += fate-exr-rgb-tile-half-piz-dw-large
fate-exr-rgb-tile-half-piz-dw-large: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_half_piz_dw_large.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-uint32-piz-dw-large
fate-exr-rgb-tile-uint32-piz-dw-large: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_uint32_piz_dw_large.exr -pix_fmt rgb48le

FATE_EXR += fate-exr-rgb-scanline-half-zip-dw-outside
fate-exr-rgb-scanline-half-zip-dw-outside: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_zip_dw_outside.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-half-zip-dw-outside
fate-exr-rgb-tile-half-zip-dw-outside: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_half_zip_dw_outside.exr -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-zip-half-0x0-0xFFFF
fate-exr-rgb-scanline-zip-half-0x0-0xFFFF: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_zip_half_float_0x0_to_0xFFFF.exr -pix_fmt gbrpf32le

FATE_EXR-$(call DEMDEC, IMAGE2, EXR) += $(FATE_EXR)

FATE_IMAGE += $(FATE_EXR-yes)
fate-exr: $(FATE_EXR-yes)

FATE_JPG += fate-jpg-12bpp
fate-jpg-12bpp: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpg/12bpp.jpg -f rawvideo -pix_fmt gray16le -vf setsar=sar=sar,scale

FATE_JPG += fate-jpg-jfif
fate-jpg-jfif: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpg/20242.jpg

FATE_JPG-$(call DEMDEC, IMAGE2, MJPEG) += $(FATE_JPG)
FATE_IMAGE += $(FATE_JPG-yes)
fate-jpg: $(FATE_JPG-yes)

FATE_IMAGE-$(call DEMDEC, IMAGE2, QDRAW) += fate-pict
fate-pict: CMD = framecrc -i $(TARGET_SAMPLES)/quickdraw/TRU256.PCT -pix_fmt rgb24

FATE_IMAGE-$(call DEMDEC, IMAGE2, PICTOR) += fate-pictor
fate-pictor: CMD = framecrc -i $(TARGET_SAMPLES)/pictor/MFISH.PIC -pix_fmt rgb24 -vf scale

FATE_IMAGE-$(call PARSERDEMDEC, PNG, IMAGE2PIPE, PNG) += fate-pngparser
fate-pngparser: CMD = framecrc -f image2pipe -i $(TARGET_SAMPLES)/png1/feed_4x_concat.png -pix_fmt rgba

define FATE_IMGSUITE_PNG
FATE_PNG += fate-png-$(1)
fate-png-$(1): CMD = framecrc -auto_conversion_filters -i $(TARGET_SAMPLES)/png1/lena-$(1).png -sws_flags +accurate_rnd+bitexact -pix_fmt rgb24
endef

PNG_COLORSPACES = gray8 gray16 rgb24 rgb48 rgba rgba64 ya8 ya16
$(foreach CLSP,$(PNG_COLORSPACES),$(eval $(call FATE_IMGSUITE_PNG,$(CLSP))))

FATE_PNG += fate-png-int-rgb24
fate-png-int-rgb24: CMD = framecrc -i $(TARGET_SAMPLES)/png1/lena-int_rgb24.png -sws_flags +accurate_rnd+bitexact

FATE_PNG-$(call DEMDEC, IMAGE2, PNG) += $(FATE_PNG)
FATE_IMAGE += $(FATE_PNG-yes)
fate-png: $(FATE_PNG-yes)

FATE_IMAGE-$(call DEMDEC, IMAGE2, PTX) += fate-ptx
fate-ptx: CMD = framecrc -i $(TARGET_SAMPLES)/ptx/_113kw_pic.ptx -pix_fmt rgb24 -vf scale

define FATE_IMGSUITE_PSD
FATE_PSD += fate-psd-$(1)
fate-psd-$(1): CMD = framecrc -i $(TARGET_SAMPLES)/psd/lena-$(1).psd -sws_flags +accurate_rnd+bitexact -pix_fmt rgb24 -vf scale
endef

PSD_COLORSPACES = gray8 gray16 rgb24 rgb48 rgba rgba64 ya8 ya16
$(foreach CLSP,$(PSD_COLORSPACES),$(eval $(call FATE_IMGSUITE_PSD,$(CLSP))))

FATE_PSD += fate-psd-lena-127x127-rgb24
fate-psd-lena-127x127-rgb24: CMD = framecrc -i $(TARGET_SAMPLES)/psd/lena-127x127_rgb24.psd

FATE_PSD += fate-psd-lena-rgb-rle-127x127-16b
fate-psd-lena-rgb-rle-127x127-16b: CMD = framecrc -i $(TARGET_SAMPLES)/psd/lena-rgb_rle_127x127_16b.psd

FATE_PSD += fate-psd-lena-rgb-rle-127x127-8b
fate-psd-lena-rgb-rle-127x127-8b: CMD = framecrc -i $(TARGET_SAMPLES)/psd/lena-rgb_rle_127x127_8b.psd

FATE_PSD += fate-psd-lena-rgba-rle-128x128-8b
fate-psd-lena-rgba-rle-128x128-8b: CMD = framecrc -i $(TARGET_SAMPLES)/psd/lena-rgba_rle_128x128_8b.psd

FATE_PSD += fate-psd-lena-256c
fate-psd-lena-256c: CMD = framecrc -i $(TARGET_SAMPLES)/psd/lena-256c.psd

FATE_PSD += fate-psd-lena-bitmap
fate-psd-lena-bitmap: CMD = framecrc -i $(TARGET_SAMPLES)/psd/lena-bitmap.psd

FATE_PSD += fate-psd-duo-tone-color
fate-psd-duo-tone-color: CMD = framecrc -i $(TARGET_SAMPLES)/psd/duotone-color.psd

FATE_PSD-$(call DEMDEC, IMAGE2, PSD) += $(FATE_PSD)

FATE_IMAGE += $(FATE_PSD-yes)
fate-psd: $(FATE_PSD-yes)

define FATE_IMGSUITE_SGI
FATE_SGI += fate-sgi-$(1) fate-sgi-$(1)-rle
fate-sgi-$(1): CMD = framecrc -i $(TARGET_SAMPLES)/sgi/libav_$(1).sgi -sws_flags +accurate_rnd+bitexact
fate-sgi-$(1)-rle: CMD = framecrc -i $(TARGET_SAMPLES)/sgi/libav_$(1)_rle.sgi -sws_flags +accurate_rnd+bitexact
endef

SGI_COLORSPACES = gray8 gray16 rgb24 rgb48 rgba rgba64
$(foreach CLSP,$(SGI_COLORSPACES),$(eval $(call FATE_IMGSUITE_SGI,$(CLSP))))

FATE_SGI-$(call DEMDEC, IMAGE2, SGI) += $(FATE_SGI)
FATE_IMAGE += $(FATE_SGI-yes)
fate-sgi: $(FATE_SGI-yes)

FATE_SUNRASTER += fate-sunraster-1bit-raw
fate-sunraster-1bit-raw: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/lena-1bit-raw.sun

FATE_SUNRASTER += fate-sunraster-1bit-rle
fate-sunraster-1bit-rle: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/lena-1bit-rle.sun

FATE_SUNRASTER += fate-sunraster-8bit-raw
fate-sunraster-8bit-raw: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/lena-8bit-raw.sun -pix_fmt rgb24 -vf scale

FATE_SUNRASTER += fate-sunraster-8bit_gray-raw
fate-sunraster-8bit_gray-raw: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/gray.ras

FATE_SUNRASTER += fate-sunraster-8bit-rle
fate-sunraster-8bit-rle: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/lena-8bit-rle.sun -pix_fmt rgb24 -vf scale

FATE_SUNRASTER += fate-sunraster-24bit-raw
fate-sunraster-24bit-raw: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/lena-24bit-raw.sun

FATE_SUNRASTER += fate-sunraster-24bit-rle
fate-sunraster-24bit-rle: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/lena-24bit-rle.sun

FATE_SUNRASTER-$(call DEMDEC, IMAGE2, SUNRAST) += $(FATE_SUNRASTER)

FATE_IMAGE += $(FATE_SUNRASTER-yes)
fate-sunraster: $(FATE_SUNRASTER-yes)

FATE_TARGA = CBW8       \
             CCM8       \
             CTC16      \
             CTC24      \
             CTC32      \
             UBW8       \
             UCM8       \
             UTC16      \
             UTC24      \
             UTC32

FATE_TARGA := $(FATE_TARGA:%=fate-targa-conformance-%)  \
              fate-targa-top-to-bottom

FATE_TARGA-$(call DEMDEC, IMAGE2, TARGA) += $(FATE_TARGA)

FATE_IMAGE += $(FATE_TARGA-yes)
fate-targa: $(FATE_TARGA-yes)

fate-targa-conformance-CBW8:  CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/CBW8.TGA
fate-targa-conformance-CCM8:  CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/CCM8.TGA  -pix_fmt rgba -vf scale
fate-targa-conformance-CTC16: CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/CTC16.TGA -pix_fmt rgb555le
fate-targa-conformance-CTC24: CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/CTC24.TGA
fate-targa-conformance-CTC32: CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/CTC32.TGA -pix_fmt bgra
fate-targa-conformance-UBW8:  CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/UBW8.TGA
fate-targa-conformance-UCM8:  CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/UCM8.TGA  -pix_fmt rgba -vf scale
fate-targa-conformance-UTC16: CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/UTC16.TGA -pix_fmt rgb555le
fate-targa-conformance-UTC24: CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/UTC24.TGA
fate-targa-conformance-UTC32: CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/UTC32.TGA -pix_fmt bgra

fate-targa-top-to-bottom: CMD = framecrc -i $(TARGET_SAMPLES)/targa/lena-top-to-bottom.tga

FATE_TIFF += fate-tiff-fax-g3
fate-tiff-fax-g3: CMD = framecrc -i $(TARGET_SAMPLES)/CCITT_fax/G31D.TIF

FATE_TIFF += fate-tiff-fax-g3s
fate-tiff-fax-g3s: CMD = framecrc -i $(TARGET_SAMPLES)/CCITT_fax/G31DS.TIF

FATE_TIFF-$(call DEMDEC, IMAGE2, TIFF) += $(FATE_TIFF)

FATE_IMAGE += $(FATE_TIFF-yes)
fate-tiff: $(FATE_TIFF-yes)

FATE_WEBP += fate-webp-rgb-lossless
fate-webp-rgb-lossless: CMD = framecrc -i $(TARGET_SAMPLES)/webp/rgb_lossless.webp

FATE_WEBP += fate-webp-rgb-lena-lossless
fate-webp-rgb-lena-lossless: CMD = framecrc -i $(TARGET_SAMPLES)/webp/rgb_lena_lossless.webp

FATE_WEBP += fate-webp-rgb-lena-lossless-rgb24
fate-webp-rgb-lena-lossless-rgb24: CMD = framecrc -i $(TARGET_SAMPLES)/webp/rgb_lena_lossless.webp -pix_fmt rgb24 -vf scale

FATE_WEBP += fate-webp-rgba-lossless
fate-webp-rgba-lossless: CMD = framecrc -i $(TARGET_SAMPLES)/webp/rgba_lossless.webp

FATE_WEBP += fate-webp-rgb-lossy-q80
fate-webp-rgb-lossy-q80: CMD = framecrc -i $(TARGET_SAMPLES)/webp/rgb_q80.webp

FATE_WEBP += fate-webp-rgba-lossy-q80
fate-webp-rgba-lossy-q80: CMD = framecrc -i $(TARGET_SAMPLES)/webp/rgba_q80.webp

FATE_WEBP-$(call DEMDEC, IMAGE2, WEBP) += $(FATE_WEBP)
FATE_IMAGE += $(FATE_WEBP-yes)
fate-webp: $(FATE_WEBP-yes)

FATE_IMAGE-$(call DEMDEC, IMAGE2, XFACE) += fate-xface
fate-xface: CMD = framecrc -i $(TARGET_SAMPLES)/xface/lena.xface

FATE_XBM += fate-xbm10
fate-xbm10: CMD = framecrc -i $(TARGET_SAMPLES)/xbm/xl.xbm

FATE_XBM += fate-xbm11
fate-xbm11: CMD = framecrc -i $(TARGET_SAMPLES)/xbm/lbw.xbm

FATE_XBM-$(call DEMDEC, IMAGE2, XBM) += $(FATE_XBM)
FATE_IMAGE += $(FATE_XBM-yes)
fate-xbm: $(FATE_XBM-yes)

FATE_IMAGE += $(FATE_IMAGE-yes)
FATE_IMAGE_PROBE += $(FATE_IMAGE_PROBE-yes)

FATE_SAMPLES_FFMPEG += $(FATE_IMAGE)
FATE_SAMPLES_FFPROBE += $(FATE_IMAGE_PROBE)

fate-image: $(FATE_IMAGE) $(FATE_IMAGE_PROBE)
