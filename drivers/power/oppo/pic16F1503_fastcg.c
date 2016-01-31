/****************************************************
**Description:pic1503 update firmware and driver
**Author:liaofuchun lfc@oppo.com lfc0727@163.com
**Date:2013-12-22
*****************************************************/
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/gpio.h>
#include "pic16F1503_fastcg_firmware.h"

static struct task_struct *pic16f_fw_update_task = NULL;

#define BYTE_OFFSET 2
#define BYTES_TO_WRITE 16

#ifdef CONFIG_MACH_FIND7	//FIND7/FIND7WX:pic1503
#define ERASE_COUNT   		96	//0x200-0x7FF
#define READ_COUNT			95	//192

#else	//FIND7OP or other pic1508 
#define ERASE_COUNT   		224	//0x200-0xFFF
#define READ_COUNT			223	//448
#endif

#define	FW_CHECK_FAIL		0
#define	FW_CHECK_SUCCESS	1

static struct i2c_client *pic16F_client;

int pic_fw_ver_count = sizeof(Pic16F_firmware_data);
int pic_need_to_up_fw = 0;
int pic_have_updated = 0;

#ifndef CONFIG_MACH_OPPO
//Fuchun.Liao@EXP.Driver,2014/04/11,modify for add regs check after fw update
int pic16f_fw_update(void)
{
	unsigned char *buf;
	unsigned char temp;
	unsigned char zero_buf[1]={0};
	unsigned char addr_buf[2]={0x02,0x00};
	unsigned char temp_buf[1];
	int i,rc=0;
	unsigned int count=0,addr=0x200;

	pr_err("%s pic16F_update_fw,erase data ing.......\n",__func__);
	//while(!need_to_up_fw);
	//pic16F_client = client;
	//buf = firmware_data;
    buf = Pic16F_firmware_data;
	//erase address 0x200-0x7FF 
	for(i = 0;i < ERASE_COUNT;i++){
		//first:set address
		rc = i2c_smbus_write_i2c_block_data(pic16F_client,0x01,2,&addr_buf[0]);
		if(rc < 0){
			pr_err("%s pic16F_update_fw,i2c_write 0x01 error\n",__func__);
			goto pic16F_update_fw_err;
			}
		//erase data:0x10 words once
		i2c_smbus_write_i2c_block_data(pic16F_client,0x04,1,&zero_buf[0]);
		msleep(1);
		i2c_smbus_read_i2c_block_data(pic16F_client,0x04,1,&temp_buf[0]);
		//printk("lfc read 0x04,temp_buf[0]:0x%x\n",temp_buf[0]);
		addr = addr + 0x10;
		addr_buf[0] = addr >> 8;
		addr_buf[1] = addr & 0xFF;
		//printk("lfc addr_buf[0]:0x%x,addr_buf[1]:0x%x\n",addr_buf[0],addr_buf[1]);
		}
		msleep(10);
		
	//write data begin
		while(count < sizeof(Pic16F_firmware_data))
		{
			//printk("lfc count+1 buf[%d]:0x%x\n",count+1,buf[count+1]);
			#if 0
			addr = ((buf[count]<<8)|(buf[count+1]))>>1;
			addr_buf[0] = addr >> 8;
			addr_buf[1] = addr & 0xFF;
			printk("lfc count+1 buf[%d]:0x%x\n",count,buf[count]);
			printk("lfc count+2 buf[%d]:0x%x\n",count+1,buf[count+1]);
			#endif
			addr_buf[0]=buf[count+1];
			addr_buf[1]=buf[count];
			//printk("lfc write data addr_buf[0]:0x%x,addr_buf[1]:0x%x\n",addr_buf[0],addr_buf[1]);
			rc = i2c_smbus_write_i2c_block_data(pic16F_client,0x01,2,&addr_buf[0]);
			if(rc < 0)
				pr_err("%s i2c_write 0x01 error\n",__func__);
			
			//byte_count = buf[count];
				//swap low byte and high byte begin
				//because LSB is before MSB in buf,but pic16F receive MSB first
				for(i = 0;i < 2*BYTES_TO_WRITE;i = (i+2)){
					//printk("lfc before swap buf[%d]:0x%x,buf[%d]:0x%x\n",count+BYTE_OFFSET+i,buf[count+BYTE_OFFSET+i],count+BYTE_OFFSET+i+1,buf[count+BYTE_OFFSET+i+1]);
					temp = buf[count+BYTE_OFFSET+i];
					buf[count+BYTE_OFFSET+i] = buf[count+BYTE_OFFSET+i+1];
					buf[count+BYTE_OFFSET+i+1] = temp;
					//printk("lfc after swap buf[%d]:0x%x,buf[%d]:0x%x\n",count+BYTE_OFFSET+i,buf[count+BYTE_OFFSET+i],count+BYTE_OFFSET+i+1,buf[count+BYTE_OFFSET+i+1]);
					}
				//swap low byte and high byte end
				//write 16 bytes data to pic16F
				i2c_smbus_write_i2c_block_data(pic16F_client,0x02,BYTES_TO_WRITE,&buf[count+BYTE_OFFSET]);
			
				i2c_smbus_write_i2c_block_data(pic16F_client,0x05,1,&zero_buf[0]);
				i2c_smbus_read_i2c_block_data(pic16F_client,0x05,1,&temp_buf[0]);
				//printk("lfc read 0x05,temp_buf[0]:0x%x\n",temp_buf[0]);

				//write 16 bytes data to pic16F again
				i2c_smbus_write_i2c_block_data(pic16F_client,0x02,BYTES_TO_WRITE,&buf[count+BYTE_OFFSET+BYTES_TO_WRITE]);
			
				i2c_smbus_write_i2c_block_data(pic16F_client,0x05,1,&zero_buf[0]);
				i2c_smbus_read_i2c_block_data(pic16F_client,0x05,1,&temp_buf[0]);
				//printk("lfc read again 0x05,temp_buf[0]:0x%x\n",temp_buf[0]);
				
				count = count + BYTE_OFFSET + 2*BYTES_TO_WRITE;

				msleep(2);
				//printk("lfc sizeof(pic16F_firmware_data):%d\n",sizeof(Pic16F_firmware_data));
				if(count > sizeof(Pic16F_firmware_data))
					break;
		}
			i2c_smbus_write_i2c_block_data(pic16F_client,0x06,1,&zero_buf[0]);
			i2c_smbus_read_i2c_block_data(pic16F_client,0x06,1,&temp_buf[0]);
			pic_have_updated = 1;
	//write data end
	pr_err("%s pic16F update_fw success\n",__func__);
	return 0;

pic16F_update_fw_err:
	pr_err("%s pic16F update_fw fail\n",__func__);
	return 1;
}

#else
static bool pic16f_fw_check(void)
{
	unsigned char addr_buf[2] = {0x02,0x00};
	unsigned char data_buf[32] = {0x0};
	int rc,i,j,addr;
	
	//first:set address
	rc = i2c_smbus_write_i2c_block_data(pic16F_client,0x01,2,&addr_buf[0]);
	if(rc < 0){
		pr_err("%s i2c_write 0x01 error\n",__func__);
		goto i2c_err;
	}
	msleep(10);
	for(i = 0;i < READ_COUNT;i++){	//1508:448,1503:192
		i2c_smbus_read_i2c_block_data(pic16F_client,0x03,16,&data_buf[0]);
		msleep(2);
		i2c_smbus_read_i2c_block_data(pic16F_client,0x03,16,&data_buf[16]);
		
		addr = 0x200 + i * 16;
		/*
		pr_err("%s addr = 0x%x,%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x \
			%x %x %x %x %x %x %x %x %x\n",__func__,addr,
			data_buf[0],data_buf[1],data_buf[2],data_buf[3],data_buf[4],data_buf[5],data_buf[6],data_buf[7],
			data_buf[8],data_buf[9],data_buf[10],data_buf[11],data_buf[12],data_buf[13],data_buf[14],
			data_buf[15],data_buf[16],data_buf[17],data_buf[18],data_buf[19],data_buf[20],data_buf[21],data_buf[22],
			data_buf[23],data_buf[24],data_buf[25],data_buf[26],data_buf[27],data_buf[28],data_buf[29],data_buf[30],
			data_buf[31]);*/
		
		//compare recv_buf with Pic16F_firmware_data[] begin
		for(j = 0;j < 32;j++){
			if(addr != Pic16F_firmware_data[i * 34])
				continue;
			if(data_buf[j] != Pic16F_firmware_data[i * 34 + 2 + j]){
				pr_err("%s fail,data_buf[%d]:0x%x != Pic16F_fimware_data[%d]:0x%x\n",__func__,
					j,data_buf[j],(i * 34 + 1 + j),Pic16F_firmware_data[i * 34 + 1 + j]);
				return FW_CHECK_FAIL;
			}
		}
		//compare recv_buf with Pic16F_firmware_data[] end
	}
	pr_info("%s success\n",__func__);
	return FW_CHECK_SUCCESS;
	
i2c_err:
	pr_err("%s failed\n",__func__);
	return FW_CHECK_FAIL;
}

static int pic16f_fw_write(unsigned char *data_buf,unsigned int offset,unsigned int length)
{
	unsigned int count = 0;
	unsigned char zero_buf[1] = {0};
	unsigned char temp_buf[1] = {0};
	unsigned char addr_buf[2] = {0x00,0x00};
	unsigned char temp;
	int i,rc;

	count = offset;
	//write data begin
	while(count < (offset + length))
	{
		addr_buf[0] = data_buf[count + 1];
		addr_buf[1] = data_buf[count];
		//printk("%s write data addr_buf[0]:0x%x,addr_buf[1]:0x%x\n",__func__,addr_buf[0],addr_buf[1]);
		rc = i2c_smbus_write_i2c_block_data(pic16F_client,0x01,2,&addr_buf[0]);
		if(rc < 0){
			pr_err("%s i2c_write 0x01 error\n",__func__);
			return -1;
		}

		//byte_count = buf[count];
		//swap low byte and high byte begin
		//because LSB is before MSB in buf,but pic16F receive MSB first
		for(i = 0;i < 2 * BYTES_TO_WRITE;i = (i+2)){
			temp = data_buf[count+BYTE_OFFSET+i];
			data_buf[count+BYTE_OFFSET+i] = data_buf[count+BYTE_OFFSET+i+1];
			data_buf[count+BYTE_OFFSET+i+1] = temp;
		}
		//swap low byte and high byte end
		//write 16 bytes data to pic16F
		i2c_smbus_write_i2c_block_data(pic16F_client,0x02,BYTES_TO_WRITE,&data_buf[count+BYTE_OFFSET]);
			
		i2c_smbus_write_i2c_block_data(pic16F_client,0x05,1,&zero_buf[0]);
		i2c_smbus_read_i2c_block_data(pic16F_client,0x05,1,&temp_buf[0]);
		//printk("lfc read 0x05,temp_buf[0]:0x%x\n",temp_buf[0]);

		//write 16 bytes data to pic16F again
		i2c_smbus_write_i2c_block_data(pic16F_client,0x02,BYTES_TO_WRITE,&data_buf[count+BYTE_OFFSET+BYTES_TO_WRITE]);
			
		i2c_smbus_write_i2c_block_data(pic16F_client,0x05,1,&zero_buf[0]);
		i2c_smbus_read_i2c_block_data(pic16F_client,0x05,1,&temp_buf[0]);
		//printk("lfc read again 0x05,temp_buf[0]:0x%x\n",temp_buf[0]);
				
		count = count + BYTE_OFFSET + 2 * BYTES_TO_WRITE;

		msleep(2);
		//pr_err("%s count:%d,offset:%d,length:%d\n",__func__,count,offset,length);
		if(count > (offset + length - 1)){
			break;
		}
	}
	return 0;
}
	
int pic16f_fw_update(bool pull96)
{
	unsigned char zero_buf[1] = {0};
	unsigned char addr_buf[2] = {0x02,0x00};
	unsigned char temp_buf[1];
	int i,rc=0;
	unsigned int addr = 0x200;
	int download_again = 0;
	
	pr_err("%s pic16F_update_fw,erase data ing.......\n",__func__);

	//pull up GPIO96 to power on MCU1503
	if(pull96){
		gpio_set_value(96,1);
		rc = gpio_tlmm_config(GPIO_CFG(96, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA),GPIO_CFG_ENABLE);
		if(rc < 0){
			pr_err("%s pull up GPIO96 fail\n",__func__);
			return rc;
		}
		msleep(300);
	}

update_fw:
	//erase address 0x200-0x7FF 
	for(i = 0;i < ERASE_COUNT;i++){
		//first:set address
		rc = i2c_smbus_write_i2c_block_data(pic16F_client,0x01,2,&addr_buf[0]);
		if(rc < 0){
			pr_err("%s pic16F_update_fw,i2c_write 0x01 error\n",__func__);
			goto update_fw_err;
			}
		//erase data:0x10 words once
		i2c_smbus_write_i2c_block_data(pic16F_client,0x04,1,&zero_buf[0]);
		msleep(1);
		i2c_smbus_read_i2c_block_data(pic16F_client,0x04,1,&temp_buf[0]);
		//printk("lfc read 0x04,temp_buf[0]:0x%x\n",temp_buf[0]);
		addr = addr + 0x10;
		addr_buf[0] = addr >> 8;
		addr_buf[1] = addr & 0xFF;
		//printk("lfc addr_buf[0]:0x%x,addr_buf[1]:0x%x\n",addr_buf[0],addr_buf[1]);
	}
	msleep(10);
		
	pic16f_fw_write(Pic16F_firmware_data,0,sizeof(Pic16F_firmware_data) - 34);

	//fw check begin:read data from pic1503/1508,and compare it with Pic16F_firmware_data[]
	rc = pic16f_fw_check();
	if(rc == FW_CHECK_FAIL){
		download_again++;
		if(download_again > 3){
			goto update_fw_err;
		}
		pr_err("%s fw check fail,download fw again\n",__func__);
		goto update_fw;
	}
	//fw check end

	//write 0x7F0~0x7FF(0x7FF = 0x3455)
	rc = pic16f_fw_write(Pic16F_firmware_data,sizeof(Pic16F_firmware_data) - 34,34);
	if(rc < 0){
		goto update_fw_err;
	}
	//write 0x7F0~0x7FF end
	
	msleep(2);
	//jump to app code begin
	i2c_smbus_write_i2c_block_data(pic16F_client,0x06,1,&zero_buf[0]);
	i2c_smbus_read_i2c_block_data(pic16F_client,0x06,1,&temp_buf[0]);
	//jump to app code end
	
	pic_have_updated = 1;
	
	//pull down GPIO96 to power off MCU1503/1508
	if(pull96) {
		gpio_set_value(96,0);
		rc = gpio_tlmm_config(GPIO_CFG(96, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),GPIO_CFG_ENABLE);
		if(rc < 0){
			pr_err("%s pull down GPIO96 fail\n",__func__);
		}
	}	
	pr_err("%s pic16F update_fw success\n",__func__);
	return 0;

update_fw_err:
	if(pull96){
		gpio_set_value(96,0);
		rc = gpio_tlmm_config(GPIO_CFG(96, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),GPIO_CFG_ENABLE);
		if(rc < 0){
			pr_err("%s pull down GPIO96 fail\n",__func__);
		}
	}
	pr_err("%s pic16F update_fw fail\n",__func__);
	return 1;
}
#endif

static int pic16f_fw_update_thread(void *data)
{
	pic16f_fw_update(1);
	return 0;
}
static int pic16F_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	pr_err("%s pic16F_probe\n",__func__);
	if(!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s pic16F_probe,i2c_func error\n",__func__);
		goto err_check_functionality_failed;
		}	
	pic16F_client = client;
	pic16f_fw_update_task = kthread_create(pic16f_fw_update_thread,NULL,"pic16f_fw_update_thread");
	if(IS_ERR(pic16f_fw_update_task)){
		pr_err("%s create kthread fail\n",__func__);
	}else{
		wake_up_process(pic16f_fw_update_task);
	}
	return 0;
	
err_check_functionality_failed:
	pr_err("%s pic16F_probe fail\n",__func__);
	return 0;
}
    

static int pic16F_remove(struct i2c_client *client)
{
    return 0;
}

static const struct of_device_id pic16f_match[] = {
	{ .compatible = "microchip,pic16f_fastcg" },
	{ },
};


static const struct i2c_device_id pic16f_id[] = {
	{ "pic16f_fastcg", 1 },
	{},
};
MODULE_DEVICE_TABLE(i2c, pic16f_id);

static struct i2c_driver pic16f_fastcg_driver = {
	.driver		= {
		.name = "pic16f_fastcg",
		.owner	= THIS_MODULE,
		.of_match_table = pic16f_match,
	},
	.probe		= pic16F_probe,
	.remove		= pic16F_remove,
	.id_table	= pic16f_id,
};

static int __init pic16f_fastcg_init(void)
{
	int ret;
	ret = i2c_add_driver(&pic16f_fastcg_driver);
	if (ret)
		printk(KERN_ERR "Unable to register pic16f_fastcg driver\n");
	return ret;
}
module_init(pic16f_fastcg_init);

static void __exit pic16f_fastcg_exit(void)
{
	i2c_del_driver(&pic16f_fastcg_driver);
}
module_exit(pic16f_fastcg_exit);


