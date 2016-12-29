PROJECT_HOME = ../../..
BUILD_DIR ?= $(PROJECT_HOME)/build/make
LIBUV_ROOT ?= /usr/local/libuv

include $(BUILD_DIR)/make.defs

CPPSRCS += SampleRPCServer.cc
CPPFLAGS += -DMOCA_RPC_FULL

EXETARGET = SampleNanoChannel SampleEasyChannel SampleChannel SampleChannelC \
						SampleServer SampleServerC SampleRPCServer SampleRPCServerC \
						Throughput ThroughputAsync EchoServer SpreadClient TubeClient \
						BenchmarkThreadPool
CXXFLAGS += -std=c++11
LDFLAGS += -std=c++11

INCLUDE_DIRS += $(PROJECT_HOME)/src/cpp/include $(PROJECT_HOME)/src/cpp/rpc $(LIBUV_ROOT)/include

DEPLIBS += MocaRPC z uuid uv

include $(BUILD_DIR)/make.rules
