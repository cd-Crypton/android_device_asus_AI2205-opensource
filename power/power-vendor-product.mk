#Power product definitions
PRODUCT_PACKAGES += android.hardware.power-service
PRODUCT_PACKAGES += android.hardware.power-impl

#Powerhint File
ifeq ($(TARGET_BOARD_PLATFORM),msmnile)
PRODUCT_COPY_FILES += vendor/qcom/opensource/power/config/msmnile/powerhint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.xml
else ifeq ($(TARGET_BOARD_PLATFORM),kona)
PRODUCT_COPY_FILES += vendor/qcom/opensource/power/config/kona/powerhint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.xml
else ifeq ($(TARGET_BOARD_PLATFORM),lito)
PRODUCT_COPY_FILES += vendor/qcom/opensource/power/config/lito/powerhint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.xml
else ifeq ($(TARGET_BOARD_PLATFORM),atoll)
PRODUCT_COPY_FILES += vendor/qcom/opensource/power/config/atoll/powerhint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.xml
else ifeq ($(TARGET_BOARD_PLATFORM),lahaina)
PRODUCT_COPY_FILES += vendor/qcom/opensource/power/config/lahaina/powerhint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.xml
else ifeq ($(TARGET_BOARD_PLATFORM),holi)
PRODUCT_COPY_FILES += vendor/qcom/opensource/power/config/holi/powerhint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.xml
else ifeq ($(TARGET_BOARD_PLATFORM),taro)
PRODUCT_COPY_FILES += vendor/qcom/opensource/power/config/taro/powerhint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.xml
else ifeq ($(TARGET_BOARD_PLATFORM),kalama)
ifneq (,$(filter AI2205,$(ASUS_BUILD_PROJECT)))
PRODUCT_COPY_FILES += vendor/qcom/opensource/power/config/kalama/powerhint_AI2205.xml:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.xml
else
PRODUCT_COPY_FILES += vendor/qcom/opensource/power/config/kalama/powerhint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.xml
endif
endif
