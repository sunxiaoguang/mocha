PROJECT_HOME = ../../..
BUILD_DIR ?= $(PROJECT_HOME)/build/make

include $(BUILD_DIR)/make.defs

OCPPSRCS += MCRPCChannel.mm

LIB_NAME = MCRPCChannel
INCLUDE_DIRS += ../include ../rpc
LDFLAGS += -std=c++11 -stdlib=libc++
CXXFLAGS += -std=c++11 -stdlib=libc++ -fobjc-arc 

include $(BUILD_DIR)/make.rules

