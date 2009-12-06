LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(call all-subdir-java-files)
LOCAL_MODULE := spp_test
include $(BUILD_JAVA_LIBRARY)

include $(CLEAR_VARS)
ALL_PREBUILT += $(TARGET_OUT)/bin/spp_test
$(TARGET_OUT)/bin/spp_test : $(LOCAL_PATH)/spp_test | $(ACP)
	$(transform-prebuilt-to-target)

