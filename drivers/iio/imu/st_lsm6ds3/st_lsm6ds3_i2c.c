/*
 * STMicroelectronics lsm6ds3 i2c driver
 *
 * Copyright 2014 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>

#include "st_lsm6ds3.h"
#include <mach/eint.h>
#include <mach/mt_gpio.h>
#include <cust_eint.h>
#include "cust_gpio_usage.h"
#include <linux/platform_data/st_sensors_pdata.h>
#include <linux/dma-mapping.h>

static void *gpDMABuf_va;
static dma_addr_t gpDMABuf_pa=0;
#define LSM_DMA_MAX_TRANSACTION_LEN  8 * 1024 /* 8K fifo data */
#define ST_LSM6DS3_FIFO_DATA_OUT_L		0x3e
int st_lsm6ds3_i2c_read_fifo(struct lsm6ds3_data *cdata, int len, u8 *data)
{
	int ret = 0;

	char fifo_addr[1] = {ST_LSM6DS3_FIFO_DATA_OUT_L};
	struct i2c_client *client = to_i2c_client(cdata->dev);

	if (len > LSM_DMA_MAX_TRANSACTION_LEN) {
		printk(KERN_EMERG "__lsm6ds3_read_fifo len error\n");
		return -1;
	}

	struct i2c_msg msgs[] = {
		{
			.addr  =  client->addr,
			.flags  =  0,
			.len  =  1,
			.buf  =  fifo_addr,
			//.scl_rate = 400 * 1000,
		},
		{
			.addr  =  client->addr,
			/* the ext_flag is mtk specific */
			.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
			.flags  = I2C_M_RD,
			.len  =  len,
			.buf  =  (u8 *)gpDMABuf_pa,
			//.scl_rate = 400 * 1000,
		},
	};

	mutex_lock(&cdata->bank_registers_lock);
	ret = i2c_transfer(client->adapter, msgs, 2);
	mutex_unlock(&cdata->bank_registers_lock);
	if(ret < 0){
		printk(KERN_EMERG "_lsm6ds3_read_fifo i2c_transfer error, ret: %d\n", ret);
		return ret;
	}

	memcpy(data, gpDMABuf_va, len);
	return 0;
}

#define I2C_TRANS_TIMING (100)
#define I2C_WR_MAX (7)
static int lsm6ds3_i2c_write(struct i2c_client *client, u8 *buffer, int len)//len : size of data,expect addr
{

#if 1
    int length = len;
    int writebytes = 0;
    int ret = 0;
    u8 mem_addr = buffer[0];
    u8  *ptr = buffer+1;
    unsigned char buf[I2C_WR_MAX + 1];

    struct i2c_msg msgs[3]={{0}, };


//    printk("lsm: %s len:%d\n",__func__, len);
    memset(buf, 0, sizeof(buf));

    while(length > 0)
    {
              if (length > I2C_WR_MAX)
                       writebytes = I2C_WR_MAX;
              else
                       writebytes = length;

				memset(buf, 0, sizeof(buf));
				buf[0] = mem_addr;
				memcpy(buf+1, ptr, writebytes);
				msgs[0].addr = client->addr & I2C_MASK_FLAG;
				msgs[0].flags = 0;
				msgs[0].buf = buf;
				msgs[0].len = writebytes+1;
				msgs[0].timing = I2C_TRANS_TIMING;
				msgs[0].ext_flag = 0;

          	ret = i2c_transfer(client->adapter, msgs, 1);
              if (ret!=1)
              {
                       printk("lsm: i2c transfer error ret:%d, write_bytes:%d, Line %d\n", ret, writebytes+1, __LINE__);
                       return -1;
              }

              length -= writebytes;
              mem_addr += writebytes;
              ptr += writebytes;
    }

    return len;
#endif
}

static int lsm6ds3_i2c_read(struct i2c_client *client, u8 *buffer,
								  int len)
{
    int length = len;
    int readbytes = 0;
    int ret = 0;
    u8 mem_addr = buffer[0];
    unsigned char buf[I2C_WR_MAX];
    struct i2c_msg msgs[2] = {{0}, };

    while(length > 0)
    {
              if (length > I2C_WR_MAX)
                       readbytes = I2C_WR_MAX;
              else
                       readbytes = length;

				msgs[0].addr = client->addr & I2C_MASK_FLAG;
				msgs[0].flags = 0;
				msgs[0].buf = &mem_addr;
				msgs[0].len = 1;
				msgs[0].timing = I2C_TRANS_TIMING;
				msgs[0].ext_flag = 0;

				memset(buf, 0, sizeof(buf));
				msgs[1].addr = client->addr & I2C_MASK_FLAG;
				msgs[1].flags = I2C_M_RD;
				msgs[1].buf = buf;
				msgs[1].len = readbytes;
				msgs[1].timing = I2C_TRANS_TIMING;
				msgs[1].ext_flag = 0;

				ret = i2c_transfer(client->adapter, msgs, 2);
				  if (ret!=2)
				  {
						   printk("lsm: i2c transfer error ret:%d, read_bytes:%d, addr:0x%x, Line %d\n",
								   	   	   	   	   	   	   	   ret, readbytes, mem_addr, __LINE__);
						   return -1;
				  }


              length -= readbytes;
              mem_addr += readbytes;
              memcpy(buffer, buf, readbytes);
              buffer += readbytes;
    }

    return len;

}

static int st_lsm6ds3_i2c_read(struct lsm6ds3_data *cdata,
				u8 reg_addr, int len, u8 *data, bool b_lock)
{
	int err = 0;
	struct i2c_msg msg[2];
	struct i2c_client *client = to_i2c_client(cdata->dev);

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].len = 1;
	msg[0].buf = &reg_addr;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = data;

	data[0] = reg_addr;
	if (b_lock) {
		mutex_lock(&cdata->bank_registers_lock);
		lsm6ds3_i2c_read(client, data, len);

//		err = i2c_transfer(client->adapter, msg, 2);
		mutex_unlock(&cdata->bank_registers_lock);
	} else
		lsm6ds3_i2c_read(client, data, len);
//		err = i2c_transfer(client->adapter, msg, 2);

	return err;
}

static int st_lsm6ds3_i2c_write(struct lsm6ds3_data *cdata,
				u8 reg_addr, int len, u8 *data, bool b_lock)
{
	int err = 0;
	u8 send[len + 1];
	struct i2c_msg msg;
	struct i2c_client *client = to_i2c_client(cdata->dev);

	send[0] = reg_addr;
	memcpy(&send[1], data, len * sizeof(u8));
//	len++;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = len;
	msg.buf = send;

	if (b_lock) {
		mutex_lock(&cdata->bank_registers_lock);

		lsm6ds3_i2c_write(client,send, len);
//		err = i2c_transfer(client->adapter, &msg, 1);
		mutex_unlock(&cdata->bank_registers_lock);
	} else
		lsm6ds3_i2c_write(client,send, len);
//		err = i2c_transfer(client->adapter, &msg, 1);

	return err;
}

static const struct st_lsm6ds3_transfer_function st_lsm6ds3_tf_i2c = {
	.write = st_lsm6ds3_i2c_write,
	.read = st_lsm6ds3_i2c_read,
};

static int st_lsm6ds3_i2c_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	int err;
	struct lsm6ds3_data *cdata;

	cdata = kmalloc(sizeof(*cdata), GFP_KERNEL);
	if (!cdata)
		return -ENOMEM;

	ST_INFO("st start\n");

	cdata->dev = &client->dev;
	cdata->name = client->name;
	i2c_set_clientdata(client, cdata);

	cdata->tf = &st_lsm6ds3_tf_i2c;

	if (!gpDMABuf_pa) {

		gpDMABuf_va = dma_alloc_coherent(cdata->dev,
			LSM_DMA_MAX_TRANSACTION_LEN, &gpDMABuf_pa, GFP_KERNEL);
		if(!gpDMABuf_va){
			ST_INFO( "Allocate DMA I2C Buffer failed!\n");
			goto free_data;
		}
		ST_INFO( "Allocate DMA I2C Buffer success!\n");
	}
	
	//  mtk hw platform related
	extern  int sensor_gpio_to_irq(void);
	client->irq = sensor_gpio_to_irq();

	ST_INFO( "irq: %d\n", client->irq);
	err = st_lsm6ds3_common_probe(cdata, client->irq);
	if (err < 0)
		goto free_data;


	ST_INFO("st ready to go\n\n");
	return 0;

free_data:
	kfree(cdata);
	ST_INFO("probe failed\n");
	return err;
}

static int st_lsm6ds3_i2c_remove(struct i2c_client *client)
{
	struct lsm6ds3_data *cdata = i2c_get_clientdata(client);

	st_lsm6ds3_common_remove(cdata, client->irq);
	kfree(cdata);

	return 0;
}

#ifdef CONFIG_PM
static int st_lsm6ds3_suspend(struct device *dev)
{
	struct lsm6ds3_data *cdata = i2c_get_clientdata(to_i2c_client(dev));

	//return st_lsm6ds3_common_suspend(cdata);
	return 0;
}

static int st_lsm6ds3_resume(struct device *dev)
{
	struct lsm6ds3_data *cdata = i2c_get_clientdata(to_i2c_client(dev));

	//return st_lsm6ds3_common_resume(cdata);
	return 0;
}

static const struct dev_pm_ops st_lsm6ds3_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_lsm6ds3_suspend, st_lsm6ds3_resume)
};

#define ST_LSM6DS3_PM_OPS		(&st_lsm6ds3_pm_ops)
#else /* CONFIG_PM */
#define ST_LSM6DS3_PM_OPS		NULL
#endif /* CONFIG_PM */

static const struct i2c_device_id st_lsm6ds3_id_table[] = {
//	{ LSM6DS3_DEV_NAME, 0 },
	{ "lsm6ds3", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, st_lsm6ds3_id_table);

static struct i2c_driver st_lsm6ds3_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "st-lsm6ds3-i2c",
		.pm = ST_LSM6DS3_PM_OPS,
	},
	.probe = st_lsm6ds3_i2c_probe,
	.remove = st_lsm6ds3_i2c_remove,
	.id_table = st_lsm6ds3_id_table,
};

struct st_sensors_platform_data pdata ={ .drdy_int_pin = 1, };

static struct i2c_board_info  single_chip_board_info[] = {
	{
		I2C_BOARD_INFO("lsm6ds3", 0x6a),//0x6a
		//.irq = (CUST_EINT_GYRO_NUM),
		//.platform_data = &pdata,
	},
};
static int __init lsm6ds3_board_init(void)
{
	printk("%s meizu\n",__func__);
	i2c_register_board_info(3, single_chip_board_info, 1);
	return 0;
}

postcore_initcall(lsm6ds3_board_init);
module_i2c_driver(st_lsm6ds3_driver);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics lsm6ds3 i2c driver");
MODULE_LICENSE("GPL v2");
