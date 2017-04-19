PROJECT_HOME = ..
BUILD_DIR ?= $(PROJECT_HOME)/build/make
include $(BUILD_DIR)/make.defs
LIBUV_ROOT = $(CURDIR)/libuv-$(BUILD_PLATFORM)
export LIBUV_ROOT

PRE_BUILD = build 
.PHONY: build

build:
	echo $(LIBUV_ROOT)
	cd $(CURDIR)/libuv-1.11.0 && ./autogen.sh && ./configure --enable-static=yes --enable-shared=no --with-pic --prefix=$(LIBUV_ROOT) && make -j 8 install
	cd $(LIBUV_ROOT)/lib && ar x libuv.a

clean-all:
	cd libuv-1.11.0 && make clean || true
	rm -rf $(LIBUV_ROOT)

include $(BUILD_DIR)/make.rules
