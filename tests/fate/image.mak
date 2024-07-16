ADD_SCALE_IF = $(if $(findstring -vf scale,$(1)), SCALE_FILTER)

FATE_ALIASPIX += fate-aliaspix-bgr
fate-aliaspix-bgr: CMD = transcode alias_pix $(TARGET_SAMPLES)/aliaspix/first.pix image2 "-c alias_pix" "-map 0 -map 0 -pix_fmt:0 bgr24 -c:v:1 copy"

FATE_ALIASPIX += fate-aliaspix-gray
fate-aliaspix-gray: CMD = transcode alias_pix $(TARGET_SAMPLES)/aliaspix/firstgray.pix image2 "-c alias_pix" "-map 0 -map 0 -pix_fmt:0 gray -c:v:1 copy"

FATE_ALIASPIX-$(call TRANSCODE, ALIAS_PIX, IMAGE2 IMAGE2_ALIAS_PIX) += $(FATE_ALIASPIX)
FATE_IMAGE += $(FATE_ALIASPIX-yes)
fate-aliaspix: $(FATE_ALIASPIX-yes)

FATE_BRENDERPIX += fate-brenderpix-24
fate-brenderpix-24: CMD = framecrc -c:v brender_pix -i $(TARGET_SAMPLES)/brenderpix/sbwheel.pix

FATE_BRENDERPIX += fate-brenderpix-565
fate-brenderpix-565: CMD = framecrc -c:v brender_pix -i $(TARGET_SAMPLES)/brenderpix/maximafront.pix

FATE_BRENDERPIX-$(call DEMDEC, IMAGE2, BRENDER_PIX, SCALE_FILTER) += fate-brenderpix-defpal
fate-brenderpix-defpal: CMD = framecrc -c:v brender_pix -i $(TARGET_SAMPLES)/brenderpix/rivrock1.pix -pix_fmt rgb24 -vf scale

FATE_BRENDERPIX-$(call DEMDEC, IMAGE2, BRENDER_PIX, SCALE_FILTER) += fate-brenderpix-intpal
fate-brenderpix-intpal: CMD = framecrc -c:v brender_pix -i $(TARGET_SAMPLES)/brenderpix/testtex.pix -pix_fmt rgb24 -vf scale

FATE_BRENDERPIX += fate-brenderpix-y400a
fate-brenderpix-y400a: CMD = framecrc -c:v brender_pix -i $(TARGET_SAMPLES)/brenderpix/gears.pix

FATE_BRENDERPIX-$(call DEMDEC, IMAGE2, BRENDER_PIX) += $(FATE_BRENDERPIX)
FATE_IMAGE_FRAMECRC += $(FATE_BRENDERPIX-yes)
fate-brenderpix: $(FATE_BRENDERPIX-yes)

FATE_IMAGE_FRAMECRC-$(call PARSERDEMDEC, BMP, IMAGE2PIPE, BMP, SCALE_FILTER) += fate-bmpparser
fate-bmpparser: CMD = framecrc -f image2pipe -i $(TARGET_SAMPLES)/bmp/numbers.bmp -pix_fmt rgb24 -vf scale

define FATE_IMGSUITE_DDS
FATE_DDS-$(call DEMDEC, IMAGE2, DDS, $(call ADD_SCALE_IF, $(DDS_OPTS_$(1)))) += fate-dds-$(1)
fate-dds-$(1): CMD = framecrc -i $(TARGET_SAMPLES)/dds/fate_$(1).dds $(DDS_OPTS_$(1))
endef

DDS_OPTS_pal    := -sws_flags +accurate_rnd+bitexact -pix_fmt rgba -vf scale
DDS_OPTS_pal-ati:= -sws_flags +accurate_rnd+bitexact -pix_fmt rgba -vf scale
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

FATE_IMAGE_FRAMECRC += $(FATE_DDS-yes)
fate-dds: $(FATE_DDS-yes)

FATE_IMAGE_FRAMECRC-$(call DEMDEC, IMAGE2, DPX) += fate-dpx
fate-dpx: CMD = framecrc -i $(TARGET_SAMPLES)/dpx/lighthouse_rgb48.dpx

# The following sample has frames whose dimensions differ on a per-frame basis
# and therefore needs the scale filter.
FATE_IMAGE_FRAMECRC-$(call PARSERDEMDEC, DPX, IMAGE2PIPE, DPX, SCALE_FILTER) += fate-dpxparser
fate-dpxparser: CMD = framecrc -f image2pipe -i $(TARGET_SAMPLES)/dpx/lena_4x_concat.dpx -sws_flags +accurate_rnd+bitexact

FATE_IMAGE_PROBE-$(call DEMDEC, IMAGE2, DPX) += fate-dpx-probe
fate-dpx-probe: CMD = probeframes -show_entries frame=color_transfer,color_range,color_space,color_primaries,sample_aspect_ratio $(TARGET_SAMPLES)/dpx/cyan.dpx

FATE_EXR += fate-exr-slice-raw
fate-exr-slice-raw: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_slice_raw.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-slice-rle
fate-exr-slice-rle: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_slice_rle.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-slice-zip1
fate-exr-slice-zip1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_slice_zip1.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-slice-zip16
fate-exr-slice-zip16: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_slice_zip16.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-slice-pxr24
fate-exr-slice-pxr24: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_slice_pxr24.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-pxr24-float-12x8
fate-exr-rgb-scanline-pxr24-float-12x8: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_pxr24_float_12x8.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgba-multiscanline-half-b44
fate-exr-rgba-multiscanline-half-b44: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_multiscanline_half_b44.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-float-b44
fate-exr-rgb-scanline-float-b44: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_float_b44.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-half-b44-12x8
fate-exr-rgb-scanline-half-b44-12x8: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_b44_12x8.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-half-b44-13x9
fate-exr-rgb-scanline-half-b44-13x9: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_b44_13x9.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-float-raw-12x8
fate-exr-rgb-tile-float-raw-12x8: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_float_raw_12x8.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-float-raw-150x130
fate-exr-rgb-tile-float-raw-150x130: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_float_raw_150x130.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-half-raw-12x8
fate-exr-rgb-tile-half-raw-12x8: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_half_raw_12x8.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44-13x9-l1
fate-exr-rgba-scanline-float-half-b44-13x9-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44_13x9.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44-13x9-l2
fate-exr-rgba-scanline-float-half-b44-13x9-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44_13x9.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44-12x8-l1
fate-exr-rgba-scanline-float-half-b44-12x8-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44_12x8.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44-12x8-l2
fate-exr-rgba-scanline-float-half-b44-12x8-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44_12x8.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44a-12x8-l1
fate-exr-rgba-scanline-float-half-b44a-12x8-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44a_12x8.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44a-12x8-l2
fate-exr-rgba-scanline-float-half-b44a-12x8-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44a_12x8.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44a-13x9-l1
fate-exr-rgba-scanline-float-half-b44a-13x9-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44a_13x9.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgba-scanline-float-half-b44a-13x9-l2
fate-exr-rgba-scanline-float-half-b44a-13x9-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgba_scanline_float_half_b44a_13x9.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-pxr24-float-half-l1
fate-exr-rgb-tile-pxr24-float-half-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_pxr24_float_half.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-pxr24-float-half-l2
fate-exr-rgb-tile-pxr24-float-half-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_tile_pxr24_float_half.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-pxr24-half-float-l1
fate-exr-rgb-tile-pxr24-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_pxr24_half_float.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-pxr24-half-float-l2
fate-exr-rgb-tile-pxr24-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_tile_pxr24_half_float.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-half-float-b44-12x8-l1
fate-exr-rgb-tile-half-float-b44-12x8-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_half_float_b44_12x8.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-half-float-b44-12x8-l2
fate-exr-rgb-tile-half-float-b44-12x8-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_tile_half_float_b44_12x8.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-zip-half-float-l1
fate-exr-rgb-tile-zip-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_zip_half_float.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-zip-half-float-l2
fate-exr-rgb-tile-zip-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_tile_zip_half_float.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-zip1-half-float-l1
fate-exr-rgb-tile-zip1-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_zip1_half_float.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-zip1-half-float-l2
fate-exr-rgb-tile-zip1-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_tile_zip1_half_float.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-rle-half-float-l1
fate-exr-rgb-tile-rle-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_rle_half_float.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-rle-half-float-l2
fate-exr-rgb-tile-rle-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_tile_rle_half_float.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-raw-half-float-l1
fate-exr-rgb-tile-raw-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_raw_half_float.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-raw-half-float-l2
fate-exr-rgb-tile-raw-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_tile_raw_half_float.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-b44-half-float-12x8-l1
fate-exr-rgb-scanline-b44-half-float-12x8-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_b44_half_float_12x8.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-b44-half-float-12x8-l2
fate-exr-rgb-scanline-b44-half-float-12x8-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_b44_half_float_12x8.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-pxr24-half-float-l1
fate-exr-rgb-scanline-pxr24-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_pxr24_half_float.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-pxr24-half-float-l2
fate-exr-rgb-scanline-pxr24-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_pxr24_half_float.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-pxr24-float-half-l1
fate-exr-rgb-scanline-pxr24-float-half-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_pxr24_float_half.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-pxr24-float-half-l2
fate-exr-rgb-scanline-pxr24-float-half-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_pxr24_float_half.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-pxr24-half-uint32-13x9
fate-exr-rgb-scanline-pxr24-half-uint32-13x9: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_pxr24_half_uint32_13x9.exr -pix_fmt rgb48le -vf scale

FATE_EXR += fate-exr-rgb-scanline-zip-half-float-l1
fate-exr-rgb-scanline-zip-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_zip_half_float.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-zip-half-float-l2
fate-exr-rgb-scanline-zip-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_zip_half_float.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-zip1-half-float-l1
fate-exr-rgb-scanline-zip1-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_zip1_half_float.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-zip1-half-float-l2
fate-exr-rgb-scanline-zip1-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_zip1_half_float.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-rle-half-float-l1
fate-exr-rgb-scanline-rle-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_rle_half_float.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-rle-half-float-l2
fate-exr-rgb-scanline-rle-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_rle_half_float.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-raw-half-float-l1
fate-exr-rgb-scanline-raw-half-float-l1: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_raw_half_float.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-raw-half-float-l2
fate-exr-rgb-scanline-raw-half-float-l2: CMD = framecrc -layer "VRaySamplerInfo" -i $(TARGET_SAMPLES)/exr/rgb_scanline_raw_half_float.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-scanline-b44-uint32
fate-exr-rgb-scanline-b44-uint32: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_b44_uint32.exr -vf scale -pix_fmt rgb48le

FATE_EXR += fate-exr-rgb-scanline-pxr24-uint32
fate-exr-rgb-scanline-pxr24-uint32: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_pxr24_uint32.exr -vf scale -pix_fmt rgb48le

FATE_EXR += fate-exr-rgb-scanline-zip1-half-float-l1-zero-offsets
fate-exr-rgb-scanline-zip1-half-float-l1-zero-offsets: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_zip1_half_float_zero_offsets.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-half-piz-bw
fate-exr-rgb-scanline-half-piz-bw: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_piz_bw.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-half-piz-color
fate-exr-rgb-scanline-half-piz-color: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_piz_color.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-half-piz-dw-t01
fate-exr-rgb-scanline-half-piz-dw-t01: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_piz_dw_t01.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-float-piz-48x32
fate-exr-rgb-scanline-float-piz-48x32: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_float_piz_48x32.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-none-negative-red
fate-exr-rgb-scanline-none-negative-red: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_none_negative_red.exr -vf scale -pix_fmt gbrpf32le


FATE_EXR += fate-exr-rgb-b44a-half-negative-4x4
fate-exr-rgb-b44a-half-negative-4x4: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_b44a_half_negative_4x4.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-y-tile-zip-half-12x8
fate-exr-y-tile-zip-half-12x8: CMD = framecrc -i $(TARGET_SAMPLES)/exr/y_tile_zip_half_12x8.exr -vf scale -pix_fmt grayf32le

FATE_EXR += fate-exr-y-scanline-zip-half-12x8
fate-exr-y-scanline-zip-half-12x8: CMD = framecrc -i $(TARGET_SAMPLES)/exr/y_scanline_zip_half_12x8.exr -vf scale -pix_fmt grayf32le

FATE_EXR += fate-exr-rgb-scanline-half-piz-dw-t08
fate-exr-rgb-scanline-half-piz-dw-t08: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_piz_dw_t08.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgba-zip16-16x32-flag4
fate-exr-rgba-zip16-16x32-flag4: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgba_zip16_16x32_flag4.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-ya-scanline-zip-half-12x8
fate-exr-ya-scanline-zip-half-12x8: CMD = framecrc -i $(TARGET_SAMPLES)/exr/ya_scanline_zip_half_12x8.exr -vf scale -pix_fmt gbrapf32le

FATE_EXR += fate-exr-rgb-tile-half-zip
fate-exr-rgb-tile-half-zip: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_half_zip.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-float-zip-dw-large
fate-exr-rgb-scanline-float-zip-dw-large: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_float_zip_dw_large.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-half-piz-dw-large
fate-exr-rgb-scanline-half-piz-dw-large: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_piz_dw_large.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-half-zip-dw-large
fate-exr-rgb-scanline-half-zip-dw-large: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_zip_dw_large.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-uint32-piz-dw-large
fate-exr-rgb-scanline-uint32-piz-dw-large: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_uint32_piz_dw_large.exr -vf scale -pix_fmt rgb48le

FATE_EXR += fate-exr-rgb-tile-half-piz-dw-large
fate-exr-rgb-tile-half-piz-dw-large: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_half_piz_dw_large.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-uint32-piz-dw-large
fate-exr-rgb-tile-uint32-piz-dw-large: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_uint32_piz_dw_large.exr -vf scale -pix_fmt rgb48le

FATE_EXR += fate-exr-rgb-scanline-half-zip-dw-outside
fate-exr-rgb-scanline-half-zip-dw-outside: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_half_zip_dw_outside.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-tile-half-zip-dw-outside
fate-exr-rgb-tile-half-zip-dw-outside: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_tile_half_zip_dw_outside.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR += fate-exr-rgb-scanline-zip-half-0x0-0xFFFF
fate-exr-rgb-scanline-zip-half-0x0-0xFFFF: CMD = framecrc -i $(TARGET_SAMPLES)/exr/rgb_scanline_zip_half_float_0x0_to_0xFFFF.exr -vf scale -pix_fmt gbrpf32le

FATE_EXR-$(call DEMDEC, IMAGE2, EXR, SCALE_FILTER) += $(FATE_EXR)

FATE_IMAGE_FRAMECRC += $(FATE_EXR-yes)
fate-exr: $(FATE_EXR-yes)

FATE_JPG-$(call DEMDEC, IMAGE2, MJPEG, SCALE_FILTER) += fate-jpg-12bpp
fate-jpg-12bpp: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpg/12bpp.jpg -f rawvideo -pix_fmt gray16le -vf setsar=sar=sar,scale

FATE_JPG += fate-jpg-jfif
fate-jpg-jfif: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpg/20242.jpg

FATE_JPG += fate-jpg-rgb-baseline
fate-jpg-rgb-baseline: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpg/george-insect-rgb-baseline.jpg

FATE_JPG += fate-jpg-rgb-progressive
fate-jpg-rgb-progressive: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpg/george-insect-rgb-progressive.jpg

FATE_JPG += fate-jpg-rgb-221
fate-jpg-rgb-221: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpg/george-insect-rgb-xyb.jpg

FATE_JPG += fate-jpg-rgb-1
fate-jpg-rgb-1: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpg/jpg-8930-1.jpg
FATE_JPG += fate-jpg-rgb-2
fate-jpg-rgb-2: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpg/jpg-8930-2.jpg
FATE_JPG += fate-jpg-rgb-3
fate-jpg-rgb-3: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpg/jpg-8930-3.jpg
FATE_JPG += fate-jpg-rgb-4
fate-jpg-rgb-4: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpg/jpg-8930-4.jpg
FATE_JPG += fate-jpg-rgb-5
fate-jpg-rgb-5: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpg/jpg-8930-5.jpg

FATE_JPG_TRANSCODE-$(call TRANSCODE, MJPEG, MJPEG IMAGE_JPEG_PIPE, IMAGE_PNG_PIPE_DEMUXER PNG_DECODER SCALE_FILTER) += fate-jpg-icc
fate-jpg-icc: CMD = transcode png_pipe $(TARGET_SAMPLES)/png1/lena-int_rgb24.png mjpeg "-vf scale" "" "-show_frames"

FATE_JPG-$(call DEMDEC, IMAGE2, MJPEG) += $(FATE_JPG)
FATE_IMAGE_FRAMECRC += $(FATE_JPG-yes)
FATE_IMAGE_TRANSCODE += $(FATE_JPG_TRANSCODE-yes)
fate-jpg: $(FATE_JPG-yes) $(FATE_JPG_TRANSCODE-yes)

FATE_JPEGLS += fate-jpegls-2bpc
fate-jpegls-2bpc: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpegls/4.jls

FATE_JPEGLS += fate-jpegls-3bpc
fate-jpegls-3bpc: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpegls/8.jls

FATE_JPEGLS += fate-jpegls-5bpc
fate-jpegls-5bpc: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpegls/32.jls

FATE_JPEGLS += fate-jpegls-7bpc
fate-jpegls-7bpc: CMD = framecrc -idct simple -i $(TARGET_SAMPLES)/jpegls/128.jls

FATE_JPEGLS-$(call DEMDEC, IMAGE2, JPEGLS) += $(FATE_JPEGLS)
FATE_IMAGE_FRAMECRC += $(FATE_JPEGLS-yes)
fate-jpegls: $(FATE_JPEGLS-yes)

FATE_IMAGE_FRAMECRC-$(call DEMDEC, IMAGE2, QDRAW) += fate-pict
fate-pict: CMD = framecrc -i $(TARGET_SAMPLES)/quickdraw/TRU256.PCT -pix_fmt rgb24

FATE_IMAGE_FRAMECRC-$(call DEMDEC, IMAGE2, PICTOR, SCALE_FILTER) += fate-pictor
fate-pictor: CMD = framecrc -i $(TARGET_SAMPLES)/pictor/MFISH.PIC -pix_fmt rgb24 -vf scale

FATE_IMAGE_FRAMECRC-$(call PARSERDEMDEC, PNG, IMAGE2PIPE, PNG) += fate-pngparser
fate-pngparser: CMD = framecrc -f image2pipe -i $(TARGET_SAMPLES)/png1/feed_4x_concat.png -pix_fmt rgba

define FATE_IMGSUITE_PNG
FATE_PNG-$(call DEMDEC, IMAGE2, PNG, SCALE_FILTER) += fate-png-$(1)
fate-png-$(1): CMD = framecrc -auto_conversion_filters -i $(TARGET_SAMPLES)/png1/lena-$(1).png -sws_flags +accurate_rnd+bitexact -pix_fmt rgb24
endef

PNG_COLORSPACES = gray8 gray16 rgb24 rgb48 rgba rgba64 ya8 ya16
$(foreach CLSP,$(PNG_COLORSPACES),$(eval $(call FATE_IMGSUITE_PNG,$(CLSP))))

FATE_PNG += fate-png-int-rgb24
fate-png-int-rgb24: CMD = framecrc -i $(TARGET_SAMPLES)/png1/lena-int_rgb24.png -sws_flags +accurate_rnd+bitexact

FATE_PNG_PROBE += fate-png-frame-metadata
fate-png-frame-metadata: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_entries frame_tags \
    -i $(TARGET_SAMPLES)/filter/pixelart0.png

FATE_PNG_PROBE += fate-png-side-data
fate-png-side-data: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_frames \
    -i $(TARGET_SAMPLES)/png1/lena-int_rgb24.png

FATE_PNG_TRANSCODE-$(call TRANSCODE, PNG, IMAGE2 IMAGE_PNG_PIPE) += fate-png-icc
fate-png-icc: CMD = transcode png_pipe $(TARGET_SAMPLES)/png1/lena-int_rgb24.png image2 "-c png" "" "-show_frames"

FATE_PNG_PROBE-$(call ALLYES, LCMS2) += fate-png-icc-parse
fate-png-icc-parse: CMD = run ffprobe$(PROGSSUF)$(EXESUF) -show_frames \
    -flags2 icc_profiles $(TARGET_SAMPLES)/png1/lena-int_rgb24.png

FATE_PNG_TRANSCODE-$(call TRANSCODE, PNG HEVC, IMAGE2PIPE HEVC, \
    IMAGE_PNG_PIPE_DEMUXER HEVC_PARSER PNG_DECODER SCALE_FILTER) += fate-png-mdcv
fate-png-mdcv: CMD = transcode hevc $(TARGET_SAMPLES)/hevc/hdr10_plus_h265_sample.hevc image2pipe \
    "-pix_fmt rgb24 -vf scale -c png" "" \
    "-show_frames -show_entries frame=side_data_list -of flat"

FATE_PNG-$(call DEMDEC, IMAGE2, PNG) += $(FATE_PNG)
FATE_PNG_PROBE-$(call DEMDEC, IMAGE2, PNG) += $(FATE_PNG_PROBE)
FATE_IMAGE_FRAMECRC += $(FATE_PNG-yes)
FATE_IMAGE_PROBE += $(FATE_PNG_PROBE-yes)
FATE_IMAGE_TRANSCODE += $(FATE_PNG_TRANSCODE-yes)
fate-png: $(FATE_PNG-yes) $(FATE_PNG_PROBE-yes) $(FATE_PNG_TRANSCODE-yes)

FATE_IMAGE_FRAMECRC-$(call DEMDEC, IMAGE2, PTX, SCALE_FILTER) += fate-ptx
fate-ptx: CMD = framecrc -i $(TARGET_SAMPLES)/ptx/_113kw_pic.ptx -pix_fmt rgb24 -vf scale

define FATE_IMGSUITE_PSD
FATE_PSD-$(call DEMDEC, IMAGE2, PSD, SCALE_FILTER) += fate-psd-$(1)
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

FATE_IMAGE_FRAMECRC += $(FATE_PSD-yes)
fate-psd: $(FATE_PSD-yes)

define FATE_IMGSUITE_SGI
FATE_SGI += fate-sgi-$(1) fate-sgi-$(1)-rle
fate-sgi-$(1): CMD = framecrc -i $(TARGET_SAMPLES)/sgi/libav_$(1).sgi -sws_flags +accurate_rnd+bitexact
fate-sgi-$(1)-rle: CMD = framecrc -i $(TARGET_SAMPLES)/sgi/libav_$(1)_rle.sgi -sws_flags +accurate_rnd+bitexact
endef

SGI_COLORSPACES = gray8 gray16 rgb24 rgb48 rgba rgba64
$(foreach CLSP,$(SGI_COLORSPACES),$(eval $(call FATE_IMGSUITE_SGI,$(CLSP))))

FATE_SGI-$(call DEMDEC, IMAGE2, SGI) += $(FATE_SGI)
FATE_IMAGE_FRAMECRC += $(FATE_SGI-yes)
fate-sgi: $(FATE_SGI-yes)

define FATE_IMGSUITE_SUNRASTER
FATE_SUNRASTER-$(call DEMDEC, IMAGE2, SUNRAST, $(call ADD_SCALE_IF, $(SUNRASTER_OPTS_$(1)))) += fate-sunraster-$(1)
fate-sunraster-$(1): CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/lena-$(1).sun $(SUNRASTER_OPTS_$(1))
endef
SUNRASTER_OPTS_8bit-raw := -pix_fmt rgb24 -vf scale
SUNRASTER_OPTS_8bit-rle := -pix_fmt rgb24 -vf scale
SUNRASTER_TESTS := 1bit-raw 1bit-rle 8bit-raw 8bit-rle 24bit-raw 24bit-rle
$(foreach TEST,$(SUNRASTER_TESTS),$(eval $(call FATE_IMGSUITE_SUNRASTER,$(TEST))))

FATE_SUNRASTER += fate-sunraster-8bit_gray-raw
fate-sunraster-8bit_gray-raw: CMD = framecrc -i $(TARGET_SAMPLES)/sunraster/gray.ras

FATE_SUNRASTER-$(call DEMDEC, IMAGE2, SUNRAST) += $(FATE_SUNRASTER)

FATE_IMAGE_FRAMECRC += $(FATE_SUNRASTER-yes)
fate-sunraster: $(FATE_SUNRASTER-yes)

define FATE_IMGSUITE_TARGA
FATE_TARGA-$(call DEMDEC, IMAGE2, TARGA, $(call ADD_SCALE_IF, $(TARGA_OPTS_$(1)))) += fate-targa-conformance-$(1)
fate-targa-conformance-$(1): CMD = framecrc -i $(TARGET_SAMPLES)/targa-conformance/$(1).TGA $(TARGA_OPTS_$(1))
endef
TARGA_FMT := CBW8       \
             CCM8       \
             CTC16      \
             CTC24      \
             CTC32      \
             UBW8       \
             UCM8       \
             UTC16      \
             UTC24      \
             UTC32
TARGA_OPTS_CCM8  := -pix_fmt rgba -vf scale
TARGA_OPTS_UCM8  := -pix_fmt rgba -vf scale
TARGA_OPTS_UTC16 := -pix_fmt rgb555le
$(foreach FMT,$(TARGA_FMT),$(eval $(call FATE_IMGSUITE_TARGA,$(FMT))))

FATE_TARGA-$(call DEMDEC, IMAGE2, TARGA) += fate-targa-top-to-bottom
fate-targa-top-to-bottom: CMD = framecrc -i $(TARGET_SAMPLES)/targa/lena-top-to-bottom.tga

FATE_IMAGE_FRAMECRC += $(FATE_TARGA-yes)
fate-targa: $(FATE_TARGA-yes)

FATE_TIFF += fate-tiff-fax-g3
fate-tiff-fax-g3: CMD = framecrc -i $(TARGET_SAMPLES)/CCITT_fax/G31D.TIF

FATE_TIFF += fate-tiff-fax-g3s
fate-tiff-fax-g3s: CMD = framecrc -i $(TARGET_SAMPLES)/CCITT_fax/G31DS.TIF

FATE_TIFF += fate-tiff-uncompressed-rgbf32le
fate-tiff-uncompressed-rgbf32le: CMD = framecrc -i $(TARGET_SAMPLES)/tiff/uncompressed_rgbf32le.tif

FATE_TIFF += fate-tiff-uncompressed-rgbaf32le
fate-tiff-uncompressed-rgbaf32le: CMD = framecrc -i $(TARGET_SAMPLES)/tiff/uncompressed_rgbaf32le.tif

FATE_TIFF += fate-tiff-lzw-rgbf32le
fate-tiff-lzw-rgbf32le: CMD = framecrc -i $(TARGET_SAMPLES)/tiff/lzw_rgbf32le.tif

FATE_TIFF += fate-tiff-lzw-rgbaf32le
fate-tiff-lzw-rgbaf32le: CMD = framecrc -i $(TARGET_SAMPLES)/tiff/lzw_rgbaf32le.tif

FATE_TIFF_ZIP += fate-tiff-zip-rgbf32le
fate-tiff-zip-rgbf32le: CMD = framecrc -i $(TARGET_SAMPLES)/tiff/zip_rgbf32le.tif

FATE_TIFF_ZIP += fate-tiff-zip-rgbaf32le
fate-tiff-zip-rgbaf32le: CMD = framecrc -i $(TARGET_SAMPLES)/tiff/zip_rgbaf32le.tif

FATE_TIFF-$(call FRAMECRC, IMAGE2, TIFF, ZLIB) += $(FATE_TIFF_ZIP)
FATE_TIFF-$(call FRAMECRC, IMAGE2, TIFF) += $(FATE_TIFF)

FATE_IMAGE_FRAMECRC += $(FATE_TIFF-yes)
fate-tiff: $(FATE_TIFF-yes)

FATE_WEBP += fate-webp-rgb-lossless
fate-webp-rgb-lossless: CMD = framecrc -i $(TARGET_SAMPLES)/webp/rgb_lossless.webp

FATE_WEBP += fate-webp-rgb-lena-lossless
fate-webp-rgb-lena-lossless: CMD = framecrc -i $(TARGET_SAMPLES)/webp/rgb_lena_lossless.webp

FATE_WEBP-$(call DEMDEC, IMAGE2, WEBP, SCALE_FILTER) += fate-webp-rgb-lena-lossless-rgb24
fate-webp-rgb-lena-lossless-rgb24: CMD = framecrc -i $(TARGET_SAMPLES)/webp/rgb_lena_lossless.webp -pix_fmt rgb24 -vf scale

FATE_WEBP += fate-webp-rgba-lossless
fate-webp-rgba-lossless: CMD = framecrc -i $(TARGET_SAMPLES)/webp/rgba_lossless.webp

FATE_WEBP += fate-webp-rgb-lossless-palette-predictor
fate-webp-rgb-lossless-palette-predictor: CMD = framecrc -i $(TARGET_SAMPLES)/webp/dual_transform.webp

FATE_WEBP += fate-webp-rgb-lossy-q80
fate-webp-rgb-lossy-q80: CMD = framecrc -i $(TARGET_SAMPLES)/webp/rgb_q80.webp

FATE_WEBP += fate-webp-rgba-lossy-q80
fate-webp-rgba-lossy-q80: CMD = framecrc -i $(TARGET_SAMPLES)/webp/rgba_q80.webp

FATE_WEBP-$(call DEMDEC, IMAGE2, WEBP) += $(FATE_WEBP)
FATE_IMAGE_FRAMECRC += $(FATE_WEBP-yes)
fate-webp: $(FATE_WEBP-yes)

FATE_IMAGE_FRAMECRC-$(call DEMDEC, IMAGE2, XFACE) += fate-xface
fate-xface: CMD = framecrc -i $(TARGET_SAMPLES)/xface/lena.xface

FATE_XBM += fate-xbm10
fate-xbm10: CMD = framecrc -i $(TARGET_SAMPLES)/xbm/xl.xbm

FATE_XBM += fate-xbm11
fate-xbm11: CMD = framecrc -i $(TARGET_SAMPLES)/xbm/lbw.xbm

FATE_XBM-$(call DEMDEC, IMAGE2, XBM) += $(FATE_XBM)
FATE_IMAGE_FRAMECRC += $(FATE_XBM-yes)
fate-xbm: $(FATE_XBM-yes)

FATE_IMAGE-$(call ALLYES, FILE_PROTOCOL FRAMECRC_MUXER PIPE_PROTOCOL) += $(FATE_IMAGE_FRAMECRC) $(FATE_IMAGE_FRAMECRC-yes)
FATE_IMAGE += $(FATE_IMAGE-yes)
FATE_IMAGE_PROBE += $(FATE_IMAGE_PROBE-yes)
FATE_IMAGE_TRANSCODE += $(FATE_IMAGE_TRANSCODE-yes)

FATE_SAMPLES_FFMPEG += $(FATE_IMAGE)
FATE_SAMPLES_FFPROBE += $(FATE_IMAGE_PROBE)
FATE_SAMPLES_FFMPEG_FFPROBE += $(FATE_IMAGE_TRANSCODE)

fate-image: $(FATE_IMAGE) $(FATE_IMAGE_PROBE) $(FATE_IMAGE_TRANSCODE)
