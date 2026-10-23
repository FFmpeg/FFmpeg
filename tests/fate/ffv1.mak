FATE_FFV1_FLOAT32-$(call TRANSCODE, FFV1, NUT, LAVFI_INDEV WRAPPED_AVFRAME_DECODER COLOR_FILTER FORMAT_FILTER GEQ_FILTER SCALE_FILTER) += \
    fate-ffv1-golomb-float32-26bit fate-ffv1-golomb-float32-1bit
FATE_FFV1_FLOAT32-$(call TRANSCODE, FFV1, NUT, LAVFI_INDEV WRAPPED_AVFRAME_DECODER TESTSRC_FILTER FORMAT_FILTER SCALE_FILTER) += \
    fate-ffv1-float32-slices

fate-ffv1-golomb-float32-26bit: CMD = transcode lavfi "color=black:s=352x288:r=1:d=8,format=gbrpf32,geq=r=X+Y*W:g=X+Y*W:b=X+W*if(lt(Y\,2*(25+N+floor(N/2)+2*floor(N/7))-4)\,if(mod(Y\,2)\,290-(25+N+floor(N/2)+2*floor(N/7))+(Y-1)/2\,290-2*(25+N+floor(N/2)+2*floor(N/7))+Y/2)\,if(eq(Y\,2*(25+N+floor(N/2)+2*floor(N/7))-4)\,19\,if(lt(Y\,2*(25+N+floor(N/2)+2*floor(N/7))+16)\,Y-2*(25+N+floor(N/2)+2*floor(N/7))+3\,if(lt(Y\,286)\,Y-2*(25+N+floor(N/2)+2*floor(N/7))+4\,Y-(25+N+floor(N/2)+2*floor(N/7))+2))))" nut \
    "-c:v ffv1 -strict experimental -coder 0 -remap_mode 1 -slices 1 -g 1" "-pix_fmt gbrpf32le -vf scale"

# Planes with 2 distinct values are coded with 1 bit residuals while the
# Golomb-Rice parameter k starts at 2.
fate-ffv1-golomb-float32-1bit: CMD = transcode lavfi "color=black:s=64x48:r=1:d=1,format=gbrpf32,geq=r=1+mod(floor(X*Y/7)\,2):g=1+mod(floor(X*Y/7)\,2):b=1+mod(floor(X*Y/7)\,2)" nut \
    "-c:v ffv1 -strict experimental -coder 0 -remap_mode 1" "-pix_fmt gbrpf32le -vf scale"

# Default slice layout of a 32bit float frame larger than 360x288
fate-ffv1-float32-slices: CMD = transcode lavfi "testsrc=size=704x576:r=1:d=1,format=gbrpf32" nut \
    "-c:v ffv1 -strict experimental" "-pix_fmt gbrpf32le -vf scale"

FATE_FFMPEG += $(FATE_FFV1_FLOAT32-yes)
fate-ffv1: $(FATE_FFV1_FLOAT32-yes)
