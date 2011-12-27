FATE_TESTS += fate-msmpeg4v1
fate-msmpeg4v1: CMD = framecrc -flags +bitexact -dct fastint -idct simple -i $(SAMPLES)/msmpeg4v1/mpg4.avi -an

FATE_TESTS += fate-msvideo1-16bit
fate-msvideo1-16bit: CMD = framecrc -i $(SAMPLES)/cram/clock-cram16.avi -pix_fmt rgb24

FATE_TESTS += fate-msvideo1-8bit
fate-msvideo1-8bit: CMD = framecrc -i $(SAMPLES)/cram/skating.avi -t 1 -pix_fmt rgb24

FATE_TESTS += fate-wmv8-drm
# discard last packet to avoid fails due to overread of VC-1 decoder
fate-wmv8-drm: CMD = framecrc -cryptokey 137381538c84c068111902a59c5cf6c340247c39 -i $(SAMPLES)/wmv8/wmv_drm.wmv -an -vframes 162

FATE_TESTS += fate-wmv8-drm-nodec
fate-wmv8-drm-nodec: CMD = framecrc -cryptokey 137381538c84c068111902a59c5cf6c340247c39 -i $(SAMPLES)/wmv8/wmv_drm.wmv -acodec copy -vcodec copy

FATE_TESTS += fate-vc1
fate-vc1: CMD = framecrc -i $(SAMPLES)/vc1/SA00040.vc1
