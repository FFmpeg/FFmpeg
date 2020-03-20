OBJS-$(CONFIG_DNN)                           += dnn/dnn_interface.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layers.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layer_pad.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layer_conv2d.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layer_depth2space.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layer_maximum.o
OBJS-$(CONFIG_DNN)                           += dnn/dnn_backend_native_layer_mathbinary.o

DNN-OBJS-$(CONFIG_LIBTENSORFLOW)             += dnn/dnn_backend_tf.o

OBJS-$(CONFIG_DNN)                           += $(DNN-OBJS-yes)
