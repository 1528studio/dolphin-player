LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := application

APP_SUBDIRS := $(patsubst $(LOCAL_PATH)/%, %, $(shell find $(LOCAL_PATH)/src -type d))
APP_SUBDIRS += $(patsubst $(LOCAL_PATH)/%, %, $(shell find $(LOCAL_PATH)/resources -type d))

# Add more subdirs here, like src/subdir1 src/subdir2

LOCAL_CFLAGS := $(foreach D, $(APP_SUBDIRS), -I$(LOCAL_PATH)/$(D)) \
				-I$(AVPLAYER_PATH)"/jni/sdl/include" \
				-I$(AVPLAYER_PATH)"/jni/sdl_ttf" \
				-I$(AVPLAYER_PATH)"/jni/ffmpeg" \
				-D__STDC_CONSTANT_MACROS \
				-I$(AVPLAYER_PATH)"/jni/sdl_image/include" \
				-I$(AVPLAYER_PATH)"/jni/app/include" \
				-I$(AVPLAYER_PATH)"/jni/iconv/include" \
				-I$(AVPLAYER_PATH)"/jni/iconv/srclib" \
				-I$(AVPLAYER_PATH)"/jni/universalchardet/include" \
				-I$(AVPLAYER_PATH)"/jni/yuv2rgb/include" \

LOCAL_CFLAGS += $(CC_OPTIMIZE_FLAG) 

ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
   LOCAL_CFLAGS += -DHAVE_NEON=1
endif
#Change C++ file extension as appropriate
LOCAL_CPP_EXTENSION := .cpp

LOCAL_SRC_FILES := $(foreach F, $(APP_SUBDIRS), $(addprefix $(F)/,$(notdir $(wildcard $(LOCAL_PATH)/$(F)/*.cpp))))
# Uncomment to also add C sources
LOCAL_SRC_FILES += $(foreach F, $(APP_SUBDIRS), $(addprefix $(F)/,$(notdir $(wildcard $(LOCAL_PATH)/$(F)/*.c))))

LOCAL_SRC_FILES += $(foreach F, $(APP_SUBDIRS), $(addprefix $(F)/,$(notdir $(wildcard $(LOCAL_PATH)/$(F)/*.S))))

#LOCAL_SHARED_LIBRARIES := ffmpeg sdl sdl_ttf sdl_image iconv universalchardet andprof yuv2rgb
#LOCAL_SHARED_LIBRARIES := ffmpeg sdl sdl_ttf sdl_image iconv universalchardet yuv2rgb
LOCAL_SHARED_LIBRARIES := sdl sdl_ttf sdl_image iconv universalchardet yuv2rgb

LOCAL_STATIC_LIBRARIES := freetype

LIBS_WITH_LONG_SYMBOLS := $(strip $(shell \
	for f in $(LOCAL_PATH)/../../libs/armeabi/*.so ; do \
		if echo $$f | grep "libapplication[.]so" > /dev/null ; then \
			continue ; \
		fi ; \
		if [ -e "$$f" ] ; then \
			if nm -g $$f | cut -c 12- | egrep '.{128}' > /dev/null ; then \
				echo $$f | grep -o 'lib[^/]*[.]so' ; \
			fi ; \
		fi ; \
	done \
) )

ifneq "$(LIBS_WITH_LONG_SYMBOLS)" ""
$(foreach F, $(LIBS_WITH_LONG_SYMBOLS), \
$(info Library $(F): abusing symbol names are: \
$(shell nm -g $(LOCAL_PATH)/../../libs/armeabi/$(F) | cut -c 12- | egrep '.{128}' ) ) \
$(info Library $(F) contains symbol names longer than 128 bytes, \
YOUR CODE WILL DEADLOCK WITHOUT ANY WARNING when you'll access such function - \
please make this library static to avoid problems. ) )
$(error Detected libraries with too long symbol names. Remove all files under project/libs/armeabi, make these libs static, and recompile)
endif


LOCAL_LDLIBS := -lGLESv1_CM -ldl -llog 
LOCAL_LDLIBS += "c:/p/mylibs/libffmpeg.so"

include $(BUILD_SHARED_LIBRARY)
