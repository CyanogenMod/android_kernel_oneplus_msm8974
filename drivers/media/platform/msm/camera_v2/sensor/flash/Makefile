ccflags-y += -Idrivers/media/platform/msm/camera_v2
ccflags-y += -Idrivers/media/platform/msm/camera_v2/sensor/io
ccflags-y += -Idrivers/media/platform/msm/camera_v2/sensor/cci
ifeq ($(CONFIG_MACH_OPPO),y)
obj-$(CONFIG_MSMB_CAMERA) += msm_led_cci.o
endif
obj-$(CONFIG_MSMB_CAMERA) += msm_led_flash.o
obj-$(CONFIG_MSMB_CAMERA) += msm_led_trigger.o
ifneq ($(CONFIG_MACH_OPPO),y)
obj-$(CONFIG_MSMB_CAMERA) += msm_led_i2c_trigger.o
obj-$(CONFIG_MSMB_CAMERA) += adp1660.o
endif
obj-$(CONFIG_MSMB_CAMERA) += msm_led_torch.o
