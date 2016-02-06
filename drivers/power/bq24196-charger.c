
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/bitops.h>
#include <linux/kthread.h>
#include <linux/qpnp-charger.h>

//bq24196 reg
#define INPUT_SOURCE_CTRL 			0x00
#define POWER_ON_CONF				0x01
#define CHARGE_CURRENT_CTRL 		0x02
#define PRECHARGE_TERMCURR_CTRL 	0x03
#define CHARGE_VOL_CTRL				0x04
#define CHARGE_TERM_TIMER_CTRL		0x05
#define THERMAL_REGULATION_CTRL		0x06
#define MISC_OPERATE_CTRL			0x07
#define SYS_STS						0x08
#define FAULT_REG					0x09
#define	VENDOR_PART_REV_CTRL		0x0A

//set charge parameter limit
#define BQ24196_CHG_IBATMAX_MIN		512
#define BQ24196_CHG_IBATMAX_MAX		2496
#define BQ24196_CHG_IBATMAX_HRM		2048
#define BQ24196_TERM_CURR_MIN		128
#define BQ24196_TERM_CURR_MAX		2048
#define BQ24196_CHG_IUSBMAX_MIN		100
#define BQ24196_CHG_IUSBMAX_MAX		3000

static DEFINE_IDR(bq24196_charger_id);

struct bq24196_device_info;
struct bq24196_access_methods {
	int (*read)(struct bq24196_device_info *di,u8 reg,u8 length,char *buf);
	int (*write)(struct bq24196_device_info *di,u8 reg,u8 length,char *buf);
};
struct bq24196_device_info {
	struct device			*dev;
	int				id;
	struct bq24196_access_methods	*bus;
	struct i2c_client		*client;
	struct task_struct		*feedwdt_task;
	struct mutex			i2c_lock;
	atomic_t suspended; //sjc1118

	/* 300ms delay is needed after bq27541 is powered up
	 * and before any successful I2C transaction
	 */

};

struct bq24196_device_info *bq24196_di;
struct i2c_client *bq24196_client;

/* Ensure I2C bus is active for OTG */
static bool suspended = false;
static DECLARE_COMPLETION(resume_done);
static DEFINE_SPINLOCK(resume_lock);

static int bq24196_read_i2c(struct bq24196_device_info *di,u8 reg,u8 length,char *buf)
{
	struct i2c_client *client = di->client;
	int retval;

	if (atomic_read(&di->suspended) == 1) //sjc1118
		return -1;

	mutex_lock(&bq24196_di->i2c_lock);
	retval = i2c_smbus_read_i2c_block_data(client,reg,length,&buf[0]);
	mutex_unlock(&bq24196_di->i2c_lock);
	if( retval < 0){
		pr_info("bq24196 read i2c error\n");
	}
	return retval;
}
static int bq24196_write_i2c(struct bq24196_device_info *di,u8 reg,u8 length,char *buf)
{
	struct i2c_client *client = di->client;
	int retval;

	if (atomic_read(&di->suspended) == 1) //sjc1118
		return -1;
	
	mutex_lock(&bq24196_di->i2c_lock);
	retval = i2c_smbus_write_i2c_block_data(client,reg,length,&buf[0]);
	mutex_unlock(&bq24196_di->i2c_lock);
	if( retval < 0){
		pr_info("bq24196 write i2c error\n");
	}
	return retval;
}
static int bq24196_read(struct bq24196_device_info *di,u8 reg,u8 length,char *buf)
{
	return di->bus->read(di,reg,length,buf);
}
static int bq24196_write(struct bq24196_device_info *di,u8 reg,u8 length,char *buf)
{
	return di->bus->write(di,reg,length,buf);
}

static int
bq24196_chg_masked_write(struct bq24196_device_info *di, u8 reg,
						u8 mask, u8 value, int length)
{
	int rc;
	char buf[1]={0x0};
	
	rc = bq24196_read(di,reg,length,&buf[0]);
	if ( rc < 0 ){
		pr_err("bq24196 read i2c failed,reg_addr=0x%x,rc=%d\n",reg,rc);
		return rc;
	}
	buf[0] &= ~ mask;
	buf[0]  = buf[0] | value;
	rc = bq24196_write(di,reg,length,&buf[0]);
	if( rc < 0 ){
		pr_err("bq24196 write i2c failed,reg_addr=0x%x,rc=%d\n",reg,rc);
		return rc;
	}

	return 0;
}

#define BQ24196_IBATMAX_BITS	0xFF
static int 
bq24196_ibatmax_set(struct bq24196_device_info *di, int chg_current)
{
	u8 value = 0;
	pr_info("%s chg_current:%d\n",__func__,chg_current);
	if(chg_current == 200) {
		value = (1024 - BQ24196_CHG_IBATMAX_MIN)/64;
		value <<= 2;
		value |= 1;
		return bq24196_chg_masked_write(di,CHARGE_CURRENT_CTRL,BQ24196_IBATMAX_BITS,value,1);
	} 
#ifdef CONFIG_MACH_MSM8974_14001
       /* yangfangbiao@oneplus.cn, 2015/03/15	set charge current 300ma  */
	else if(chg_current == 300) {
		value = (1536 - BQ24196_CHG_IBATMAX_MIN)/64;
		value <<= 2;
		value |= 1;
		return bq24196_chg_masked_write(di,CHARGE_CURRENT_CTRL,BQ24196_IBATMAX_BITS,value,1);
	} 
#endif /*CONFIG_MACH_MSM8974_14001*/
	else if(chg_current == 500) {
		value = (2496 - BQ24196_CHG_IBATMAX_MIN)/64;
		value <<= 2;
		value |= 1;
		return bq24196_chg_masked_write(di,CHARGE_CURRENT_CTRL,BQ24196_IBATMAX_BITS,value,1);
	} else {
		if ( (chg_current < BQ24196_CHG_IBATMAX_MIN)
				|| (chg_current > BQ24196_CHG_IBATMAX_MAX) ) {
			chg_current = BQ24196_CHG_IBATMAX_MIN;
			pr_err("bad ibatmA=%d,default to 512mA\n", chg_current);
		}

#ifdef CONFIG_MACH_MSM8974_14001
// Jingchun.Wang@Phone.Bsp.Driver, 2014/09/30  Add for avoid BATFET OCP when ibat<1024mA 
		if(chg_current < 1024) {
			chg_current = BQ24196_CHG_IBATMAX_HRM;
		}
#endif /*CONFIG_MACH_MSM8974_14001*/
		

		value = (chg_current - BQ24196_CHG_IBATMAX_MIN)/64;
		value <<= 2;
		//pr_info("%s value:0x%x\n",__func__,value);
		return bq24196_chg_masked_write(di,CHARGE_CURRENT_CTRL,BQ24196_IBATMAX_BITS,value,1);
	}
}

#define BQ24196_IBATTERM_BITS	0xF
static int 
bq24196_ibatterm_set(struct bq24196_device_info *di, int term_current)
{
	u8 value = 0;

	if( (term_current < BQ24196_TERM_CURR_MIN) || (term_current > BQ24196_TERM_CURR_MAX)){
		term_current = BQ24196_TERM_CURR_MIN;
		//pr_info("%s bad term_current,set to default 128mA\n",__func__);
	}

	value = (term_current - BQ24196_TERM_CURR_MIN)/128;
	return bq24196_chg_masked_write(di,PRECHARGE_TERMCURR_CTRL,BQ24196_IBATTERM_BITS,value,1);		
}

#define BQ24196_IUSBMAX_BITS	0X07
static int
bq24196_iusbmax_set(struct bq24196_device_info *di, int mA)
{
	u8 value = 0;
	pr_info("%s mA:%d\n",__func__,mA);

	if( (mA < BQ24196_CHG_IUSBMAX_MIN) || ( mA > BQ24196_CHG_IUSBMAX_MAX )){
		pr_info("bad iusbmA:%d asked to set\n",mA);
		return -EINVAL;
	}

	if( mA == BQ24196_CHG_IUSBMAX_MAX )
		value = 0x7;
	else if( mA >= 2000 )
		value = 0x6;
	else if( mA >= 1500 )
		value = 0x5;
	else if( mA >= 1200 )
		value = 0x4;
	else if( mA >= 900 )
		value = 0x3;
	else if( mA >= 500 )
		value = 0x2;
	else if( mA >= 150 )
		value = 0x1;
	else if( mA >= 100 )
		value = 0x0;
	return bq24196_chg_masked_write(di,INPUT_SOURCE_CTRL,BQ24196_IUSBMAX_BITS,value,1);
}

#define BQ24196_VBATDET_BITS	0x1
static int
bq24196_vbatdet_set(struct bq24196_device_info *di, int vbatdet)
{
	u8 value = 0;
	
	if( vbatdet == 100 ){
		value = 0x0;
	}
	else if( vbatdet == 300 ){
		value = 0x1;
	}
	else{
		value = 0x0;
		pr_err("bad vbatdet:%d,default to 100mv\n",vbatdet);
		
	}
	return bq24196_chg_masked_write(di,CHARGE_VOL_CTRL,BQ24196_VBATDET_BITS,value,1);
}

#define BQ24196_CHG_VDDMAX_MIN	3504
#define BQ24196_CHG_VDDMAX_MAX 4400
#define BQ24196_VDDMAX_BITS		0xFC

static int
bq24196_vddmax_set(struct bq24196_device_info *di, int voltage)
{
	u8 value = 0;
	pr_info("%s voltage:%d\n",__func__,voltage);
	
	if (voltage < BQ24196_CHG_VDDMAX_MIN
			|| voltage > BQ24196_CHG_VDDMAX_MAX) {
		pr_err("bad vddmax mV=%d asked to set\n", voltage);
		return -EINVAL;
	}
		
	value = (voltage - BQ24196_CHG_VDDMAX_MIN)/16;
	value <<= 2;

	return bq24196_chg_masked_write(di,CHARGE_VOL_CTRL,BQ24196_VDDMAX_BITS,value,1);
}

#define BQ24196_VINMIN_MIN	3880
#define BQ24196_VINMIN_MAX	5080
#define BQ24196_VINMIN_BITS	0x78
static int
bq24196_vinmin_set(struct bq24196_device_info *di, int voltage)
{
	u8 value = 0;
	//pr_info("%s voltage:%d\n",__func__,voltage);
	if (voltage < BQ24196_VINMIN_MIN
			|| voltage > BQ24196_VINMIN_MAX) {
		pr_err("bad vinmin mV=%d asked to set\n", voltage);
		return -EINVAL;
	}

	value = (voltage - BQ24196_VINMIN_MIN)/80;
	value <<= 3;
	return bq24196_chg_masked_write(di,INPUT_SOURCE_CTRL,BQ24196_VINMIN_BITS,value,1);	//default 4.36v
}

#define BQ24196_CHARGE_TIMEOUT_BITS		0x06
static int
bq24196_check_charge_timeout(struct bq24196_device_info *di,int hours)
{
	u8 value = 0;

#ifdef CONGIF_OPPO_CMCC_OPTR
/* OPPO 2014-06-19 sjc Add for CMCC test: disable HW timer */
	if (hours >= 1000)
		return bq24196_chg_masked_write(di, CHARGE_TERM_TIMER_CTRL, 0x08, 0x0, 1);
#endif

	if( hours >= 20 )	//20 hours
		value = 0x06;
	else if( hours >= 12 )	//12 hours
		value = 0x04;
	else if( hours >= 8 )	//8 hours
		value = 0x02;
	else					//5 hours
		value = 0x0;
	return bq24196_chg_masked_write(di,CHARGE_TERM_TIMER_CTRL,BQ24196_CHARGE_TIMEOUT_BITS,value,1);
}

#define BQ24196_OTG_CURRENT_BITS	0x01
static int 
bq24196_otg_current_set(struct bq24196_device_info *di,int otg_current)
{
	u8 value = 0;
	// otg current:default 1.3A, modify to 500ma
	if(otg_current >= 1000)
		value = 0x1;	//1300ma
	else
		value = 0x0;	//500ma
	return bq24196_chg_masked_write(di,POWER_ON_CONF,BQ24196_OTG_CURRENT_BITS,value,1);
}

#define BQ24196_WDT_SET_BITS		0x30
static int 
bq24196_wdt_set(struct bq24196_device_info *di,int seconds)
{
	u8 value = 0;
	if(seconds == 0)	//disable wdt
		value = 0x0;
	else if(seconds == 40)
		value = 0x10;
	else if(seconds == 80)
		value = 0x20;
	else if(seconds == 160)
		value = 0x30;
	else
		pr_err("%s bad seconds:%d\n",__func__,seconds);
	return bq24196_chg_masked_write(di,CHARGE_TERM_TIMER_CTRL,BQ24196_WDT_SET_BITS,value,1);
}

#define BQ24196_REGS_RESET_BITS		0x80
static int
bq24196_regs_reset(struct bq24196_device_info *di,int reset)
{
	u8 value = 0;
	if(reset)	// 1 is reset
		value = 0x80;
	else
		value = 0x0;
	return bq24196_chg_masked_write(di,POWER_ON_CONF,BQ24196_REGS_RESET_BITS,value,1);
}

#define BQ24196_CHARGEEN_BITS	0x30
static int
bq24196_charge_en(struct bq24196_device_info *di, int enable)
{
	u8 value = 0;
	pr_info("%s enable:%d\n",__func__,enable);

	if( enable == 1 )	//enable charge
		value = 0x10;
	else if( enable == 0 )	//disable charge
		value = 0x0;
	else if( enable == 2 )	//OTG
		value = 0x20;
	return bq24196_chg_masked_write(di,POWER_ON_CONF,BQ24196_CHARGEEN_BITS,value,1);
}

static int 
bq24196_get_charge_en(struct bq24196_device_info *di)
{
	char value_buf;
	int rc;
		
	rc = bq24196_read(di,POWER_ON_CONF,1,&value_buf);
	if(rc < 0) {
		pr_err("read charge en status fail\n");
		return 0;
	}
	if((value_buf & 0x30) == 0x0)//disable charge
		return 0;
	else if((value_buf & 0x30) == 0x10) //enable charge
		return 1;
	else //OTG
		return 2;
}

#define BQ24196_USB_SUSPEND_ENABLE_BITS	0x80
static int 
bq24196_usb_suspend_enable(struct bq24196_device_info *di,int enable)
{
	u8 value = 0x0;
	pr_info("%s enable:%d\n",__func__,enable);
	if(enable)
		value = 0x80;
	else
		value = 0x0;
	return bq24196_chg_masked_write(di,INPUT_SOURCE_CTRL,BQ24196_USB_SUSPEND_ENABLE_BITS,value,1);
}

static int bq24196_get_system_status(struct bq24196_device_info *di)
{
	char value_buf;
	int rc;
		
	rc = bq24196_read(di,SYS_STS,1,&value_buf);
	if(rc < 0) {
		pr_err("read system status fail\n");
		return 0;
	}
	return value_buf;
}

static int bq24196_chg_iusbmax_set(int mA)
{
	return bq24196_iusbmax_set(bq24196_di,mA);
}
static int bq24196_chg_ibatmax_set(int mA)
{
	return bq24196_ibatmax_set(bq24196_di,mA);
}
static int bq24196_chg_ibatterm_set(int mA)
{
	return bq24196_ibatterm_set(bq24196_di,mA);
}
static int bq24196_chg_vddmax_set(int mV)
{
	return bq24196_vddmax_set(bq24196_di,mV);
}
static int bq24196_chg_vinmin_set(int mV)
{
	return bq24196_vinmin_set(bq24196_di,mV);
}
static int bq24196_chg_vbatdet_set(int mV)
{
	return bq24196_vbatdet_set(bq24196_di,mV);
}
static int bq24196_chg_check_charge_timeout(int hours)
{
	return bq24196_check_charge_timeout(bq24196_di,hours);
}

static int bq24196_chg_otg_current_set(int mA)
{
	return bq24196_otg_current_set(bq24196_di,mA);
}

static int bq24196_chg_wdt_set(int seconds)
{
	return bq24196_wdt_set(bq24196_di,seconds);
}

static int bq24196_chg_regs_reset(int reset)
{
	return bq24196_regs_reset(bq24196_di,reset);
}

static int bq24196_chg_charge_en(int enable)
{	
	return bq24196_charge_en(bq24196_di,enable);
}

static int bq24196_chg_get_charge_en(void)
{
	return bq24196_get_charge_en(bq24196_di);
}

static int bq24196_chg_get_system_status(void)
{
	return bq24196_get_system_status(bq24196_di);
}

static int bq24196_chg_usb_suspend_enable(int enable)
{
	return bq24196_usb_suspend_enable(bq24196_di,enable);
}

static struct qpnp_external_charger bq24196_charger = {
	.chg_vddmax_set			= bq24196_chg_vddmax_set,
	.chg_vbatdet_set		= bq24196_chg_vbatdet_set,
	.chg_iusbmax_set		= bq24196_chg_iusbmax_set,
	.chg_ibatmax_set		= bq24196_chg_ibatmax_set,
	.chg_ibatterm_set		= bq24196_chg_ibatterm_set,
	.chg_vinmin_set			= bq24196_chg_vinmin_set,
	.check_charge_timeout	= bq24196_chg_check_charge_timeout,
	.chg_charge_en			= bq24196_chg_charge_en,
	.chg_get_system_status	= bq24196_chg_get_system_status,
	.chg_get_charge_en		= bq24196_chg_get_charge_en,
	.chg_usb_suspend_enable	= bq24196_chg_usb_suspend_enable,
	.chg_otg_current_set	= bq24196_chg_otg_current_set,
	.chg_wdt_set			= bq24196_chg_wdt_set,
	.chg_regs_reset			= bq24196_chg_regs_reset,
};

#define BQ24196_RESET_BITS 0x80
static void bq24196_hw_config_init(struct bq24196_device_info *di)
{
	char value_buf[2]={0x0};

	bq24196_read(di,SYS_STS,1,&value_buf[0]);
	//FAULT_REG need to read twice
	bq24196_read(di,FAULT_REG,1,&value_buf[1]);
	bq24196_read(di,FAULT_REG,1,&value_buf[1]);
	pr_info("bq24196 SYS_STS:0x%x,fault_reg:0x%x\n",value_buf[0],value_buf[1]);
	bq24196_chg_regs_reset(1);	//reset all regs to default
	bq24196_chg_wdt_set(0);		//disable wdt
	qpnp_external_charger_register(&bq24196_charger);
}

static int bq24196_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	char *name;
	struct bq24196_device_info *di;
	struct bq24196_access_methods *bus;
	int num;
	int retval = 0;
	
	printk("lfc bq24196_probe\n");
	if(!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk("lfc bq24196_probe,i2c_func error\n");
		goto err_check_functionality_failed;
		}
	/* Get new ID for the new battery device */
	retval = idr_pre_get(&bq24196_charger_id, GFP_KERNEL);
	if (retval == 0)
		return -ENOMEM;
	retval = idr_get_new(&bq24196_charger_id, client, &num);
	if (retval < 0)
		return retval;
	
	name = kasprintf(GFP_KERNEL, "%s-%d", id->name, num);
	if (!name) {
		dev_err(&client->dev, "failed to allocate device name\n");
		retval = -ENOMEM;
		goto bq24196_chg_failed_1;
	}

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&client->dev, "failed to allocate device info data\n");
		retval = -ENOMEM;
		goto bq24196_chg_failed_2;
	}
	di->id = num;

	bus = kzalloc(sizeof(*bus), GFP_KERNEL);
	if (!bus) {
		dev_err(&client->dev, "failed to allocate access method "
					"data\n");
		retval = -ENOMEM;
		goto bq24196_chg_failed_3;
	}
	
	i2c_set_clientdata(client, di);
	di->dev = &client->dev;
	bus->read = &bq24196_read_i2c;
	bus->write = &bq24196_write_i2c;
	di->bus = bus;
	di->client = client;
	bq24196_client = client;
	bq24196_di = di;
	atomic_set(&di->suspended, 0); //sjc1118
	mutex_init(&di->i2c_lock);
	bq24196_hw_config_init(di);
	
	return 0;
	
err_check_functionality_failed:
	printk("lfc bq24196_probe fail\n");
	return 0;

bq24196_chg_failed_3:
	kfree(di);
bq24196_chg_failed_2:
	kfree(name);
bq24196_chg_failed_1:
	idr_remove(&bq24196_charger_id,num);
	return retval;	
}
    

static int bq24196_remove(struct i2c_client *client)
{
	qpnp_external_charger_unregister(&bq24196_charger);
    return 0;
}

static const struct of_device_id bq24196_match[] = {
	{ .compatible = "ti,bq24196_charger" },
	{ },
};


static const struct i2c_device_id bq24196_id[] = {
	{ "bq24196_charger", 1 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq24196_id);

static void bq24196_suspended(bool state)
{
	unsigned long flags;

	spin_lock_irqsave(&resume_lock, flags);
	suspended = state;
	spin_unlock_irqrestore(&resume_lock, flags);

	if (!suspended)
		complete_all(&resume_done);
}

void bq24196_wait_for_resume(void)
{
	unsigned long flags;
	bool asleep;

	spin_lock_irqsave(&resume_lock, flags);
	asleep = suspended;
	spin_unlock_irqrestore(&resume_lock, flags);

	if (asleep) {
		INIT_COMPLETION(resume_done);
		wait_for_completion_timeout(&resume_done,
					msecs_to_jiffies(50));
	}
}

static int bq24196_suspend(struct device *dev) //sjc1118
{
	struct bq24196_device_info *chip = dev_get_drvdata(dev);

	bq24196_suspended(true);
	atomic_set(&chip->suspended, 1);

	return 0;
}

static int bq24196_resume(struct device *dev) //sjc1118
{
	struct bq24196_device_info *chip = dev_get_drvdata(dev);

	atomic_set(&chip->suspended, 0);
	bq24196_suspended(false);

	return 0;
}

static const struct dev_pm_ops bq24196_pm_ops = { //sjc1118
	.resume		= bq24196_resume,
	.suspend		= bq24196_suspend,
};

static struct i2c_driver bq24196_charger_driver = {
	.driver		= {
		.name = "bq24196_charger",
		.owner	= THIS_MODULE,
		.of_match_table = bq24196_match,
		.pm		= &bq24196_pm_ops,
	},
	.probe		= bq24196_probe,
	.remove		= bq24196_remove,
	.id_table	= bq24196_id,
};

static int __init bq24196_charger_init(void)
{
	int ret;
	ret = i2c_add_driver(&bq24196_charger_driver);
	if (ret)
		printk(KERN_ERR "Unable to register bq24196_charger driver\n");
	return ret;
}
module_init(bq24196_charger_init);

static void __exit bq24196_charger_exit(void)
{
	i2c_del_driver(&bq24196_charger_driver);
}
module_exit(bq24196_charger_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Qualcomm Innovation Center, Inc.");
MODULE_DESCRIPTION("BQ24196 battery charger driver");

