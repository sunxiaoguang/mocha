LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := mocarpc_nano_jni
LOCAL_SRC_FILES := JniChannel.cc RPCChannelNano.cc RPCNano.cc RPCChannelEasy.cc RPCProtocol.cc RPCLogging.cc
LOCAL_CPP_EXTENSION=.cc

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../include $(LOCAL_PATH)/../../rpc
LOCAL_CPPFLAGS := -D__STDC_LIMIT_MACROS -DMOCA_RPC_NANO
LOCAL_LDLIBS := -lz -llog

include $(BUILD_SHARED_LIBRARY)
