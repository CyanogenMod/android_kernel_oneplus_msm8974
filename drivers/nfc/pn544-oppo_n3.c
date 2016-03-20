/***********************************************************
** Copyright (C), 2008-2012, OPPO Mobile Comm Corp., Ltd
** CONFIG_MACH_OPPO
** File: - pn544.c
* Description: Source file for nfc driver.
** Version: 1.0
** Date : 2013/10/15
** Author: yuyi@Dep.Group.Module
**
************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/nfc/pn544.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>

#define MAX_BUFFER_SIZE	512

#define APQ_NFC_VEN_GPIO 14
#define APQ_NFC_FIRM_GPIO 13
#define APQ_NFC_IRQ_GPIO 59

#define PN544_VEN	GPIO_CFG(APQ_NFC_VEN_GPIO, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA)
#define PN544_FIRM	GPIO_CFG(APQ_NFC_FIRM_GPIO, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA)
#define PN544_IRQ	GPIO_CFG(APQ_NFC_IRQ_GPIO, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA)

struct pn544_i2c_platform_data nfc_pdata = {
	.irq_gpio = APQ_NFC_IRQ_GPIO,
	.ven_gpio = APQ_NFC_VEN_GPIO,
	.firm_gpio = APQ_NFC_FIRM_GPIO,
};

struct pn544_dev {
	wait_queue_head_t read_wq;
	struct mutex read_mutex;
	struct i2c_client *client;
	struct miscdevice pn544_device;
	unsigned int ven_gpio;
	unsigned int firm_gpio;
	unsigned int irq_gpio;
	bool irq_enabled;
	spinlock_t irq_enabled_lock;
};

static void pn544_disable_irq(struct pn544_dev *pn544_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&pn544_dev->irq_enabled_lock, flags);
	if (pn544_dev->irq_enabled) {
		disable_irq_nosync(pn544_dev->client->irq);
		pn544_dev->irq_enabled = false;
	}
	spin_unlock_irqrestore(&pn544_dev->irq_enabled_lock, flags);
}

static irqreturn_t pn544_dev_irq_handler(int irq, void *dev_id)
{
	struct pn544_dev *pn544_dev = dev_id;

	if (!gpio_get_value(pn544_dev->irq_gpio)) {
		return IRQ_HANDLED;
	}

	pn544_disable_irq(pn544_dev);

	/* Wake up waiting readers */
	wake_up(&pn544_dev->read_wq);

	return IRQ_HANDLED;
}

static ssize_t pn544_dev_read(struct file *filp, char __user * buf,
			      size_t count, loff_t * offset)
{
	struct pn544_dev *pn544_dev = filp->private_data;
	char tmp[MAX_BUFFER_SIZE];
	int ret;
	if (count > MAX_BUFFER_SIZE)
		count = MAX_BUFFER_SIZE;

	mutex_lock(&pn544_dev->read_mutex);

	if (!gpio_get_value(pn544_dev->irq_gpio)) {
		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto fail;
		}

		pn544_dev->irq_enabled = true;
		enable_irq(pn544_dev->client->irq);
		ret = wait_event_interruptible(pn544_dev->read_wq,
					       gpio_get_value(pn544_dev->
							      irq_gpio));

		pn544_disable_irq(pn544_dev);

		if (ret)
			goto fail;
	}

	/* Read data */
	ret = i2c_master_recv(pn544_dev->client, tmp, count);
	mutex_unlock(&pn544_dev->read_mutex);

	/* pn544 seems to be slow in handling I2C read requests
	 * so add 1ms delay after recv operation */
	if (ret < 0) {
		pr_err("%s: i2c_master_recv returned %d\n", __func__, ret);
		return ret;
	}
	if (ret > count) {
		pr_err("%s: received too many bytes from i2c (%d)\n", __func__,
		       ret);
		return -EIO;
	}
	if (copy_to_user(buf, tmp, ret)) {
		pr_warning("%s : failed to copy to user space\n", __func__);
		return -EFAULT;
	}

	return ret;

fail:
	mutex_unlock(&pn544_dev->read_mutex);
	return ret;
}

static ssize_t pn544_dev_write(struct file *filp, const char __user * buf,
			       size_t count, loff_t * offset)
{
	struct pn544_dev *pn544_dev;
	char tmp[MAX_BUFFER_SIZE];
	int ret;

	pn544_dev = filp->private_data;

	if (count > MAX_BUFFER_SIZE) {
		count = MAX_BUFFER_SIZE;
	}
	if (copy_from_user(tmp, buf, count)) {
		pr_err("%s : failed to copy from user space\n", __func__);
		return -EFAULT;
	}

	/* Write data */
	ret = i2c_master_send(pn544_dev->client, tmp, count);
	if (ret != count) {
		pr_err("%s : i2c_master_send returned %d\n", __func__, ret);
		ret = -EIO;
	}

	return ret;
}

static int pn544_dev_open(struct inode *inode, struct file *filp)
{
	struct pn544_dev *pn544_dev = container_of(filp->private_data,
						   struct pn544_dev,
						   pn544_device);

	filp->private_data = pn544_dev;

	pr_err("%s : %d,%d\n", __func__, imajor(inode), iminor(inode));

	return 0;
}

static long pn544_dev_ioctl(struct file *filp, unsigned int cmd,
			    unsigned long arg)
{
	struct pn544_dev *pn544_dev = filp->private_data;
	int ret = 0;

	switch (cmd) {
	case PN544_SET_PWR:
		if (arg == 2) {
			/* power on with firmware download (requires hw reset)
			 */
			printk("%s power on with firmware\n", __func__);
			ret = disable_irq_wake(pn544_dev->client->irq);
			if (ret < 0) {
				pr_err("%s : power on with firmware "
				       "disable_irq_wake ret = %d\n",
				       __func__, ret);
			}
			gpio_set_value(pn544_dev->ven_gpio, 1);
			gpio_set_value(pn544_dev->firm_gpio, 1);
			msleep(10);
			gpio_set_value(pn544_dev->ven_gpio, 0);
			msleep(50);
			gpio_set_value(pn544_dev->ven_gpio, 1);
			msleep(10);
		} else if (arg == 1) {
			/* power on */
			ret = enable_irq_wake(pn544_dev->client->irq);
			if (ret < 0) {
				pr_err("%s : power on "
				       "enable_irq_wake ret = %d\n",
				       __func__, ret);
			}
			printk("%s power on\n", __func__);
			gpio_set_value(pn544_dev->firm_gpio, 0);
			gpio_set_value(pn544_dev->ven_gpio, 1);
			msleep(10);
		} else if (arg == 0) {
			/* power off */
			ret = disable_irq_wake(pn544_dev->client->irq);
			if (ret < 0) {
				printk("%s : power off "
				       "disable_irq_wake ret = %d\n",
				       __func__, ret);
			}
			printk("%s power off\n", __func__);
			gpio_set_value(pn544_dev->firm_gpio, 0);
			gpio_set_value(pn544_dev->ven_gpio, 0);
			msleep(50);
		} else {
			pr_err("%s bad arg %lu\n", __func__, arg);
			return -EINVAL;
		}
		break;
	default:
		pr_err("%s bad ioctl %u\n", __func__, cmd);
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations pn544_dev_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = pn544_dev_read,
	.write = pn544_dev_write,
	.open = pn544_dev_open,
	.unlocked_ioctl = pn544_dev_ioctl,
};

static int pn544_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	int ret;
	struct pn544_i2c_platform_data *platform_data;
	struct pn544_dev *pn544_dev = NULL;
	struct clk *p65T_clk = NULL;

	client->dev.platform_data = &nfc_pdata;

	platform_data = client->dev.platform_data;
	if (platform_data == NULL) {
		pr_err("%s : nfc probe fail\n", __func__);
		return -ENODEV;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s : need I2C_FUNC_I2C\n", __func__);
		return -ENODEV;
	}
	//IRQ
	ret = gpio_request(platform_data->irq_gpio, "nfc_int");
	if (ret) {
		pr_err("gpio_nfc_int request error\n");
		return -ENODEV;
	}
	//VEN
	ret = gpio_request(platform_data->ven_gpio, "nfc_ven");
	if (ret) {
		pr_err("gpio_nfc_ven request error\n");
		return -ENODEV;
	}
	//FIRM
	ret = gpio_request(platform_data->firm_gpio, "nfc_firm");
	if (ret) {
		pr_err("gpio_nfc_firm request error\n");
		return -ENODEV;
	}

	p65T_clk = clk_get(&client->dev, "cxo_out_d1");
	if (p65T_clk == NULL) {
		pr_err("pn65T clk error");
		goto err_exit;
	}

	ret = clk_prepare_enable(p65T_clk);
	if (ret) {
		pr_err("pn65T clk_prepare_disable ret = %d", ret);
		p65T_clk = NULL;
		clk_disable_unprepare(p65T_clk);
	}

	pn544_dev = kzalloc(sizeof(*pn544_dev), GFP_KERNEL);
	if (pn544_dev == NULL) {
		dev_err(&client->dev,
			"failed to allocate memory for module data\n");
		ret = -ENOMEM;
		goto err_exit;
	}

	pn544_dev->irq_gpio = platform_data->irq_gpio;
	pn544_dev->ven_gpio = platform_data->ven_gpio;
	pn544_dev->firm_gpio = platform_data->firm_gpio;
	pn544_dev->client = client;

	ret = gpio_direction_input(pn544_dev->irq_gpio);
	if (ret < 0) {
		pr_err("%s : not able to set irq_gpio as input\n", __func__);
		goto err_exit;
	}
	if (platform_data->firm_gpio) {
		ret = gpio_direction_output(pn544_dev->firm_gpio, 0);
		if (ret < 0) {
			pr_err("%s : not able to set firm_gpio as output\n",
			       __func__);
			goto err_exit;
		}
	}
	ret = gpio_direction_output(pn544_dev->ven_gpio, 1);
	if (ret < 0) {
		pr_err("%s : not able to set ven_gpio as output\n", __func__);
		goto err_exit;
	}

	/* init mutex and queues */
	init_waitqueue_head(&pn544_dev->read_wq);
	mutex_init(&pn544_dev->read_mutex);
	spin_lock_init(&pn544_dev->irq_enabled_lock);

	pn544_dev->pn544_device.minor = MISC_DYNAMIC_MINOR;
	pn544_dev->pn544_device.name = "pn544";
	pn544_dev->pn544_device.fops = &pn544_dev_fops;

	ret = misc_register(&pn544_dev->pn544_device);
	if (ret) {
		pr_err("%s : misc_register failed\n", __FILE__);
		goto err_misc_register;
	}

	/* request irq.  the irq is set whenever the chip has data available
	 * for reading.  it is cleared when all data has been read.
	 */
	pr_info("%s : requesting IRQ %d\n", __func__, client->irq);
	pn544_dev->irq_enabled = true;

	ret = request_irq(client->irq, pn544_dev_irq_handler, IRQF_TRIGGER_HIGH,
			  client->name, pn544_dev);
	if (ret) {
		dev_err(&client->dev, "request_irq failed\n");
		goto err_request_irq_failed;
	}

	pn544_disable_irq(pn544_dev);
	i2c_set_clientdata(client, pn544_dev);

	gpio_set_value(pn544_dev->ven_gpio, 1);
	msleep(10);
	gpio_set_value(pn544_dev->ven_gpio, 0);

	return 0;

err_request_irq_failed:
	misc_deregister(&pn544_dev->pn544_device);
err_misc_register:
	mutex_destroy(&pn544_dev->read_mutex);
	kfree(pn544_dev);
err_exit:
	gpio_free(pn544_dev->irq_gpio);
	gpio_free(pn544_dev->ven_gpio);
	gpio_free(pn544_dev->firm_gpio);
	return ret;
}

static int pn544_remove(struct i2c_client *client)
{
	struct pn544_dev *pn544_dev;

	pn544_dev = i2c_get_clientdata(client);
	free_irq(client->irq, pn544_dev);
	misc_deregister(&pn544_dev->pn544_device);
	mutex_destroy(&pn544_dev->read_mutex);
	gpio_free(pn544_dev->irq_gpio);
	gpio_free(pn544_dev->ven_gpio);
	gpio_free(pn544_dev->firm_gpio);
	kfree(pn544_dev);

	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id pn544_of_match_table[] = {
	{.compatible = "pn544,nxp-nfc",},
	{},
};
#else
#define pn544_of_match_table NULL
#endif

static const struct i2c_device_id pn544_id[] = {
	{"pn544", 0},
	{}
};

static struct i2c_driver pn544_driver = {
	.id_table = pn544_id,
	.probe = pn544_probe,
	.remove = pn544_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "pn544",
		.of_match_table = pn544_of_match_table,
	},
};

module_i2c_driver(pn544_driver);

MODULE_AUTHOR("Sylvain Fonteneau");
MODULE_DESCRIPTION("NFC PN544 driver");
MODULE_LICENSE("GPL");
