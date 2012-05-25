#libs to be built for QCOM targets only

ifeq ($(BOARD_USES_QCOM_HARDWARE),true)
    ifeq ($(BOARD_USES_LEGACY_QCOM),true)
        display-hals := libgralloc-legacy libcopybit
    else ifeq ($(TARGET_BOARD_PLATFORM),qsd8k)
        display-hals := libhwcomposer-qsd8k libgralloc libcopybit libgenlock
        display-hals += libtilerenderer libqcomui
    else
        display-hals := libqcomui libtilerenderer
        display-hals += libhwcomposer liboverlay libgralloc libgenlock libcopybit
    endif
        include $(call all-named-subdir-makefiles,$(display-hals))
endif

