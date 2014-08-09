/*
 * driver.h -- SoC Regulator driver support.
 *
 * Copyright (C) 2007, 2008 Wolfson Microelectronics PLC.
 *
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Regulator Driver Interface.
 */

#ifndef __LINUX_REGULATOR_DRIVER_H_
#define __LINUX_REGULATOR_DRIVER_H_

#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/regulator/consumer.h>

struct regulator_dev;
struct regulator_init_data;

enum regulator_status {
	REGULATOR_STATUS_OFF,
	REGULATOR_STATUS_ON,
	REGULATOR_STATUS_ERROR,
	/* fast/normal/idle/standby are flavors of "on" */
	REGULATOR_STATUS_FAST,
	REGULATOR_STATUS_NORMAL,
	REGULATOR_STATUS_IDLE,
	REGULATOR_STATUS_STANDBY,
};

/**
 * struct regulator_ops - regulator operations.
 *
 * @enable: Configure the regulator as enabled.
 * @disable: Configure the regulator as disabled.
 * @is_enabled: Return 1 if the regulator is enabled, 0 if not.
 *		May also return negative errno.
 *
 * @set_voltage: Set the voltage for the regulator within the range specified.
 *               The driver should select the voltage closest to min_uV.
 * @set_voltage_sel: Set the voltage for the regulator using the specified
 *                   selector.
 * @get_voltage: Return the currently configured voltage for the regulator.
 * @get_voltage_sel: Return the currently configured voltage selector for the
 *                   regulator.
 * @list_voltage: Return one of the supported voltages, in microvolts; zero
 *	if the selector indicates a voltage that is unusable on this system;
 *	or negative errno.  Selectors range from zero to one less than
 *	regulator_desc.n_voltages.  Voltages may be reported in any order.
 *
 * @set_current_limit: Configure a limit for a current-limited regulator.
 * @get_current_limit: Get the configured limit for a current-limited regulator.
 *
 * @set_mode: Set the configured operating mode for the regulator.
 * @get_mode: Get the configured operating mode for the regulator.
 * @get_status: Return actual (not as-configured) status of regulator, as a
 *	REGULATOR_STATUS value (or negative errno)
 * @get_optimum_mode: Get the most efficient operating mode for the regulator
 *                    when running with the specified parameters.
 *
 * @enable_time: Time taken for the regulator voltage output voltage to
 *               stabilise after being enabled, in microseconds.
 * @set_voltage_time_sel: Time taken for the regulator voltage output voltage
 *               to stabilise after being set to a new value, in microseconds.
 *               The function provides the from and to voltage selector, the
 *               function should return the worst case.
 *
 * @set_suspend_voltage: Set the voltage for the regulator when the system
 *                       is suspended.
 * @set_suspend_enable: Mark the regulator as enabled when the system is
 *                      suspended.
 * @set_suspend_disable: Mark the regulator as disabled when the system is
 *                       suspended.
 * @set_suspend_mode: Set the operating mode for the regulator when the
 *                    system is suspended.
 *
 * This struct describes regulator operations which can be implemented by
 * regulator chip drivers.
 */
struct regulator_ops {

	/* enumerate supported voltages */
	int (*list_voltage) (struct regulator_dev *, unsigned selector);

	/* get/set regulator voltage */
	int (*set_voltage) (struct regulator_dev *, int min_uV, int max_uV,
			    unsigned *selector);
	int (*set_voltage_sel) (struct regulator_dev *, unsigned selector);
	int (*get_voltage) (struct regulator_dev *);
	int (*get_voltage_sel) (struct regulator_dev *);

	/* get/set regulator current  */
	int (*set_current_limit) (struct regulator_dev *,
				 int min_uA, int max_uA);
	int (*get_current_limit) (struct regulator_dev *);

	/* enable/disable regulator */
	int (*enable) (struct regulator_dev *);
	int (*disable) (struct regulator_dev *);
	int (*is_enabled) (struct regulator_dev *);

	/* get/set regulator operating mode (defined in consumer.h) */
	int (*set_mode) (struct regulator_dev *, unsigned int mode);
	unsigned int (*get_mode) (struct regulator_dev *);

	/* Time taken to enable or set voltage on the regulator */
	int (*enable_time) (struct regulator_dev *);
	int (*set_voltage_time_sel) (struct regulator_dev *,
				     unsigned int old_selector,
				     unsigned int new_selector);

	/* report regulator status ... most other accessors report
	 * control inputs, this reports results of combining inputs
	 * from Linux (and other sources) with the actual load.
	 * returns REGULATOR_STATUS_* or negative errno.
	 */
	int (*get_status)(struct regulator_dev *);

	/* get most efficient regulator operating mode for load */
	unsigned int (*get_optimum_mode) (struct regulator_dev *, int input_uV,
					  int output_uV, int load_uA);

	/* the operations below are for configuration of regulator state when
	 * its parent PMIC enters a global STANDBY/HIBERNATE state */

	/* set regulator suspend voltage */
	int (*set_suspend_voltage) (struct regulator_dev *, int uV);

	/* enable/disable regulator in suspend state */
	int (*set_suspend_enable) (struct regulator_dev *);
	int (*set_suspend_disable) (struct regulator_dev *);

	/* set regulator suspend operating mode (defined in consumer.h) */
	int (*set_suspend_mode) (struct regulator_dev *, unsigned int mode);
};

/*
 * Regulators can either control voltage or current.
 */
enum regulator_type {
	REGULATOR_VOLTAGE,
	REGULATOR_CURRENT,
};

/**
 * struct regulator_desc - Regulator descriptor
 *
 * Each regulator registered with the core is described with a structure of
 * this type.
 *
 * @name: Identifying name for the regulator.
 * @supply_name: Identifying the regulator supply
 * @id: Numerical identifier for the regulator.
 * @n_voltages: Number of selectors available for ops.list_voltage().
 * @ops: Regulator operations table.
 * @irq: Interrupt number for the regulator.
 * @type: Indicates if the regulator is a voltage or current regulator.
 * @owner: Module providing the regulator, used for refcounting.
 */
struct regulator_desc {
	const char *name;
	const char *supply_name;
	int id;
	unsigned n_voltages;
	struct regulator_ops *ops;
	int irq;
	enum regulator_type type;
	struct module *owner;
};

/*
 * struct regulator_dev
 *
 * Voltage / Current regulator class device. One for each
 * regulator.
 *
 * This should *not* be used directly by anything except the regulator
 * core and notification injection (which should take the mutex and do
 * no other direct access).
 */
struct regulator_dev {
	struct regulator_desc *desc;
	int exclusive;
	u32 use_count;
	u32 open_count;

	/* lists we belong to */
	struct list_head list; /* list of all regulators */

	/* lists we own */
	struct list_head consumer_list; /* consumers we supply */

	struct blocking_notifier_head notifier;
	struct mutex mutex; /* consumer lock */
	struct module *owner;
	struct device dev;
	struct regulation_constraints *constraints;
	struct regulator *supply;	/* for tree */

	struct delayed_work disable_work;
	int deferred_disables;

	void *reg_data;		/* regulator_dev data */

	struct dentry *debugfs;
};

struct regulator_dev *regulator_register(struct regulator_desc *regulator_desc,
	struct device *dev, const struct regulator_init_data *init_data,
	void *driver_data, struct device_node *of_node);
void regulator_unregister(struct regulator_dev *rdev);

int regulator_notifier_call_chain(struct regulator_dev *rdev,
				  unsigned long event, void *data);

void *rdev_get_drvdata(struct regulator_dev *rdev);
struct device *rdev_get_dev(struct regulator_dev *rdev);
int rdev_get_id(struct regulator_dev *rdev);

int regulator_mode_to_status(unsigned int);

void *regulator_get_init_drvdata(struct regulator_init_data *reg_init_data);

#endif
