#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME 	:= irserver
NKOLBAN_LIB 	:= cpp_utils

include $(IDF_PATH)/make/project.mk

menuconfig defconfig: components/$(NKOLBAN_LIB)

components/$(NKOLBAN_LIB): components
	cp -R $(NKOLBAN_ESP32_PATH)/$(NKOLBAN_LIB) components/

components:
	mkdir $@
