#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME 	:= irserver
NKOLBAN_LIB 	:= cpp_utils
CURL_LIB	:= curl
OUTERCOMPONENTS	:= components/$(NKOLBAN_LIB) components/posix \
		   components/mongoose

include $(IDF_PATH)/make/project.mk

menuconfig defconfig: $(OUTERCOMPONENTS)

components/$(NKOLBAN_LIB): components
	cp -R $(NKOLBAN_ESP32_PATH)/$(NKOLBAN_LIB) components/

components/posix: components
	echo BUILDING $@ target
	cp -R $(NKOLBAN_ESP32_PATH)/posix $@
	touch $@/component.mk

components/mongoose: components
	cd components; \
	git clone https://github.com/cesanta/mongoose.git
	echo "COMPONENT_ADD_INCLUDEDIRS=." > $@/component.mk
	echo "CFLAGS += -DMG_ENABLE_HTTP_STREAMING_MULTIPART " \
	     "-DMG_ENABLE_FILESYSTEM=1" >> $@/component.mk

components:
	mkdir $@
