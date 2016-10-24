FATE_MOV += fate-mov-dar
fate-mov-dar: CMD = probestream display_aspect_ratio $(TARGET_SAMPLES)/mov/displaymatrix.mov

FATE_MOV += fate-mov-display-matrix
fate-mov-display-matrix: CMD = probestream matrix $(TARGET_SAMPLES)/mov/displaymatrix.mov

FATE_MOV += fate-mov-rotation
fate-mov-rotation: CMD = probestream rotation $(TARGET_SAMPLES)/mov/displaymatrix.mov

FATE_MOV += fate-mov-sar
fate-mov-sar: CMD = probestream sample_aspect_ratio $(TARGET_SAMPLES)/mov/displaymatrix.mov

$(FATE_MOV): avprobe$(EXESUF)
FATE_SAMPLES-$(call ALLYES, AVPROBE MOV_DEMUXER) += $(FATE_MOV)
fate-mov: $(FATE_MOV)
