#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME 	:= irserver
NKOLBAN_LIB 	:= cpp_utils
CURL_LIB	:= curl
OUTERCOMPONENTS	:= components/$(NKOLBAN_LIB) components/posix \
		   components/mongoose
SIGNEDIMAGE	:= build/$(PROJECT_NAME).sbin

myall: all $(SIGNEDIMAGE)

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

$(SIGNEDIMAGE): build/$(PROJECT_NAME).bin
	tools/signimage $(CONFIG_OTA_IMAGE_SIGNING_KEY) \
	                build/$(PROJECT_NAME).bin

otaflash: $(SIGNEDIMAGE)
	if [ "$(OTAUSER)" = "" ] || [ "$(OTAPASS)" = "" ] || \
	   [ "$(OTAADDR)" = "" ]; then \
	    echo "Parameters need to proceed OTA are not specified." >&2; \
	    echo "Make sure following variables are specified:" >&2; \
	    echo "    OTAUSER" >&2; \
	    echo "    OTAPATH" >&2; \
	    echo "    OTAADDR" >&2; \
	    exit 1;\
	fi
	tools/ota "$(OTAUSER)" "$(OTAPASS)" "$(OTAADDR)" $(SIGNEDIMAGE)
