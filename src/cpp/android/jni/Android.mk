LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := mocarpc_jni
LOCAL_SRC_FILES := com_moca_rpc_RPC.cc RPC.cc
LOCAL_CPP_EXTENSION=.cc

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../rpc
LOCAL_CPPFLAGS := -D__STDC_LIMIT_MACROS
LOCAL_LDLIBS := -lz

include $(BUILD_SHARED_LIBRARY)
