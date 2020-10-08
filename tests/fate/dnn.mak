FATE_DNN += fate-dnn-layer-pad
fate-dnn-layer-pad: $(DNNTESTSDIR)/dnn-layer-pad-test$(EXESUF)
fate-dnn-layer-pad: CMD = run $(DNNTESTSDIR)/dnn-layer-pad-test$(EXESUF)
fate-dnn-layer-pad: CMP = null

FATE_DNN += fate-dnn-layer-conv2d
fate-dnn-layer-conv2d: $(DNNTESTSDIR)/dnn-layer-conv2d-test$(EXESUF)
fate-dnn-layer-conv2d: CMD = run $(DNNTESTSDIR)/dnn-layer-conv2d-test$(EXESUF)
fate-dnn-layer-conv2d: CMP = null

FATE_DNN += fate-dnn-layer-depth2space
fate-dnn-layer-depth2space: $(DNNTESTSDIR)/dnn-layer-depth2space-test$(EXESUF)
fate-dnn-layer-depth2space: CMD = run $(DNNTESTSDIR)/dnn-layer-depth2space-test$(EXESUF)
fate-dnn-layer-depth2space: CMP = null

FATE_DNN += fate-dnn-layer-mathbinary
fate-dnn-layer-mathbinary: $(DNNTESTSDIR)/dnn-layer-mathbinary-test$(EXESUF)
fate-dnn-layer-mathbinary: CMD = run $(DNNTESTSDIR)/dnn-layer-mathbinary-test$(EXESUF)
fate-dnn-layer-mathbinary: CMP = null

FATE_DNN += fate-dnn-layer-maximum
fate-dnn-layer-maximum: $(DNNTESTSDIR)/dnn-layer-maximum-test$(EXESUF)
fate-dnn-layer-maximum: CMD = run $(DNNTESTSDIR)/dnn-layer-maximum-test$(EXESUF)
fate-dnn-layer-maximum: CMP = null

FATE_DNN += fate-dnn-layer-mathunary
fate-dnn-layer-mathunary: $(DNNTESTSDIR)/dnn-layer-mathunary-test$(EXESUF)
fate-dnn-layer-mathunary: CMD = run $(DNNTESTSDIR)/dnn-layer-mathunary-test$(EXESUF)
fate-dnn-layer-mathunary: CMP = null

FATE_DNN += fate-dnn-layer-avgpool
fate-dnn-layer-avgpool: $(DNNTESTSDIR)/dnn-layer-avgpool-test$(EXESUF)
fate-dnn-layer-avgpool: CMD = run $(DNNTESTSDIR)/dnn-layer-avgpool-test$(EXESUF)
fate-dnn-layer-avgpool: CMP = null

FATE-$(CONFIG_DNN) += $(FATE_DNN)

fate-dnn: $(FATE_DNN)
