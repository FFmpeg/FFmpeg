FATE_FFV1_FLOAT32-$(call TRANSCODE, FFV1, NUT, LAVFI_INDEV WRAPPED_AVFRAME_DECODER COLOR_FILTER FORMAT_FILTER GEQ_FILTER SCALE_FILTER) += \
    fate-ffv1-golomb-float32-26bit

fate-ffv1-golomb-float32-26bit: CMD = transcode lavfi "color=black:s=352x288:r=1:d=8,format=gbrpf32,geq=r=X+Y*W:g=X+Y*W:b=X+W*if(lt(Y\,2*(25+N+floor(N/2)+2*floor(N/7))-4)\,if(mod(Y\,2)\,290-(25+N+floor(N/2)+2*floor(N/7))+(Y-1)/2\,290-2*(25+N+floor(N/2)+2*floor(N/7))+Y/2)\,if(eq(Y\,2*(25+N+floor(N/2)+2*floor(N/7))-4)\,19\,if(lt(Y\,2*(25+N+floor(N/2)+2*floor(N/7))+16)\,Y-2*(25+N+floor(N/2)+2*floor(N/7))+3\,if(lt(Y\,286)\,Y-2*(25+N+floor(N/2)+2*floor(N/7))+4\,Y-(25+N+floor(N/2)+2*floor(N/7))+2))))" nut \
    "-c:v ffv1 -strict experimental -coder 0 -remap_mode 1 -slices 1 -g 1" "-pix_fmt gbrpf32le -vf scale"

FATE_FFMPEG += $(FATE_FFV1_FLOAT32-yes)
fate-ffv1: $(FATE_FFV1_FLOAT32-yes)

