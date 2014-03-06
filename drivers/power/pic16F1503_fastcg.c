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
#include "pic16F1503_fastcg_firmware.h"

static struct task_struct *pic16f_fw_update_task = NULL;

#define BYTE_OFFSET 2
#define BYTES_TO_WRITE 16

#if defined CONFIG_MACH_FIND7 || defined CONFIG_MACH_FIND7WX	//FIND7/FIND7WX:pic1503
#define ERASE_COUNT   96	//0x200-0x7FF
#else	//FIND7OP or other pic1508 
#define ERASE_COUNT   224	//0x200-0xFFF
#endif

static struct i2c_client *pic16F_client;

int pic_fw_ver_count = sizeof(Pic16F_firmware_data);
int pic_need_to_up_fw = 0;
int pic_have_updated = 0;

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

static int pic16f_fw_update_thread(void *data)
{
	pic16f_fw_update();
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

#if 0
static void Pic16FEnableFlashing()
{
    unsigned char uData;
    unsigned char uStatus;
    int retry = 3;

    printk("\n lfc Enable Reflash...\n");

    // Reflash is enabled by first reading the bootloader ID from the firmware and write it back
    Pic16FReadBootloadID();
//    Pic16FWriteBootloadID();

    // Make sure Reflash is not already enabled
    do {
        readRMI(SynaF34_FlashControl, &uData, 1);
        printk("----Read reflash enable ---uData=0x%x--\n",uData);
    } while (uData  ==  0x0f);//while (((uData & 0x0f) != 0x00));

    // Clear ATTN
    readRMI (SynaF01DataBase, &uStatus, 1);
    printk("----Read status ---uStatus=0x%x--\n",uStatus);
    if ((uStatus &0x40) == 0) {
        // Write the "Enable Flash Programming command to F34 Control register
        // Wait for ATTN and then clear the ATTN.
        //uData = 0x0f;    //lemon
        readRMI(SynaF34_FlashControl, &uData, 1);
        uData = uData | 0x0f;  
        writeRMI(SynaF34_FlashControl, &uData, 1);
        SynaWaitForATTN();
        readRMI((SynaF01DataBase + 1), &uStatus, 1);

        // Scan the PDT again to ensure all register offsets are correct
        SynaSetup();

        // Read the "Program Enabled" bit of the F34 Control register, and proceed only if the
        // bit is set.
        readRMI(SynaF34_FlashControl, &uData, 1);
        printk("----read--enable ---uData=0x%x--\n",uData);
        while (uData != 0x80) {
            // In practice, if uData!=0x80 happens for multiple counts, it indicates reflash
            // is failed to be enabled, and program should quit
            printk("%s Can NOT enable reflash !!!\n",__func__);

            if (!retry--)
                return -1;
            readRMI(SynaF34_FlashControl, &uData, 1);
            printk("----read--enable ---uData=0x%x--\n",uData);
        }
    }
    return 0;
}
#endif
#if 0   
   fp = fopen(FILE_PATH,"r");
   if(fp == NULL){
    printk("lfc open file_path fail\n");
	return -1;
   }
   fgets(data,50,fp);
   printk("lfc data[0]:%c,data[1]:%c,data[2]:%c\n",data[0],data[1],data[2]);
   while ( data[0] == ':'){
   	 int bytecount = ctohex(data[0]);
	 int fileaddress = ((data[2]<<8)|data[3])>>1;
	 int recordtype = data[4];
	 if(recordtype )
	 
	 
   	}  
#endif 


