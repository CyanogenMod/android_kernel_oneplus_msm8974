obj-$(CONFIG_MSM_VIDC_V4L2) += vidc/
obj-$(CONFIG_MSM_WFD) += wfd/
obj-$(CONFIG_DVB_MPQ) += dvb/

ifeq ($(CONFIG_ONEPLUS_CAMERA),y)
obj-$(CONFIG_MSMB_CAMERA) += camera_oneplus_v2/
endif
ifeq ($(CONFIG_OPPO_CAMERA),y)
obj-$(CONFIG_MSMB_CAMERA) += camera_oppo_v2/
endif
ifeq ($(CONFIG_MSMB_CAMERA_DEFAULT),y)
obj-$(CONFIG_MSMB_CAMERA) += camera_v2/
endif
obj-y += vcap/
