FATE_DNN += fate-dnn-layer-pad
fate-dnn-layer-pad: $(DNNTESTSDIR)/dnn-layer-pad-test$(EXESUF)
fate-dnn-layer-pad: CMD = run $(DNNTESTSDIR)/dnn-layer-pad-test$(EXESUF)
fate-dnn-layer-pad: CMP = null

FATE_DNN += fate-dnn-layer-conv2d
fate-dnn-layer-conv2d: $(DNNTESTSDIR)/dnn-layer-conv2d-test$(EXESUF)
fate-dnn-layer-conv2d: CMD = run $(DNNTESTSDIR)/dnn-layer-conv2d-test$(EXESUF)
fate-dnn-layer-conv2d: CMP = null

FATE-yes += $(FATE_DNN)

fate-dnn: $(FATE_DNN)
