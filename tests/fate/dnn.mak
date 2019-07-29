FATE_DNN += fate-dnn-layer-pad
fate-dnn-layer-pad: $(DNNTESTSDIR)/dnn-layer-pad-test$(EXESUF)
fate-dnn-layer-pad: CMD = run $(DNNTESTSDIR)/dnn-layer-pad-test$(EXESUF)
fate-dnn-layer-pad: CMP = null

FATE-yes += $(FATE_DNN)

fate-dnn: $(FATE_DNN)
