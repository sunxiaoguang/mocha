PROJECT_HOME = ../../..
BUILD_DIR ?= $(PROJECT_HOME)/build/make

include $(BUILD_DIR)/make.defs

CPPSRCS += TestServer.cc

EXETARGET = TestServer
CXXFLAGS += -std=c++11
LDFLAGS += -std=c++11

INCLUDE_DIRS += $(PROJECT_HOME)/src/cpp/include
LIBRARY_DIRS += /usr/local/libuv/lib

DEPLIBS += MocaRPC uv z

include $(BUILD_DIR)/make.rules
