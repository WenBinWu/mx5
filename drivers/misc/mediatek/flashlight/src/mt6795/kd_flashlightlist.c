#ifdef WIN32
#include "win_test.h"
#include "stdio.h"
#include "kd_flashlight.h"
#else
#ifdef CONFIG_COMPAT

#include <linux/fs.h>
#include <linux/compat.h>

#endif

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "kd_camera_hw.h"
#include <mach/upmu_sw.h>

#endif
#ifdef CONFIG_COMPAT
#include <linux/fs.h>
#include <linux/compat.h>
#endif
#include "kd_flashlight.h"
#include <linux/wakelock.h>




//return value :
  // KAL_FALSE : charger�S���ϥΤ�
   //KAL_TRUE : charger�ϥΤ�

extern kal_bool bat_is_charger_exist(void);

    //return value:
      //  true: execute in OTG mode
        //false: otherwise
extern bool mtk_is_host_mode(void);

int strobe_getPartId(int sensorDev, int strobeId);

MUINT32 strobeInit_dummy(FLASHLIGHT_FUNCTION_STRUCT **pfFunc);
MUINT32 constantFlashlightInit(FLASHLIGHT_FUNCTION_STRUCT **pfFunc);
//MUINT32 strobeInit_main(FLASHLIGHT_FUNCTION_STRUCT **pfFunc);
MUINT32 strobeInit_main_sid1_part2(FLASHLIGHT_FUNCTION_STRUCT **pfFunc);
MUINT32 strobeInit_main_sid2_part1(FLASHLIGHT_FUNCTION_STRUCT **pfFunc);
MUINT32 strobeInit_main_sid2_part2(FLASHLIGHT_FUNCTION_STRUCT **pfFunc);
MUINT32 subStrobeInit(FLASHLIGHT_FUNCTION_STRUCT **pfFunc);
MUINT32 strobeInit_sub_sid1_part2(FLASHLIGHT_FUNCTION_STRUCT **pfFunc);
MUINT32 strobeInit_sub_sid2_part1(FLASHLIGHT_FUNCTION_STRUCT **pfFunc);
MUINT32 strobeInit_sub_sid2_part2(FLASHLIGHT_FUNCTION_STRUCT **pfFunc);
struct wake_lock flash_wake_lock;

/******************************************************************************
 * Definition
******************************************************************************/

/* device name and major number */
#define FLASHLIGHT_DEVNAME            "kd_camera_flashlight"

/******************************************************************************
 * Debug configuration
******************************************************************************/
#ifdef WIN32
#define logI(fmt, ...)    {printf(fmt, __VA_ARGS__); printf("\n");}
#define logE(fmt, ...)    {printf("merror: %d ", __LINE__); printf(fmt, __VA_ARGS__); printf("\n");}
#else
	#define PFX "[KD_CAMERA_FLASHLIGHT]"
	#define PK_DBG_NONE(fmt, arg...)    do {} while (0)
	#define PK_DBG_FUNC(fmt, arg...)    printk(KERN_INFO PFX "%s: " fmt, __FUNCTION__ ,##arg)

	#define PK_WARN(fmt, arg...)        printk(KERN_WARNING PFX "%s: " fmt, __FUNCTION__ ,##arg)
	#define PK_NOTICE(fmt, arg...)      printk(KERN_NOTICE PFX "%s: " fmt, __FUNCTION__ ,##arg)
	#define PK_INFO(fmt, arg...)        printk(KERN_INFO PFX "%s: " fmt, __FUNCTION__ ,##arg)
	#define PK_TRC_FUNC(f)              printk(PFX "<%s>\n", __FUNCTION__);
	#define PK_TRC_VERBOSE(fmt, arg...) printk(PFX fmt, ##arg)

	#define DEBUG_KD_STROBE
	#ifdef DEBUG_KD_STROBE
	#define logI PK_DBG_FUNC
	#define logE(fmt, arg...)         printk(KERN_ERR PFX "%s: " fmt, __FUNCTION__ ,##arg)
	#else
	#define logI(a,...)
	#define logE(a,...)
	#endif
#endif

#define FL_I2C_BUSNUM 3
#define FL_DRVNAME	"kd_flashlight"
static struct i2c_board_info __initdata kd_flashlight_dev={ I2C_BOARD_INFO("kd_flashlight", 0x63)};
static struct i2c_client *flashlight_i2c_client = NULL;

struct flashlight_data{
    spinlock_t lock;
    wait_queue_head_t read_wait;
    struct semaphore sem;
};
static struct flashlight_data flashlight_private;
//==============================
// variables
//==============================
static DEFINE_SPINLOCK(g_6332BoostLock);
static FLASHLIGHT_FUNCTION_STRUCT *g_pFlashInitFunc[e_Max_Sensor_Dev_Num][e_Max_Strobe_Num_Per_Dev][e_Max_Part_Num_Per_Dev];
static int gLowBatDuty[e_Max_Sensor_Dev_Num][e_Max_Strobe_Num_Per_Dev];
static int g_strobePartId[e_Max_Sensor_Dev_Num][e_Max_Strobe_Num_Per_Dev];

//==============================
// functions
//==============================
extern U32 pmic_config_interface (U32 RegNum, U32 val, U32 MASK, U32 SHIFT);

/*******************************************************************************
* WriteRegI2C
********************************************************************************/
int flashlight_WriteRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u16 i2cId)
{
    int  i4RetValue = 0;
    int retry = 3;

	spin_lock(&flashlight_private.lock);
	flashlight_i2c_client->addr = (i2cId >> 1);
	flashlight_i2c_client->ext_flag = (flashlight_i2c_client->ext_flag)&(~I2C_DMA_FLAG);
	spin_unlock(&flashlight_private.lock);

    do {
	i4RetValue = i2c_master_send(flashlight_i2c_client, a_pSendData, a_sizeSendData);
	if (i4RetValue != a_sizeSendData) {
	    logI("[LC898212XD] I2C send failed!!, Addr = 0x%x, Data = 0x%x \n", a_pSendData[0], a_pSendData[1] );
	}
	else {
	    break;
	}
	udelay(50);
    } while ((retry--) > 0);

    return 0;
}

/*******************************************************************************
* ReadRegI2C
********************************************************************************/
int flashlight_ReadRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u8 *a_pRecvData, u16 a_sizeRecvData, u16 i2cId)
{
	int  i4RetValue = 0;

	spin_lock(&flashlight_private.lock);
	flashlight_i2c_client->addr = (i2cId >> 1);
	flashlight_i2c_client->ext_flag = (flashlight_i2c_client->ext_flag)&(~I2C_DMA_FLAG);
	spin_unlock(&flashlight_private.lock);

	i4RetValue = i2c_master_send(flashlight_i2c_client, a_pSendData, a_sizeSendData);
	if (i4RetValue != a_sizeSendData) {
		logI("[LC898212XD] I2C send failed!!, Addr = 0x%x\n", a_pSendData[0]);
	        return -1;
	}

	i4RetValue = i2c_master_recv(flashlight_i2c_client, (u8 *)a_pRecvData, a_sizeRecvData);
	if (i4RetValue != a_sizeRecvData) {
		logI("[LC898212XD] I2C read failed!! \n");
	        return -1;
	}

	return 0;
}
static int setReg6332(int reg, int gRegV, int gRegM, int gRegSh)
{
    pmic_config_interface(reg,gRegV,gRegM,gRegSh);
    logI("\nQQQS pmic_config_interface %d %d %d %d (0x%x 0x%x 0x%x 0x%x)\n",reg, gRegV, gRegM, gRegSh, reg, gRegV, gRegM, gRegSh);
    return 0;
}

int gBoostAudioUser=0;
int gBoostFlashUser=0;
int mt6332_OpenBoost4Audio(void)
{

    int userCnt;
    spin_lock_irq(&g_6332BoostLock);
    gBoostAudioUser=1;
    spin_unlock_irq(&g_6332BoostLock);
    userCnt = gBoostAudioUser+gBoostFlashUser;
    logI("\nQQQO mt6332_OpenBoost4Audio %d %d\n",gBoostAudioUser,gBoostFlashUser);
    if(userCnt==1)
        setReg6332(0x809e,	0x2,	0xffff,	0);


    return 0;
}
int mt6332_CloseBoost4Audio(void)
{
    int userCnt;
    spin_lock_irq(&g_6332BoostLock);
    gBoostAudioUser=0;
    spin_unlock_irq(&g_6332BoostLock);
    userCnt = gBoostAudioUser+gBoostFlashUser;



    logI("\nQQQO mt6332_CloseBoost4Audio %d %d line=%d\n",gBoostAudioUser,gBoostFlashUser,__LINE__);
    if(userCnt==0)
    {
        //    Ana_Set_Reg(0x853e,  0x0020, 0xffff); //  VSBST_CON10 , 1 enable
        setReg6332(0x853e,	0x0020,	0xffff,	0);
        msleep(30);
        setReg6332(0x809c,	0x2,	0xffff,	0);
    }
    return 0;
}
int mt6332_OpenBoost4Flash(void)
{
    int userCnt;
    spin_lock_irq(&g_6332BoostLock);
    gBoostFlashUser=1;
    spin_unlock_irq(&g_6332BoostLock);
    userCnt = gBoostAudioUser+gBoostFlashUser;
    logI("\nQQQO mt6332_OpenBoost4Flash %d %d\n",gBoostAudioUser,gBoostFlashUser);
    if(userCnt==1)
        setReg6332(0x809e,	0x2,	0xffff,	0);
        return 0;
}

int mt6332_CloseBoost4Flash(void)
{
    int userCnt;
    spin_lock_irq(&g_6332BoostLock);
    gBoostFlashUser=0;
    spin_unlock_irq(&g_6332BoostLock);
    userCnt = gBoostAudioUser+gBoostFlashUser;
    logI("\nQQQO mt6332_CloseBoost4Flash %d %d\n",gBoostAudioUser,gBoostFlashUser);
    if(userCnt==0)
        setReg6332(0x809c,	0x2,	0xffff,	0);
       return 0;

}


int globalInit(void)
{
	int i;
	int j;
	int k;
	logI("globalInit");
	for(i=0;i<e_Max_Sensor_Dev_Num;i++)
	for(j=0;j<e_Max_Strobe_Num_Per_Dev;j++)
	{
	    gLowBatDuty[i][j]=-1;
		g_strobePartId[i][j]=1;
		for(k=0;k<e_Max_Part_Num_Per_Dev;k++)
		{
			g_pFlashInitFunc[i][j][k]=0;
		}

	}
	return 0;
}

int	checkAndRelease(void)
{
	int i;
	int j;
	int k;
	for(i=0;i<e_Max_Sensor_Dev_Num;i++)
	for(j=0;j<e_Max_Strobe_Num_Per_Dev;j++)
	for(k=0;k<e_Max_Part_Num_Per_Dev;k++)
	{
		if(g_pFlashInitFunc[i][j][k]!=0)
		{
		    logI("checkAndRelease %d %d %d", i, j, k);
			g_pFlashInitFunc[i][j][k]->flashlight_release(0);
			g_pFlashInitFunc[i][j][k]=0;
		}
		else
        {
        }
	}
	return 0;
}
int getSensorDevIndex(int sensorDev)
{
	if(sensorDev==e_CAMERA_MAIN_SENSOR)
		return 0;
	else if(sensorDev==e_CAMERA_SUB_SENSOR)
		return 1;
	else if(sensorDev==e_CAMERA_MAIN_2_SENSOR)
		return 2;
	else
	{
		logE("sensorDev=%d is wrong",sensorDev);
		return -1;
	}
}
int getStrobeIndex(int strobeId)
{
	if(strobeId<1 ||  strobeId>2)
	{
		logE("strobeId=%d is wrong",strobeId);
		return -1;
	}
	return strobeId-1;
}
int getPartIndex(int partId)
{
	if(partId<1 ||  partId>2)
	{
		logE("partId=%d is wrong",partId);
		return -1;
	}
	return partId-1;
}

MINT32 default_flashlight_open(void *pArg) {
    logI("[default_flashlight_open] E ~");
    return 0;
}
MINT32 default_flashlight_release(void *pArg) {
    logI("[default_flashlight_release] E ~");
    return 0;
}
MINT32 default_flashlight_ioctl(unsigned int cmd, unsigned long arg) {
    int i4RetValue = 0;
    int iFlashType = (int)FLASHLIGHT_NONE;
    kdStrobeDrvArg kdArg;
	unsigned long copyRet;
    copyRet = copy_from_user(&kdArg , (void *)arg , sizeof(kdStrobeDrvArg));


    switch(cmd)
    {
        case FLASHLIGHTIOC_G_FLASHTYPE:
            iFlashType = FLASHLIGHT_NONE;
            kdArg.arg = iFlashType;
            if(copy_to_user((void __user *) arg , (void*)&kdArg , sizeof(kdStrobeDrvArg)))
            {
				logE("[FLASHLIGHTIOC_G_FLASHTYPE] ioctl copy to user failed ~");
                return -EFAULT;
            }
            break;
    	default :
    		logI("[default_flashlight_ioctl] ~");
    		break;
    }
    return i4RetValue;
}

FLASHLIGHT_FUNCTION_STRUCT	defaultFlashlightFunc=
{
	default_flashlight_open,
	default_flashlight_release,
	default_flashlight_ioctl,
};

UINT32 strobeInit_dummy(FLASHLIGHT_FUNCTION_STRUCT **pfFunc) {
    if (pfFunc!=NULL) {
        *pfFunc=&defaultFlashlightFunc;
    }
    return 0;
}
//========================================================================
static int setFlashDrv(int sensorDev, int strobeId)
{
	int partId;
	int sensorDevIndex;
    int strobeIndex;
	int partIndex;
	FLASHLIGHT_FUNCTION_STRUCT** ppF=0;
	sensorDevIndex = getSensorDevIndex(sensorDev);
	strobeIndex = getStrobeIndex(strobeId);
	if(sensorDevIndex<0 || strobeIndex<0 )
		return -1;
	partId = g_strobePartId[sensorDevIndex][strobeIndex];
	partIndex = getPartIndex(partId);
	if(partIndex<0)
		return -1;

	logI("setFlashDrv sensorDev=%d, strobeId=%d, partId=%d ~",sensorDev, strobeId, partId);
	
	ppF = &g_pFlashInitFunc[sensorDevIndex][strobeIndex][partIndex];
	if(sensorDev==e_CAMERA_MAIN_SENSOR)
	{
    	#if defined(DUMMY_FLASHLIGHT)
    	    strobeInit_dummy(ppF);
    	#else
        	if(strobeId==1)
        	{
                if(partId==1)
        			constantFlashlightInit(ppF);
        		else if(partId==2)
        			strobeInit_main_sid1_part2(ppF);
        	}
        	else if(strobeId==2)
        	{
        		if(partId==1)
        			strobeInit_main_sid2_part1(ppF);

        		else if(partId==2)
        			strobeInit_main_sid2_part2(ppF);
        	}
        #endif
	}
	else if(sensorDev==e_CAMERA_SUB_SENSOR)
	{
	    if(strobeId==1)
    	{
            if(partId==1)
    			subStrobeInit(ppF);
    		else if(partId==2)
    			strobeInit_sub_sid1_part2(ppF);
    	}
    	else if(strobeId==2)
    	{
    		if(partId==1)
    			strobeInit_sub_sid2_part1(ppF);
    		else if(partId==2)
    			strobeInit_sub_sid2_part2(ppF);
    	}
	}


	if((*ppF)!=0)
	{
		(*ppF)->flashlight_open(0);
		logI("setFlashDrv ok %d",__LINE__);
	}
	else
	{
		logE("set function pointer not found!!");
		return -1;
	}
	return 0;
}
static int decFlash(void)
{
    int i;
	int j;
	int k;
	int duty;
	for(i=0;i<e_Max_Sensor_Dev_Num;i++)
	for(j=0;j<e_Max_Strobe_Num_Per_Dev;j++)
	for(k=0;k<e_Max_Part_Num_Per_Dev;k++)
	{
		if(g_pFlashInitFunc[i][j][k]!=0)
		{
		    if(gLowBatDuty[i][j]!=-1)
		    {
		        duty = gLowBatDuty[i][j];
		        logI("decFlash i,j,k,duty %d %d %d %d", i, j, k, duty);
    			g_pFlashInitFunc[i][j][k]->flashlight_ioctl(FLASH_IOC_SET_DUTY, duty);
		    }
		}
	}
	return 0;
}




static int gLowPowerVbat=LOW_BATTERY_LEVEL_0;
static void Lbat_protection_powerlimit_flash(LOW_BATTERY_LEVEL level)
{
    logI("Lbat_protection level=%d ln=%d",level, __LINE__);
    if (level == LOW_BATTERY_LEVEL_0)
    {
        gLowPowerVbat=LOW_BATTERY_LEVEL_0;
    }
    else if (level == LOW_BATTERY_LEVEL_1)
    {
        decFlash();
        gLowPowerVbat=LOW_BATTERY_LEVEL_1;

    }
    else if(level == LOW_BATTERY_LEVEL_2)
    {
        decFlash();
        gLowPowerVbat=LOW_BATTERY_LEVEL_2;
    }
    else
    {
        //unlimit cpu and gpu
    }
}



static int gLowPowerPer=BATTERY_PERCENT_LEVEL_0;

static void bat_per_protection_powerlimit_flashlight(BATTERY_PERCENT_LEVEL level)
{
    logI("bat_per_protection level=%d ln=%d",level, __LINE__);
    if (level == BATTERY_PERCENT_LEVEL_0)
    {
        gLowPowerPer=BATTERY_PERCENT_LEVEL_0;
    }
    else if(level == BATTERY_PERCENT_LEVEL_1)
    {
        decFlash();
        gLowPowerPer=BATTERY_PERCENT_LEVEL_1;
    }
    else
    {
        //unlimit cpu and gpu
    }
}



//========================================================================


static long flashlight_ioctl_core(struct file *file, unsigned int cmd, unsigned long arg)
{
    int vTemp;
	int partId;
	int sensorDevIndex;
	int strobeIndex;
	int partIndex;
    int i4RetValue = 0;
	kdStrobeDrvArg kdArg;
	unsigned long copyRet;
    copyRet = copy_from_user(&kdArg , (void *)arg , sizeof(kdStrobeDrvArg));
	logI("flashlight_ioctl cmd=0x%x(nr=%d), senorDev=0x%x ledId=0x%x arg=0x%lx",cmd, _IOC_NR(cmd), kdArg.sensorDev, kdArg.strobeId ,(unsigned long)kdArg.arg);
	sensorDevIndex = getSensorDevIndex(kdArg.sensorDev);
	strobeIndex = getStrobeIndex(kdArg.strobeId);
	if(sensorDevIndex<0 || strobeIndex<0 )
		return -1;
	partId = g_strobePartId[sensorDevIndex][strobeIndex];
	partIndex = getPartIndex(partId);



    switch(cmd)
    {
        case FLASH_IOC_GET_PROTOCOL_VERSION:
            i4RetValue=1;
            break;
        case FLASH_IOC_IS_CHARGER_IN:
            vTemp = bat_is_charger_exist();
            logI("FLASH_IOC_IS_CHARGER_IN r=%d",vTemp);
            kdArg.arg = vTemp;
            if(copy_to_user((void __user *) arg , (void*)&kdArg , sizeof(kdStrobeDrvArg)))
			{
				logE("[FLASH_IOC_IS_CHARGER_IN] ioctl copy to user failed ~");
				return -EFAULT;
			}
            break;
        case FLASH_IOC_IS_OTG_USE:
            vTemp = mtk_is_host_mode();
            logI("FLASH_IOC_IS_CHARGER_IN r=%d",vTemp);
            kdArg.arg = vTemp;
            if(copy_to_user((void __user *) arg , (void*)&kdArg , sizeof(kdStrobeDrvArg)))
			{
				logE("[FLASH_IOC_IS_CHARGER_IN] ioctl copy to user failed ~");
				return -EFAULT;
			}
            break;
        case FLASH_IOC_IS_LOW_POWER:
            logI("FLASH_IOC_IS_LOW_POWER");
            {
                int isLow=0;
                if(gLowPowerPer!=BATTERY_PERCENT_LEVEL_0 || gLowPowerVbat!=LOW_BATTERY_LEVEL_0)
                    isLow=1;
                logI("FLASH_IOC_IS_LOW_POWER %d %d %d",gLowPowerPer,gLowPowerVbat,isLow);
                kdArg.arg = isLow;
                if(copy_to_user((void __user *) arg , (void*)&kdArg , sizeof(kdStrobeDrvArg)))
    			{
    				logE("[FLASH_IOC_IS_LOW_POWER] ioctl copy to user failed ~");
    				return -EFAULT;
    			}
		    }
			break;

        case FLASH_IOC_LOW_POWER_DETECT_START:
	        logI("FLASH_IOC_LOW_POWER_DETECT_START");
            gLowBatDuty[sensorDevIndex][strobeIndex]=kdArg.arg;
            break;

        case FLASH_IOC_LOW_POWER_DETECT_END:
   	        logI("FLASH_IOC_LOW_POWER_DETECT_END");
   	        gLowBatDuty[sensorDevIndex][strobeIndex]=-1;
            break;
        case FLASHLIGHTIOC_X_SET_DRIVER:
            i4RetValue = setFlashDrv(kdArg.sensorDev,  kdArg.strobeId);
            break;
		case FLASH_IOC_GET_PART_ID:
       	case FLASH_IOC_GET_MAIN_PART_ID:
		case FLASH_IOC_GET_SUB_PART_ID:
		case FLASH_IOC_GET_MAIN2_PART_ID:
			{
				int partId;
				partId = strobe_getPartId(kdArg.sensorDev, kdArg.strobeId);
				g_strobePartId[sensorDevIndex][strobeIndex]=partId;
				kdArg.arg = partId;
				if(copy_to_user((void __user *) arg , (void*)&kdArg , sizeof(kdStrobeDrvArg)))
				{
					logE("[FLASH_IOC_GET_PART_ID] ioctl copy to user failed ~");
					return -EFAULT;
				}
				logI("FLASH_IOC_GET_PART_ID line=%d partId=%d",__LINE__,partId);
			}
          	break;
		case FLASH_IOC_UNINIT:
			{
				FLASHLIGHT_FUNCTION_STRUCT *pF;
				if(partIndex<0) {
					logE("[FLASH_IOC_UNINIT] Invalid partIndex.\n");
					return -EFAULT;
				}
				pF = g_pFlashInitFunc[sensorDevIndex][strobeIndex][partIndex];
				if(pF!=0)
				{
					i4RetValue = pF->flashlight_release((void*)0);
					pF=0;

				}
				else
				{
					logE("[FLASH_IOC_UNINIT] function pointer is wrong ~");
				}
			}
			break;
		case FLASH_IOC_GET_FLASH_DRIVER_NAME_ID:
    	case FLASH_IOC_GET_PRE_ON_TIME_MS:
			{
				FLASHLIGHT_FUNCTION_STRUCT *pF;
				if(partIndex<0) {
					logE("[FLASH_IOC_GET_PRE_ON_TIME_MS] Invalid partIndex.\n");
					return -EFAULT;
				}
				pF = g_pFlashInitFunc[sensorDevIndex][strobeIndex][partIndex];
				if(pF!=0)
				{
					i4RetValue = pF->flashlight_ioctl(cmd,arg);
				}
				else
				{
					logE("[FLASH_IOC_GET_PRE_ON_TIME_MS] function pointer is wrong ~");
				}
			}
    		break;
		default :
			{
				FLASHLIGHT_FUNCTION_STRUCT *pF;
				if(partIndex<0) {
					logE("[default] Invalid partIndex.\n");
					return -EFAULT;
				}
				pF = g_pFlashInitFunc[sensorDevIndex][strobeIndex][partIndex];
				if(pF!=0)
				{
					i4RetValue = pF->flashlight_ioctl(cmd,kdArg.arg);
				}
				else
				{
					logE("[default] function pointer is wrong ~");
				}
			}
    		break;
    }
    return i4RetValue;
}


static long flashlight_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int err;
    //int dir;
    err = flashlight_ioctl_core(file, cmd, arg);
    //dir  = _IOC_DIR(cmd);
    //if(dir &_IOC_READ)
    {
      //  copy_to_user
    }
    return err;
}

#ifdef CONFIG_COMPAT

/*
static int compat_arg_struct_user32_to_kernel(
			struct StrobeDrvArg __user *data32,
			struct compat_StrobeDrvArg __user *data)
{
	compat_int_t i;
	int err=0;

	err |= get_user(i, &data32->sensorDev);
	err |= put_user(i, &data->sensorDev);

	err |= get_user(i, &data32->arg);
	err |= put_user(i, &data->arg);

	return err;
}

static int compat_arg_struct_kernel_to_user32(
			struct StrobeDrvArg __user *data32,
			struct compat_StrobeDrvArg __user *data)

{
	compat_int_t i;
	int err=0;

    err |= get_user(i, &data->sensorDev);
	err |= put_user(i, &data32->sensorDev);
	err |= get_user(i, &data->arg);
	err |= put_user(i, &data32->arg);

	return err;
}*/



static long my_ioctl_compat(struct file *filep, unsigned int cmd, unsigned long arg)
{
	int err;
	//int copyRet;
	kdStrobeDrvArg* pUObj;
	logI("flash my_ioctl_compat2 line=%d cmd=%d arg=%ld \n",__LINE__,cmd,arg);
	pUObj = compat_ptr(arg);

    /*
	kdStrobeDrvArg* pUObj;
	pUObj = compat_ptr(arg);
	kdStrobeDrvArg obj;
	copyRet = copy_from_user(&obj , (void *)pUObj , sizeof(kdStrobeDrvArg));
    logI("strobe arg %d %d %d\n", obj.sensorDev, obj.strobeId, obj.arg);
    obj.arg = 23411;
    copy_to_user((void __user *) arg , (void*)&obj , sizeof(kdStrobeDrvArg));
    */




	//data = compat_alloc_user_space(sizeof(*data));
	//if (sys_data == NULL)
		//    return -EFAULT;
    //err = compat_arg_struct_user32_to_kernel(data32, data);
    //arg2 = (unsigned long)data32;
    err = flashlight_ioctl_core(filep, cmd, (unsigned long)pUObj);

    return err;
}
#endif


static int flashlight_open(struct inode *inode, struct file *file)
{
	int i4RetValue = 0;
	static int bInited=0;
	if(bInited==0)
	{
		globalInit();
		bInited=1;
	}
    logI("[flashlight_open] E ~");
    return i4RetValue;
}

static int flashlight_release(struct inode *inode, struct file *file)
{
    logI("[flashlight_release] E ~");

	checkAndRelease();

    return 0;
}

static ssize_t sub_flash_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	//sprintf(buf, "%d\n", duty);
	return 1;
}

static ssize_t sub_flash_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	FLASHLIGHT_FUNCTION_STRUCT* pF = NULL;
	static int sub_strobe_init = 0;
	int duty;
	int ret = 0;

	sscanf(buf, "%d\n", &duty);

    	subStrobeInit(&pF);

	/* the max torch mode duty is 5 */
	if (duty > 5)
		duty = 5;

	/* when duty < 0, flash will be closed. */
	if (duty >= 0) {
		if (sub_strobe_init == 0) {
			ret = pF->flashlight_open(0);
			if (ret) {
				logE("[flash1_store] flash1 busy!");
				return ret;
			}

			/*
			 * Prevent system into suspend(deep sleep) when
			 * flash is being used, this is because flash power
			 * is from PMIC and PMIC will be sleep when system
			 * into suspend. we expect torch flash can be used
			 * even if system into sleep. 
			 */
			sub_strobe_init++;
		}

		ret = pF->flashlight_ioctl(FLASH_IOC_SET_TIME_OUT_TIME_MS, 0);
		ret += pF->flashlight_ioctl(FLASH_IOC_SET_DUTY, duty);
		ret += pF->flashlight_ioctl(FLASH_IOC_SET_ONOFF, 1);
		if (ret)
			logE("[flash1_store] flash1 ioctl failed!");
	} else {
		pF->flashlight_ioctl(FLASH_IOC_SET_ONOFF, 0);
		pF->flashlight_release(0);

		sub_strobe_init = 0;
	}

	return count;
}
static ssize_t flash1_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	//sprintf(buf, "%d\n", duty);
	return 1;
}

static ssize_t flash1_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	FLASHLIGHT_FUNCTION_STRUCT* pF = NULL;
	static int strobe1_init = 0;
	int duty;
	int ret = 0;

	sscanf(buf, "%d\n", &duty);

	constantFlashlightInit(&pF);

	/* the max torch mode duty is 5 */
	if (duty > 5)
		duty = 5;

	/* when duty < 0, flash will be closed. */
	if (duty >= 0) {
		if (strobe1_init == 0) {
			ret = pF->flashlight_open(0);
			if (ret) {
				logE("[flash1_store] flash1 busy!");
				return ret;
			}

			/*
			 * Prevent system into suspend(deep sleep) when
			 * flash is being used, this is because flash power
			 * is from PMIC and PMIC will be sleep when system
			 * into suspend. we expect torch flash can be used
			 * even if system into sleep. 
			 */
			strobe1_init++;
		}

		ret = pF->flashlight_ioctl(FLASH_IOC_SET_TIME_OUT_TIME_MS, 0);
		ret += pF->flashlight_ioctl(FLASH_IOC_SET_DUTY, duty);
		ret += pF->flashlight_ioctl(FLASH_IOC_SET_ONOFF, 1);
		if (ret)
			logE("[flash1_store] flash1 ioctl failed!");
	} else {
		pF->flashlight_ioctl(FLASH_IOC_SET_ONOFF, 0);
		pF->flashlight_release(0);

		strobe1_init = 0;
	}

	return count;
}

static ssize_t flash2_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	//sprintf(buf, "%d\n", duty);
	return 1;
}

static ssize_t flash2_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	FLASHLIGHT_FUNCTION_STRUCT* pF = NULL;
	static int strobe2_init = 0;
	int duty;
	int ret = 0;

	sscanf(buf, "%d\n", &duty);

        strobeInit_main_sid2_part1(&pF);

	/* the max torch mode duty is 5 */
	if (duty > 5)
		duty = 5;

	/* when duty < 0, flash will be closed. */
	if (duty >= 0) {
		if (strobe2_init == 0) {
			ret = pF->flashlight_open(0);
			if (ret) {
				logE("[flash2_store] flash2 busy!");
				return ret;
			}

			strobe2_init++;
		}

		ret = pF->flashlight_ioctl(FLASH_IOC_SET_TIME_OUT_TIME_MS, 0);
		ret += pF->flashlight_ioctl(FLASH_IOC_SET_DUTY, duty);
		ret += pF->flashlight_ioctl(FLASH_IOC_SET_ONOFF, 1);
		if (ret)
			logE("[flash2_store] flash2 ioctl failed!");
	} else {
		pF->flashlight_ioctl(FLASH_IOC_SET_ONOFF, 0);
		pF->flashlight_release(0);
		strobe2_init = 0;
	}

	return count;
}

#ifdef WIN32
int fl_open(struct inode *inode, struct file *file)
{
	return flashlight_open(inode, file);
}
int fl_release(struct inode *inode, struct file *file)
{
	return flashlight_release(inode, file);
}
long fl_ioctrl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return flashlight_ioctl(file, cmd, arg);
}

#else
//========================================================================
//========================================================================
//========================================================================
/* Kernel interface */
static struct file_operations flashlight_fops = {
    .owner      = THIS_MODULE,
    .unlocked_ioctl      = flashlight_ioctl,
    .open       = flashlight_open,
    .release    = flashlight_release,
#ifdef CONFIG_COMPAT
    .compat_ioctl = my_ioctl_compat,
#endif
};

static struct device_attribute dev_attr_flash1 = {
	.attr = {.name = "flash1", .mode = 0644},
	.show = flash1_show,
	.store = flash1_store,
};

static struct device_attribute dev_attr_flash2 = {
	.attr = {.name = "flash2", .mode = 0644},
	.show = flash2_show,
	.store = flash2_store,
};

static struct device_attribute dev_attr_sub_flash = {
	.attr = {.name = "sub_flash", .mode = 0644},
	.show = sub_flash_show,
	.store = sub_flash_store,
};

//========================================================================
// Driver interface
//========================================================================
static struct class *flashlight_class = NULL;
static struct device *flashlight_device = NULL;
static dev_t flashlight_devno;
static struct cdev flashlight_cdev;
//========================================================================
#define ALLOC_DEVNO
static int flashlight_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret = 0, err = 0;

    flashlight_i2c_client = client;

    logI("[flashlight_probe] start ~");

#ifdef ALLOC_DEVNO
    ret = alloc_chrdev_region(&flashlight_devno, 0, 1, FLASHLIGHT_DEVNAME);
    if (ret) {
        logE("[flashlight_probe] alloc_chrdev_region fail: %d ~", ret);
        goto flashlight_probe_error;
    } else {
        logI("[flashlight_probe] major: %d, minor: %d ~", MAJOR(flashlight_devno), MINOR(flashlight_devno));
    }
    cdev_init(&flashlight_cdev, &flashlight_fops);
    flashlight_cdev.owner = THIS_MODULE;
    err = cdev_add(&flashlight_cdev, flashlight_devno, 1);
    if (err) {
        logE("[flashlight_probe] cdev_add fail: %d ~", err);
        goto flashlight_probe_error;
    }
#else
    #define FLASHLIGHT_MAJOR 242
    ret = register_chrdev(FLASHLIGHT_MAJOR, FLASHLIGHT_DEVNAME, &flashlight_fops);
    if (ret != 0) {
        logE("[flashlight_probe] Unable to register chardev on major=%d (%d) ~", FLASHLIGHT_MAJOR, ret);
        return ret;
    }
    flashlight_devno = MKDEV(FLASHLIGHT_MAJOR, 0);
#endif


    flashlight_class = class_create(THIS_MODULE, "flashlightdrv");
    if (IS_ERR(flashlight_class)) {
        logE("[flashlight_probe] Unable to create class, err = %d ~", (int)PTR_ERR(flashlight_class));
        goto flashlight_probe_error;
    }

    flashlight_device = device_create(flashlight_class, NULL, flashlight_devno, NULL, FLASHLIGHT_DEVNAME);
    if(NULL == flashlight_device){
        logE("[flashlight_probe] device_create fail ~");
        goto flashlight_probe_error;
    }

	device_create_file(flashlight_device, &dev_attr_flash1);
	device_create_file(flashlight_device, &dev_attr_flash2);
	device_create_file(flashlight_device, &dev_attr_sub_flash);

    //initialize members
    spin_lock_init(&flashlight_private.lock);
    init_waitqueue_head(&flashlight_private.read_wait);
    //init_MUTEX(&flashlight_private.sem);
    sema_init(&flashlight_private.sem, 1);

    logI("[flashlight_probe] Done ~");
    return 0;

flashlight_probe_error:
#ifdef ALLOC_DEVNO
    if (err == 0)
        cdev_del(&flashlight_cdev);
    if (ret == 0)
        unregister_chrdev_region(flashlight_devno, 1);
#else
    if (ret == 0)
        unregister_chrdev(MAJOR(flashlight_devno), FLASHLIGHT_DEVNAME);
#endif
    return -1;
}

static int flashlight_i2c_remove(struct i2c_client *client)
{

    logI("[flashlight_probe] start\n");

#ifdef ALLOC_DEVNO
    cdev_del(&flashlight_cdev);
    unregister_chrdev_region(flashlight_devno, 1);
#else
    unregister_chrdev(MAJOR(flashlight_devno), FLASHLIGHT_DEVNAME);
#endif
    device_destroy(flashlight_class, flashlight_devno);
    class_destroy(flashlight_class);

    logI("[flashlight_probe] Done ~");
    return 0;
}


static const struct i2c_device_id flashlight_i2c_id[] = {{FL_DRVNAME,0},{}};
struct i2c_driver flashlight_i2c_driver = {
    .probe = flashlight_i2c_probe,
    .remove = flashlight_i2c_remove,
    .driver.name = FL_DRVNAME,
    .id_table = flashlight_i2c_id,
};  

static int flashlight_probe(struct platform_device *dev)
{
    return i2c_add_driver(&flashlight_i2c_driver);
}

static int flashlight_remove(struct platform_device *dev)
{
    i2c_del_driver(&flashlight_i2c_driver);
    return 0;
}

static struct platform_driver flashlight_platform_driver =
{
    .probe      = flashlight_probe,
    .remove     = flashlight_remove,
    .driver     = {
        .name = FLASHLIGHT_DEVNAME,
		.owner	= THIS_MODULE,
    },
};

static struct platform_device flashlight_platform_device = {
    .name = FLASHLIGHT_DEVNAME,
    .id = 0,
    .dev = {
    }
};

static int __init flashlight_init(void)
{
    int ret = 0;
    logI("[flashlight_probe] start ~");

    i2c_register_board_info(FL_I2C_BUSNUM, &kd_flashlight_dev, 1);

	ret = platform_device_register (&flashlight_platform_device);
	if (ret) {
        logE("[flashlight_probe] platform_device_register fail ~");
        return ret;
	}

    ret = platform_driver_register(&flashlight_platform_driver);
	if(ret){
		logE("[flashlight_probe] platform_driver_register fail ~");
		return ret;
	}

	register_low_battery_notify(&Lbat_protection_powerlimit_flash, LOW_BATTERY_PRIO_FLASHLIGHT);
	register_battery_percent_notify(&bat_per_protection_powerlimit_flashlight, BATTERY_PERCENT_PRIO_FLASHLIGHT);
	wake_lock_init(&flash_wake_lock, WAKE_LOCK_SUSPEND, "flash_lock_wakelock");


	logI("[flashlight_probe] done! ~");
    return ret;
}

static void __exit flashlight_exit(void)
{
    logI("[flashlight_probe] start ~");
    platform_driver_unregister(&flashlight_platform_driver);
    //to flush work queue
    //flush_scheduled_work();
    logI("[flashlight_probe] done! ~");
}

//========================================================
module_init(flashlight_init);
module_exit(flashlight_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jackie Su <jackie.su@mediatek.com>");
MODULE_DESCRIPTION("Flashlight control Driver");

#endif
