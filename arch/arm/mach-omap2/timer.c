/*
 * linux/arch/arm/mach-omap2/timer.c
 *
 * OMAP2 GP timer support.
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Update to use new clocksource/clockevent layers
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 * Copyright (C) 2007 MontaVista Software, Inc.
 *
 * Original driver:
 * Copyright (C) 2005 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *         Juha Yrjölä <juha.yrjola@nokia.com>
 * OMAP Dual-mode timer framework support by Timo Teras
 *
 * Some parts based off of TI's 24xx code:
 *
 * Copyright (C) 2004-2009 Texas Instruments, Inc.
 *
 * Roughly modelled after the OMAP1 MPU timer code.
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/slab.h>
#include <linux/sched_clock.h>

#include <asm/mach/time.h>
#include <plat/dmtimer.h>
#include <asm/smp_twd.h>

#include "common.h"
#include <plat/omap_hwmod.h>
#include <plat/omap_device.h>
#include <plat/omap-pm.h>

#include "powerdomain.h"

/* Parent clocks, eventually these will come from the clock framework */

#define OMAP2_MPU_SOURCE	"sys_ck"
#define OMAP3_MPU_SOURCE	OMAP2_MPU_SOURCE
#define OMAP4_MPU_SOURCE	"sys_clkin_ck"
#define OMAP2_32K_SOURCE	"func_32k_ck"
#define OMAP3_32K_SOURCE	"omap_32k_fck"
#define OMAP4_32K_SOURCE	"sys_32k_ck"

#ifdef CONFIG_OMAP_32K_TIMER
#define OMAP2_CLKEV_SOURCE	OMAP2_32K_SOURCE
#define OMAP3_CLKEV_SOURCE	OMAP3_32K_SOURCE
#define OMAP4_CLKEV_SOURCE	OMAP4_32K_SOURCE
#define OMAP3_SECURE_TIMER	12
#else
#define OMAP2_CLKEV_SOURCE	OMAP2_MPU_SOURCE
#define OMAP3_CLKEV_SOURCE	OMAP3_MPU_SOURCE
#define OMAP4_CLKEV_SOURCE	OMAP4_MPU_SOURCE
#define OMAP3_SECURE_TIMER	1
#endif

/* MAX_GPTIMER_ID: number of GPTIMERs on the chip */
#define MAX_GPTIMER_ID		12

static u32 sys_timer_reserved;

/* Clockevent code */

static struct omap_dm_timer clkev;
static struct clock_event_device clockevent_gpt;

static irqreturn_t omap2_gp_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &clockevent_gpt;

	__omap_dm_timer_write_status(&clkev, OMAP_TIMER_INT_OVERFLOW);

	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct irqaction omap2_gp_timer_irq = {
	.name		= "gp timer",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= omap2_gp_timer_interrupt,
};

static int omap2_gp_timer_set_next_event(unsigned long cycles,
					 struct clock_event_device *evt)
{
	__omap_dm_timer_load_start(&clkev, OMAP_TIMER_CTRL_ST,
						0xffffffff - cycles, 1);

	return 0;
}

static void omap2_gp_timer_set_mode(enum clock_event_mode mode,
				    struct clock_event_device *evt)
{
	u32 period;

	__omap_dm_timer_stop(&clkev, 1, clkev.rate);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		period = clkev.rate / HZ;
		period -= 1;
		/* Looks like we need to first set the load value separately */
		__omap_dm_timer_write(&clkev, OMAP_TIMER_LOAD_REG,
					0xffffffff - period, 1);
		__omap_dm_timer_load_start(&clkev,
					OMAP_TIMER_CTRL_AR | OMAP_TIMER_CTRL_ST,
						0xffffffff - period, 1);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static struct clock_event_device clockevent_gpt = {
	.name		= "gp timer",
	.features       = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.shift		= 32,
	.set_next_event	= omap2_gp_timer_set_next_event,
	.set_mode	= omap2_gp_timer_set_mode,
};

static int __init omap_dm_timer_init_one(struct omap_dm_timer *timer,
						int gptimer_id,
						const char *fck_source)
{
	char name[10]; /* 10 = sizeof("gptXX_Xck0") */
	struct omap_hwmod *oh;
	size_t size;
	int res = 0;

	sprintf(name, "timer%d", gptimer_id);
	omap_hwmod_setup_one(name);
	oh = omap_hwmod_lookup(name);
	if (!oh)
		return -ENODEV;

	timer->irq = oh->mpu_irqs[0].irq;
	timer->phys_base = oh->slaves[0]->addr->pa_start;
	size = oh->slaves[0]->addr->pa_end - timer->phys_base;

	/* Static mapping, never released */
	timer->io_base = ioremap(timer->phys_base, size);
	if (!timer->io_base)
		return -ENXIO;

	/* After the dmtimer is using hwmod these clocks won't be needed */
	sprintf(name, "gpt%d_fck", gptimer_id);
	timer->fclk = clk_get(NULL, name);
	if (IS_ERR(timer->fclk))
		return -ENODEV;

	sprintf(name, "gpt%d_ick", gptimer_id);
	timer->iclk = clk_get(NULL, name);
	if (IS_ERR(timer->iclk)) {
		clk_put(timer->fclk);
		return -ENODEV;
	}

	omap_hwmod_enable(oh);

	sys_timer_reserved |= (1 << (gptimer_id - 1));

	if (gptimer_id != 12) {
		struct clk *src;

		src = clk_get(NULL, fck_source);
		if (IS_ERR(src)) {
			res = -EINVAL;
		} else {
			res = __omap_dm_timer_set_source(timer->fclk, src);
			if (IS_ERR_VALUE(res))
				pr_warning("%s: timer%i cannot set source\n",
						__func__, gptimer_id);
			clk_put(src);
		}
	}
	__omap_dm_timer_init_regs(timer);
	__omap_dm_timer_reset(timer, 1, 1);
	timer->posted = 1;

	timer->rate = clk_get_rate(timer->fclk);

	timer->reserved = 1;

	return res;
}

static void __init omap2_gp_clockevent_init(int gptimer_id,
						const char *fck_source)
{
	int res;

	res = omap_dm_timer_init_one(&clkev, gptimer_id, fck_source);
	BUG_ON(res);

	omap2_gp_timer_irq.dev_id = (void *)&clkev;
	setup_irq(clkev.irq, &omap2_gp_timer_irq);

	__omap_dm_timer_int_enable(&clkev, OMAP_TIMER_INT_OVERFLOW);

	clockevent_gpt.mult = div_sc(clkev.rate, NSEC_PER_SEC,
				     clockevent_gpt.shift);
	clockevent_gpt.max_delta_ns =
		clockevent_delta2ns(0xffffffff, &clockevent_gpt);
	clockevent_gpt.min_delta_ns =
		clockevent_delta2ns(3, &clockevent_gpt);
		/* Timer internal resynch latency. */

	clockevent_gpt.cpumask = cpumask_of(0);
	clockevents_register_device(&clockevent_gpt);

	pr_info("OMAP clockevent source: GPTIMER%d at %lu Hz\n",
		gptimer_id, clkev.rate);
}

/* Clocksource code */

#ifdef CONFIG_OMAP_32K_TIMER
/*
 * When 32k-timer is enabled, don't use GPTimer for clocksource
 * instead, just leave default clocksource which uses the 32k
 * sync counter.  See clocksource setup in plat-omap/counter_32k.c
 */

static void __init omap2_gp_clocksource_init(int unused, const char *dummy)
{
	omap_init_clocksource_32k();
}

#else

static struct omap_dm_timer clksrc;

/*
 * clocksource
 */
static cycle_t clocksource_read_cycles(struct clocksource *cs)
{
	return (cycle_t)__omap_dm_timer_read_counter(&clksrc, 1);
}

static struct clocksource clocksource_gpt = {
	.name		= "gp timer",
	.rating		= 300,
	.read		= clocksource_read_cycles,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static u32 notrace dmtimer_read_sched_clock(void)
{
	if (clksrc.reserved)
		return __omap_dm_timer_read_counter(&clksrc, 1);

	return 0;
}

/* Setup free-running counter for clocksource */
static void __init omap2_gp_clocksource_init(int gptimer_id,
						const char *fck_source)
{
	int res;

	res = omap_dm_timer_init_one(&clksrc, gptimer_id, fck_source);
	BUG_ON(res);

	pr_info("OMAP clocksource: GPTIMER%d at %lu Hz\n",
		gptimer_id, clksrc.rate);

	__omap_dm_timer_load_start(&clksrc,
			OMAP_TIMER_CTRL_ST | OMAP_TIMER_CTRL_AR, 0, 1);
	setup_sched_clock(dmtimer_read_sched_clock, 32, clksrc.rate);

	if (clocksource_register_hz(&clocksource_gpt, clksrc.rate))
		pr_err("Could not register clocksource %s\n",
			clocksource_gpt.name);
}
#endif

#define OMAP_SYS_TIMER_INIT(name, clkev_nr, clkev_src,			\
				clksrc_nr, clksrc_src)			\
static void __init omap##name##_timer_init(void)			\
{									\
	omap2_gp_clockevent_init((clkev_nr), clkev_src);		\
	omap2_gp_clocksource_init((clksrc_nr), clksrc_src);		\
}

#define OMAP_SYS_TIMER(name)						\
struct sys_timer omap##name##_timer = {					\
	.init	= omap##name##_timer_init,				\
};

#ifdef CONFIG_ARCH_OMAP2
OMAP_SYS_TIMER_INIT(2, 1, OMAP2_CLKEV_SOURCE, 2, OMAP2_MPU_SOURCE)
OMAP_SYS_TIMER(2)
#endif

#ifdef CONFIG_ARCH_OMAP3
OMAP_SYS_TIMER_INIT(3, 1, OMAP3_CLKEV_SOURCE, 2, OMAP3_MPU_SOURCE)
OMAP_SYS_TIMER(3)
OMAP_SYS_TIMER_INIT(3_secure, OMAP3_SECURE_TIMER, OMAP3_CLKEV_SOURCE,
			2, OMAP3_MPU_SOURCE)
OMAP_SYS_TIMER(3_secure)
#endif

#ifdef CONFIG_ARCH_OMAP4
#ifdef CONFIG_LOCAL_TIMERS
static DEFINE_TWD_LOCAL_TIMER(twd_local_timer,
			      OMAP44XX_LOCAL_TWD_BASE,
			      OMAP44XX_IRQ_LOCALTIMER);
#endif

static void __init omap4_timer_init(void)
{
	omap2_gp_clockevent_init(1, OMAP4_CLKEV_SOURCE);
	omap2_gp_clocksource_init(2, OMAP4_MPU_SOURCE);
#ifdef CONFIG_LOCAL_TIMERS
	/* Local timers are not supprted on OMAP4430 ES1.0 */
	if (omap_rev() != OMAP4430_REV_ES1_0) {
		int err;

		err = twd_local_timer_register(&twd_local_timer);
		if (err)
			pr_err("twd_local_timer_register failed %d\n", err);
	}
#endif
}
OMAP_SYS_TIMER(4)
#endif

/**
 * omap2_dm_timer_set_src - change the timer input clock source
 * @pdev:	timer platform device pointer
 * @source:	array index of parent clock source
 */
static int omap2_dm_timer_set_src(struct platform_device *pdev, int source)
{
	int ret;
	struct dmtimer_platform_data *pdata = pdev->dev.platform_data;
	struct clk *fclk, *parent;
	char *parent_name = NULL;

	fclk = clk_get(&pdev->dev, "fck");
	if (IS_ERR_OR_NULL(fclk)) {
		dev_err(&pdev->dev, "%s: %d: clk_get() FAILED\n",
				__func__, __LINE__);
		return -EINVAL;
	}

	switch (source) {
	case OMAP_TIMER_SRC_SYS_CLK:
		parent_name = "sys_ck";
		break;

	case OMAP_TIMER_SRC_32_KHZ:
		parent_name = "32k_ck";
		break;

	case OMAP_TIMER_SRC_EXT_CLK:
		if (pdata->timer_ip_version == OMAP_TIMER_IP_VERSION_1) {
			parent_name = "alt_ck";
			break;
		}
		dev_err(&pdev->dev, "%s: %d: invalid clk src.\n",
			__func__, __LINE__);
		clk_put(fclk);
		return -EINVAL;
	}

	parent = clk_get(&pdev->dev, parent_name);
	if (IS_ERR_OR_NULL(parent)) {
		dev_err(&pdev->dev, "%s: %d: clk_get() %s FAILED\n",
			__func__, __LINE__, parent_name);
		clk_put(fclk);
		return -EINVAL;
	}

	ret = clk_set_parent(fclk, parent);
	if (IS_ERR_VALUE(ret)) {
		dev_err(&pdev->dev, "%s: clk_set_parent() to %s FAILED\n",
			__func__, parent_name);
		ret = -EINVAL;
	}

	clk_put(parent);
	clk_put(fclk);

	return ret;
}

/**
 * omap_timer_init - build and register timer device with an
 * associated timer hwmod
 * @oh:	timer hwmod pointer to be used to build timer device
 * @user:	parameter that can be passed from calling hwmod API
 *
 * Called by omap_hwmod_for_each_by_class to register each of the timer
 * devices present in the system. The number of timer devices is known
 * by parsing through the hwmod database for a given class name. At the
 * end of function call memory is allocated for timer device and it is
 * registered to the framework ready to be proved by the driver.
 */
static int __init omap_timer_init(struct omap_hwmod *oh, void *unused)
{
	int id;
	int ret = 0;
	char *name = "omap_timer";
	struct dmtimer_platform_data *pdata;
	struct platform_device *pdev;
	struct omap_timer_capability_dev_attr *timer_dev_attr;
	struct powerdomain *pwrdm;

	pr_debug("%s: %s\n", __func__, oh->name);

	/* on secure device, do not register secure timer */
	timer_dev_attr = oh->dev_attr;
	if (omap_type() != OMAP2_DEVICE_TYPE_GP && timer_dev_attr)
		if (timer_dev_attr->timer_capability == OMAP_TIMER_SECURE)
			return ret;

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		pr_err("%s: No memory for [%s]\n", __func__, oh->name);
		return -ENOMEM;
	}

	/*
	 * Extract the IDs from name field in hwmod database
	 * and use the same for constructing ids' for the
	 * timer devices. In a way, we are avoiding usage of
	 * static variable witin the function to do the same.
	 * CAUTION: We have to be careful and make sure the
	 * name in hwmod database does not change in which case
	 * we might either make corresponding change here or
	 * switch back static variable mechanism.
	 */
	sscanf(oh->name, "timer%2d", &id);

	pdata->set_timer_src = omap2_dm_timer_set_src;
	pdata->timer_ip_version = oh->class->rev;

	/* Mark clocksource and clockevent timers as reserved */
	if ((sys_timer_reserved >> (id - 1)) & 0x1)
		pdata->reserved = 1;

	pwrdm = omap_hwmod_get_pwrdm(oh);
	pdata->loses_context = pwrdm_can_ever_lose_context(pwrdm);
#ifdef CONFIG_PM
	pdata->get_context_loss_count = omap_pm_get_dev_context_loss_count;
#endif
	pdev = omap_device_build(name, id, oh, pdata, sizeof(*pdata),
				 NULL, 0, 0);

	if (IS_ERR(pdev)) {
		pr_err("%s: Can't build omap_device for %s: %s.\n",
			__func__, name, oh->name);
		ret = -EINVAL;
	}

	kfree(pdata);

	return ret;
}

/**
 * omap2_dm_timer_init - top level regular device initialization
 *
 * Uses dedicated hwmod api to parse through hwmod database for
 * given class name and then build and register the timer device.
 */
static int __init omap2_dm_timer_init(void)
{
	int ret;

	ret = omap_hwmod_for_each_by_class("timer", omap_timer_init, NULL);
	if (unlikely(ret)) {
		pr_err("%s: device registration failed.\n", __func__);
		return -EINVAL;
	}

	return 0;
}
arch_initcall(omap2_dm_timer_init);
