ccflags-y := -Ofast -fgcse-sm -marm -mtune=cortex-a15 -mcpu=cortex-a15 -march=armv7ve -mfpu=neon-vfpv4
obj-$(CONFIG_ION) +=	ion.o ion_heap.o ion_page_pool.o ion_system_heap.o \
			ion_carveout_heap.o ion_chunk_heap.o
obj-$(CONFIG_CMA) += ion_cma_heap.o ion_cma_secure_heap.o
obj-$(CONFIG_ION_TEST) += ion_test.o
obj-$(CONFIG_ION_TEGRA) += tegra/
obj-$(CONFIG_ION_MSM) += ion_removed_heap.o msm/
