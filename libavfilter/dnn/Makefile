OBJS-$(CONFIG_DNN)                           += dnn/dnn_interface.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_io_proc.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layers.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layer_avgpool.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layer_dense.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layer_pad.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layer_conv2d.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layer_depth2space.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layer_maximum.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layer_mathbinary.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layer_mathunary.o

DNN-OBJS-$(CONFIG_LIBTENSORFLOW)             += dnn/dnn_backend_tf.o
DNN-OBJS-$(CONFIG_LIBOPENVINO)               += dnn/dnn_backend_openvino.o

OBJS-$(CONFIG_DNN)                           += $(DNN-OBJS-yes)
