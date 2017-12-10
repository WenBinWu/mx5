/*****************************************************************************
 *
 * Filename:
 * ---------
 *   IMX220liteonmipi_Sensor.c
 *
 * Project:
 * --------
 *   ALPS
 *
 * Description:
 * ------------
 *   Source code of Sensor driver
 *
 *
 *------------------------------------------------------------------------------
 * Upper this line, this part is controlled by CC/CQ. DO NOT MODIFY!!
 *============================================================================
 ****************************************************************************/

#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/xlog.h>

#include <mach/mt_chip.h>

#include "kd_camera_hw.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"

#include "imx220liteonmipi_Sensor.h"

/****************************Modify Following Strings for Debug****************************/
#define PFX "imx220_camera_sensor_liteon"
#define LOG_1 LOG_INF("IMX220,MIPI 4LANE\n")
#define LOG_2 LOG_INF("preview 1280*960@30fps,864Mbps/lane; video 1280*960@30fps,864Mbps/lane; capture 5M@30fps,864Mbps/lane\n")
/****************************   Modify end    *******************************************/

#define LOG_INF(format, args...)    xlog_printk(ANDROID_LOG_INFO   , PFX, "[%s] " format, __FUNCTION__, ##args)

static DEFINE_SPINLOCK(imgsensor_drv_lock);

u8 *liteon_otp_buf;

extern int platform_match;
extern int back_cam_id;

#define	MAX_READ_WRITE_SIZE	8
#define	OTP_LSC_SIZE	1945
#define	OTP_START_ADDR	0x0000
#define	E2PROM_WRITE_ID	0xA8

struct imx220_liteon_write_buffer {
	u8 addr[2];
	u8 data[MAX_READ_WRITE_SIZE];
};

static kal_uint8 mode_change = 0;

static imgsensor_info_struct imgsensor_info = {
    .sensor_id = IMX220_LITEON_SENSOR_ID & 0x0FFF,

    .checksum_value = 0xad7fa572,

    .pre = {
		.pclk = 355200000,				//record different mode's pclk
		.linelength = 5904,				//record different mode's linelength
		.framelength = 2016,			//record different mode's framelength
		.startx = 0,					//record different mode's startx of grabwindow
		.starty = 0,					//record different mode's starty of grabwindow
		.grabwindow_width = 2624,		//record different mode's width of grabwindow
		.grabwindow_height = 1968,		//record different mode's height of grabwindow
		/*	 following for MIPIDataLowPwr2HighSpeedSettleDelayCount by different scenario	*/
		.mipi_data_lp2hs_settle_dc = 60,//unit , ns
		/*	 following for GetDefaultFramerateByScenario()	*/
		.max_framerate = 300,	
    },
    .pre1 = {
		.pclk = 355200000,				//record different mode's pclk
		.linelength = 5904,				//record different mode's linelength
		.framelength = 2000,			//record different mode's framelength
		.startx = 0,					//record different mode's startx of grabwindow
		.starty = 0,					//record different mode's starty of grabwindow
		.grabwindow_width = 2336,		//record different mode's width of grabwindow
		.grabwindow_height = 1760,		//record different mode's height of grabwindow
		/*	 following for MIPIDataLowPwr2HighSpeedSettleDelayCount by different scenario	*/
		.mipi_data_lp2hs_settle_dc = 60,//unit , ns
		/*	 following for GetDefaultFramerateByScenario()	*/
		.max_framerate = 300,
    },
    .zsd = {
		.pclk = 390400000,
		.linelength = 5904,
		.framelength = 2640,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 4608,
		.grabwindow_height = 2592,
		.mipi_data_lp2hs_settle_dc = 60,//unit , ns
		.max_framerate = 250,
    },
    .cap = {
		.pclk = 355200000,
		.linelength = 5904,
		.framelength = 3984,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 5248,
		.grabwindow_height = 3936,
		.mipi_data_lp2hs_settle_dc = 60,//unit , ns
		.max_framerate = 150,
    },
    .slim_cap = {
		.pclk = 355200000,				//record different mode's pclk
		.linelength = 5904,				//record different mode's linelength
		.framelength = 2016,			//record different mode's framelength
		.startx = 0,					//record different mode's startx of grabwindow
		.starty = 0,					//record different mode's starty of grabwindow
		.grabwindow_width = 2624,		//record different mode's width of grabwindow
		.grabwindow_height = 1968,		//record different mode's height of grabwindow
		/*	 following for MIPIDataLowPwr2HighSpeedSettleDelayCount by different scenario	*/
		.mipi_data_lp2hs_settle_dc = 85,//unit , ns
		/*	 following for GetDefaultFramerateByScenario()	*/
		.max_framerate = 300,	
    },
    .cap1 = {                           //capture for PIP 24fps relative information, capture1 mode must use same framelength, linelength with Capture mode for shutter calculate
		.pclk = 355200000,
		.linelength = 5904,
		.framelength = 3600,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 4672,
		.grabwindow_height = 3504,
		.mipi_data_lp2hs_settle_dc = 60,//unit , ns
		.max_framerate = 160,
    },
    .hd_4k_video = {
		.pclk = 390400000,
		.linelength = 5904,
		.framelength = 2224,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 3840,
		.grabwindow_height = 2176,
		.mipi_data_lp2hs_settle_dc = 85,//unit , ns
		.max_framerate = 300,
    },
    /* 1080P video without video eis */
    .normal_video = {
		.pclk = 220000000,
		.linelength = 5904,
		.framelength = 1236,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 1920,
		.grabwindow_height = 1088,
		.mipi_data_lp2hs_settle_dc = 85,//unit , ns
		.max_framerate = 300,
    },
    .hs_video = {
		.pclk = 480000000,
		.linelength = 5904,
		.framelength = 792,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 1280,
		.grabwindow_height = 720,
		.mipi_data_lp2hs_settle_dc = 60,//unit ns, reduce LP2HS settle time for high speed.
		.max_framerate = 1020,
    },
    .slim_video = {
		.pclk = 184000000,
		.linelength = 5904,
		.framelength = 1036,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 1312,
		.grabwindow_height = 984,
		.mipi_data_lp2hs_settle_dc = 85,//unit , ns
		.max_framerate = 300,
    },
    .hd_gis_video = {
		.pclk = 240000000,
		.linelength = 5904,
		.framelength = 1352,
		.startx = 0,
		.starty = 0,
		.grabwindow_width = 2112,
		.grabwindow_height = 1188,
		.mipi_data_lp2hs_settle_dc = 85,//unit , ns
		.max_framerate = 300,
    },
    .margin = 8,            //sensor framelength & shutter margin
    .min_shutter = 1,       //min shutter
    .max_frame_length = 0xfffe,//max framelength by sensor register's limitation
    .ae_shut_delay_frame = 0,   //shutter delay frame for AE cycle, 2 frame with ispGain_delay-shut_delay=2-0=2
    .ae_sensor_gain_delay_frame = 0,//sensor gain delay frame for AE cycle,2 frame with ispGain_delay-sensor_gain_delay=2-0=2
    .ae_ispGain_delay_frame = 2,//isp gain delay frame for AE cycle
    .ihdr_support = 0,    //1, support; 0,not support
    .ihdr_le_firstline = 0,  //1,le first ; 0, se first
    .sensor_mode_num = 9,     //support sensor mode num

    .cap_delay_frame = 0,       //enter capture delay frame num
    .pre_delay_frame = 0,       //enter preview delay frame num
    .video_delay_frame = 0,     //enter video delay frame num
    .hd_gis_video_delay_frame = 0,     //enter 1080P gis video delay frame num
    .hs_video_delay_frame = 0,  //enter high speed video  delay frame num
    .slim_video_delay_frame = 0,//enter slim video delay frame num
    .zsd_delay_frame = 0,       //enter zsd delay frame num
    .hd_4k_video_delay_frame = 0,       //enter hd_4k_video delay frame num
    .slim_cap_delay_frame = 0,       //enter slim_cap delay frame num

    .isp_driving_current = ISP_DRIVING_8MA,
    .sensor_interface_type = SENSOR_INTERFACE_TYPE_MIPI,//sensor_interface_type
    .mipi_sensor_type = MIPI_OPHY_NCSI2, //0,MIPI_OPHY_NCSI2;  1,MIPI_OPHY_CSI2
    .mipi_settle_delay_mode = MIPI_SETTLEDELAY_MANUAL,//0,MIPI_SETTLEDELAY_AUTO; 1,MIPI_SETTLEDELAY_MANUAL
    .sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_B,//sensor output first pixel color
    .mclk = 24,//mclk value, suggest 24 or 26 for 24Mhz or 26Mhz
    .mipi_lane_num = SENSOR_MIPI_4_LANE,
    .i2c_addr_table = {0x20, 0x34, 0xff},
};


static imgsensor_struct imgsensor = {
    .mirror = IMAGE_NORMAL,             //mirrorflip information
    .sensor_mode = IMGSENSOR_MODE_INIT, //IMGSENSOR_MODE enum value,record current sensor mode,such as: INIT, Preview, Capture, Video,High Speed Video, Slim Video
    .shutter = 0x3D0,                   //current shutter
    .gain = 0x100,                      //current gain
    .dummy_pixel = 0,                   //current dummypixel
    .dummy_line = 0,                    //current dummyline
    .current_fps = 300,  //full size current fps : 24fps for PIP, 30fps for Normal or ZSD
    .autoflicker_en = KAL_FALSE,  //auto flicker enable: KAL_FALSE for disable auto flicker, KAL_TRUE for enable auto flicker
    .test_pattern = KAL_FALSE,      //test pattern mode or not. KAL_FALSE for in test pattern mode, KAL_TRUE for normal output
    .current_scenario_id = MSDK_SCENARIO_ID_CAMERA_PREVIEW,//current scenario id
    .ihdr_en = 0, //sensor need support LE, SE with HDR feature
    .i2c_write_id = 0x20,
};

static kal_uint16 sensor_gain_mapping[71][2] = {
    {71  ,25 },
    {76  ,42 },
    {83  ,59 },
    {89  ,73 },
    {96  ,85 },
    {102 ,96 },
    {108 ,105},
    {115 ,114},
    {121 ,121},
    {128 ,128},
    {134 ,134},
    {140 ,140},
    {147 ,145},
    {153 ,149},
    {160 ,154},
    {166 ,158},
    {172 ,161},
    {179 ,164},
    {185 ,168},
    {192 ,171},
    {200 ,174},
    {208 ,177},
    {216 ,180},
    {224 ,183},
    {232 ,185},
    {240 ,188},
    {248 ,190},
    {256 ,192},
    {264 ,194},
    {272 ,196},
    {280 ,197},
    {288 ,199},
    {296 ,201},
    {304 ,202},
    {312 ,203},
    {320 ,205},
    {328 ,206},
    {336 ,207},
    {344 ,208},
    {352 ,209},
    {360 ,210},
    {368 ,211},
    {376 ,212},
    {384 ,213},
    {390 ,214},
    {399 ,215},
    {409 ,216},
    {419 ,217},
    {431 ,218},
    {442 ,219},
    {455 ,220},
    {467 ,221},
    {481 ,222},
    {496 ,223},
    {512 ,224},
    {528 ,225},
    {546 ,226},
    {559 ,227},
    {573 ,228},
    {590 ,229},
    {608 ,230},
    {630 ,231},
    {653 ,232},
    {679 ,233},
    {711 ,234},
    {743 ,235},
    {775 ,236},
    {816 ,237},
    {864 ,238},
    {916 ,239},
    {960 ,240}
};

/* Sensor output window information */
static SENSOR_WINSIZE_INFO_STRUCT imgsensor_winsize_info[11] =
{{ 5248, 3936, 0000, 0000, 5248, 3936, 2624, 1968, 0000, 0000, 2624, 1968, 0000, 0000, 2624, 1968}, // Preview 
 { 5248, 3936, 0000, 0000, 5248, 3936, 5248, 3936, 0000, 0000, 5248, 3936, 0000, 0000, 5248, 3936}, // capture 
 { 5248, 3936, 704, 888, 3840, 2176, 1920, 1088, 0000, 0000, 1920, 1088, 0000, 0000, 1920, 1088}, // video 
// { 5248, 3936, 512, 780, 4224, 2376, 2112, 1188, 0000, 0000, 2112, 1188, 0000, 0000, 2112, 1188}, // video 
 { 5248, 3936, 64, 528, 5120, 2880, 1280, 720, 0000, 0000, 1280, 720, 0000, 0000, 1280,  720}, //hight speed video 
 { 5248, 3936, 0000, 0000, 5248, 3936, 1312, 984, 0000, 0000, 1312, 984, 0000, 0000, 1312,  984},  //slim video
// { 5248, 3936, 584, 744, 4080, 2448, 4080, 2448, 0000, 0000, 4080, 2448, 0000, 0000, 4080,  2448},  //zsd
// { 5248, 3936, 374, 616, 4480, 2688, 4480, 2688, 0000, 0000, 4480, 2688, 0000, 0000, 4480,  2688},  //zsd
 { 5248, 3936, 320, 672, 4608, 2592, 4608, 2592, 0000, 0000, 4608, 2592, 0000, 0000, 4608,  2592},  //zsd
 { 5248, 3936, 0000, 0000, 5248, 3936, 2624, 1968, 0000, 0000, 2624, 1968, 0000, 0000, 2624,  1968}, //slim capture
 { 5248, 3936, 704, 888, 3840, 2176, 3840, 2176, 0000, 0000, 3840, 2176, 0000, 0000, 3840,  2176},// video4k
 { 5248, 3936, 512, 780, 4224, 2376, 2112, 1188, 0000, 0000, 2112, 1188, 0000, 0000, 2112,  1188},//1080P gis video
 { 5248, 3936, 288, 216, 4672, 3504, 4672, 3504, 0000, 0000, 4672, 3504, 0000, 0000, 4672, 3504},// capture 
 { 5248, 3936, 288, 208, 4672, 3520, 2336, 1760, 0000, 0000, 2336, 1760, 0000, 0000, 2336, 1760},};// preview


static bool cpu_chip_is_mt6795m(void)
{
	unsigned int chip_code = mt_get_chip_info(CHIP_INFO_FUNCTION_CODE);
	LOG_INF("cpu chip ID:%d====\n", chip_code);
	/*
	 * code = mt_get_chip_info(CHIP_INFO_FUNCTION_CODE)
	 * code == 1, 2, 3, 4, 5; CPU chip is MT6795M;
	 * code == 6, 7, 8, 9, 10; CPU chip is MT6795;
	 * code == 11, 12, 13, 14, 15; CPU chip is MT6795T;
	 * code == others; CPU chip is MT6795;
	 * MT6795M only support 16M capture;
	 * MT6795/MT6795T can support 21M capture;
	 */
	if (chip_code <= 5)
		return true;
	else
		return false;
}

static kal_uint16 read_cmos_sensor(kal_uint32 addr)
{
    kal_uint16 get_byte=0;

    char pu_send_cmd[2] = {(char)(addr >> 8), (char)(addr & 0xFF) };
    iReadRegI2C(pu_send_cmd, 2, (u8*)&get_byte, 1, imgsensor.i2c_write_id);

    return get_byte;
}

static void write_cmos_sensor(kal_uint32 addr, kal_uint32 para)
{
    char pu_send_cmd[3] = {(char)(addr >> 8), (char)(addr & 0xFF), (char)(para & 0xFF)};
    iWriteRegI2C(pu_send_cmd, 3, imgsensor.i2c_write_id);
}

static int imx220_read_otp(u16 addr, u8 *buf)
{
	int ret = 0;
	u8 pu_send_cmd[2] = {(u8)(addr >> 8), (u8)(addr & 0xFF)};

	ret = iReadRegI2C(pu_send_cmd, 2, (u8*)buf, 1, E2PROM_WRITE_ID);
	if (ret < 0)
		LOG_INF("read data from liteon otp e2prom failed!\n");

	return ret;
}

static void imx220_read_otp_lsc(u16 addr, u8 *otp_buf)
{
	int i;
	int ret;
	u8 pu_send_cmd[2];

	for (i = 0; i < OTP_LSC_SIZE; i += MAX_READ_WRITE_SIZE) {
		pu_send_cmd[0] = (u8)(addr >> 8);
		pu_send_cmd[1] = (u8)(addr & 0xFF);

		if (i + MAX_READ_WRITE_SIZE > OTP_LSC_SIZE)
			ret = iReadRegI2C(pu_send_cmd, 2, (u8 *)(otp_buf + i), (OTP_LSC_SIZE - i), E2PROM_WRITE_ID);
		else
			ret = iReadRegI2C(pu_send_cmd, 2, (u8 *)(otp_buf + i), MAX_READ_WRITE_SIZE, E2PROM_WRITE_ID);

		if (ret < 0)
			LOG_INF("read lsc table from liteon otp e2prom failed!\n");

		addr += MAX_READ_WRITE_SIZE;
	}
}

static void imx220_write_otp_sensor(u16 addr, u8 *otp_buf)
{
	struct imx220_liteon_write_buffer buffer;
	int i;
	int ret;

	for (i = 0; i < OTP_LSC_SIZE; i += MAX_READ_WRITE_SIZE) {
		buffer.addr[0] = (u8)(addr >> 8);
		buffer.addr[1] = (u8)(addr & 0xFF);

		if (i + MAX_READ_WRITE_SIZE > OTP_LSC_SIZE) {
			memcpy(buffer.data, otp_buf + i, (OTP_LSC_SIZE - i));
			ret = iBurstWriteReg((u8 *)&buffer, (OTP_LSC_SIZE - i + 2), imgsensor.i2c_write_id);
		} else {
			memcpy(buffer.data, otp_buf + i, MAX_READ_WRITE_SIZE);
			ret = iBurstWriteReg((u8 *)&buffer, MAX_READ_WRITE_SIZE + 2, imgsensor.i2c_write_id);
		}

		if (ret < 0)
			LOG_INF("write lsc table into sensor failed!\n");

		addr += MAX_READ_WRITE_SIZE;
	}
}

static void set_dummy()
{
    LOG_INF("dummyline = %d, dummypixels = %d \n", imgsensor.dummy_line, imgsensor.dummy_pixel);
    /* you can set dummy by imgsensor.dummy_line and imgsensor.dummy_pixel, or you can set dummy by imgsensor.frame_length and imgsensor.line_length */
    write_cmos_sensor(0x0104, 0x01);
    write_cmos_sensor(0x0340, (imgsensor.frame_length >> 8) & 0xFF);
    write_cmos_sensor(0x0341, imgsensor.frame_length & 0xFF);
    write_cmos_sensor(0x0342, (imgsensor.line_length >> 8) & 0xFF);
    write_cmos_sensor(0x0343, imgsensor.line_length & 0xFF);
    write_cmos_sensor(0x0104, 0x00);

}   /*  set_dummy  */

static signed char get_sensor_temp()
{
	signed char temp = 0;
	/* temp enable */
	write_cmos_sensor(0x0220, 0x01);
	temp = read_cmos_sensor(0x0222);

#if 0
	if (((unsigned char)temp > 0x80) && ((unsigned char)temp < 0xED))
		temp = 0xEC;	//temperature -20 degree.
	else if (((unsigned char)temp > 0x4F) && ((unsigned char)temp < 0x80))
		temp = 0x50;	//temperature 80 degree.
#endif
	return temp;
}

static kal_uint32 return_sensor_id()
{
    return ((read_cmos_sensor(0x0016) << 8) | read_cmos_sensor(0x0017));
}
static void set_max_framerate(UINT16 framerate,kal_bool min_framelength_en)
{
    kal_int16 dummy_line;
    kal_uint32 frame_length = imgsensor.frame_length;
    //unsigned long flags;

    LOG_INF("framerate = %d, min framelength should enable? \n", framerate,min_framelength_en);

    frame_length = imgsensor.pclk / framerate * 10 / imgsensor.line_length;
    spin_lock(&imgsensor_drv_lock);
    imgsensor.frame_length = (frame_length > imgsensor.min_frame_length) ? frame_length : imgsensor.min_frame_length;
    imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
    //dummy_line = frame_length - imgsensor.min_frame_length;
    //if (dummy_line < 0)
        //imgsensor.dummy_line = 0;
    //else
        //imgsensor.dummy_line = dummy_line;
    //imgsensor.frame_length = frame_length + imgsensor.dummy_line;
    if (imgsensor.frame_length > imgsensor_info.max_frame_length)
    {
        imgsensor.frame_length = imgsensor_info.max_frame_length;
        imgsensor.dummy_line = imgsensor.frame_length - imgsensor.min_frame_length;
    }
    if (min_framelength_en)
        imgsensor.min_frame_length = imgsensor.frame_length;
    spin_unlock(&imgsensor_drv_lock);
    set_dummy();
}   /*  set_max_framerate  */


static void write_shutter(kal_uint32 shutter)
{
	kal_uint16 realtime_fps = 0;
	kal_uint32 frame_length = 0;
	kal_uint32 line_length=0;
	kal_uint32 origi_shutter;

	LOG_INF("enter shutter =%d \n", shutter);
	/* 0x3500, 0x3501, 0x3502 will increase VBLANK to get exposure larger than frame exposure */
	/* AE doesn't update sensor gain at capture mode, thus extra exposure lines must be updated here. */

	/* if shutter bigger than frame_length, should extend frame length first */
	spin_lock(&imgsensor_drv_lock);
	if (shutter > imgsensor.min_frame_length - imgsensor_info.margin)
		imgsensor.frame_length = shutter + imgsensor_info.margin;
	else
		imgsensor.frame_length = imgsensor.min_frame_length;
	if (imgsensor.frame_length > imgsensor_info.max_frame_length)
		imgsensor.frame_length = imgsensor_info.max_frame_length;
	spin_unlock(&imgsensor_drv_lock);
	shutter = (shutter < imgsensor_info.min_shutter) ? imgsensor_info.min_shutter : shutter;

	/* Just should be called in capture case with long exposure */
        if(shutter == 1)
        {
        line_length = 5904;
	shutter = 2;
        
        write_cmos_sensor(0x0100, 0x00);	/* stream off */
        
        /* Clock Setting */
	write_cmos_sensor(0x0301, 0x0A);
	write_cmos_sensor(0x0303, 0x01);
	write_cmos_sensor(0x0304, 0x03);
	write_cmos_sensor(0x0305, 0x06);
	write_cmos_sensor(0x0306, 0x00);
	write_cmos_sensor(0x0307, 0x6F);
	write_cmos_sensor(0x0309, 0x0A);
	write_cmos_sensor(0x030B, 0x01);
	write_cmos_sensor(0x030C, 0x00);
	write_cmos_sensor(0x030D, 0xD0);
        
        /* Global Timing Setting */
	write_cmos_sensor(0x0830, 0x00);
	write_cmos_sensor(0x0831, 0x6F);
	write_cmos_sensor(0x0832, 0x00);
	write_cmos_sensor(0x0833, 0x2F);
	write_cmos_sensor(0x0834, 0x00);
	write_cmos_sensor(0x0835, 0x57);
	write_cmos_sensor(0x0836, 0x00);
	write_cmos_sensor(0x0837, 0x2F);
	write_cmos_sensor(0x0838, 0x00);
	write_cmos_sensor(0x0839, 0x2F);
	write_cmos_sensor(0x083A, 0x00);
	write_cmos_sensor(0x083B, 0x2F);
	write_cmos_sensor(0x083C, 0x00);
	write_cmos_sensor(0x083D, 0xBF);
	write_cmos_sensor(0x083E, 0x00);
	write_cmos_sensor(0x083F, 0x27);
	write_cmos_sensor(0x0842, 0x00);
	write_cmos_sensor(0x0843, 0x0F);
	write_cmos_sensor(0x0844, 0x00);
	write_cmos_sensor(0x0845, 0x37);
	write_cmos_sensor(0x0847, 0x02);

	/*Line Length Setting*/
        write_cmos_sensor(0x0342, (line_length >> 8) & 0xFF);
        write_cmos_sensor(0x0343, line_length & 0xFF);
        /* stream on */
        write_cmos_sensor(0x0100, 0x01);

	spin_lock(&imgsensor_drv_lock);
	if (imgsensor.sensor_mode == IMGSENSOR_MODE_SLIM_CAPTURE) {
		imgsensor.pclk = imgsensor_info.slim_cap.pclk;
		imgsensor.line_length = imgsensor_info.slim_cap.linelength;
		imgsensor.frame_length = imgsensor_info.slim_cap.framelength;  
	} else {
		imgsensor.pclk = imgsensor_info.cap.pclk;
		imgsensor.line_length = imgsensor_info.cap.linelength;
		imgsensor.frame_length = imgsensor_info.cap.framelength;
	}
	spin_unlock(&imgsensor_drv_lock);
        
        }

        if(shutter > (imgsensor_info.max_frame_length - imgsensor_info.margin))
        {
	/* limit the max long exposure to 20s */
        if(shutter > 1200000)
		shutter = 1200000;

	line_length = 5904;

        /* Clock Setting */
        write_cmos_sensor(0x0301, 0x06);
        write_cmos_sensor(0x0303, 0x02);
        write_cmos_sensor(0x0304, 0x01);
        write_cmos_sensor(0x0305, 0x06);
        write_cmos_sensor(0x0306, 0x00);
        write_cmos_sensor(0x0307, 0x0F);
        write_cmos_sensor(0x0309, 0x0A);
        write_cmos_sensor(0x030B, 0x01);
        write_cmos_sensor(0x030C, 0x00);
        write_cmos_sensor(0x030D, 0x55);
        
        /*Global Timing Setting 	
        Address value*/
        write_cmos_sensor(0x0830, 0x00);
        write_cmos_sensor(0x0831, 0x57);
        write_cmos_sensor(0x0832, 0x00);
        write_cmos_sensor(0x0833, 0x1F);
        write_cmos_sensor(0x0834, 0x00);
        write_cmos_sensor(0x0835, 0x37);
        write_cmos_sensor(0x0836, 0x00);
        write_cmos_sensor(0x0837, 0x1F);
        write_cmos_sensor(0x0838, 0x00);
        write_cmos_sensor(0x0839, 0x17);
        write_cmos_sensor(0x083A, 0x00);
        write_cmos_sensor(0x083B, 0x17);
        write_cmos_sensor(0x083C, 0x00);
        write_cmos_sensor(0x083D, 0x77);
        write_cmos_sensor(0x083E, 0x00);
        write_cmos_sensor(0x083F, 0x17);
        write_cmos_sensor(0x0842, 0x00);
        write_cmos_sensor(0x0843, 0x0F);
        write_cmos_sensor(0x0844, 0x00);
        write_cmos_sensor(0x0845, 0x37);
        write_cmos_sensor(0x0847, 0x02);
        //slow clock from 355.2M to 120M

	spin_lock(&imgsensor_drv_lock);
	imgsensor.pclk = 120000000;
	spin_unlock(&imgsensor_drv_lock);

	/* calculate shutter as different pclk 120MHZ/355.2MHZ */
	origi_shutter = shutter;
	shutter = origi_shutter * 1200 / 3552;
        if(shutter > (imgsensor_info.max_frame_length - imgsensor_info.margin)) {
		line_length = origi_shutter * 1200 / (imgsensor_info.max_frame_length - imgsensor_info.margin) * 5904 / 3552;
		line_length = (line_length + 1) / 2 * 2;
	}
 
	spin_lock(&imgsensor_drv_lock);
	if (shutter > imgsensor.min_frame_length - imgsensor_info.margin)		
		imgsensor.frame_length = shutter + imgsensor_info.margin;
	else
		imgsensor.frame_length = imgsensor.min_frame_length;

	if (imgsensor.frame_length > imgsensor_info.max_frame_length)
		imgsensor.frame_length = imgsensor_info.max_frame_length;
	spin_unlock(&imgsensor_drv_lock);
	shutter = (shutter < imgsensor_info.min_shutter) ? imgsensor_info.min_shutter : shutter;

	/* line_length range is 5904 <-> 0x8F10 */
        if(line_length > 0x8F10)
            line_length = 0x8F10;
        if(line_length < 5904)
            line_length = 5904;
        write_cmos_sensor(0x0342, (line_length >> 8) & 0xFF);
        write_cmos_sensor(0x0343, line_length & 0xFF);
 
	imgsensor.line_length = line_length;
        }

	shutter = (shutter > (imgsensor_info.max_frame_length - imgsensor_info.margin)) ? (imgsensor_info.max_frame_length - imgsensor_info.margin) : shutter;
	

	if (imgsensor.autoflicker_en) {
		realtime_fps = imgsensor.pclk / imgsensor.line_length * 10 / imgsensor.frame_length;
        	if(realtime_fps >= 297 && realtime_fps <= 305) {
			set_max_framerate(296,0);
            		write_cmos_sensor(0x0104, 0x01);
        	} else if(realtime_fps >= 237 && realtime_fps <= 245) {
            		set_max_framerate(236,0);
            		write_cmos_sensor(0x0104, 0x01);
        	} else if(realtime_fps >= 183 && realtime_fps <= 220) {
            		set_max_framerate(146,0);
            		write_cmos_sensor(0x0104, 0x01);
        	} else if(realtime_fps >= 157 && realtime_fps <= 183) {
            		set_max_framerate(157,0);
            		write_cmos_sensor(0x0104, 0x01);
        	} else if(realtime_fps >= 138 && realtime_fps <= 157) {
            		set_max_framerate(138,0);
            		write_cmos_sensor(0x0104, 0x01);
        	} else if(realtime_fps >= 129 && realtime_fps <= 138) {
            		set_max_framerate(129,0);
            		write_cmos_sensor(0x0104, 0x01);
        	} else if(realtime_fps >= 116 && realtime_fps <= 129) {
            		set_max_framerate(116,0);
            		write_cmos_sensor(0x0104, 0x01);
        	} else if(realtime_fps >= 105 && realtime_fps <= 116) {
            		set_max_framerate(105,0);
            		write_cmos_sensor(0x0104, 0x01);
        	} else if(realtime_fps >= 96 && realtime_fps <= 105) {
            		set_max_framerate(96,0);
            		write_cmos_sensor(0x0104, 0x01);
		} else {
			// Extend frame length
			write_cmos_sensor(0x0104, 0x01);
			write_cmos_sensor(0x0340, (imgsensor.frame_length >> 8) & 0xFF);
			write_cmos_sensor(0x0341, imgsensor.frame_length & 0xFF);
		}
	} else {
        	// Extend frame length
        	write_cmos_sensor(0x0104, 0x01);
        	write_cmos_sensor(0x0340, (imgsensor.frame_length >> 8) & 0xFF);
        	write_cmos_sensor(0x0341, imgsensor.frame_length & 0xFF);
	}

	/* Update Shutter */
	write_cmos_sensor(0x0202, (shutter >> 8) & 0xFF);
	write_cmos_sensor(0x0203, shutter & 0xFF);
	write_cmos_sensor(0x0104, 0x00);
	LOG_INF("Exit! shutter =%d, framelength =%d, linelength = %d\n", shutter,imgsensor.frame_length, imgsensor.line_length);
}   /*  write_shutter  */



/*************************************************************************
* FUNCTION
*   set_shutter
*
* DESCRIPTION
*   This function set e-shutter of sensor to change exposure time.
*
* PARAMETERS
*   iShutter : exposured lines
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static void set_shutter(kal_uint32 shutter)
{
    unsigned long flags;
    spin_lock_irqsave(&imgsensor_drv_lock, flags);
    imgsensor.shutter = shutter;
    spin_unlock_irqrestore(&imgsensor_drv_lock, flags);

    write_shutter(shutter);
    LOG_INF("sensor temperature: %d.\n", get_sensor_temp());
}   /*  set_shutter */



static kal_uint16 gain2reg(const kal_uint16 gain)
{
        kal_uint8 i;

    for (i = 0; i < 71; i++) {
        if(gain <= sensor_gain_mapping[i][0]){
            break;
        }
    }
    if(gain != sensor_gain_mapping[i][0])
            LOG_INF("Gain mapping don't correctly:%d %d \n", gain, sensor_gain_mapping[i][0]);
        return sensor_gain_mapping[i][1];
}

/*************************************************************************
* FUNCTION
*   set_gain
*
* DESCRIPTION
*   This function is to set global gain to sensor.
*
* PARAMETERS
*   iGain : sensor global gain(base: 0x40)
*
* RETURNS
*   the actually gain set to sensor.
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint16 set_gain(kal_uint16 gain)
{
    kal_uint16 reg_gain;

    /* 0x350A[0:1], 0x350B[0:7] AGC real gain */
    /* [0:3] = N meams N /16 X  */
    /* [4:9] = M meams M X       */
    /* Total gain = M + N /16 X   */

    //
    if (gain < BASEGAIN || gain > 15 * BASEGAIN) {
        LOG_INF("Error gain setting");

        if (gain < BASEGAIN)
            gain = BASEGAIN;
        else if (gain > 15 * BASEGAIN)
            gain = 15 * BASEGAIN;
    }

    reg_gain = gain2reg(gain);
    spin_lock(&imgsensor_drv_lock);
    imgsensor.gain = reg_gain;
    spin_unlock(&imgsensor_drv_lock);
    LOG_INF("gain = %d , reg_gain = 0x%x\n ", gain, reg_gain);

    write_cmos_sensor(0x0104, 0x01);
    write_cmos_sensor(0x0205, reg_gain & 0xFF);
    write_cmos_sensor(0x0104, 0x00);

    /*
     * WORKAROUND! stream on after set shutter/gain, which will get
     * first valid capture frame.
     */
    if (mode_change && ((imgsensor.sensor_mode == IMGSENSOR_MODE_CAPTURE)
	|| (imgsensor.sensor_mode == IMGSENSOR_MODE_SLIM_CAPTURE)
	|| (imgsensor.sensor_mode == IMGSENSOR_MODE_PREVIEW))) {
		write_cmos_sensor(0x0100,0x01);
		mode_change = 0;
	}

    return gain;
}   /*  set_gain  */

static void ihdr_write_shutter_gain(kal_uint16 le, kal_uint16 se, kal_uint16 gain)
{
/*
    LOG_INF("le:0x%x, se:0x%x, gain:0x%x\n",le,se,gain);
    if (imgsensor.ihdr_en) {

        spin_lock(&imgsensor_drv_lock);
            if (le > imgsensor.min_frame_length - imgsensor_info.margin)
                imgsensor.frame_length = le + imgsensor_info.margin;
            else
                imgsensor.frame_length = imgsensor.min_frame_length;
            if (imgsensor.frame_length > imgsensor_info.max_frame_length)
                imgsensor.frame_length = imgsensor_info.max_frame_length;
            spin_unlock(&imgsensor_drv_lock);
            if (le < imgsensor_info.min_shutter) le = imgsensor_info.min_shutter;
            if (se < imgsensor_info.min_shutter) se = imgsensor_info.min_shutter;



                // Extend frame length
                write_cmos_sensor(0x0104, 0x01);
                write_cmos_sensor(0x0340, (imgsensor.frame_length >> 8) & 0xFF);
                write_cmos_sensor(0x0341, imgsensor.frame_length & 0xFF);
                write_cmos_sensor(0x0202, (le >> 8) & 0xFF);
                write_cmos_sensor(0x0203, le & 0xFF);


        write_cmos_sensor(0x3508, (se << 4) & 0xFF);
        write_cmos_sensor(0x3507, (se >> 4) & 0xFF);
        write_cmos_sensor(0x3506, (se >> 12) & 0x0F);
        write_cmos_sensor(0x0104, 0x00);

        set_gain(gain);
    }
*/
}



static void set_mirror_flip(kal_uint8 image_mirror)
{
    LOG_INF("image_mirror = %d\n", image_mirror);

    /********************************************************
       *
       *   0x3820[2] ISP Vertical flip
       *   0x3820[1] Sensor Vertical flip
       *
       *   0x3821[2] ISP Horizontal mirror
       *   0x3821[1] Sensor Horizontal mirror
       *
       *   ISP and Sensor flip or mirror register bit should be the same!!
       *
       ********************************************************/

    switch (image_mirror) {
        case IMAGE_NORMAL:
            write_cmos_sensor(0x3820,((read_cmos_sensor(0x3820) & 0xF9) | 0x00));
            write_cmos_sensor(0x3821,((read_cmos_sensor(0x3821) & 0xF9) | 0x06));
            break;
        case IMAGE_H_MIRROR:
            write_cmos_sensor(0x3820,((read_cmos_sensor(0x3820) & 0xF9) | 0x00));
            write_cmos_sensor(0x3821,((read_cmos_sensor(0x3821) & 0xF9) | 0x00));
            break;
        case IMAGE_V_MIRROR:
            write_cmos_sensor(0x3820,((read_cmos_sensor(0x3820) & 0xF9) | 0x06));
            write_cmos_sensor(0x3821,((read_cmos_sensor(0x3821) & 0xF9) | 0x06));
            break;
        case IMAGE_HV_MIRROR:
            write_cmos_sensor(0x3820,((read_cmos_sensor(0x3820) & 0xF9) | 0x06));
            write_cmos_sensor(0x3821,((read_cmos_sensor(0x3821) & 0xF9) | 0x00));
            break;
        default:
            LOG_INF("Error image_mirror setting\n");
    }

}

static void set_thread_priority(kal_uint16 pri_val)
{
	struct sched_param param = { .sched_priority = 0 };
	param.sched_priority = pri_val;

	if (pri_val > 0) {
		sched_setscheduler(current, SCHED_RR, &param);
		set_current_state(TASK_INTERRUPTIBLE);
	} else {
		sched_setscheduler(current, SCHED_NORMAL, &param);
	}
}

/*************************************************************************
* FUNCTION
*   night_mode
*
* DESCRIPTION
*   This function night mode of sensor.
*
* PARAMETERS
*   bEnable: KAL_TRUE -> enable night mode, otherwise, disable night mode
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static void night_mode(kal_bool enable)
{
/*No Need to implement this function*/
}   /*  night_mode  */

static void sensor_init(void)
{
	LOG_INF("%s\n", __func__);
	/* Initial settings for sensor */
	write_cmos_sensor(0x011E, 0x18);
	write_cmos_sensor(0x011F, 0x00);
	write_cmos_sensor(0x0101, 0x03);
	write_cmos_sensor(0x0220, 0x01);
	write_cmos_sensor(0x9301, 0x01);
	write_cmos_sensor(0x9278, 0x19);
	write_cmos_sensor(0x93B4, 0x07);
	write_cmos_sensor(0x93B5, 0x07);
	write_cmos_sensor(0x9412, 0x29);
	write_cmos_sensor(0x941B, 0x1B);
	write_cmos_sensor(0x941F, 0x86);
	write_cmos_sensor(0x943E, 0x05);
	write_cmos_sensor(0x9507, 0x1B);
	write_cmos_sensor(0x950B, 0x1B);
	write_cmos_sensor(0x950F, 0x1B);
	write_cmos_sensor(0x960F, 0xCA);
	write_cmos_sensor(0x9631, 0x00);
	write_cmos_sensor(0x9633, 0x40);

	/* disable sensor lsc */
	write_cmos_sensor(0xAC03, 0x01);
	write_cmos_sensor(0x0700, 0x00);
	write_cmos_sensor(0xBA03, 0x00);
	write_cmos_sensor(0xBA07, 0x00);
}   /*  sensor_init  */

static void imx220_4k_setting(void)
{
	LOG_INF("%s\n", __func__);
	set_thread_priority(40);
	write_cmos_sensor(0x0100, 0x00);	/* stream off */
	/*
	 * 3840x2160 @29.9fps.
	 * pclk = 390.4MHZ
	 * frame_length = 2208 
	 * line_length = 5904
	 */
	/* Clock Setting */
	write_cmos_sensor(0x0301, 0x0A);
	write_cmos_sensor(0x0303, 0x01);
	write_cmos_sensor(0x0304, 0x03);
	write_cmos_sensor(0x0305, 0x06);
	write_cmos_sensor(0x0306, 0x00);
	write_cmos_sensor(0x0307, 0x7A);
	write_cmos_sensor(0x0309, 0x0A);
	write_cmos_sensor(0x030B, 0x01);
	write_cmos_sensor(0x030C, 0x00);
	write_cmos_sensor(0x030D, 0xD0);

	/* Size setting */
	write_cmos_sensor(0x0340, 0x08);
	write_cmos_sensor(0x0341, 0xB0);
	write_cmos_sensor(0x0342, 0x17);
	write_cmos_sensor(0x0343, 0x10);
	write_cmos_sensor(0x0344, 0x02);
	write_cmos_sensor(0x0345, 0xC0);
	write_cmos_sensor(0x0346, 0x03);
	write_cmos_sensor(0x0347, 0x78);
	write_cmos_sensor(0x0348, 0x11);
	write_cmos_sensor(0x0349, 0xBF);
	write_cmos_sensor(0x034A, 0x0B);
	write_cmos_sensor(0x034B, 0xF7);
	write_cmos_sensor(0x034C, 0x0F);
	write_cmos_sensor(0x034D, 0x00);
	write_cmos_sensor(0x034E, 0x08);
	write_cmos_sensor(0x034F, 0x80);
	write_cmos_sensor(0x0350, 0x00);
	write_cmos_sensor(0x0351, 0x00);
	write_cmos_sensor(0x0352, 0x00);
	write_cmos_sensor(0x0353, 0x00);
	write_cmos_sensor(0x0354, 0x0F);
	write_cmos_sensor(0x0355, 0x00);
	write_cmos_sensor(0x0356, 0x08);
	write_cmos_sensor(0x0357, 0x80);
	write_cmos_sensor(0x901D, 0x08);

	/* Mode Setting */
	write_cmos_sensor(0x0108, 0x03);
	write_cmos_sensor(0x0112, 0x0A);
	write_cmos_sensor(0x0113, 0x0A);
	write_cmos_sensor(0x0381, 0x01);
	write_cmos_sensor(0x0383, 0x01);
	write_cmos_sensor(0x0385, 0x01);
	write_cmos_sensor(0x0387, 0x01);
	write_cmos_sensor(0x0390, 0x00);
	write_cmos_sensor(0x0391, 0x11);
	write_cmos_sensor(0x0392, 0x00);
	write_cmos_sensor(0x1307, 0x00);
	write_cmos_sensor(0x9512, 0x40);
	write_cmos_sensor(0x9873, 0x00);
	write_cmos_sensor(0xAB07, 0x01);
	write_cmos_sensor(0xB802, 0x01);
	write_cmos_sensor(0xB933, 0x00);

	/* OptionnalFunction setting */
	write_cmos_sensor(0x13C0, 0x01);
	write_cmos_sensor(0x13C1, 0x02);
	write_cmos_sensor(0x9909, 0x01);
	write_cmos_sensor(0x991E, 0x01);

	/* Global Timing Setting */
	write_cmos_sensor(0x0830, 0x00);
	write_cmos_sensor(0x0831, 0x6F);
	write_cmos_sensor(0x0832, 0x00);
	write_cmos_sensor(0x0833, 0x2F);
	write_cmos_sensor(0x0834, 0x00);
	write_cmos_sensor(0x0835, 0x57);
	write_cmos_sensor(0x0836, 0x00);
	write_cmos_sensor(0x0837, 0x2F);
	write_cmos_sensor(0x0838, 0x00);
	write_cmos_sensor(0x0839, 0x2F);
	write_cmos_sensor(0x083A, 0x00);
	write_cmos_sensor(0x083B, 0x2F);
	write_cmos_sensor(0x083C, 0x00);
	write_cmos_sensor(0x083D, 0xBF);
	write_cmos_sensor(0x083E, 0x00);
	write_cmos_sensor(0x083F, 0x27);
	write_cmos_sensor(0x0842, 0x00);
	write_cmos_sensor(0x0843, 0x0F);
	write_cmos_sensor(0x0844, 0x00);
	write_cmos_sensor(0x0845, 0x37);
	write_cmos_sensor(0x0847, 0x02);

	/* Integration Time Setting */
	write_cmos_sensor(0x0202, 0x08);
	write_cmos_sensor(0x0203, 0x98);

	/* Gain Setting */
	write_cmos_sensor(0x0205, 0x00);
	write_cmos_sensor(0x020E, 0x01);
	write_cmos_sensor(0x020F, 0x00);
	write_cmos_sensor(0x0210, 0x01);
	write_cmos_sensor(0x0211, 0x00);
	write_cmos_sensor(0x0212, 0x01);
	write_cmos_sensor(0x0213, 0x00);
	write_cmos_sensor(0x0214, 0x01);
	write_cmos_sensor(0x0215, 0x00);

	/* HDR Setting */
	write_cmos_sensor(0x0233, 0x00);
	write_cmos_sensor(0x0234, 0x00);
	write_cmos_sensor(0x0235, 0x40);
	write_cmos_sensor(0x0238, 0x00);
	write_cmos_sensor(0x0239, 0x08);
	write_cmos_sensor(0x13C2, 0x00);
	write_cmos_sensor(0x13C3, 0x00);
	write_cmos_sensor(0x13C4, 0x00);
	write_cmos_sensor(0xAD34, 0x14);
	write_cmos_sensor(0xAD35, 0x7F);
	write_cmos_sensor(0xAD36, 0x00);
	write_cmos_sensor(0xAD37, 0x00);
	write_cmos_sensor(0xAD38, 0x0F);
	write_cmos_sensor(0xAD39, 0x5D);
	write_cmos_sensor(0xAD3A, 0x00);
	write_cmos_sensor(0xAD3B, 0x00);
	write_cmos_sensor(0xAD3C, 0x14);
	write_cmos_sensor(0xAD3D, 0x7F);
	write_cmos_sensor(0xAD3E, 0x0F);
	write_cmos_sensor(0xAD3F, 0x5D);

	/* Bypass Setting */
	write_cmos_sensor(0xA303, 0x00);
	write_cmos_sensor(0xA403, 0x00);
	write_cmos_sensor(0xA602, 0x00);
	write_cmos_sensor(0xA703, 0x01);
	write_cmos_sensor(0xA903, 0x01);
	write_cmos_sensor(0xA956, 0x01);
	write_cmos_sensor(0xAA03, 0x01);
	write_cmos_sensor(0xAB03, 0x01);
	write_cmos_sensor(0xAD03, 0x01);
	write_cmos_sensor(0xAE03, 0x01);
	write_cmos_sensor(0xB803, 0x00);
	write_cmos_sensor(0xB903, 0x01);
	write_cmos_sensor(0xB91A, 0x01);
	write_cmos_sensor(0xBB03, 0x01);

	/* Single Correct setting */
	write_cmos_sensor(0x4003, 0x00);
	write_cmos_sensor(0x4004, 0x00);
	write_cmos_sensor(0x4005, 0x00);
	write_cmos_sensor(0x4006, 0x80);
	write_cmos_sensor(0x4007, 0x80);
	write_cmos_sensor(0x4008, 0x80);
	write_cmos_sensor(0x400C, 0x00);
	write_cmos_sensor(0x400D, 0x00);
	write_cmos_sensor(0x400E, 0x00);
	write_cmos_sensor(0x400F, 0x80);
	write_cmos_sensor(0x4010, 0x80);
	write_cmos_sensor(0x4011, 0x80);

	/* Mode etc setting */
	write_cmos_sensor(0x3008, 0x01);
	write_cmos_sensor(0x3009, 0x71);
	write_cmos_sensor(0x300A, 0x51);
	write_cmos_sensor(0x9431, 0x03);
	write_cmos_sensor(0xA913, 0x00);
	write_cmos_sensor(0xAC13, 0x00);
	write_cmos_sensor(0xAC3F, 0x00);
	write_cmos_sensor(0xAE8B, 0x29);
	write_cmos_sensor(0xB812, 0x00);

	/* LNR setting */
	write_cmos_sensor(0x3516, 0x00);
	write_cmos_sensor(0x3517, 0x00);
	write_cmos_sensor(0x3518, 0x00);
	write_cmos_sensor(0x3519, 0x00);
	write_cmos_sensor(0x351A, 0x00);
	write_cmos_sensor(0x351B, 0x00);
	write_cmos_sensor(0x351C, 0x00);
	write_cmos_sensor(0x351D, 0x00);
	write_cmos_sensor(0x351E, 0x00);
	write_cmos_sensor(0x351F, 0x00);
	write_cmos_sensor(0x3520, 0x00);
	write_cmos_sensor(0x3521, 0x00);
	write_cmos_sensor(0x3522, 0x00);

	/* stream on */
	write_cmos_sensor(0x0100, 0x01);
	set_thread_priority(0);
}	/* 4k setting */

static void imx220_hd_gis_video_setting(void)
{
	LOG_INF("%s\n", __func__);
	set_thread_priority(40);
	write_cmos_sensor(0x0100, 0x00);	/* stream off */

	/*
	 * 2112x1188@30fps
	 * Pclk = 240Mhz
	 * frame_length = 1352
	 * line_length = 5904
	 */
	/* Clock Setting */
	write_cmos_sensor(0x0301, 0x06);
	write_cmos_sensor(0x0303, 0x02);
	write_cmos_sensor(0x0304, 0x02);
	write_cmos_sensor(0x0305, 0x06);
	write_cmos_sensor(0x0306, 0x00);
	write_cmos_sensor(0x0307, 0x3C);
	write_cmos_sensor(0x0309, 0x0A);
	write_cmos_sensor(0x030B, 0x01);
	write_cmos_sensor(0x030C, 0x00);
	write_cmos_sensor(0x030D, 0x5C);

	/* Size setting */
	write_cmos_sensor(0x0340, 0x05);
	write_cmos_sensor(0x0341, 0x48);
	write_cmos_sensor(0x0342, 0x17);
	write_cmos_sensor(0x0343, 0x10);
	write_cmos_sensor(0x0344, 0x02);
	write_cmos_sensor(0x0345, 0x00);
	write_cmos_sensor(0x0346, 0x03);
	write_cmos_sensor(0x0347, 0x0C);
	write_cmos_sensor(0x0348, 0x12);
	write_cmos_sensor(0x0349, 0x7F);
	write_cmos_sensor(0x034A, 0x0C);
	write_cmos_sensor(0x034B, 0x53);
	write_cmos_sensor(0x034C, 0x08);
	write_cmos_sensor(0x034D, 0x40);
	write_cmos_sensor(0x034E, 0x04);
	write_cmos_sensor(0x034F, 0xA4);
	write_cmos_sensor(0x0350, 0x00);
	write_cmos_sensor(0x0351, 0x00);
	write_cmos_sensor(0x0352, 0x00);
	write_cmos_sensor(0x0353, 0x00);
	write_cmos_sensor(0x0354, 0x08);
	write_cmos_sensor(0x0355, 0x40);
	write_cmos_sensor(0x0356, 0x04);
	write_cmos_sensor(0x0357, 0xA4);
	write_cmos_sensor(0x901D, 0x08);

	/* Mode Setting */
	write_cmos_sensor(0x0108, 0x03);
	write_cmos_sensor(0x0112, 0x0A);
	write_cmos_sensor(0x0113, 0x0A);
	write_cmos_sensor(0x0381, 0x01);
	write_cmos_sensor(0x0383, 0x01);
	write_cmos_sensor(0x0385, 0x01);
	write_cmos_sensor(0x0387, 0x01);
	write_cmos_sensor(0x0390, 0x01);
	write_cmos_sensor(0x0391, 0x22);
	write_cmos_sensor(0x0392, 0x00);
	write_cmos_sensor(0x1307, 0x02);
	write_cmos_sensor(0x9512, 0x40);
	write_cmos_sensor(0x9873, 0x00);
	write_cmos_sensor(0xAB07, 0x01);
	write_cmos_sensor(0xB802, 0x01);
	write_cmos_sensor(0xB933, 0x00);

	/* OptionnalFunction setting */
	write_cmos_sensor(0x0700, 0x00);
	write_cmos_sensor(0x13C0, 0x00);
	write_cmos_sensor(0x13C1, 0x01);
	write_cmos_sensor(0x9909, 0x01);
	write_cmos_sensor(0x991E, 0x01);
	write_cmos_sensor(0xBA03, 0x00);
	write_cmos_sensor(0xBA07, 0x00);

	/* Global Timing Setting */
	write_cmos_sensor(0x0830, 0x00);
	write_cmos_sensor(0x0831, 0x57);
	write_cmos_sensor(0x0832, 0x00);
	write_cmos_sensor(0x0833, 0x1F);
	write_cmos_sensor(0x0834, 0x00);
	write_cmos_sensor(0x0835, 0x37);
	write_cmos_sensor(0x0836, 0x00);
	write_cmos_sensor(0x0837, 0x1F);
	write_cmos_sensor(0x0838, 0x00);
	write_cmos_sensor(0x0839, 0x17);
	write_cmos_sensor(0x083A, 0x00);
	write_cmos_sensor(0x083B, 0x17);
	write_cmos_sensor(0x083C, 0x00);
	write_cmos_sensor(0x083D, 0x77);
	write_cmos_sensor(0x083E, 0x00);
	write_cmos_sensor(0x083F, 0x17);
	write_cmos_sensor(0x0842, 0x00);
	write_cmos_sensor(0x0843, 0x0F);
	write_cmos_sensor(0x0844, 0x00);
	write_cmos_sensor(0x0845, 0x37);
	write_cmos_sensor(0x0847, 0x02);

	/* Integration Time Setting */
	write_cmos_sensor(0x0202, 0x05);
	write_cmos_sensor(0x0203, 0x40);

	/* Gain Setting */
	write_cmos_sensor(0x0205, 0x00);
	write_cmos_sensor(0x020E, 0x01);
	write_cmos_sensor(0x020F, 0x00);
	write_cmos_sensor(0x0210, 0x01);
	write_cmos_sensor(0x0211, 0x00);
	write_cmos_sensor(0x0212, 0x01);
	write_cmos_sensor(0x0213, 0x00);
	write_cmos_sensor(0x0214, 0x01);
	write_cmos_sensor(0x0215, 0x00);

	/* HDR Setting */
	write_cmos_sensor(0x0233, 0x00);
	write_cmos_sensor(0x0234, 0x00);
	write_cmos_sensor(0x0235, 0x40);
	write_cmos_sensor(0x0238, 0x00);
	write_cmos_sensor(0x0239, 0x08);
	write_cmos_sensor(0x13C2, 0x00);
	write_cmos_sensor(0x13C3, 0x00);
	write_cmos_sensor(0x13C4, 0x00);
	write_cmos_sensor(0xAD34, 0x14);
	write_cmos_sensor(0xAD35, 0x7F);
	write_cmos_sensor(0xAD36, 0x00);
	write_cmos_sensor(0xAD37, 0x00);
	write_cmos_sensor(0xAD38, 0x0F);
	write_cmos_sensor(0xAD39, 0x5D);
	write_cmos_sensor(0xAD3A, 0x00);
	write_cmos_sensor(0xAD3B, 0x00);
	write_cmos_sensor(0xAD3C, 0x14);
	write_cmos_sensor(0xAD3D, 0x7F);
	write_cmos_sensor(0xAD3E, 0x0F);
	write_cmos_sensor(0xAD3F, 0x5D);

	/* Bypass Setting */
	write_cmos_sensor(0xA303, 0x00);
	write_cmos_sensor(0xA403, 0x00);
	write_cmos_sensor(0xA602, 0x00);
	write_cmos_sensor(0xA703, 0x01);
	write_cmos_sensor(0xA903, 0x01);
	write_cmos_sensor(0xA956, 0x01);
	write_cmos_sensor(0xAA03, 0x01);
	write_cmos_sensor(0xAB03, 0x00);
	write_cmos_sensor(0xAC03, 0x01);
	write_cmos_sensor(0xAD03, 0x01);
	write_cmos_sensor(0xAE03, 0x01);
	write_cmos_sensor(0xB803, 0x00);
	write_cmos_sensor(0xB903, 0x01);
	write_cmos_sensor(0xB91A, 0x01);
	write_cmos_sensor(0xBB03, 0x01);

	/* Single Correct setting */
	write_cmos_sensor(0x4003, 0x00);
	write_cmos_sensor(0x4004, 0x00);
	write_cmos_sensor(0x4005, 0x00);
	write_cmos_sensor(0x4006, 0x80);
	write_cmos_sensor(0x4007, 0x80);
	write_cmos_sensor(0x4008, 0x80);
	write_cmos_sensor(0x400C, 0x00);
	write_cmos_sensor(0x400D, 0x00);
	write_cmos_sensor(0x400E, 0x00);
	write_cmos_sensor(0x400F, 0x80);
	write_cmos_sensor(0x4010, 0x80);
	write_cmos_sensor(0x4011, 0x80);

	/* Mode etc setting */
	write_cmos_sensor(0x3008, 0x01);
	write_cmos_sensor(0x3009, 0x71);
	write_cmos_sensor(0x300A, 0x51);
	write_cmos_sensor(0x9431, 0x03);
	write_cmos_sensor(0xA913, 0x00);
	write_cmos_sensor(0xAC13, 0x00);
	write_cmos_sensor(0xAC3F, 0x00);
	write_cmos_sensor(0xAE8B, 0x29);
	write_cmos_sensor(0xB812, 0x00);

	/* LNR setting */
	write_cmos_sensor(0x3516, 0x00);
	write_cmos_sensor(0x3517, 0x00);
	write_cmos_sensor(0x3518, 0x00);
	write_cmos_sensor(0x3519, 0x00);
	write_cmos_sensor(0x351A, 0x00);
	write_cmos_sensor(0x351B, 0x00);
	write_cmos_sensor(0x351C, 0x00);
	write_cmos_sensor(0x351D, 0x00);
	write_cmos_sensor(0x351E, 0x00);
	write_cmos_sensor(0x351F, 0x00);
	write_cmos_sensor(0x3520, 0x00);
	write_cmos_sensor(0x3521, 0x00);
	write_cmos_sensor(0x3522, 0x00);

	/* stream on */
	write_cmos_sensor(0x0100, 0x01);
	set_thread_priority(0);
}	/* 1080P gis video setting */

static void imx220_1080p_setting(void)
{
	LOG_INF("%s\n", __func__);
	set_thread_priority(40);
	write_cmos_sensor(0x0100, 0x00);	/* stream off */
	/*
	 * 1920 x 1080 @30.1fps
	 * Pclk = 220MHZ
	 * frame_length = 1236
	 * line_length = 5904
	 */
	/* Clock Setting */
	write_cmos_sensor(0x0301, 0x06);
	write_cmos_sensor(0x0303, 0x02);
	write_cmos_sensor(0x0304, 0x02);
	write_cmos_sensor(0x0305, 0x06);
	write_cmos_sensor(0x0306, 0x00);
	write_cmos_sensor(0x0307, 0x37);
	write_cmos_sensor(0x0309, 0x0A);
	write_cmos_sensor(0x030B, 0x01);
	write_cmos_sensor(0x030C, 0x00);
	write_cmos_sensor(0x030D, 0x55);

	/* Size setting */
	write_cmos_sensor(0x0340, 0x04);
	write_cmos_sensor(0x0341, 0xD4);
	write_cmos_sensor(0x0342, 0x17);
	write_cmos_sensor(0x0343, 0x10);
	write_cmos_sensor(0x0344, 0x02);
	write_cmos_sensor(0x0345, 0xC0);
	write_cmos_sensor(0x0346, 0x03);
	write_cmos_sensor(0x0347, 0x78);
	write_cmos_sensor(0x0348, 0x11);
	write_cmos_sensor(0x0349, 0xBF);
	write_cmos_sensor(0x034A, 0x0B);
	write_cmos_sensor(0x034B, 0xF7);
	write_cmos_sensor(0x034C, 0x07);
	write_cmos_sensor(0x034D, 0x80);
	write_cmos_sensor(0x034E, 0x04);
	write_cmos_sensor(0x034F, 0x40);
	write_cmos_sensor(0x0350, 0x00);
	write_cmos_sensor(0x0351, 0x00);
	write_cmos_sensor(0x0352, 0x00);
	write_cmos_sensor(0x0353, 0x00);
	write_cmos_sensor(0x0354, 0x07);
	write_cmos_sensor(0x0355, 0x80);
	write_cmos_sensor(0x0356, 0x04);
	write_cmos_sensor(0x0357, 0x40);
	write_cmos_sensor(0x901D, 0x08);

	/* Mode Setting */
	write_cmos_sensor(0x0108, 0x03);
	write_cmos_sensor(0x0112, 0x0A);
	write_cmos_sensor(0x0113, 0x0A);
	write_cmos_sensor(0x0381, 0x01);
	write_cmos_sensor(0x0383, 0x01);
	write_cmos_sensor(0x0385, 0x01);
	write_cmos_sensor(0x0387, 0x01);
	write_cmos_sensor(0x0390, 0x01);
	write_cmos_sensor(0x0391, 0x22);
	write_cmos_sensor(0x0392, 0x00);
	write_cmos_sensor(0x1307, 0x02);
	write_cmos_sensor(0x9512, 0x40);
	write_cmos_sensor(0x9873, 0x00);
	write_cmos_sensor(0xAB07, 0x01);
	write_cmos_sensor(0xB802, 0x01);
	write_cmos_sensor(0xB933, 0x00);

	/* OptionnalFunction setting */
	write_cmos_sensor(0x13C0, 0x00);
	write_cmos_sensor(0x13C1, 0x01);
	write_cmos_sensor(0x9909, 0x01);
	write_cmos_sensor(0x991E, 0x01);

	/* Global Timing Setting */
	write_cmos_sensor(0x0830, 0x00);
	write_cmos_sensor(0x0831, 0x57);
	write_cmos_sensor(0x0832, 0x00);
	write_cmos_sensor(0x0833, 0x1F);
	write_cmos_sensor(0x0834, 0x00);
	write_cmos_sensor(0x0835, 0x37);
	write_cmos_sensor(0x0836, 0x00);
	write_cmos_sensor(0x0837, 0x1F);
	write_cmos_sensor(0x0838, 0x00);
	write_cmos_sensor(0x0839, 0x17);
	write_cmos_sensor(0x083A, 0x00);
	write_cmos_sensor(0x083B, 0x17);
	write_cmos_sensor(0x083C, 0x00);
	write_cmos_sensor(0x083D, 0x77);
	write_cmos_sensor(0x083E, 0x00);
	write_cmos_sensor(0x083F, 0x17);
	write_cmos_sensor(0x0842, 0x00);
	write_cmos_sensor(0x0843, 0x0F);
	write_cmos_sensor(0x0844, 0x00);
	write_cmos_sensor(0x0845, 0x37);
	write_cmos_sensor(0x0847, 0x02);

	/* Integration Time Setting */
	write_cmos_sensor(0x0202, 0x06);
	write_cmos_sensor(0x0203, 0x68);

	/* Gain Setting */
	write_cmos_sensor(0x0205, 0x00);
	write_cmos_sensor(0x020E, 0x01);
	write_cmos_sensor(0x020F, 0x00);
	write_cmos_sensor(0x0210, 0x01);
	write_cmos_sensor(0x0211, 0x00);
	write_cmos_sensor(0x0212, 0x01);
	write_cmos_sensor(0x0213, 0x00);
	write_cmos_sensor(0x0214, 0x01);
	write_cmos_sensor(0x0215, 0x00);

	/* HDR Setting */
	write_cmos_sensor(0x0233, 0x00);
	write_cmos_sensor(0x0234, 0x00);
	write_cmos_sensor(0x0235, 0x40);
	write_cmos_sensor(0x0238, 0x00);
	write_cmos_sensor(0x0239, 0x08);
	write_cmos_sensor(0x13C2, 0x00);
	write_cmos_sensor(0x13C3, 0x00);
	write_cmos_sensor(0x13C4, 0x00);
	write_cmos_sensor(0xAD34, 0x14);
	write_cmos_sensor(0xAD35, 0x7F);
	write_cmos_sensor(0xAD36, 0x00);
	write_cmos_sensor(0xAD37, 0x00);
	write_cmos_sensor(0xAD38, 0x0F);
	write_cmos_sensor(0xAD39, 0x5D);
	write_cmos_sensor(0xAD3A, 0x00);
	write_cmos_sensor(0xAD3B, 0x00);
	write_cmos_sensor(0xAD3C, 0x14);
	write_cmos_sensor(0xAD3D, 0x7F);
	write_cmos_sensor(0xAD3E, 0x0F);
	write_cmos_sensor(0xAD3F, 0x5D);

	/* Bypass Setting */
	write_cmos_sensor(0xA303, 0x00);
	write_cmos_sensor(0xA403, 0x00);
	write_cmos_sensor(0xA602, 0x00);
	write_cmos_sensor(0xA703, 0x01);
	write_cmos_sensor(0xA903, 0x01);
	write_cmos_sensor(0xA956, 0x01);
	write_cmos_sensor(0xAA03, 0x01);
	write_cmos_sensor(0xAB03, 0x00);
	write_cmos_sensor(0xAD03, 0x01);
	write_cmos_sensor(0xAE03, 0x01);
	write_cmos_sensor(0xB803, 0x00);
	write_cmos_sensor(0xB903, 0x01);
	write_cmos_sensor(0xB91A, 0x01);
	write_cmos_sensor(0xBB03, 0x01);

	/* Single Correct setting */
	write_cmos_sensor(0x4003, 0x00);
	write_cmos_sensor(0x4004, 0x00);
	write_cmos_sensor(0x4005, 0x00);
	write_cmos_sensor(0x4006, 0x80);
	write_cmos_sensor(0x4007, 0x80);
	write_cmos_sensor(0x4008, 0x80);
	write_cmos_sensor(0x400C, 0x00);
	write_cmos_sensor(0x400D, 0x00);
	write_cmos_sensor(0x400E, 0x00);
	write_cmos_sensor(0x400F, 0x80);
	write_cmos_sensor(0x4010, 0x80);
	write_cmos_sensor(0x4011, 0x80);

	/* Mode etc setting */
	write_cmos_sensor(0x3008, 0x01);
	write_cmos_sensor(0x3009, 0x71);
	write_cmos_sensor(0x300A, 0x51);
	write_cmos_sensor(0x9431, 0x03);
	write_cmos_sensor(0xA913, 0x00);
	write_cmos_sensor(0xAC13, 0x00);
	write_cmos_sensor(0xAC3F, 0x00);
	write_cmos_sensor(0xAE8B, 0x29);
	write_cmos_sensor(0xB812, 0x00);

	/* LNR setting */
	write_cmos_sensor(0x3516, 0x00);
	write_cmos_sensor(0x3517, 0x00);
	write_cmos_sensor(0x3518, 0x00);
	write_cmos_sensor(0x3519, 0x00);
	write_cmos_sensor(0x351A, 0x00);
	write_cmos_sensor(0x351B, 0x00);
	write_cmos_sensor(0x351C, 0x00);
	write_cmos_sensor(0x351D, 0x00);
	write_cmos_sensor(0x351E, 0x00);
	write_cmos_sensor(0x351F, 0x00);
	write_cmos_sensor(0x3520, 0x00);
	write_cmos_sensor(0x3521, 0x00);
	write_cmos_sensor(0x3522, 0x00);

	/* stream on */
	write_cmos_sensor(0x0100, 0x01);
	set_thread_priority(0);
}	/* 1080P setting */

static void imx220_1312_984_setting(void)
{
	LOG_INF("%s\n", __func__);
	set_thread_priority(40);
	write_cmos_sensor(0x0100, 0x00);	/* stream off */
	/*
	 * 1312 x 984 @30fps
	 * Pclk = 184MHZ
	 * frame_length = 1036
	 * line_length = 5904
	 */
	/* Clock Setting */
	write_cmos_sensor(0x0301, 0x06);
	write_cmos_sensor(0x0303, 0x02);
	write_cmos_sensor(0x0304, 0x02);
	write_cmos_sensor(0x0305, 0x06);
	write_cmos_sensor(0x0306, 0x00);
	write_cmos_sensor(0x0307, 0x2E);
	write_cmos_sensor(0x0309, 0x0A);
	write_cmos_sensor(0x030B, 0x01);
	write_cmos_sensor(0x030C, 0x00);
	write_cmos_sensor(0x030D, 0x55);

	/* Size setting */
	write_cmos_sensor(0x0340, 0x04);
	write_cmos_sensor(0x0341, 0x0C);
	write_cmos_sensor(0x0342, 0x17);
	write_cmos_sensor(0x0343, 0x10);
	write_cmos_sensor(0x0344, 0x00);
	write_cmos_sensor(0x0345, 0x00);
	write_cmos_sensor(0x0346, 0x00);
	write_cmos_sensor(0x0347, 0x00);
	write_cmos_sensor(0x0348, 0x14);
	write_cmos_sensor(0x0349, 0x7F);
	write_cmos_sensor(0x034A, 0x0F);
	write_cmos_sensor(0x034B, 0x5F);
	write_cmos_sensor(0x034C, 0x05);
	write_cmos_sensor(0x034D, 0x20);
	write_cmos_sensor(0x034E, 0x03);
	write_cmos_sensor(0x034F, 0xD8);
	write_cmos_sensor(0x0350, 0x00);
	write_cmos_sensor(0x0351, 0x00);
	write_cmos_sensor(0x0352, 0x00);
	write_cmos_sensor(0x0353, 0x00);
	write_cmos_sensor(0x0354, 0x05);
	write_cmos_sensor(0x0355, 0x20);
	write_cmos_sensor(0x0356, 0x03);
	write_cmos_sensor(0x0357, 0xD8);
	write_cmos_sensor(0x901D, 0x08);

	/* Mode Setting */
	write_cmos_sensor(0x0108, 0x03);
	write_cmos_sensor(0x0112, 0x0A);
	write_cmos_sensor(0x0113, 0x0A);
	write_cmos_sensor(0x0381, 0x01);
	write_cmos_sensor(0x0383, 0x01);
	write_cmos_sensor(0x0385, 0x01);
	write_cmos_sensor(0x0387, 0x03);
	write_cmos_sensor(0x0390, 0x01);
	write_cmos_sensor(0x0391, 0x42);
	write_cmos_sensor(0x0392, 0x00);
	write_cmos_sensor(0x1307, 0x02);
	write_cmos_sensor(0x9512, 0x40);
	write_cmos_sensor(0x9873, 0x00);
	write_cmos_sensor(0xAB07, 0x01);
	write_cmos_sensor(0xB802, 0x01);
	write_cmos_sensor(0xB933, 0x01);

	/* OptionnalFunction setting */
	write_cmos_sensor(0x13C0, 0x00);
	write_cmos_sensor(0x13C1, 0x01);
	write_cmos_sensor(0x9909, 0x01);
	write_cmos_sensor(0x991E, 0x01);

	/* Global Timing Setting */
	write_cmos_sensor(0x0830, 0x00);
	write_cmos_sensor(0x0831, 0x57);
	write_cmos_sensor(0x0832, 0x00);
	write_cmos_sensor(0x0833, 0x1F);
	write_cmos_sensor(0x0834, 0x00);
	write_cmos_sensor(0x0835, 0x37);
	write_cmos_sensor(0x0836, 0x00);
	write_cmos_sensor(0x0837, 0x1F);
	write_cmos_sensor(0x0838, 0x00);
	write_cmos_sensor(0x0839, 0x17);
	write_cmos_sensor(0x083A, 0x00);
	write_cmos_sensor(0x083B, 0x17);
	write_cmos_sensor(0x083C, 0x00);
	write_cmos_sensor(0x083D, 0x77);
	write_cmos_sensor(0x083E, 0x00);
	write_cmos_sensor(0x083F, 0x17);
	write_cmos_sensor(0x0842, 0x00);
	write_cmos_sensor(0x0843, 0x0F);
	write_cmos_sensor(0x0844, 0x00);
	write_cmos_sensor(0x0845, 0x37);
	write_cmos_sensor(0x0847, 0x02);

	/* Integration Time Setting */
	write_cmos_sensor(0x0202, 0x04);
	write_cmos_sensor(0x0203, 0x00);

	/* Gain Setting */
	write_cmos_sensor(0x0205, 0x00);
	write_cmos_sensor(0x020E, 0x01);
	write_cmos_sensor(0x020F, 0x00);
	write_cmos_sensor(0x0210, 0x01);
	write_cmos_sensor(0x0211, 0x00);
	write_cmos_sensor(0x0212, 0x01);
	write_cmos_sensor(0x0213, 0x00);
	write_cmos_sensor(0x0214, 0x01);
	write_cmos_sensor(0x0215, 0x00);

	/* HDR Setting */
	write_cmos_sensor(0x0233, 0x00);
	write_cmos_sensor(0x0234, 0x00);
	write_cmos_sensor(0x0235, 0x40);
	write_cmos_sensor(0x0238, 0x00);
	write_cmos_sensor(0x0239, 0x08);
	write_cmos_sensor(0x13C2, 0x00);
	write_cmos_sensor(0x13C3, 0x00);
	write_cmos_sensor(0x13C4, 0x00);
	write_cmos_sensor(0xAD34, 0x14);
	write_cmos_sensor(0xAD35, 0x7F);
	write_cmos_sensor(0xAD36, 0x00);
	write_cmos_sensor(0xAD37, 0x00);
	write_cmos_sensor(0xAD38, 0x0F);
	write_cmos_sensor(0xAD39, 0x5D);
	write_cmos_sensor(0xAD3A, 0x00);
	write_cmos_sensor(0xAD3B, 0x00);
	write_cmos_sensor(0xAD3C, 0x14);
	write_cmos_sensor(0xAD3D, 0x7F);
	write_cmos_sensor(0xAD3E, 0x0F);
	write_cmos_sensor(0xAD3F, 0x5D);

	/* Bypass Setting */
	write_cmos_sensor(0xA303, 0x00);
	write_cmos_sensor(0xA403, 0x00);
	write_cmos_sensor(0xA602, 0x00);
	write_cmos_sensor(0xA703, 0x01);
	write_cmos_sensor(0xA903, 0x01);
	write_cmos_sensor(0xA956, 0x01);
	write_cmos_sensor(0xAA03, 0x01);
	write_cmos_sensor(0xAB03, 0x00);
	write_cmos_sensor(0xAD03, 0x01);
	write_cmos_sensor(0xAE03, 0x01);
	write_cmos_sensor(0xB803, 0x00);
	write_cmos_sensor(0xB903, 0x01);
	write_cmos_sensor(0xB91A, 0x01);
	write_cmos_sensor(0xBB03, 0x01);

	/* Single Correct setting */
	write_cmos_sensor(0x4003, 0x00);
	write_cmos_sensor(0x4004, 0x00);
	write_cmos_sensor(0x4005, 0x00);
	write_cmos_sensor(0x4006, 0x80);
	write_cmos_sensor(0x4007, 0x80);
	write_cmos_sensor(0x4008, 0x80);
	write_cmos_sensor(0x400C, 0x00);
	write_cmos_sensor(0x400D, 0x00);
	write_cmos_sensor(0x400E, 0x00);
	write_cmos_sensor(0x400F, 0x80);
	write_cmos_sensor(0x4010, 0x80);
	write_cmos_sensor(0x4011, 0x80);

	/* Mode etc setting */
	write_cmos_sensor(0x3008, 0x01);
	write_cmos_sensor(0x3009, 0x71);
	write_cmos_sensor(0x300A, 0x51);
	write_cmos_sensor(0x9431, 0x07);
	write_cmos_sensor(0xA913, 0x00);
	write_cmos_sensor(0xAC13, 0x00);
	write_cmos_sensor(0xAC3F, 0x00);
	write_cmos_sensor(0xAE8B, 0x36);
	write_cmos_sensor(0xB812, 0x01);

	/* LNR setting */
	write_cmos_sensor(0x3516, 0x00);
	write_cmos_sensor(0x3517, 0x00);
	write_cmos_sensor(0x3518, 0x00);
	write_cmos_sensor(0x3519, 0x00);
	write_cmos_sensor(0x351A, 0x00);
	write_cmos_sensor(0x351B, 0x00);
	write_cmos_sensor(0x351C, 0x00);
	write_cmos_sensor(0x351D, 0x00);
	write_cmos_sensor(0x351E, 0x00);
	write_cmos_sensor(0x351F, 0x00);
	write_cmos_sensor(0x3520, 0x00);
	write_cmos_sensor(0x3521, 0x00);
	write_cmos_sensor(0x3522, 0x00);

	/* stream on */
	write_cmos_sensor(0x0100, 0x01);
	set_thread_priority(0);
}	/* 1312x984 setting */

static void imx220_720p_hs_setting(void)
{
	LOG_INF("%s\n", __func__);
	set_thread_priority(40);
	write_cmos_sensor(0x0100, 0x00);	/* stream off */
	/*
	 * 1280 x 720 @100fps
	 * Pclk = 480MHZ
	 * frame_length = 792
	 * line_length = 5904
	 */
	/* Clock Setting */
	write_cmos_sensor(0x0301, 0x0A);
	write_cmos_sensor(0x0303, 0x01);
	write_cmos_sensor(0x0304, 0x03);
	write_cmos_sensor(0x0305, 0x06);
	write_cmos_sensor(0x0306, 0x00);
	write_cmos_sensor(0x0307, 0x96);
	write_cmos_sensor(0x0309, 0x0A);
	write_cmos_sensor(0x030B, 0x02);
	write_cmos_sensor(0x030C, 0x00);
	write_cmos_sensor(0x030D, 0xD0);

	/* Size setting */
	write_cmos_sensor(0x0340, 0x03);
	write_cmos_sensor(0x0341, 0x18);
	write_cmos_sensor(0x0342, 0x17);
	write_cmos_sensor(0x0343, 0x10);
	write_cmos_sensor(0x0344, 0x00);
	write_cmos_sensor(0x0345, 0x40);
	write_cmos_sensor(0x0346, 0x02);
	write_cmos_sensor(0x0347, 0x10);
	write_cmos_sensor(0x0348, 0x14);
	write_cmos_sensor(0x0349, 0x3F);
	write_cmos_sensor(0x034A, 0x0D);
	write_cmos_sensor(0x034B, 0x4F);
	write_cmos_sensor(0x034C, 0x05);
	write_cmos_sensor(0x034D, 0x00);
	write_cmos_sensor(0x034E, 0x02);
	write_cmos_sensor(0x034F, 0xD0);
	write_cmos_sensor(0x0350, 0x00);
	write_cmos_sensor(0x0351, 0x00);
	write_cmos_sensor(0x0352, 0x00);
	write_cmos_sensor(0x0353, 0x00);
	write_cmos_sensor(0x0354, 0x05);
	write_cmos_sensor(0x0355, 0x00);
	write_cmos_sensor(0x0356, 0x02);
	write_cmos_sensor(0x0357, 0xD0);
	write_cmos_sensor(0x901D, 0x08);

	/* Mode Setting */
	write_cmos_sensor(0x0108, 0x03);
	write_cmos_sensor(0x0112, 0x0A);
	write_cmos_sensor(0x0113, 0x0A);
	write_cmos_sensor(0x0381, 0x01);
	write_cmos_sensor(0x0383, 0x01);
	write_cmos_sensor(0x0385, 0x01);
	write_cmos_sensor(0x0387, 0x03);
	write_cmos_sensor(0x0390, 0x01);
	write_cmos_sensor(0x0391, 0x42);
	write_cmos_sensor(0x0392, 0x00);
	write_cmos_sensor(0x1307, 0x02);
	write_cmos_sensor(0x9512, 0x40);
	write_cmos_sensor(0x9873, 0x00);
	write_cmos_sensor(0xAB07, 0x01);
	write_cmos_sensor(0xB802, 0x01);
	write_cmos_sensor(0xB933, 0x01);

	/* OptionnalFunction setting */
	write_cmos_sensor(0x13C0, 0x00);
	write_cmos_sensor(0x13C1, 0x01);
	write_cmos_sensor(0x9909, 0x01);
	write_cmos_sensor(0x991E, 0x01);

	/* Global Timing Setting */
	write_cmos_sensor(0x0830, 0x00);
	write_cmos_sensor(0x0831, 0x47);
	write_cmos_sensor(0x0832, 0x00);
	write_cmos_sensor(0x0833, 0x0F);
	write_cmos_sensor(0x0834, 0x00);
	write_cmos_sensor(0x0835, 0x1F);
	write_cmos_sensor(0x0836, 0x00);
	write_cmos_sensor(0x0837, 0x17);
	write_cmos_sensor(0x0838, 0x00);
	write_cmos_sensor(0x0839, 0x0F);
	write_cmos_sensor(0x083A, 0x00);
	write_cmos_sensor(0x083B, 0x0F);
	write_cmos_sensor(0x083C, 0x00);
	write_cmos_sensor(0x083D, 0x47);
	write_cmos_sensor(0x083E, 0x00);
	write_cmos_sensor(0x083F, 0x0F);
	write_cmos_sensor(0x0842, 0x00);
	write_cmos_sensor(0x0843, 0x0F);
	write_cmos_sensor(0x0844, 0x00);
	write_cmos_sensor(0x0845, 0x37);
	write_cmos_sensor(0x0847, 0x02);

	/* Integration Time Setting */
	write_cmos_sensor(0x0202, 0x03);
	write_cmos_sensor(0x0203, 0x30);

	/* Gain Setting */
	write_cmos_sensor(0x0205, 0x00);
	write_cmos_sensor(0x020E, 0x01);
	write_cmos_sensor(0x020F, 0x00);
	write_cmos_sensor(0x0210, 0x01);
	write_cmos_sensor(0x0211, 0x00);
	write_cmos_sensor(0x0212, 0x01);
	write_cmos_sensor(0x0213, 0x00);
	write_cmos_sensor(0x0214, 0x01);
	write_cmos_sensor(0x0215, 0x00);

	/* HDR Setting */
	write_cmos_sensor(0x0233, 0x00);
	write_cmos_sensor(0x0234, 0x00);
	write_cmos_sensor(0x0235, 0x40);
	write_cmos_sensor(0x0238, 0x00);
	write_cmos_sensor(0x0239, 0x08);
	write_cmos_sensor(0x13C2, 0x00);
	write_cmos_sensor(0x13C3, 0x00);
	write_cmos_sensor(0x13C4, 0x00);
	write_cmos_sensor(0xAD34, 0x14);
	write_cmos_sensor(0xAD35, 0x7F);
	write_cmos_sensor(0xAD36, 0x00);
	write_cmos_sensor(0xAD37, 0x00);
	write_cmos_sensor(0xAD38, 0x0F);
	write_cmos_sensor(0xAD39, 0x5D);
	write_cmos_sensor(0xAD3A, 0x00);
	write_cmos_sensor(0xAD3B, 0x00);
	write_cmos_sensor(0xAD3C, 0x14);
	write_cmos_sensor(0xAD3D, 0x7F);
	write_cmos_sensor(0xAD3E, 0x0F);
	write_cmos_sensor(0xAD3F, 0x5D);

	/* Bypass Setting */
	write_cmos_sensor(0xA303, 0x00);
	write_cmos_sensor(0xA403, 0x00);
	write_cmos_sensor(0xA602, 0x00);
	write_cmos_sensor(0xA703, 0x01);
	write_cmos_sensor(0xA903, 0x01);
	write_cmos_sensor(0xA956, 0x01);
	write_cmos_sensor(0xAA03, 0x01);
	write_cmos_sensor(0xAB03, 0x00);
	write_cmos_sensor(0xAD03, 0x01);
	write_cmos_sensor(0xAE03, 0x01);
	write_cmos_sensor(0xB803, 0x00);
	write_cmos_sensor(0xB903, 0x01);
	write_cmos_sensor(0xB91A, 0x01);
	write_cmos_sensor(0xBB03, 0x01);

	/* Single Correct setting */
	write_cmos_sensor(0x4003, 0x00);
	write_cmos_sensor(0x4004, 0x00);
	write_cmos_sensor(0x4005, 0x00);
	write_cmos_sensor(0x4006, 0x80);
	write_cmos_sensor(0x4007, 0x80);
	write_cmos_sensor(0x4008, 0x80);
	write_cmos_sensor(0x400C, 0x00);
	write_cmos_sensor(0x400D, 0x00);
	write_cmos_sensor(0x400E, 0x00);
	write_cmos_sensor(0x400F, 0x80);
	write_cmos_sensor(0x4010, 0x80);
	write_cmos_sensor(0x4011, 0x80);

	/* Mode etc setting */
	write_cmos_sensor(0x3008, 0x01);
	write_cmos_sensor(0x3009, 0x71);
	write_cmos_sensor(0x300A, 0x51);
	write_cmos_sensor(0x9431, 0x07);
	write_cmos_sensor(0xA913, 0x00);
	write_cmos_sensor(0xAC13, 0x00);
	write_cmos_sensor(0xAC3F, 0x00);
	write_cmos_sensor(0xAE8B, 0x29);
	write_cmos_sensor(0xB812, 0x01);

	/* LNR setting */
	write_cmos_sensor(0x3516, 0x00);
	write_cmos_sensor(0x3517, 0x00);
	write_cmos_sensor(0x3518, 0x00);
	write_cmos_sensor(0x3519, 0x00);
	write_cmos_sensor(0x351A, 0x00);
	write_cmos_sensor(0x351B, 0x00);
	write_cmos_sensor(0x351C, 0x00);
	write_cmos_sensor(0x351D, 0x00);
	write_cmos_sensor(0x351E, 0x00);
	write_cmos_sensor(0x351F, 0x00);
	write_cmos_sensor(0x3520, 0x00);
	write_cmos_sensor(0x3521, 0x00);
	write_cmos_sensor(0x3522, 0x00);

	/* stream on */
	write_cmos_sensor(0x0100, 0x01);
	set_thread_priority(0);
}	/* 720p high speed setting */

static void imx220_5M_setting(void)
{
	LOG_INF("%s\n", __func__);
	/*preview(1/2binning)
	 * H : 2624
	 * V : 1968
	*/
	set_thread_priority(40);
	write_cmos_sensor(0x0100,0x00);	
	/* Clock Setting		
		Address value */
	write_cmos_sensor(0x0301, 0x0A);
	write_cmos_sensor(0x0303, 0x01);
	write_cmos_sensor(0x0304, 0x03);
	write_cmos_sensor(0x0305, 0x06);
	write_cmos_sensor(0x0306, 0x00);
	write_cmos_sensor(0x0307, 0x6F);
	write_cmos_sensor(0x0309, 0x0A);
	write_cmos_sensor(0x030B, 0x01);
	write_cmos_sensor(0x030C, 0x00);
	write_cmos_sensor(0x030D, 0x70);
			
	/*Size setting		
		Address value*/
	write_cmos_sensor(0x0340, 0x07);
	write_cmos_sensor(0x0341, 0xE0);
	write_cmos_sensor(0x0342, 0x17);
	write_cmos_sensor(0x0343, 0x10);
	write_cmos_sensor(0x0344, 0x00);
	write_cmos_sensor(0x0345, 0x00);
	write_cmos_sensor(0x0346, 0x00);
	write_cmos_sensor(0x0347, 0x00);
	write_cmos_sensor(0x0348, 0x14);  //5248
	write_cmos_sensor(0x0349, 0x7F);
	write_cmos_sensor(0x034A, 0x0F);  //3936
	write_cmos_sensor(0x034B, 0x5F);
	write_cmos_sensor(0x034C, 0x0A);  //2624
	write_cmos_sensor(0x034D, 0x40);
	write_cmos_sensor(0x034E, 0x07);  //1968
	write_cmos_sensor(0x034F, 0xB0);
	write_cmos_sensor(0x0350, 0x00);
	write_cmos_sensor(0x0351, 0x00);
	write_cmos_sensor(0x0352, 0x00);
	write_cmos_sensor(0x0353, 0x00);
	write_cmos_sensor(0x0354, 0x0A);
	write_cmos_sensor(0x0355, 0x40);
	write_cmos_sensor(0x0356, 0x07);
	write_cmos_sensor(0x0357, 0xB0);
	write_cmos_sensor(0x901D, 0x08);
			
	/*Mode Setting		
		Address value*/
	write_cmos_sensor(0x0108, 0x03);
	write_cmos_sensor(0x0112, 0x0A);
	write_cmos_sensor(0x0113, 0x0A);
	write_cmos_sensor(0x0381, 0x01);
	write_cmos_sensor(0x0383, 0x01);
	write_cmos_sensor(0x0385, 0x01);
	write_cmos_sensor(0x0387, 0x01);
	write_cmos_sensor(0x0390, 0x01);
	write_cmos_sensor(0x0391, 0x22);
	write_cmos_sensor(0x0392, 0x00);
	write_cmos_sensor(0x1307, 0x02);
	write_cmos_sensor(0x9512, 0x40);
	write_cmos_sensor(0x9873, 0x00);
	write_cmos_sensor(0xAB07, 0x01);
	write_cmos_sensor(0xB802, 0x01);
	write_cmos_sensor(0xB933, 0x00);
			
	/*OptionnalFunction setting 	
		Address value */
	write_cmos_sensor(0x13C0, 0x00);
	write_cmos_sensor(0x13C1, 0x01);
	write_cmos_sensor(0x9909, 0x01);
	write_cmos_sensor(0x991E, 0x01);
			
	/*Global Timing Setting 	
		Address value*/
	write_cmos_sensor(0x0830, 0x00);
	write_cmos_sensor(0x0831, 0x5F);
	write_cmos_sensor(0x0832, 0x00);
	write_cmos_sensor(0x0833, 0x27);
	write_cmos_sensor(0x0834, 0x00);
	write_cmos_sensor(0x0835, 0x47);
	write_cmos_sensor(0x0836, 0x00);
	write_cmos_sensor(0x0837, 0x27);
	write_cmos_sensor(0x0838, 0x00);
	write_cmos_sensor(0x0839, 0x1F);
	write_cmos_sensor(0x083A, 0x00);
	write_cmos_sensor(0x083B, 0x27);
	write_cmos_sensor(0x083C, 0x00);
	write_cmos_sensor(0x083D, 0x97);
	write_cmos_sensor(0x083E, 0x00);
	write_cmos_sensor(0x083F, 0x1F);
	write_cmos_sensor(0x0842, 0x00);
	write_cmos_sensor(0x0843, 0x0F);
	write_cmos_sensor(0x0844, 0x00);
	write_cmos_sensor(0x0845, 0x37);
	write_cmos_sensor(0x0847, 0x02);
			
	/*Integration Time Setting		
		Address value*/
	write_cmos_sensor(0x0202, 0x07);
	write_cmos_sensor(0x0203, 0xD8);
			
	/*Gain Setting		
		Address value*/
	write_cmos_sensor(0x0205, 0x00);
	write_cmos_sensor(0x020E, 0x01);
	write_cmos_sensor(0x020F, 0x00);
	write_cmos_sensor(0x0210, 0x01);
	write_cmos_sensor(0x0211, 0x00);
	write_cmos_sensor(0x0212, 0x01);
	write_cmos_sensor(0x0213, 0x00);
	write_cmos_sensor(0x0214, 0x01);
	write_cmos_sensor(0x0215, 0x00);
			
	/*HDR Setting		
		Address value*/
	write_cmos_sensor(0x0233, 0x00);
	write_cmos_sensor(0x0234, 0x00);
	write_cmos_sensor(0x0235, 0x40);
	write_cmos_sensor(0x0238, 0x00);
	write_cmos_sensor(0x0239, 0x08);
	write_cmos_sensor(0x13C2, 0x00);
	write_cmos_sensor(0x13C3, 0x00);
	write_cmos_sensor(0x13C4, 0x00);
	write_cmos_sensor(0xAD34, 0x14);
	write_cmos_sensor(0xAD35, 0x7F);
	write_cmos_sensor(0xAD36, 0x00);
	write_cmos_sensor(0xAD37, 0x00);
	write_cmos_sensor(0xAD38, 0x0F);
	write_cmos_sensor(0xAD39, 0x5D);
	write_cmos_sensor(0xAD3A, 0x00);
	write_cmos_sensor(0xAD3B, 0x00);
	write_cmos_sensor(0xAD3C, 0x14);
	write_cmos_sensor(0xAD3D, 0x7F);
	write_cmos_sensor(0xAD3E, 0x0F);
	write_cmos_sensor(0xAD3F, 0x5D);
			
	/*Bypass Setting		
		Address value*/
	write_cmos_sensor(0xA303, 0x00);
	write_cmos_sensor(0xA403, 0x00);
	write_cmos_sensor(0xA602, 0x00);
	write_cmos_sensor(0xA703, 0x01);
	write_cmos_sensor(0xA903, 0x01);
	write_cmos_sensor(0xA956, 0x01);
	write_cmos_sensor(0xAA03, 0x01);
	write_cmos_sensor(0xAB03, 0x00);
	write_cmos_sensor(0xAD03, 0x01);
	write_cmos_sensor(0xAE03, 0x01);
	write_cmos_sensor(0xB803, 0x01);
	write_cmos_sensor(0xB903, 0x01);
	write_cmos_sensor(0xB91A, 0x01);
	write_cmos_sensor(0xBB03, 0x01);
			
	/*Single Correct setting		
		Address value*/
	write_cmos_sensor(0x4003, 0x00);
	write_cmos_sensor(0x4004, 0x00);
	write_cmos_sensor(0x4005, 0x00);
	write_cmos_sensor(0x4006, 0x80);
	write_cmos_sensor(0x4007, 0x80);
	write_cmos_sensor(0x4008, 0x80);
	write_cmos_sensor(0x400C, 0x00);
	write_cmos_sensor(0x400D, 0x00);
	write_cmos_sensor(0x400E, 0x00);
	write_cmos_sensor(0x400F, 0x80);
	write_cmos_sensor(0x4010, 0x80);
	write_cmos_sensor(0x4011, 0x80);
			
	/*Mode etc setting		
		Address value*/
	write_cmos_sensor(0x3008, 0x01);
	write_cmos_sensor(0x3009, 0x71);
	write_cmos_sensor(0x300A, 0x51);
	write_cmos_sensor(0x9431, 0x03);
	write_cmos_sensor(0xA913, 0x00);
	write_cmos_sensor(0xAC13, 0x00);
	write_cmos_sensor(0xAC3F, 0x00);
	write_cmos_sensor(0xAE8B, 0x36);
	write_cmos_sensor(0xB812, 0x00);
			
	/*LNR setting		
		Address value*/
	write_cmos_sensor(0x3516, 0x00);
	write_cmos_sensor(0x3517, 0x00);
	write_cmos_sensor(0x3518, 0x00);
	write_cmos_sensor(0x3519, 0x00);
	write_cmos_sensor(0x351A, 0x00);
	write_cmos_sensor(0x351B, 0x00);
	write_cmos_sensor(0x351C, 0x00);
	write_cmos_sensor(0x351D, 0x00);
	write_cmos_sensor(0x351E, 0x00);
	write_cmos_sensor(0x351F, 0x00);
	write_cmos_sensor(0x3520, 0x00);
	write_cmos_sensor(0x3521, 0x00);
	write_cmos_sensor(0x3522, 0x00);

	set_thread_priority(0);
	/*
	 * stream-on will be done after set shutter/gain.
	 */
}	/* 5M_setting */

static void pre_4m_setting(void)
{
	LOG_INF("%s\n", __func__);
	/* 4m preview setting(1/2binning)
	 * H : 2336
	 * V : 1760
	*/
	set_thread_priority(40);
	write_cmos_sensor(0x0100,0x00);	
	/* Clock Setting		
		Address value */
	write_cmos_sensor(0x0301, 0x0A);
	write_cmos_sensor(0x0303, 0x01);
	write_cmos_sensor(0x0304, 0x03);
	write_cmos_sensor(0x0305, 0x06);
	write_cmos_sensor(0x0306, 0x00);
	write_cmos_sensor(0x0307, 0x6F);
	write_cmos_sensor(0x0309, 0x0A);
	write_cmos_sensor(0x030B, 0x01);
	write_cmos_sensor(0x030C, 0x00);
	write_cmos_sensor(0x030D, 0x70);

	/*Size setting		
		Address value*/
	write_cmos_sensor(0x0340, 0x07);
	write_cmos_sensor(0x0341, 0xD0);
	write_cmos_sensor(0x0342, 0x17);
	write_cmos_sensor(0x0343, 0x10);
	write_cmos_sensor(0x0344, 0x01);
	write_cmos_sensor(0x0345, 0x20);
	write_cmos_sensor(0x0346, 0x00);
	write_cmos_sensor(0x0347, 0xD0);
	write_cmos_sensor(0x0348, 0x13);
	write_cmos_sensor(0x0349, 0x5F);
	write_cmos_sensor(0x034A, 0x0E);
	write_cmos_sensor(0x034B, 0x8F);
	write_cmos_sensor(0x034C, 0x09);
	write_cmos_sensor(0x034D, 0x20);
	write_cmos_sensor(0x034E, 0x06);
	write_cmos_sensor(0x034F, 0xE0);
	write_cmos_sensor(0x0350, 0x00);
	write_cmos_sensor(0x0351, 0x00);
	write_cmos_sensor(0x0352, 0x00);
	write_cmos_sensor(0x0353, 0x00);
	write_cmos_sensor(0x0354, 0x09);
	write_cmos_sensor(0x0355, 0x20);
	write_cmos_sensor(0x0356, 0x06);
	write_cmos_sensor(0x0357, 0xE0);
	write_cmos_sensor(0x901D, 0x08);
			
	/*Mode Setting		
		Address value*/
	write_cmos_sensor(0x0108, 0x03);
	write_cmos_sensor(0x0112, 0x0A);
	write_cmos_sensor(0x0113, 0x0A);
	write_cmos_sensor(0x0381, 0x01);
	write_cmos_sensor(0x0383, 0x01);
	write_cmos_sensor(0x0385, 0x01);
	write_cmos_sensor(0x0387, 0x01);
	write_cmos_sensor(0x0390, 0x01);
	write_cmos_sensor(0x0391, 0x22);
	write_cmos_sensor(0x0392, 0x00);
	write_cmos_sensor(0x1307, 0x02);
	write_cmos_sensor(0x9512, 0x40);
	write_cmos_sensor(0x9873, 0x00);
	write_cmos_sensor(0xAB07, 0x01);
	write_cmos_sensor(0xB802, 0x01);
	write_cmos_sensor(0xB933, 0x00);
			
	/*OptionnalFunction setting 	
		Address value */
	write_cmos_sensor(0x13C0, 0x00);
	write_cmos_sensor(0x13C1, 0x01);
	write_cmos_sensor(0x9909, 0x01);
	write_cmos_sensor(0x991E, 0x01);
			
	/*Global Timing Setting 	
		Address value*/
	write_cmos_sensor(0x0830, 0x00);
	write_cmos_sensor(0x0831, 0x5F);
	write_cmos_sensor(0x0832, 0x00);
	write_cmos_sensor(0x0833, 0x27);
	write_cmos_sensor(0x0834, 0x00);
	write_cmos_sensor(0x0835, 0x47);
	write_cmos_sensor(0x0836, 0x00);
	write_cmos_sensor(0x0837, 0x27);
	write_cmos_sensor(0x0838, 0x00);
	write_cmos_sensor(0x0839, 0x1F);
	write_cmos_sensor(0x083A, 0x00);
	write_cmos_sensor(0x083B, 0x27);
	write_cmos_sensor(0x083C, 0x00);
	write_cmos_sensor(0x083D, 0x97);
	write_cmos_sensor(0x083E, 0x00);
	write_cmos_sensor(0x083F, 0x1F);
	write_cmos_sensor(0x0842, 0x00);
	write_cmos_sensor(0x0843, 0x0F);
	write_cmos_sensor(0x0844, 0x00);
	write_cmos_sensor(0x0845, 0x37);
	write_cmos_sensor(0x0847, 0x02);

	/*Integration Time Setting		
		Address value*/
	write_cmos_sensor(0x0202, 0x07);
	write_cmos_sensor(0x0203, 0xC8);
			
	/*Gain Setting		
		Address value*/
	write_cmos_sensor(0x0205, 0x00);
	write_cmos_sensor(0x020E, 0x01);
	write_cmos_sensor(0x020F, 0x00);
	write_cmos_sensor(0x0210, 0x01);
	write_cmos_sensor(0x0211, 0x00);
	write_cmos_sensor(0x0212, 0x01);
	write_cmos_sensor(0x0213, 0x00);
	write_cmos_sensor(0x0214, 0x01);
	write_cmos_sensor(0x0215, 0x00);
			
	/*HDR Setting		
		Address value*/
	write_cmos_sensor(0x0233, 0x00);
	write_cmos_sensor(0x0234, 0x00);
	write_cmos_sensor(0x0235, 0x40);
	write_cmos_sensor(0x0238, 0x00);
	write_cmos_sensor(0x0239, 0x08);
	write_cmos_sensor(0x13C2, 0x00);
	write_cmos_sensor(0x13C3, 0x00);
	write_cmos_sensor(0x13C4, 0x00);
	write_cmos_sensor(0xAD34, 0x14);
	write_cmos_sensor(0xAD35, 0x7F);
	write_cmos_sensor(0xAD36, 0x00);
	write_cmos_sensor(0xAD37, 0x00);
	write_cmos_sensor(0xAD38, 0x0F);
	write_cmos_sensor(0xAD39, 0x5D);
	write_cmos_sensor(0xAD3A, 0x00);
	write_cmos_sensor(0xAD3B, 0x00);
	write_cmos_sensor(0xAD3C, 0x14);
	write_cmos_sensor(0xAD3D, 0x7F);
	write_cmos_sensor(0xAD3E, 0x0F);
	write_cmos_sensor(0xAD3F, 0x5D);
			
	/*Bypass Setting		
		Address value*/
	write_cmos_sensor(0xA303, 0x00);
	write_cmos_sensor(0xA403, 0x00);
	write_cmos_sensor(0xA602, 0x00);
	write_cmos_sensor(0xA703, 0x01);
	write_cmos_sensor(0xA903, 0x01);
	write_cmos_sensor(0xA956, 0x01);
	write_cmos_sensor(0xAA03, 0x01);
	write_cmos_sensor(0xAB03, 0x00);
	write_cmos_sensor(0xAD03, 0x01);
	write_cmos_sensor(0xAE03, 0x01);
	write_cmos_sensor(0xB803, 0x01);
	write_cmos_sensor(0xB903, 0x01);
	write_cmos_sensor(0xB91A, 0x01);
	write_cmos_sensor(0xBB03, 0x01);
			
	/*Single Correct setting		
		Address value*/
	write_cmos_sensor(0x4003, 0x00);
	write_cmos_sensor(0x4004, 0x00);
	write_cmos_sensor(0x4005, 0x00);
	write_cmos_sensor(0x4006, 0x80);
	write_cmos_sensor(0x4007, 0x80);
	write_cmos_sensor(0x4008, 0x80);
	write_cmos_sensor(0x400C, 0x00);
	write_cmos_sensor(0x400D, 0x00);
	write_cmos_sensor(0x400E, 0x00);
	write_cmos_sensor(0x400F, 0x80);
	write_cmos_sensor(0x4010, 0x80);
	write_cmos_sensor(0x4011, 0x80);
			
	/*Mode etc setting		
		Address value*/
	write_cmos_sensor(0x3008, 0x01);
	write_cmos_sensor(0x3009, 0x71);
	write_cmos_sensor(0x300A, 0x51);
	write_cmos_sensor(0x9431, 0x03);
	write_cmos_sensor(0xA913, 0x00);
	write_cmos_sensor(0xAC13, 0x00);
	write_cmos_sensor(0xAC3F, 0x00);
	write_cmos_sensor(0xAE8B, 0x36);
	write_cmos_sensor(0xB812, 0x00);
			
	/*LNR setting		
		Address value*/
	write_cmos_sensor(0x3516, 0x00);
	write_cmos_sensor(0x3517, 0x00);
	write_cmos_sensor(0x3518, 0x00);
	write_cmos_sensor(0x3519, 0x00);
	write_cmos_sensor(0x351A, 0x00);
	write_cmos_sensor(0x351B, 0x00);
	write_cmos_sensor(0x351C, 0x00);
	write_cmos_sensor(0x351D, 0x00);
	write_cmos_sensor(0x351E, 0x00);
	write_cmos_sensor(0x351F, 0x00);
	write_cmos_sensor(0x3520, 0x00);
	write_cmos_sensor(0x3521, 0x00);
	write_cmos_sensor(0x3522, 0x00);

	set_thread_priority(0);
	/*
	 * stream-on will be done after set shutter/gain.
	 */
}	/* preview 4m setting */

static void preview_setting(void)
{
	LOG_INF("%s\n", __func__);
	/*preview(1/2binning)
	 * H : 2624
	 * V : 1968
	*/
	set_thread_priority(40);
	write_cmos_sensor(0x0100,0x00);	
	/* Clock Setting		
		Address value */
	write_cmos_sensor(0x0301, 0x0A);
	write_cmos_sensor(0x0303, 0x01);
	write_cmos_sensor(0x0304, 0x03);
	write_cmos_sensor(0x0305, 0x06);
	write_cmos_sensor(0x0306, 0x00);
	write_cmos_sensor(0x0307, 0x6F);
	write_cmos_sensor(0x0309, 0x0A);
	write_cmos_sensor(0x030B, 0x01);
	write_cmos_sensor(0x030C, 0x00);
	write_cmos_sensor(0x030D, 0x70);
			
	/*Size setting		
		Address value*/
	write_cmos_sensor(0x0340, 0x07);
	write_cmos_sensor(0x0341, 0xE0);
	write_cmos_sensor(0x0342, 0x17);
	write_cmos_sensor(0x0343, 0x10);
	write_cmos_sensor(0x0344, 0x00);
	write_cmos_sensor(0x0345, 0x00);
	write_cmos_sensor(0x0346, 0x00);
	write_cmos_sensor(0x0347, 0x00);
	write_cmos_sensor(0x0348, 0x14);
	write_cmos_sensor(0x0349, 0x7F);
	write_cmos_sensor(0x034A, 0x0F);
	write_cmos_sensor(0x034B, 0x5F);
	write_cmos_sensor(0x034C, 0x0A);
	write_cmos_sensor(0x034D, 0x40);
	write_cmos_sensor(0x034E, 0x07);
	write_cmos_sensor(0x034F, 0xB0);
	write_cmos_sensor(0x0350, 0x00);
	write_cmos_sensor(0x0351, 0x00);
	write_cmos_sensor(0x0352, 0x00);
	write_cmos_sensor(0x0353, 0x00);
	write_cmos_sensor(0x0354, 0x0A);
	write_cmos_sensor(0x0355, 0x40);
	write_cmos_sensor(0x0356, 0x07);
	write_cmos_sensor(0x0357, 0xB0);
	write_cmos_sensor(0x901D, 0x08);
			
	/*Mode Setting		
		Address value*/
	write_cmos_sensor(0x0108, 0x03);
	write_cmos_sensor(0x0112, 0x0A);
	write_cmos_sensor(0x0113, 0x0A);
	write_cmos_sensor(0x0381, 0x01);
	write_cmos_sensor(0x0383, 0x01);
	write_cmos_sensor(0x0385, 0x01);
	write_cmos_sensor(0x0387, 0x01);
	write_cmos_sensor(0x0390, 0x01);
	write_cmos_sensor(0x0391, 0x22);
	write_cmos_sensor(0x0392, 0x00);
	write_cmos_sensor(0x1307, 0x02);
	write_cmos_sensor(0x9512, 0x40);
	write_cmos_sensor(0x9873, 0x00);
	write_cmos_sensor(0xAB07, 0x01);
	write_cmos_sensor(0xB802, 0x01);
	write_cmos_sensor(0xB933, 0x00);
			
	/*OptionnalFunction setting 	
		Address value */
	write_cmos_sensor(0x13C0, 0x00);
	write_cmos_sensor(0x13C1, 0x01);
	write_cmos_sensor(0x9909, 0x01);
	write_cmos_sensor(0x991E, 0x01);
			
	/*Global Timing Setting 	
		Address value*/
	write_cmos_sensor(0x0830, 0x00);
	write_cmos_sensor(0x0831, 0x5F);
	write_cmos_sensor(0x0832, 0x00);
	write_cmos_sensor(0x0833, 0x27);
	write_cmos_sensor(0x0834, 0x00);
	write_cmos_sensor(0x0835, 0x47);
	write_cmos_sensor(0x0836, 0x00);
	write_cmos_sensor(0x0837, 0x27);
	write_cmos_sensor(0x0838, 0x00);
	write_cmos_sensor(0x0839, 0x1F);
	write_cmos_sensor(0x083A, 0x00);
	write_cmos_sensor(0x083B, 0x27);
	write_cmos_sensor(0x083C, 0x00);
	write_cmos_sensor(0x083D, 0x97);
	write_cmos_sensor(0x083E, 0x00);
	write_cmos_sensor(0x083F, 0x1F);
	write_cmos_sensor(0x0842, 0x00);
	write_cmos_sensor(0x0843, 0x0F);
	write_cmos_sensor(0x0844, 0x00);
	write_cmos_sensor(0x0845, 0x37);
	write_cmos_sensor(0x0847, 0x02);
			
	/*Integration Time Setting		
		Address value*/
	write_cmos_sensor(0x0202, 0x07);
	write_cmos_sensor(0x0203, 0xD8);
			
	/*Gain Setting		
		Address value*/
	write_cmos_sensor(0x0205, 0x00);
	write_cmos_sensor(0x020E, 0x01);
	write_cmos_sensor(0x020F, 0x00);
	write_cmos_sensor(0x0210, 0x01);
	write_cmos_sensor(0x0211, 0x00);
	write_cmos_sensor(0x0212, 0x01);
	write_cmos_sensor(0x0213, 0x00);
	write_cmos_sensor(0x0214, 0x01);
	write_cmos_sensor(0x0215, 0x00);
			
	/*HDR Setting		
		Address value*/
	write_cmos_sensor(0x0233, 0x00);
	write_cmos_sensor(0x0234, 0x00);
	write_cmos_sensor(0x0235, 0x40);
	write_cmos_sensor(0x0238, 0x00);
	write_cmos_sensor(0x0239, 0x08);
	write_cmos_sensor(0x13C2, 0x00);
	write_cmos_sensor(0x13C3, 0x00);
	write_cmos_sensor(0x13C4, 0x00);
	write_cmos_sensor(0xAD34, 0x14);
	write_cmos_sensor(0xAD35, 0x7F);
	write_cmos_sensor(0xAD36, 0x00);
	write_cmos_sensor(0xAD37, 0x00);
	write_cmos_sensor(0xAD38, 0x0F);
	write_cmos_sensor(0xAD39, 0x5D);
	write_cmos_sensor(0xAD3A, 0x00);
	write_cmos_sensor(0xAD3B, 0x00);
	write_cmos_sensor(0xAD3C, 0x14);
	write_cmos_sensor(0xAD3D, 0x7F);
	write_cmos_sensor(0xAD3E, 0x0F);
	write_cmos_sensor(0xAD3F, 0x5D);
			
	/*Bypass Setting		
		Address value*/
	write_cmos_sensor(0xA303, 0x00);
	write_cmos_sensor(0xA403, 0x00);
	write_cmos_sensor(0xA602, 0x00);
	write_cmos_sensor(0xA703, 0x01);
	write_cmos_sensor(0xA903, 0x01);
	write_cmos_sensor(0xA956, 0x01);
	write_cmos_sensor(0xAA03, 0x01);
	write_cmos_sensor(0xAB03, 0x00);
	write_cmos_sensor(0xAD03, 0x01);
	write_cmos_sensor(0xAE03, 0x01);
	write_cmos_sensor(0xB803, 0x01);
	write_cmos_sensor(0xB903, 0x01);
	write_cmos_sensor(0xB91A, 0x01);
	write_cmos_sensor(0xBB03, 0x01);
			
	/*Single Correct setting		
		Address value*/
	write_cmos_sensor(0x4003, 0x00);
	write_cmos_sensor(0x4004, 0x00);
	write_cmos_sensor(0x4005, 0x00);
	write_cmos_sensor(0x4006, 0x80);
	write_cmos_sensor(0x4007, 0x80);
	write_cmos_sensor(0x4008, 0x80);
	write_cmos_sensor(0x400C, 0x00);
	write_cmos_sensor(0x400D, 0x00);
	write_cmos_sensor(0x400E, 0x00);
	write_cmos_sensor(0x400F, 0x80);
	write_cmos_sensor(0x4010, 0x80);
	write_cmos_sensor(0x4011, 0x80);
			
	/*Mode etc setting		
		Address value*/
	write_cmos_sensor(0x3008, 0x01);
	write_cmos_sensor(0x3009, 0x71);
	write_cmos_sensor(0x300A, 0x51);
	write_cmos_sensor(0x9431, 0x03);
	write_cmos_sensor(0xA913, 0x00);
	write_cmos_sensor(0xAC13, 0x00);
	write_cmos_sensor(0xAC3F, 0x00);
	write_cmos_sensor(0xAE8B, 0x36);
	write_cmos_sensor(0xB812, 0x00);
			
	/*LNR setting		
		Address value*/
	write_cmos_sensor(0x3516, 0x00);
	write_cmos_sensor(0x3517, 0x00);
	write_cmos_sensor(0x3518, 0x00);
	write_cmos_sensor(0x3519, 0x00);
	write_cmos_sensor(0x351A, 0x00);
	write_cmos_sensor(0x351B, 0x00);
	write_cmos_sensor(0x351C, 0x00);
	write_cmos_sensor(0x351D, 0x00);
	write_cmos_sensor(0x351E, 0x00);
	write_cmos_sensor(0x351F, 0x00);
	write_cmos_sensor(0x3520, 0x00);
	write_cmos_sensor(0x3521, 0x00);
	write_cmos_sensor(0x3522, 0x00);

	set_thread_priority(0);
	/*
	 * stream-on will be done after set shutter/gain.
	 */
}	/* preview_setting */

static void zsd_12M_setting(void)
{
	LOG_INF("%s\n", __func__);
	set_thread_priority(40);
	write_cmos_sensor(0x0100, 0x00);	/* stream off */
	/*
	 * 4608x2592 @25fps.
	 * pclk = 390.4MHZ
	 * frame_length = 2640 
	 * line_length = 5904
	 */
	/* Clock Setting */
	write_cmos_sensor(0x0301, 0x0A);
	write_cmos_sensor(0x0303, 0x01);
	write_cmos_sensor(0x0304, 0x03);
	write_cmos_sensor(0x0305, 0x06);
	write_cmos_sensor(0x0306, 0x00);
	write_cmos_sensor(0x0307, 0x7A);
	write_cmos_sensor(0x0309, 0x0A);
	write_cmos_sensor(0x030B, 0x01);
	write_cmos_sensor(0x030C, 0x00);
	write_cmos_sensor(0x030D, 0xD0);

	/* Size setting */
	write_cmos_sensor(0x0340, 0x0A);
	write_cmos_sensor(0x0341, 0x50);
	write_cmos_sensor(0x0342, 0x17);
	write_cmos_sensor(0x0343, 0x10);
	write_cmos_sensor(0x0344, 0x01);
	write_cmos_sensor(0x0345, 0x40);
	write_cmos_sensor(0x0346, 0x02);
	write_cmos_sensor(0x0347, 0xA0);
	write_cmos_sensor(0x0348, 0x13);
	write_cmos_sensor(0x0349, 0x3F);
	write_cmos_sensor(0x034A, 0x0C);
	write_cmos_sensor(0x034B, 0xBF);
	write_cmos_sensor(0x034C, 0x12);
	write_cmos_sensor(0x034D, 0x00);
	write_cmos_sensor(0x034E, 0x0A);
	write_cmos_sensor(0x034F, 0x20);
	write_cmos_sensor(0x0350, 0x00);
	write_cmos_sensor(0x0351, 0x00);
	write_cmos_sensor(0x0352, 0x00);
	write_cmos_sensor(0x0353, 0x00);
	write_cmos_sensor(0x0354, 0x12);
	write_cmos_sensor(0x0355, 0x00);
	write_cmos_sensor(0x0356, 0x0A);
	write_cmos_sensor(0x0357, 0x20);
	write_cmos_sensor(0x901D, 0x08);

	/* Mode Setting */
	write_cmos_sensor(0x0108, 0x03);
	write_cmos_sensor(0x0112, 0x0A);
	write_cmos_sensor(0x0113, 0x0A);
	write_cmos_sensor(0x0381, 0x01);
	write_cmos_sensor(0x0383, 0x01);
	write_cmos_sensor(0x0385, 0x01);
	write_cmos_sensor(0x0387, 0x01);
	write_cmos_sensor(0x0390, 0x00);
	write_cmos_sensor(0x0391, 0x11);
	write_cmos_sensor(0x0392, 0x00);
	write_cmos_sensor(0x1307, 0x00);
	write_cmos_sensor(0x9512, 0x40);
	write_cmos_sensor(0x9873, 0x00);
	write_cmos_sensor(0xAB07, 0x01);
	write_cmos_sensor(0xB802, 0x01);
	write_cmos_sensor(0xB933, 0x00);

	/* OptionnalFunction setting */
	write_cmos_sensor(0x13C0, 0x01);
	write_cmos_sensor(0x13C1, 0x02);
	write_cmos_sensor(0x9909, 0x01);
	write_cmos_sensor(0x991E, 0x01);

	/* Global Timing Setting */
	write_cmos_sensor(0x0830, 0x00);
	write_cmos_sensor(0x0831, 0x6F);
	write_cmos_sensor(0x0832, 0x00);
	write_cmos_sensor(0x0833, 0x2F);
	write_cmos_sensor(0x0834, 0x00);
	write_cmos_sensor(0x0835, 0x57);
	write_cmos_sensor(0x0836, 0x00);
	write_cmos_sensor(0x0837, 0x2F);
	write_cmos_sensor(0x0838, 0x00);
	write_cmos_sensor(0x0839, 0x2F);
	write_cmos_sensor(0x083A, 0x00);
	write_cmos_sensor(0x083B, 0x2F);
	write_cmos_sensor(0x083C, 0x00);
	write_cmos_sensor(0x083D, 0xBF);
	write_cmos_sensor(0x083E, 0x00);
	write_cmos_sensor(0x083F, 0x27);
	write_cmos_sensor(0x0842, 0x00);
	write_cmos_sensor(0x0843, 0x0F);
	write_cmos_sensor(0x0844, 0x00);
	write_cmos_sensor(0x0845, 0x37);
	write_cmos_sensor(0x0847, 0x02);

	/* Integration Time Setting */
	write_cmos_sensor(0x0202, 0x08);
	write_cmos_sensor(0x0203, 0x98);

	/* Gain Setting */
	write_cmos_sensor(0x0205, 0x00);
	write_cmos_sensor(0x020E, 0x01);
	write_cmos_sensor(0x020F, 0x00);
	write_cmos_sensor(0x0210, 0x01);
	write_cmos_sensor(0x0211, 0x00);
	write_cmos_sensor(0x0212, 0x01);
	write_cmos_sensor(0x0213, 0x00);
	write_cmos_sensor(0x0214, 0x01);
	write_cmos_sensor(0x0215, 0x00);

	/* HDR Setting */
	write_cmos_sensor(0x0233, 0x00);
	write_cmos_sensor(0x0234, 0x00);
	write_cmos_sensor(0x0235, 0x40);
	write_cmos_sensor(0x0238, 0x00);
	write_cmos_sensor(0x0239, 0x08);
	write_cmos_sensor(0x13C2, 0x00);
	write_cmos_sensor(0x13C3, 0x00);
	write_cmos_sensor(0x13C4, 0x00);
	write_cmos_sensor(0xAD34, 0x14);
	write_cmos_sensor(0xAD35, 0x7F);
	write_cmos_sensor(0xAD36, 0x00);
	write_cmos_sensor(0xAD37, 0x00);
	write_cmos_sensor(0xAD38, 0x0F);
	write_cmos_sensor(0xAD39, 0x5D);
	write_cmos_sensor(0xAD3A, 0x00);
	write_cmos_sensor(0xAD3B, 0x00);
	write_cmos_sensor(0xAD3C, 0x14);
	write_cmos_sensor(0xAD3D, 0x7F);
	write_cmos_sensor(0xAD3E, 0x0F);
	write_cmos_sensor(0xAD3F, 0x5D);

	/* Bypass Setting */
	write_cmos_sensor(0xA303, 0x00);
	write_cmos_sensor(0xA403, 0x00);
	write_cmos_sensor(0xA602, 0x00);
	write_cmos_sensor(0xA703, 0x01);
	write_cmos_sensor(0xA903, 0x01);
	write_cmos_sensor(0xA956, 0x01);
	write_cmos_sensor(0xAA03, 0x01);
	write_cmos_sensor(0xAB03, 0x01);
	write_cmos_sensor(0xAD03, 0x01);
	write_cmos_sensor(0xAE03, 0x01);
	write_cmos_sensor(0xB803, 0x00);
	write_cmos_sensor(0xB903, 0x01);
	write_cmos_sensor(0xB91A, 0x01);
	write_cmos_sensor(0xBB03, 0x01);

	/* Single Correct setting */
	write_cmos_sensor(0x4003, 0x00);
	write_cmos_sensor(0x4004, 0x00);
	write_cmos_sensor(0x4005, 0x00);
	write_cmos_sensor(0x4006, 0x80);
	write_cmos_sensor(0x4007, 0x80);
	write_cmos_sensor(0x4008, 0x80);
	write_cmos_sensor(0x400C, 0x00);
	write_cmos_sensor(0x400D, 0x00);
	write_cmos_sensor(0x400E, 0x00);
	write_cmos_sensor(0x400F, 0x80);
	write_cmos_sensor(0x4010, 0x80);
	write_cmos_sensor(0x4011, 0x80);

	/* Mode etc setting */
	write_cmos_sensor(0x3008, 0x01);
	write_cmos_sensor(0x3009, 0x71);
	write_cmos_sensor(0x300A, 0x51);
	write_cmos_sensor(0x9431, 0x03);
	write_cmos_sensor(0xA913, 0x00);
	write_cmos_sensor(0xAC13, 0x00);
	write_cmos_sensor(0xAC3F, 0x00);
	write_cmos_sensor(0xAE8B, 0x29);
	write_cmos_sensor(0xB812, 0x00);

	/* LNR setting */
	write_cmos_sensor(0x3516, 0x00);
	write_cmos_sensor(0x3517, 0x00);
	write_cmos_sensor(0x3518, 0x00);
	write_cmos_sensor(0x3519, 0x00);
	write_cmos_sensor(0x351A, 0x00);
	write_cmos_sensor(0x351B, 0x00);
	write_cmos_sensor(0x351C, 0x00);
	write_cmos_sensor(0x351D, 0x00);
	write_cmos_sensor(0x351E, 0x00);
	write_cmos_sensor(0x351F, 0x00);
	write_cmos_sensor(0x3520, 0x00);
	write_cmos_sensor(0x3521, 0x00);
	write_cmos_sensor(0x3522, 0x00);

	/* stream on */
	write_cmos_sensor(0x0100, 0x01);
	set_thread_priority(0);
}	/* zsd 12M setting */

static void cap_16m_setting(void)
{
	LOG_INF("%s\n", __func__);
	/*capture
	 * H : 4672
	 * V : 3504
	 */
	set_thread_priority(40);
	write_cmos_sensor(0x0100,0x00);	

	/* Clock Setting		
		Address value*/
	write_cmos_sensor(0x0301, 0x0A);
	write_cmos_sensor(0x0303, 0x01);
	write_cmos_sensor(0x0304, 0x03);
	write_cmos_sensor(0x0305, 0x06);
	write_cmos_sensor(0x0306, 0x00);
	write_cmos_sensor(0x0307, 0x6F);
	write_cmos_sensor(0x0309, 0x0A);
	write_cmos_sensor(0x030B, 0x01);
	write_cmos_sensor(0x030C, 0x00);
	write_cmos_sensor(0x030D, 0xD0);
			
	/*Size setting		
		Address value*/
	write_cmos_sensor(0x0340, 0x0E);
	write_cmos_sensor(0x0341, 0x10);
	write_cmos_sensor(0x0342, 0x17);
	write_cmos_sensor(0x0343, 0x10);
	write_cmos_sensor(0x0344, 0x01);
	write_cmos_sensor(0x0345, 0x20);
	write_cmos_sensor(0x0346, 0x00);
	write_cmos_sensor(0x0347, 0xD8);
	write_cmos_sensor(0x0348, 0x13);
	write_cmos_sensor(0x0349, 0x5F);
	write_cmos_sensor(0x034A, 0x0E);
	write_cmos_sensor(0x034B, 0x87);
	write_cmos_sensor(0x034C, 0x12);
	write_cmos_sensor(0x034D, 0x40);
	write_cmos_sensor(0x034E, 0x0D);
	write_cmos_sensor(0x034F, 0xB0);
	write_cmos_sensor(0x0350, 0x00);
	write_cmos_sensor(0x0351, 0x00);
	write_cmos_sensor(0x0352, 0x00);
	write_cmos_sensor(0x0353, 0x00);
	write_cmos_sensor(0x0354, 0x12);
	write_cmos_sensor(0x0355, 0x40);
	write_cmos_sensor(0x0356, 0x0D);
	write_cmos_sensor(0x0357, 0xB0);
	write_cmos_sensor(0x901D, 0x08);
			
	/*Mode Setting		
		Address value*/
	write_cmos_sensor(0x0108, 0x03);
	write_cmos_sensor(0x0112, 0x0A);
	write_cmos_sensor(0x0113, 0x0A);
	write_cmos_sensor(0x0381, 0x01);
	write_cmos_sensor(0x0383, 0x01);
	write_cmos_sensor(0x0385, 0x01);
	write_cmos_sensor(0x0387, 0x01);
	write_cmos_sensor(0x0390, 0x00);
	write_cmos_sensor(0x0391, 0x11);
	write_cmos_sensor(0x0392, 0x00);
	write_cmos_sensor(0x1307, 0x00);
	write_cmos_sensor(0x9512, 0x40);
	write_cmos_sensor(0x9873, 0x00);
	write_cmos_sensor(0xAB07, 0x01);
	write_cmos_sensor(0xB802, 0x01);
	write_cmos_sensor(0xB933, 0x00);
			
	/*OptionnalFunction setting 	
		Address value*/
	write_cmos_sensor(0x13C0, 0x01);
	write_cmos_sensor(0x13C1, 0x02);
	write_cmos_sensor(0x9909, 0x01);
	write_cmos_sensor(0x991E, 0x01);
			
	/*Global Timing Setting 	
		Address value*/
	write_cmos_sensor(0x0830, 0x00);
	write_cmos_sensor(0x0831, 0x6F);
	write_cmos_sensor(0x0832, 0x00);
	write_cmos_sensor(0x0833, 0x2F);
	write_cmos_sensor(0x0834, 0x00);
	write_cmos_sensor(0x0835, 0x57);
	write_cmos_sensor(0x0836, 0x00);
	write_cmos_sensor(0x0837, 0x2F);
	write_cmos_sensor(0x0838, 0x00);
	write_cmos_sensor(0x0839, 0x2F);
	write_cmos_sensor(0x083A, 0x00);
	write_cmos_sensor(0x083B, 0x2F);
	write_cmos_sensor(0x083C, 0x00);
	write_cmos_sensor(0x083D, 0xBF);
	write_cmos_sensor(0x083E, 0x00);
	write_cmos_sensor(0x083F, 0x27);
	write_cmos_sensor(0x0842, 0x00);
	write_cmos_sensor(0x0843, 0x0F);
	write_cmos_sensor(0x0844, 0x00);
	write_cmos_sensor(0x0845, 0x37);
	write_cmos_sensor(0x0847, 0x02);
			
	/*Integration Time Setting		
		Address value*/
	write_cmos_sensor(0x0202, 0x0E);
	write_cmos_sensor(0x0203, 0x08);
			
	/*Gain Setting		
		Address value*/
	write_cmos_sensor(0x0205, 0x00);
	write_cmos_sensor(0x020E, 0x01);
	write_cmos_sensor(0x020F, 0x00);
	write_cmos_sensor(0x0210, 0x01);
	write_cmos_sensor(0x0211, 0x00);
	write_cmos_sensor(0x0212, 0x01);
	write_cmos_sensor(0x0213, 0x00);
	write_cmos_sensor(0x0214, 0x01);
	write_cmos_sensor(0x0215, 0x00);
			
	/*HDR Setting		
		Address value*/
	write_cmos_sensor(0x0233, 0x00);
	write_cmos_sensor(0x0234, 0x00);
	write_cmos_sensor(0x0235, 0x40);
	write_cmos_sensor(0x0238, 0x00);
	write_cmos_sensor(0x0239, 0x08);
	write_cmos_sensor(0x13C2, 0x00);
	write_cmos_sensor(0x13C3, 0x00);
	write_cmos_sensor(0x13C4, 0x00);
	write_cmos_sensor(0xAD34, 0x14);
	write_cmos_sensor(0xAD35, 0x7F);
	write_cmos_sensor(0xAD36, 0x00);
	write_cmos_sensor(0xAD37, 0x00);
	write_cmos_sensor(0xAD38, 0x0F);
	write_cmos_sensor(0xAD39, 0x5D);
	write_cmos_sensor(0xAD3A, 0x00);
	write_cmos_sensor(0xAD3B, 0x00);
	write_cmos_sensor(0xAD3C, 0x14);
	write_cmos_sensor(0xAD3D, 0x7F);
	write_cmos_sensor(0xAD3E, 0x0F);
	write_cmos_sensor(0xAD3F, 0x5D);
			
	/*Bypass Setting		
		Address value*/
	write_cmos_sensor(0xA303, 0x00);
	write_cmos_sensor(0xA403, 0x00);
	write_cmos_sensor(0xA602, 0x00);
	write_cmos_sensor(0xA703, 0x01);
	write_cmos_sensor(0xA903, 0x01);
	write_cmos_sensor(0xA956, 0x01);
	write_cmos_sensor(0xAA03, 0x01);
	write_cmos_sensor(0xAB03, 0x01);
	write_cmos_sensor(0xAD03, 0x01);
	write_cmos_sensor(0xAE03, 0x01);
	write_cmos_sensor(0xB803, 0x01);
	write_cmos_sensor(0xB903, 0x01);
	write_cmos_sensor(0xB91A, 0x01);
	write_cmos_sensor(0xBB03, 0x01);
			
	/*Single Correct setting		
		Address value*/
	write_cmos_sensor(0x4003, 0x00);
	write_cmos_sensor(0x4004, 0x00);
	write_cmos_sensor(0x4005, 0x00);
	write_cmos_sensor(0x4006, 0x80);
	write_cmos_sensor(0x4007, 0x80);
	write_cmos_sensor(0x4008, 0x80);
	write_cmos_sensor(0x400C, 0x00);
	write_cmos_sensor(0x400D, 0x00);
	write_cmos_sensor(0x400E, 0x00);
	write_cmos_sensor(0x400F, 0x80);
	write_cmos_sensor(0x4010, 0x80);
	write_cmos_sensor(0x4011, 0x80);
			
	/*Mode etc setting		
		Address value*/
	write_cmos_sensor(0x3008, 0x01);
	write_cmos_sensor(0x3009, 0x71);
	write_cmos_sensor(0x300A, 0x51);
	write_cmos_sensor(0x9431, 0x03);
	write_cmos_sensor(0xA913, 0x00);
	write_cmos_sensor(0xAC13, 0x00);
	write_cmos_sensor(0xAC3F, 0x00);
	write_cmos_sensor(0xAE8B, 0x36);
	write_cmos_sensor(0xB812, 0x00);
			
	/*LNR setting		
		Address value*/
	write_cmos_sensor(0x3516, 0x00);
	write_cmos_sensor(0x3517, 0x00);
	write_cmos_sensor(0x3518, 0x00);
	write_cmos_sensor(0x3519, 0x00);
	write_cmos_sensor(0x351A, 0x00);
	write_cmos_sensor(0x351B, 0x00);
	write_cmos_sensor(0x351C, 0x00);
	write_cmos_sensor(0x351D, 0x00);
	write_cmos_sensor(0x351E, 0x00);
	write_cmos_sensor(0x351F, 0x00);
	write_cmos_sensor(0x3520, 0x00);
	write_cmos_sensor(0x3521, 0x00);
	write_cmos_sensor(0x3522, 0x00);

	set_thread_priority(0);
	/*
	 * stream-on will be done after set shutter/gain.
	 */
}	/* capture 16M setting */

static void capture_setting(kal_uint16 currefps)
{
	LOG_INF("%s\n", __func__);
	/*capture
	 * H : 5248
	 * V : 3936
	 */
	set_thread_priority(40);
	write_cmos_sensor(0x0100,0x00);	

	/* Clock Setting		
		Address value*/
	write_cmos_sensor(0x0301, 0x0A);
	write_cmos_sensor(0x0303, 0x01);
	write_cmos_sensor(0x0304, 0x03);
	write_cmos_sensor(0x0305, 0x06);
	write_cmos_sensor(0x0306, 0x00);
	write_cmos_sensor(0x0307, 0x6F);
	write_cmos_sensor(0x0309, 0x0A);
	write_cmos_sensor(0x030B, 0x01);
	write_cmos_sensor(0x030C, 0x00);
	write_cmos_sensor(0x030D, 0xD0);
			
	/*Size setting		
		Address value*/
	write_cmos_sensor(0x0340, 0x0F);
	write_cmos_sensor(0x0341, 0x90);
	write_cmos_sensor(0x0342, 0x17);
	write_cmos_sensor(0x0343, 0x10);
	write_cmos_sensor(0x0344, 0x00);
	write_cmos_sensor(0x0345, 0x00);
	write_cmos_sensor(0x0346, 0x00);
	write_cmos_sensor(0x0347, 0x00);
	write_cmos_sensor(0x0348, 0x14);
	write_cmos_sensor(0x0349, 0x7F);
	write_cmos_sensor(0x034A, 0x0F);
	write_cmos_sensor(0x034B, 0x5F);
	write_cmos_sensor(0x034C, 0x14);
	write_cmos_sensor(0x034D, 0x80);
	write_cmos_sensor(0x034E, 0x0F);
	write_cmos_sensor(0x034F, 0x60);
	write_cmos_sensor(0x0350, 0x00);
	write_cmos_sensor(0x0351, 0x00);
	write_cmos_sensor(0x0352, 0x00);
	write_cmos_sensor(0x0353, 0x00);
	write_cmos_sensor(0x0354, 0x14);
	write_cmos_sensor(0x0355, 0x80);
	write_cmos_sensor(0x0356, 0x0F);
	write_cmos_sensor(0x0357, 0x60);
	write_cmos_sensor(0x901D, 0x08);
			
	/*Mode Setting		
		Address value*/
	write_cmos_sensor(0x0108, 0x03);
	write_cmos_sensor(0x0112, 0x0A);
	write_cmos_sensor(0x0113, 0x0A);
	write_cmos_sensor(0x0381, 0x01);
	write_cmos_sensor(0x0383, 0x01);
	write_cmos_sensor(0x0385, 0x01);
	write_cmos_sensor(0x0387, 0x01);
	write_cmos_sensor(0x0390, 0x00);
	write_cmos_sensor(0x0391, 0x11);
	write_cmos_sensor(0x0392, 0x00);
	write_cmos_sensor(0x1307, 0x00);
	write_cmos_sensor(0x9512, 0x40);
	write_cmos_sensor(0x9873, 0x00);
	write_cmos_sensor(0xAB07, 0x01);
	write_cmos_sensor(0xB802, 0x01);
	write_cmos_sensor(0xB933, 0x00);
			
	/*OptionnalFunction setting 	
		Address value*/
	write_cmos_sensor(0x13C0, 0x01);
	write_cmos_sensor(0x13C1, 0x02);
	write_cmos_sensor(0x9909, 0x01);
	write_cmos_sensor(0x991E, 0x01);
			
	/*Global Timing Setting 	
		Address value*/
	write_cmos_sensor(0x0830, 0x00);
	write_cmos_sensor(0x0831, 0x6F);
	write_cmos_sensor(0x0832, 0x00);
	write_cmos_sensor(0x0833, 0x2F);
	write_cmos_sensor(0x0834, 0x00);
	write_cmos_sensor(0x0835, 0x57);
	write_cmos_sensor(0x0836, 0x00);
	write_cmos_sensor(0x0837, 0x2F);
	write_cmos_sensor(0x0838, 0x00);
	write_cmos_sensor(0x0839, 0x2F);
	write_cmos_sensor(0x083A, 0x00);
	write_cmos_sensor(0x083B, 0x2F);
	write_cmos_sensor(0x083C, 0x00);
	write_cmos_sensor(0x083D, 0xBF);
	write_cmos_sensor(0x083E, 0x00);
	write_cmos_sensor(0x083F, 0x27);
	write_cmos_sensor(0x0842, 0x00);
	write_cmos_sensor(0x0843, 0x0F);
	write_cmos_sensor(0x0844, 0x00);
	write_cmos_sensor(0x0845, 0x37);
	write_cmos_sensor(0x0847, 0x02);
			
	/*Integration Time Setting		
		Address value*/
	write_cmos_sensor(0x0202, 0x0F);
	write_cmos_sensor(0x0203, 0x88);
			
	/*Gain Setting		
		Address value*/
	write_cmos_sensor(0x0205, 0x00);
	write_cmos_sensor(0x020E, 0x01);
	write_cmos_sensor(0x020F, 0x00);
	write_cmos_sensor(0x0210, 0x01);
	write_cmos_sensor(0x0211, 0x00);
	write_cmos_sensor(0x0212, 0x01);
	write_cmos_sensor(0x0213, 0x00);
	write_cmos_sensor(0x0214, 0x01);
	write_cmos_sensor(0x0215, 0x00);
			
	/*HDR Setting		
		Address value*/
	write_cmos_sensor(0x0233, 0x00);
	write_cmos_sensor(0x0234, 0x00);
	write_cmos_sensor(0x0235, 0x40);
	write_cmos_sensor(0x0238, 0x00);
	write_cmos_sensor(0x0239, 0x08);
	write_cmos_sensor(0x13C2, 0x00);
	write_cmos_sensor(0x13C3, 0x00);
	write_cmos_sensor(0x13C4, 0x00);
	write_cmos_sensor(0xAD34, 0x14);
	write_cmos_sensor(0xAD35, 0x7F);
	write_cmos_sensor(0xAD36, 0x00);
	write_cmos_sensor(0xAD37, 0x00);
	write_cmos_sensor(0xAD38, 0x0F);
	write_cmos_sensor(0xAD39, 0x5D);
	write_cmos_sensor(0xAD3A, 0x00);
	write_cmos_sensor(0xAD3B, 0x00);
	write_cmos_sensor(0xAD3C, 0x14);
	write_cmos_sensor(0xAD3D, 0x7F);
	write_cmos_sensor(0xAD3E, 0x0F);
	write_cmos_sensor(0xAD3F, 0x5D);
			
	/*Bypass Setting		
		Address value*/
	write_cmos_sensor(0xA303, 0x00);
	write_cmos_sensor(0xA403, 0x00);
	write_cmos_sensor(0xA602, 0x00);
	write_cmos_sensor(0xA703, 0x01);
	write_cmos_sensor(0xA903, 0x01);
	write_cmos_sensor(0xA956, 0x01);
	write_cmos_sensor(0xAA03, 0x01);
	write_cmos_sensor(0xAB03, 0x01);
	write_cmos_sensor(0xAD03, 0x01);
	write_cmos_sensor(0xAE03, 0x01);
	write_cmos_sensor(0xB803, 0x01);
	write_cmos_sensor(0xB903, 0x01);
	write_cmos_sensor(0xB91A, 0x01);
	write_cmos_sensor(0xBB03, 0x01);
			
	/*Single Correct setting		
		Address value*/
	write_cmos_sensor(0x4003, 0x00);
	write_cmos_sensor(0x4004, 0x00);
	write_cmos_sensor(0x4005, 0x00);
	write_cmos_sensor(0x4006, 0x80);
	write_cmos_sensor(0x4007, 0x80);
	write_cmos_sensor(0x4008, 0x80);
	write_cmos_sensor(0x400C, 0x00);
	write_cmos_sensor(0x400D, 0x00);
	write_cmos_sensor(0x400E, 0x00);
	write_cmos_sensor(0x400F, 0x80);
	write_cmos_sensor(0x4010, 0x80);
	write_cmos_sensor(0x4011, 0x80);
			
	/*Mode etc setting		
		Address value*/
	write_cmos_sensor(0x3008, 0x01);
	write_cmos_sensor(0x3009, 0x71);
	write_cmos_sensor(0x300A, 0x51);
	write_cmos_sensor(0x9431, 0x03);
	write_cmos_sensor(0xA913, 0x00);
	write_cmos_sensor(0xAC13, 0x00);
	write_cmos_sensor(0xAC3F, 0x00);
	write_cmos_sensor(0xAE8B, 0x36);
	write_cmos_sensor(0xB812, 0x00);
			
	/*LNR setting		
		Address value*/
	write_cmos_sensor(0x3516, 0x00);
	write_cmos_sensor(0x3517, 0x00);
	write_cmos_sensor(0x3518, 0x00);
	write_cmos_sensor(0x3519, 0x00);
	write_cmos_sensor(0x351A, 0x00);
	write_cmos_sensor(0x351B, 0x00);
	write_cmos_sensor(0x351C, 0x00);
	write_cmos_sensor(0x351D, 0x00);
	write_cmos_sensor(0x351E, 0x00);
	write_cmos_sensor(0x351F, 0x00);
	write_cmos_sensor(0x3520, 0x00);
	write_cmos_sensor(0x3521, 0x00);
	write_cmos_sensor(0x3522, 0x00);

	set_thread_priority(0);
	/*
	 * stream-on will be done after set shutter/gain.
	 */
}	/* capture_setting */

static void hs_video_setting()
{
    LOG_INF("E\n");
        /*slow motion(1/4bin+crop)
    H : 1280
    V : 720
    Clock Setting
        Address value*/
    write_cmos_sensor(0x0301,0x06);
    write_cmos_sensor(0x0303,0x02);
    write_cmos_sensor(0x0304,0x02);
    write_cmos_sensor(0x0305,0x06);
    write_cmos_sensor(0x0306,0x00);
    write_cmos_sensor(0x0307,0x46);
    write_cmos_sensor(0x0309,0x0A);
    write_cmos_sensor(0x030B,0x02);
    write_cmos_sensor(0x030C,0x00);
    write_cmos_sensor(0x030D,0x66);


    /*Size setting
        Address value */
    write_cmos_sensor(0x0340,0x03);
    write_cmos_sensor(0x0341,0x16);
    write_cmos_sensor(0x0342,0x17);
    write_cmos_sensor(0x0343,0x10);
    write_cmos_sensor(0x0344,0x00);
    write_cmos_sensor(0x0345,0x40);
    write_cmos_sensor(0x0346,0x02);
    write_cmos_sensor(0x0347,0x10);
    write_cmos_sensor(0x0348,0x14);
    write_cmos_sensor(0x0349,0x3F);
    write_cmos_sensor(0x034A,0x0D);
    write_cmos_sensor(0x034B,0x4F);
    write_cmos_sensor(0x034C,0x05);
    write_cmos_sensor(0x034D,0x00);
    write_cmos_sensor(0x034E,0x02);
    write_cmos_sensor(0x034F,0xD0);
    write_cmos_sensor(0x0350,0x00);
    write_cmos_sensor(0x0351,0x00);
    write_cmos_sensor(0x0352,0x00);
    write_cmos_sensor(0x0353,0x00);
    write_cmos_sensor(0x0354,0x05);
    write_cmos_sensor(0x0355,0x00);
    write_cmos_sensor(0x0356,0x02);
    write_cmos_sensor(0x0357,0xD0);
    write_cmos_sensor(0x901D,0x08);


    /*Mode Setting
        Address value*/
    write_cmos_sensor(0x0108,0x03);
    write_cmos_sensor(0x0112,0x0A);
    write_cmos_sensor(0x0113,0x0A);
    write_cmos_sensor(0x0381,0x01);
    write_cmos_sensor(0x0383,0x01);
    write_cmos_sensor(0x0385,0x01);
    write_cmos_sensor(0x0387,0x03);
    write_cmos_sensor(0x0390,0x01);
    write_cmos_sensor(0x0391,0x42);
    write_cmos_sensor(0x0392,0x00);
    write_cmos_sensor(0x1307,0x02);
    write_cmos_sensor(0x9512,0x40);
    write_cmos_sensor(0xAB07,0x01);
    write_cmos_sensor(0xB802,0x01);
    write_cmos_sensor(0xB933,0x01);


    /*OptionnalFunction setting
        Address value*/
    write_cmos_sensor(0x0700,0x00);
    write_cmos_sensor(0x13C0,0x00);
    write_cmos_sensor(0x13C1,0x01);
    write_cmos_sensor(0x9909,0x01);
    write_cmos_sensor(0x991E,0x01);
    write_cmos_sensor(0xBA03,0x00);
    write_cmos_sensor(0xBA07,0x00);


    /*Global Timing Setting
        Address value*/
    write_cmos_sensor(0x0830,0x00);
    write_cmos_sensor(0x0831,0x47);
    write_cmos_sensor(0x0832,0x00);
    write_cmos_sensor(0x0833,0x0F);
    write_cmos_sensor(0x0834,0x00);
    write_cmos_sensor(0x0835,0x1F);
    write_cmos_sensor(0x0836,0x00);
    write_cmos_sensor(0x0837,0x17);
    write_cmos_sensor(0x0838,0x00);
    write_cmos_sensor(0x0839,0x0F);
    write_cmos_sensor(0x083A,0x00);
    write_cmos_sensor(0x083B,0x0F);
    write_cmos_sensor(0x083C,0x00);
    write_cmos_sensor(0x083D,0x47);
    write_cmos_sensor(0x083E,0x00);
    write_cmos_sensor(0x083F,0x0F);
    write_cmos_sensor(0x0842,0x00);
    write_cmos_sensor(0x0843,0x0F);
    write_cmos_sensor(0x0844,0x00);
    write_cmos_sensor(0x0845,0x37);
    write_cmos_sensor(0x0847,0x02);


    /*Integration Time Setting
        Address value*/
    write_cmos_sensor(0x0202,0x03);
    write_cmos_sensor(0x0203,0x0E);


    /*Gain Setting
        Address value*/
    write_cmos_sensor(0x0205,0x00);
    write_cmos_sensor(0x020E,0x01);
    write_cmos_sensor(0x020F,0x00);
    write_cmos_sensor(0x0210,0x01);
    write_cmos_sensor(0x0211,0x00);
    write_cmos_sensor(0x0212,0x01);
    write_cmos_sensor(0x0213,0x00);
    write_cmos_sensor(0x0214,0x01);
    write_cmos_sensor(0x0215,0x00);


    /*HDR Setting
        Address value*/
    write_cmos_sensor(0x0233,0x00);
    write_cmos_sensor(0x0234,0x00);
    write_cmos_sensor(0x0235,0x40);
    write_cmos_sensor(0x0238,0x01);
    write_cmos_sensor(0x0239,0x08);
    write_cmos_sensor(0x13C2,0x00);
    write_cmos_sensor(0x13C3,0x00);
    write_cmos_sensor(0x13C4,0x00);
    write_cmos_sensor(0x9873,0x00);
    write_cmos_sensor(0xAD34,0x14);
    write_cmos_sensor(0xAD35,0x7F);
    write_cmos_sensor(0xAD36,0x00);
    write_cmos_sensor(0xAD37,0x00);
    write_cmos_sensor(0xAD38,0x0F);
    write_cmos_sensor(0xAD39,0x5D);
    write_cmos_sensor(0xAD3A,0x00);
    write_cmos_sensor(0xAD3B,0x00);
    write_cmos_sensor(0xAD3C,0x14);
    write_cmos_sensor(0xAD3D,0x7F);
    write_cmos_sensor(0xAD3E,0x0F);
    write_cmos_sensor(0xAD3F,0x5D);


    /*Bypass Setting
        Address value*/
    write_cmos_sensor(0xA303,0x00);
    write_cmos_sensor(0xA403,0x00);
    write_cmos_sensor(0xA602,0x00);
    write_cmos_sensor(0xA703,0x01);
    write_cmos_sensor(0xA903,0x01);
    write_cmos_sensor(0xA956,0x01);
    write_cmos_sensor(0xAA03,0x01);
    write_cmos_sensor(0xAB03,0x00);
    write_cmos_sensor(0xAC03,0x01);
    write_cmos_sensor(0xAD03,0x01);
    write_cmos_sensor(0xAE03,0x01);
    write_cmos_sensor(0xB803,0x00);
    write_cmos_sensor(0xB903,0x01);
    write_cmos_sensor(0xB91A,0x01);
    write_cmos_sensor(0xBB03,0x01);


    /*Single Correct setting
        Address value*/
    write_cmos_sensor(0x4003,0x00);
    write_cmos_sensor(0x4004,0x00);
    write_cmos_sensor(0x4005,0x00);
    write_cmos_sensor(0x4006,0x80);
    write_cmos_sensor(0x4007,0x80);
    write_cmos_sensor(0x4008,0x80);
    write_cmos_sensor(0x400C,0x00);
    write_cmos_sensor(0x400D,0x00);
    write_cmos_sensor(0x400E,0x00);
    write_cmos_sensor(0x400F,0x80);
    write_cmos_sensor(0x4010,0x80);
    write_cmos_sensor(0x4011,0x80);


    /*Mode etc setting
        Address value*/
    write_cmos_sensor(0x3008,0x01);
    write_cmos_sensor(0x3009,0x71);
    write_cmos_sensor(0x300A,0x51);
    write_cmos_sensor(0x3506,0x00);
    write_cmos_sensor(0x3507,0x01);
    write_cmos_sensor(0x3508,0x00);
    write_cmos_sensor(0x3509,0x01);
    write_cmos_sensor(0x9431,0x07);
    write_cmos_sensor(0xA913,0x00);
    write_cmos_sensor(0xAC13,0x00);
    write_cmos_sensor(0xAC3F,0x00);
    write_cmos_sensor(0xAE8B,0x29);
    write_cmos_sensor(0xB812,0x01);


    /*LNR setting
        Address value*/
    write_cmos_sensor(0x3516,0x00);
    write_cmos_sensor(0x3517,0x00);
    write_cmos_sensor(0x3518,0x00);
    write_cmos_sensor(0x3519,0x00);
    write_cmos_sensor(0x351A,0x00);
    write_cmos_sensor(0x351B,0x00);
    write_cmos_sensor(0x351C,0x00);
    write_cmos_sensor(0x351D,0x00);
    write_cmos_sensor(0x351E,0x00);
    write_cmos_sensor(0x351F,0x00);
    write_cmos_sensor(0x3520,0x00);
    write_cmos_sensor(0x3521,0x00);
    write_cmos_sensor(0x3522,0x00);
    write_cmos_sensor(0x0100,0x01);
}

static void slim_video_setting()
{
    LOG_INF("E\n");

    /*HD(1/4bin+crop)
    H : 1280
    V : 720
    Clock Setting
        Address value*/
    write_cmos_sensor(0x0301,0x06);
    write_cmos_sensor(0x0303,0x02);
    write_cmos_sensor(0x0304,0x02);
    write_cmos_sensor(0x0305,0x06);
    write_cmos_sensor(0x0306,0x00);
    write_cmos_sensor(0x0307,0x46);
    write_cmos_sensor(0x0309,0x0A);
    write_cmos_sensor(0x030B,0x02);
    write_cmos_sensor(0x030C,0x00);
    write_cmos_sensor(0x030D,0x66);


    /*Size setting
        Address value*/
    write_cmos_sensor(0x0340,0x06);
    write_cmos_sensor(0x0341,0x2C);
    write_cmos_sensor(0x0342,0x17);
    write_cmos_sensor(0x0343,0x10);
    write_cmos_sensor(0x0344,0x00);
    write_cmos_sensor(0x0345,0x40);
    write_cmos_sensor(0x0346,0x02);
    write_cmos_sensor(0x0347,0x10);
    write_cmos_sensor(0x0348,0x14);
    write_cmos_sensor(0x0349,0x3F);
    write_cmos_sensor(0x034A,0x0D);
    write_cmos_sensor(0x034B,0x4F);
    write_cmos_sensor(0x034C,0x05);
    write_cmos_sensor(0x034D,0x00);
    write_cmos_sensor(0x034E,0x02);
    write_cmos_sensor(0x034F,0xD0);
    write_cmos_sensor(0x0350,0x00);
    write_cmos_sensor(0x0351,0x00);
    write_cmos_sensor(0x0352,0x00);
    write_cmos_sensor(0x0353,0x00);
    write_cmos_sensor(0x0354,0x05);
    write_cmos_sensor(0x0355,0x00);
    write_cmos_sensor(0x0356,0x02);
    write_cmos_sensor(0x0357,0xD0);
    write_cmos_sensor(0x901D,0x08);


    /*Mode Setting
        Address value*/
    write_cmos_sensor(0x0108,0x03);
    write_cmos_sensor(0x0112,0x0A);
    write_cmos_sensor(0x0113,0x0A);
    write_cmos_sensor(0x0381,0x01);
    write_cmos_sensor(0x0383,0x01);
    write_cmos_sensor(0x0385,0x01);
    write_cmos_sensor(0x0387,0x03);
    write_cmos_sensor(0x0390,0x01);
    write_cmos_sensor(0x0391,0x42);
    write_cmos_sensor(0x0392,0x00);
    write_cmos_sensor(0x1307,0x02);
    write_cmos_sensor(0x9512,0x40);
    write_cmos_sensor(0xAB07,0x01);
    write_cmos_sensor(0xB802,0x01);
    write_cmos_sensor(0xB933,0x01);


    /*OptionnalFunction setting
        Address value */
    write_cmos_sensor(0x0700,0x00);
    write_cmos_sensor(0x13C0,0x00);
    write_cmos_sensor(0x13C1,0x01);
    write_cmos_sensor(0x9909,0x01);
    write_cmos_sensor(0x991E,0x01);
    write_cmos_sensor(0xBA03,0x00);
    write_cmos_sensor(0xBA07,0x00);


    /*Global Timing Setting
        Address value*/
    write_cmos_sensor(0x0830,0x00);
    write_cmos_sensor(0x0831,0x47);
    write_cmos_sensor(0x0832,0x00);
    write_cmos_sensor(0x0833,0x0F);
    write_cmos_sensor(0x0834,0x00);
    write_cmos_sensor(0x0835,0x1F);
    write_cmos_sensor(0x0836,0x00);
    write_cmos_sensor(0x0837,0x17);
    write_cmos_sensor(0x0838,0x00);
    write_cmos_sensor(0x0839,0x0F);
    write_cmos_sensor(0x083A,0x00);
    write_cmos_sensor(0x083B,0x0F);
    write_cmos_sensor(0x083C,0x00);
    write_cmos_sensor(0x083D,0x47);
    write_cmos_sensor(0x083E,0x00);
    write_cmos_sensor(0x083F,0x0F);
    write_cmos_sensor(0x0842,0x00);
    write_cmos_sensor(0x0843,0x0F);
    write_cmos_sensor(0x0844,0x00);
    write_cmos_sensor(0x0845,0x37);
    write_cmos_sensor(0x0847,0x02);


    /*Integration Time Setting
        Address value*/
    write_cmos_sensor(0x0202,0x06);
    write_cmos_sensor(0x0203,0x24);


    /*Gain Setting
        Address value*/
    write_cmos_sensor(0x0205,0x00);
    write_cmos_sensor(0x020E,0x01);
    write_cmos_sensor(0x020F,0x00);
    write_cmos_sensor(0x0210,0x01);
    write_cmos_sensor(0x0211,0x00);
    write_cmos_sensor(0x0212,0x01);
    write_cmos_sensor(0x0213,0x00);
    write_cmos_sensor(0x0214,0x01);
    write_cmos_sensor(0x0215,0x00);


    /*HDR Setting
        Address value*/
    write_cmos_sensor(0x0233,0x00);
    write_cmos_sensor(0x0234,0x00);
    write_cmos_sensor(0x0235,0x40);
    write_cmos_sensor(0x0238,0x01);
    write_cmos_sensor(0x0239,0x08);
    write_cmos_sensor(0x13C2,0x00);
    write_cmos_sensor(0x13C3,0x00);
    write_cmos_sensor(0x13C4,0x00);
    write_cmos_sensor(0x9873,0x00);
    write_cmos_sensor(0xAD34,0x14);
    write_cmos_sensor(0xAD35,0x7F);
    write_cmos_sensor(0xAD36,0x00);
    write_cmos_sensor(0xAD37,0x00);
    write_cmos_sensor(0xAD38,0x0F);
    write_cmos_sensor(0xAD39,0x5D);
    write_cmos_sensor(0xAD3A,0x00);
    write_cmos_sensor(0xAD3B,0x00);
    write_cmos_sensor(0xAD3C,0x14);
    write_cmos_sensor(0xAD3D,0x7F);
    write_cmos_sensor(0xAD3E,0x0F);
    write_cmos_sensor(0xAD3F,0x5D);


    /*Bypass Setting
        Address value */
    write_cmos_sensor(0xA303,0x00);
    write_cmos_sensor(0xA403,0x00);
    write_cmos_sensor(0xA602,0x00);
    write_cmos_sensor(0xA703,0x01);
    write_cmos_sensor(0xA903,0x01);
    write_cmos_sensor(0xA956,0x01);
    write_cmos_sensor(0xAA03,0x01);
    write_cmos_sensor(0xAB03,0x00);
    write_cmos_sensor(0xAC03,0x01);
    write_cmos_sensor(0xAD03,0x01);
    write_cmos_sensor(0xAE03,0x01);
    write_cmos_sensor(0xB803,0x00);
    write_cmos_sensor(0xB903,0x01);
    write_cmos_sensor(0xB91A,0x01);
    write_cmos_sensor(0xBB03,0x01);


    /*Single Correct setting
        Address value */
    write_cmos_sensor(0x4003,0x00);
    write_cmos_sensor(0x4004,0x00);
    write_cmos_sensor(0x4005,0x00);
    write_cmos_sensor(0x4006,0x80);
    write_cmos_sensor(0x4007,0x80);
    write_cmos_sensor(0x4008,0x80);
    write_cmos_sensor(0x400C,0x00);
    write_cmos_sensor(0x400D,0x00);
    write_cmos_sensor(0x400E,0x00);
    write_cmos_sensor(0x400F,0x80);
    write_cmos_sensor(0x4010,0x80);
    write_cmos_sensor(0x4011,0x80);


    /*Mode etc setting
        Address value*/
    write_cmos_sensor(0x3008,0x01);
    write_cmos_sensor(0x3009,0x71);
    write_cmos_sensor(0x300A,0x51);
    write_cmos_sensor(0x3506,0x00);
    write_cmos_sensor(0x3507,0x01);
    write_cmos_sensor(0x3508,0x00);
    write_cmos_sensor(0x3509,0x01);
    write_cmos_sensor(0x9431,0x07);
    write_cmos_sensor(0xA913,0x00);
    write_cmos_sensor(0xAC13,0x00);
    write_cmos_sensor(0xAC3F,0x00);
    write_cmos_sensor(0xAE8B,0x29);
    write_cmos_sensor(0xB812,0x01);


    /*LNR setting
        Address value*/
    write_cmos_sensor(0x3516,0x00);
    write_cmos_sensor(0x3517,0x00);
    write_cmos_sensor(0x3518,0x00);
    write_cmos_sensor(0x3519,0x00);
    write_cmos_sensor(0x351A,0x00);
    write_cmos_sensor(0x351B,0x00);
    write_cmos_sensor(0x351C,0x00);
    write_cmos_sensor(0x351D,0x00);
    write_cmos_sensor(0x351E,0x00);
    write_cmos_sensor(0x351F,0x00);
    write_cmos_sensor(0x3520,0x00);
    write_cmos_sensor(0x3521,0x00);
    write_cmos_sensor(0x3522,0x00);
    write_cmos_sensor(0x0100,0x01);
}


/*************************************************************************
* FUNCTION
*   get_imgsensor_id
*
* DESCRIPTION
*   This function get the sensor ID
*
* PARAMETERS
*   *sensorID : return the sensor ID
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 get_imgsensor_id(UINT32 *sensor_id)
{
	kal_uint8 i = 0;
	int j;
	kal_uint8 retry = 2;
	u8 module_id = 0;
	int ret = 0;
	u32 platform_id = 0;
	u8 val = 0;
	kal_uint8 i2c_write_id;

	if (platform_match == 0) {
		imx220_read_otp(0x0002, &val);
		platform_id = (u32)(val << 24);
		imx220_read_otp(0x0003, &val);
		platform_id = (u32)(platform_id | (val << 16));
		imx220_read_otp(0x0004, &val);
		platform_id = (u32)(platform_id | (val << 8));
		imx220_read_otp(0x0005, &val);
		platform_id = (u32)(platform_id | val);
		LOG_INF("Read platform id, platform_id:0x%x\n", platform_id);
		if (platform_id == 0x4D303835) {
			platform_match = 1;
		} else {
			platform_match = 0;
			/* if Sensor ID is not correct, Must set *sensor_id to 0xFFFFFFFF */
			*sensor_id = 0xFFFFFFFF;
			return ERROR_SENSOR_CONNECT_FAIL;
		}
	}

    //sensor have two i2c address 0x6c 0x6d & 0x35 0x34, we should detect the module used i2c address
    while (imgsensor_info.i2c_addr_table[i] != 0xff) {
	spin_lock(&imgsensor_drv_lock);
	i2c_write_id = imgsensor.i2c_write_id;
	imgsensor.i2c_write_id = imgsensor_info.i2c_addr_table[i];
	spin_unlock(&imgsensor_drv_lock);
        do {
            *sensor_id = return_sensor_id();
	    ret = imx220_read_otp(0x0001, &module_id);
	    if ((ret >= 0) && (*sensor_id == imgsensor_info.sensor_id) && (module_id == 0x01)) {
		LOG_INF("i2c write id: 0x%x, sensor id: 0x%x, module_id:0x%x\n", imgsensor.i2c_write_id,*sensor_id, module_id);
		*sensor_id = IMX220_LITEON_SENSOR_ID;
		back_cam_id = IMX220_LITEON_SENSOR_ID;
                goto otp_read;
            }
	    LOG_INF("Read sensor id fail, addr: 0x%x, sensor_id:0x%x, module_id:0x%x\n", imgsensor.i2c_write_id,*sensor_id, module_id);
            retry--;
        } while(retry > 0);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.i2c_write_id = i2c_write_id;
	spin_unlock(&imgsensor_drv_lock);
        i++;
        retry = 2;
    }
    if ((ret < 0) || (*sensor_id != imgsensor_info.sensor_id)
	|| (module_id != 0x01)) {
        // if Sensor ID is not correct, Must set *sensor_id to 0xFFFFFFFF
        *sensor_id = 0xFFFFFFFF;
        return ERROR_SENSOR_CONNECT_FAIL;
    }

otp_read:
	/*
	 * MT6795M only support 16M capture;
	 * MT6795/MT6795T can support 21M capture;
	 */
	if (cpu_chip_is_mt6795m()) {
		memcpy((void *)&imgsensor_info.cap, (void *)&imgsensor_info.cap1, sizeof(imgsensor_mode_struct));
		memcpy((void *)&imgsensor_info.pre, (void *)&imgsensor_info.pre1, sizeof(imgsensor_mode_struct));
		memcpy((void *)&imgsensor_info.slim_cap, (void *)&imgsensor_info.pre1, sizeof(imgsensor_mode_struct));
	}
	/*
	 * read lsc calibration from OTP E2PROM.
	 */
	liteon_otp_buf = (u8 *)kzalloc(OTP_LSC_SIZE, GFP_KERNEL);

	/* read lsc calibration from E2PROM */
	imx220_read_otp_lsc(OTP_START_ADDR, liteon_otp_buf);
	for (j = 0; j < OTP_LSC_SIZE; j++) {
		printk("========imx220_liteon_otp RegIndex-%d=====val:0x%x======\n", j, *(liteon_otp_buf + j));
	}
    return ERROR_NONE;
}


/*************************************************************************
* FUNCTION
*   open
*
* DESCRIPTION
*   This function initialize the registers of CMOS sensor
*
* PARAMETERS
*   None
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 open(void)
{
    //const kal_uint8 i2c_addr[] = {IMGSENSOR_WRITE_ID_1, IMGSENSOR_WRITE_ID_2};
    kal_uint8 retry = 2;
    kal_uint32 sensor_id = 0;
    LOG_1;
    LOG_2;
    //sensor have two i2c address 0x6c 0x6d & 0x35 0x34, we should detect the module used i2c address
    do {
        sensor_id = return_sensor_id();
        if (sensor_id == imgsensor_info.sensor_id) {
            LOG_INF("i2c write id: 0x%x, sensor id: 0x%x\n", imgsensor.i2c_write_id,sensor_id);
            break;
        }
        LOG_INF("Read sensor id fail, write id: 0x%x, id: 0x%x\n", imgsensor.i2c_write_id,sensor_id);
        retry--;
    } while(retry > 0);

    if (imgsensor_info.sensor_id != sensor_id)
        return ERROR_SENSOR_CONNECT_FAIL;

    /* initail sequence write in  */
    sensor_init();

    spin_lock(&imgsensor_drv_lock);

    imgsensor.autoflicker_en= KAL_FALSE;
    imgsensor.sensor_mode = IMGSENSOR_MODE_INIT;
    imgsensor.pclk = imgsensor_info.pre.pclk;
    imgsensor.frame_length = imgsensor_info.pre.framelength;
    imgsensor.line_length = imgsensor_info.pre.linelength;
    imgsensor.min_frame_length = imgsensor_info.pre.framelength;
    imgsensor.dummy_pixel = 0;
    imgsensor.dummy_line = 0;
    imgsensor.ihdr_en = 0;
    imgsensor.test_pattern = KAL_FALSE;
    imgsensor.current_fps = imgsensor_info.pre.max_framerate;
    spin_unlock(&imgsensor_drv_lock);

    return ERROR_NONE;
}   /*  open  */



/*************************************************************************
* FUNCTION
*   close
*
* DESCRIPTION
*
*
* PARAMETERS
*   None
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 close(void)
{
    LOG_INF("E\n");

    /*No Need to implement this function*/

    return ERROR_NONE;
}   /*  close  */


/*************************************************************************
* FUNCTION
* preview
*
* DESCRIPTION
*   This function start the sensor preview.
*
* PARAMETERS
*   *image_window : address pointer of pixel numbers in one period of HSYNC
*  *sensor_config_data : address pointer of line numbers in one period of VSYNC
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 preview(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("%s\n", __func__);

	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_PREVIEW;
	mode_change = 1;
	imgsensor.pclk = imgsensor_info.pre.pclk;
	//imgsensor.video_mode = KAL_FALSE;
	imgsensor.line_length = imgsensor_info.pre.linelength;
	imgsensor.frame_length = imgsensor_info.pre.framelength;
	imgsensor.min_frame_length = imgsensor_info.pre.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	if (cpu_chip_is_mt6795m())
		pre_4m_setting();
	else
		preview_setting();

	return ERROR_NONE;
}   /*  preview   */

/*************************************************************************
* FUNCTION
*   capture
*
* DESCRIPTION
*   This function setup the CMOS sensor in capture MY_OUTPUT mode
*
* PARAMETERS
*
* RETURNS
*   None
*
* GLOBALS AFFECTED
*
*************************************************************************/
static kal_uint32 capture(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                          MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("%s\n", __func__);

	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_CAPTURE;
	mode_change = 1;
	if (imgsensor.current_fps != imgsensor_info.cap.max_framerate)
		LOG_INF("Warning: current_fps %d fps is not support, so use cap's setting: %d fps!\n",imgsensor.current_fps,imgsensor_info.cap.max_framerate/10);
	imgsensor.pclk = imgsensor_info.cap.pclk;
	imgsensor.line_length = imgsensor_info.cap.linelength;
	imgsensor.frame_length = imgsensor_info.cap.framelength;
	imgsensor.min_frame_length = imgsensor_info.cap.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	/*
	 * MT6795M only support 16M capture;
	 * MT6795/MT6795T can support 21M capture;
	 */
	if (cpu_chip_is_mt6795m())
		cap_16m_setting();
	else
		capture_setting(imgsensor.current_fps);

	return ERROR_NONE;
}   /* capture() */

static kal_uint32 zsd_capture(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
						  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("%s\n", __func__);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_ZSD_CAPTURE;
	imgsensor.pclk = imgsensor_info.zsd.pclk;
	imgsensor.line_length = imgsensor_info.zsd.linelength;
	imgsensor.frame_length = imgsensor_info.zsd.framelength;  
	imgsensor.min_frame_length = imgsensor_info.zsd.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);

	zsd_12M_setting(); 
	
	
	return ERROR_NONE;
}	/* zsd capture() */

static kal_uint32 slim_capture(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
						  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("%s\n", __func__);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_SLIM_CAPTURE;
	mode_change = 1;
	imgsensor.pclk = imgsensor_info.slim_cap.pclk;
	imgsensor.line_length = imgsensor_info.slim_cap.linelength;
	imgsensor.frame_length = imgsensor_info.slim_cap.framelength;  
	imgsensor.min_frame_length = imgsensor_info.slim_cap.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	if (cpu_chip_is_mt6795m())
		pre_4m_setting();
	else
		imx220_5M_setting(); 

	return ERROR_NONE;
}	/* slim capture() */

static kal_uint32 hd_4k_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
					  MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("%s\n", __func__);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_4K_VIDEO;
	imgsensor.pclk = imgsensor_info.hd_4k_video.pclk;
	imgsensor.line_length = imgsensor_info.hd_4k_video.linelength;
	imgsensor.frame_length = imgsensor_info.hd_4k_video.framelength;  
	imgsensor.min_frame_length = imgsensor_info.hd_4k_video.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	imx220_4k_setting();

	return ERROR_NONE;
}	/* 4k video */

static kal_uint32 normal_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("%s\n", __func__);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_VIDEO;
	imgsensor.pclk = imgsensor_info.normal_video.pclk;
	imgsensor.line_length = imgsensor_info.normal_video.linelength;
	imgsensor.frame_length = imgsensor_info.normal_video.framelength;
	imgsensor.min_frame_length = imgsensor_info.normal_video.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	imx220_1080p_setting();

	return ERROR_NONE;
}	/* normal_video */

static kal_uint32 hd_gis_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("%s\n", __func__);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_HD_GIS_VIDEO;
	imgsensor.pclk = imgsensor_info.hd_gis_video.pclk;
	imgsensor.line_length = imgsensor_info.hd_gis_video.linelength;
	imgsensor.frame_length = imgsensor_info.hd_gis_video.framelength;
	imgsensor.min_frame_length = imgsensor_info.hd_gis_video.framelength;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	imx220_hd_gis_video_setting();

	return ERROR_NONE;
}	/* normal_video */

static kal_uint32 hs_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("%s\n", __func__);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_HIGH_SPEED_VIDEO;
	imgsensor.pclk = imgsensor_info.hs_video.pclk;
	//imgsensor.video_mode = KAL_TRUE;
	imgsensor.line_length = imgsensor_info.hs_video.linelength;
	imgsensor.frame_length = imgsensor_info.hs_video.framelength; 
	imgsensor.min_frame_length = imgsensor_info.hs_video.framelength;
	imgsensor.dummy_line = 0;
	imgsensor.dummy_pixel = 0;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	imx220_720p_hs_setting();

	return ERROR_NONE;
}	/* hs_video */

static kal_uint32 slim_video(MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
	LOG_INF("%s\n", __func__);
	spin_lock(&imgsensor_drv_lock);
	imgsensor.sensor_mode = IMGSENSOR_MODE_SLIM_VIDEO;
	imgsensor.pclk = imgsensor_info.slim_video.pclk;
	//imgsensor.video_mode = KAL_TRUE;
	imgsensor.line_length = imgsensor_info.slim_video.linelength;
	imgsensor.frame_length = imgsensor_info.slim_video.framelength; 
	imgsensor.min_frame_length = imgsensor_info.slim_video.framelength;
	imgsensor.dummy_line = 0;
	imgsensor.dummy_pixel = 0;
	//imgsensor.current_fps = 300;
	imgsensor.autoflicker_en = KAL_FALSE;
	spin_unlock(&imgsensor_drv_lock);
	imx220_1312_984_setting();

	return ERROR_NONE;
}	/* slim_video */

static kal_uint32 get_resolution(MSDK_SENSOR_RESOLUTION_INFO_STRUCT *sensor_resolution)
{
    LOG_INF("E\n");
    sensor_resolution->SensorFullWidth = imgsensor_info.cap.grabwindow_width;
    sensor_resolution->SensorFullHeight = imgsensor_info.cap.grabwindow_height;

    sensor_resolution->SensorPreviewWidth = imgsensor_info.pre.grabwindow_width;
    sensor_resolution->SensorPreviewHeight = imgsensor_info.pre.grabwindow_height;

    sensor_resolution->SensorVideoWidth = imgsensor_info.normal_video.grabwindow_width;
    sensor_resolution->SensorVideoHeight = imgsensor_info.normal_video.grabwindow_height;


    sensor_resolution->SensorHighSpeedVideoWidth     = imgsensor_info.hs_video.grabwindow_width;
    sensor_resolution->SensorHighSpeedVideoHeight    = imgsensor_info.hs_video.grabwindow_height;

    sensor_resolution->SensorSlimVideoWidth  = imgsensor_info.slim_video.grabwindow_width;
    sensor_resolution->SensorSlimVideoHeight     = imgsensor_info.slim_video.grabwindow_height;

    sensor_resolution->SensorCustom1Width = imgsensor_info.zsd.grabwindow_width;
    sensor_resolution->SensorCustom1Height = imgsensor_info.zsd.grabwindow_height;

    sensor_resolution->SensorCustom2Width = imgsensor_info.hd_4k_video.grabwindow_width;
    sensor_resolution->SensorCustom2Height = imgsensor_info.hd_4k_video.grabwindow_height;

    sensor_resolution->SensorCustom3Width = imgsensor_info.slim_cap.grabwindow_width;
    sensor_resolution->SensorCustom3Height = imgsensor_info.slim_cap.grabwindow_height;

    sensor_resolution->SensorCustom4Width = imgsensor_info.hd_gis_video.grabwindow_width;
    sensor_resolution->SensorCustom4Height = imgsensor_info.hd_gis_video.grabwindow_height;

    return ERROR_NONE;
}   /*  get_resolution  */

static kal_uint32 get_info(MSDK_SCENARIO_ID_ENUM scenario_id,
                      MSDK_SENSOR_INFO_STRUCT *sensor_info,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    LOG_INF("scenario_id = %d\n", scenario_id);


    //sensor_info->SensorVideoFrameRate = imgsensor_info.normal_video.max_framerate/10; /* not use */
    //sensor_info->SensorStillCaptureFrameRate= imgsensor_info.cap.max_framerate/10; /* not use */
    //imgsensor_info->SensorWebCamCaptureFrameRate= imgsensor_info.v.max_framerate; /* not use */

    sensor_info->SensorClockPolarity = SENSOR_CLOCK_POLARITY_LOW;
    sensor_info->SensorClockFallingPolarity = SENSOR_CLOCK_POLARITY_LOW; /* not use */
    sensor_info->SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW; // inverse with datasheet
    sensor_info->SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW;
    sensor_info->SensorInterruptDelayLines = 4; /* not use */
    sensor_info->SensorResetActiveHigh = FALSE; /* not use */
    sensor_info->SensorResetDelayCount = 5; /* not use */

    sensor_info->SensroInterfaceType = imgsensor_info.sensor_interface_type;
    sensor_info->MIPIsensorType = imgsensor_info.mipi_sensor_type;
    sensor_info->SettleDelayMode = imgsensor_info.mipi_settle_delay_mode;
    sensor_info->SensorOutputDataFormat = imgsensor_info.sensor_output_dataformat;

    sensor_info->CaptureDelayFrame = imgsensor_info.cap_delay_frame;
    sensor_info->PreviewDelayFrame = imgsensor_info.pre_delay_frame;
    sensor_info->VideoDelayFrame = imgsensor_info.video_delay_frame;
    sensor_info->HighSpeedVideoDelayFrame = imgsensor_info.hs_video_delay_frame;
    sensor_info->SlimVideoDelayFrame = imgsensor_info.slim_video_delay_frame;
    sensor_info->Custom1DelayFrame = imgsensor_info.zsd_delay_frame;
    sensor_info->Custom2DelayFrame = imgsensor_info.hd_4k_video_delay_frame;
    sensor_info->Custom3DelayFrame = imgsensor_info.slim_cap_delay_frame;
    sensor_info->Custom4DelayFrame = imgsensor_info.hd_gis_video_delay_frame;

    sensor_info->SensorMasterClockSwitch = 0; /* not use */
    sensor_info->SensorDrivingCurrent = imgsensor_info.isp_driving_current;

    sensor_info->AEShutDelayFrame = imgsensor_info.ae_shut_delay_frame;          /* The frame of setting shutter default 0 for TG int */
    sensor_info->AESensorGainDelayFrame = imgsensor_info.ae_sensor_gain_delay_frame;    /* The frame of setting sensor gain */
    sensor_info->AEISPGainDelayFrame = imgsensor_info.ae_ispGain_delay_frame;
    sensor_info->IHDR_Support = imgsensor_info.ihdr_support;
    sensor_info->IHDR_LE_FirstLine = imgsensor_info.ihdr_le_firstline;
    sensor_info->SensorModeNum = imgsensor_info.sensor_mode_num;

    sensor_info->SensorMIPILaneNumber = imgsensor_info.mipi_lane_num;
    sensor_info->SensorClockFreq = imgsensor_info.mclk;
    sensor_info->SensorClockDividCount = 3; /* not use */
    sensor_info->SensorClockRisingCount = 0;
    sensor_info->SensorClockFallingCount = 2; /* not use */
    sensor_info->SensorPixelClockCount = 3; /* not use */
    sensor_info->SensorDataLatchCount = 2; /* not use */

    sensor_info->MIPIDataLowPwr2HighSpeedTermDelayCount = 0;
    sensor_info->MIPICLKLowPwr2HighSpeedTermDelayCount = 0;
    sensor_info->SensorWidthSampling = 0;  // 0 is default 1x
    sensor_info->SensorHightSampling = 0;   // 0 is default 1x
    sensor_info->SensorPacketECCOrder = 1;

    switch (scenario_id) {
        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
            sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.pre.mipi_data_lp2hs_settle_dc;

            break;
        case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
            sensor_info->SensorGrabStartX = imgsensor_info.cap.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.cap.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.cap.mipi_data_lp2hs_settle_dc;

            break;
        case MSDK_SCENARIO_ID_VIDEO_PREVIEW:

            sensor_info->SensorGrabStartX = imgsensor_info.normal_video.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.normal_video.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.normal_video.mipi_data_lp2hs_settle_dc;

            break;
        case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
            sensor_info->SensorGrabStartX = imgsensor_info.hs_video.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.hs_video.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.hs_video.mipi_data_lp2hs_settle_dc;

            break;
        case MSDK_SCENARIO_ID_SLIM_VIDEO:
            sensor_info->SensorGrabStartX = imgsensor_info.slim_video.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.slim_video.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.slim_video.mipi_data_lp2hs_settle_dc;

            break;
	case MSDK_SCENARIO_ID_CUSTOM1:
            sensor_info->SensorGrabStartX = imgsensor_info.zsd.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.zsd.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.zsd.mipi_data_lp2hs_settle_dc;

            break;
	case MSDK_SCENARIO_ID_CUSTOM2:
            sensor_info->SensorGrabStartX = imgsensor_info.hd_4k_video.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.hd_4k_video.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.hd_4k_video.mipi_data_lp2hs_settle_dc;

            break;
	case MSDK_SCENARIO_ID_CUSTOM3:
            sensor_info->SensorGrabStartX = imgsensor_info.slim_cap.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.slim_cap.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.slim_cap.mipi_data_lp2hs_settle_dc;

            break;
	case MSDK_SCENARIO_ID_CUSTOM4:
            sensor_info->SensorGrabStartX = imgsensor_info.hd_gis_video.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.hd_gis_video.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.hd_gis_video.mipi_data_lp2hs_settle_dc;

            break;
        default:
            sensor_info->SensorGrabStartX = imgsensor_info.pre.startx;
            sensor_info->SensorGrabStartY = imgsensor_info.pre.starty;

            sensor_info->MIPIDataLowPwr2HighSpeedSettleDelayCount = imgsensor_info.pre.mipi_data_lp2hs_settle_dc;
            break;
    }

    return ERROR_NONE;
}   /*  get_info  */


static kal_uint32 control(MSDK_SCENARIO_ID_ENUM scenario_id, MSDK_SENSOR_EXPOSURE_WINDOW_STRUCT *image_window,
                      MSDK_SENSOR_CONFIG_STRUCT *sensor_config_data)
{
    LOG_INF("scenario_id = %d\n", scenario_id);
    spin_lock(&imgsensor_drv_lock);
    imgsensor.current_scenario_id = scenario_id;
    spin_unlock(&imgsensor_drv_lock);
    switch (scenario_id) {
        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
            preview(image_window, sensor_config_data);
            break;
        case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
            capture(image_window, sensor_config_data);
            break;
        case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
            normal_video(image_window, sensor_config_data);
            break;
        case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
            hs_video(image_window, sensor_config_data);
            break;
        case MSDK_SCENARIO_ID_SLIM_VIDEO:
            slim_video(image_window, sensor_config_data);
            break;
	case MSDK_SCENARIO_ID_CUSTOM1:
            zsd_capture(image_window, sensor_config_data);
            break;
	case MSDK_SCENARIO_ID_CUSTOM2:
            hd_4k_video(image_window, sensor_config_data);
            break;
	case MSDK_SCENARIO_ID_CUSTOM3:
            slim_capture(image_window, sensor_config_data);
            break;
	case MSDK_SCENARIO_ID_CUSTOM4:
            hd_gis_video(image_window, sensor_config_data);
            break;
        default:
            LOG_INF("Error ScenarioId setting");
            preview(image_window, sensor_config_data);
            return ERROR_INVALID_SCENARIO_ID;
    }
    return ERROR_NONE;
}   /* control() */



static kal_uint32 set_video_mode(UINT16 framerate)
{//This Function not used after ROME
    LOG_INF("framerate = %d\n ", framerate);
    // SetVideoMode Function should fix framerate
    if (framerate == 0)
        // Dynamic frame rate
        return ERROR_NONE;
    spin_lock(&imgsensor_drv_lock);
    if ((framerate == 300) && (imgsensor.autoflicker_en == KAL_TRUE))
        imgsensor.current_fps = 296;
    else if ((framerate == 150) && (imgsensor.autoflicker_en == KAL_TRUE))
        imgsensor.current_fps = 146;
    else
        imgsensor.current_fps = framerate;
    spin_unlock(&imgsensor_drv_lock);
    set_max_framerate(imgsensor.current_fps,1);

    return ERROR_NONE;
}

static kal_uint32 set_auto_flicker_mode(kal_bool enable, UINT16 framerate)
{
    LOG_INF("enable = %d, framerate = %d \n", enable, framerate);
    spin_lock(&imgsensor_drv_lock);
    if (enable) //enable auto flicker
        imgsensor.autoflicker_en = KAL_TRUE;
    else //Cancel Auto flick
        imgsensor.autoflicker_en = KAL_FALSE;
    spin_unlock(&imgsensor_drv_lock);
    return ERROR_NONE;
}


static kal_uint32 set_max_framerate_by_scenario(MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 framerate)
{
    kal_uint32 frame_length;

    LOG_INF("scenario_id = %d, framerate = %d\n", scenario_id, framerate);

    switch (scenario_id) {
        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
            frame_length = imgsensor_info.pre.pclk / framerate * 10 / imgsensor_info.pre.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.pre.framelength) ? (frame_length - imgsensor_info.pre.framelength) : 0;
            imgsensor.frame_length = imgsensor_info.pre.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            break;
        case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
            if(framerate == 0)
                return ERROR_NONE;
            frame_length = imgsensor_info.normal_video.pclk / framerate * 10 / imgsensor_info.normal_video.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.normal_video.framelength) ? (frame_length - imgsensor_info.normal_video.framelength) : 0;
            imgsensor.frame_length = imgsensor_info.normal_video.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            break;
        case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
            if (imgsensor.current_fps != imgsensor_info.cap.max_framerate)
                    LOG_INF("Warning: current_fps %d fps is not support, so use cap's setting: %d fps!\n",framerate,imgsensor_info.cap.max_framerate/10);
            frame_length = imgsensor_info.cap.pclk / framerate * 10 / imgsensor_info.cap.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.cap.framelength) ? (frame_length - imgsensor_info.cap.framelength) : 0;
            imgsensor.frame_length = imgsensor_info.cap.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            break;
        case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
            frame_length = imgsensor_info.hs_video.pclk / framerate * 10 / imgsensor_info.hs_video.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.hs_video.framelength) ? (frame_length - imgsensor_info.hs_video.framelength) : 0;
            imgsensor.frame_length = imgsensor_info.hs_video.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            break;
        case MSDK_SCENARIO_ID_SLIM_VIDEO:
            frame_length = imgsensor_info.slim_video.pclk / framerate * 10 / imgsensor_info.slim_video.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.slim_video.framelength) ? (frame_length - imgsensor_info.slim_video.framelength): 0;
            imgsensor.frame_length = imgsensor_info.slim_video.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            break;
	case MSDK_SCENARIO_ID_CUSTOM1:
            frame_length = imgsensor_info.zsd.pclk / framerate * 10 / imgsensor_info.zsd.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.zsd.framelength) ? (frame_length - imgsensor_info.zsd.framelength): 0;
            imgsensor.frame_length = imgsensor_info.zsd.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            break;
	case MSDK_SCENARIO_ID_CUSTOM2:
            frame_length = imgsensor_info.hd_4k_video.pclk / framerate * 10 / imgsensor_info.hd_4k_video.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.hd_4k_video.framelength) ? (frame_length - imgsensor_info.hd_4k_video.framelength): 0;
            imgsensor.frame_length = imgsensor_info.hd_4k_video.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            break;
	case MSDK_SCENARIO_ID_CUSTOM3:
            frame_length = imgsensor_info.slim_cap.pclk / framerate * 10 / imgsensor_info.slim_cap.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.slim_cap.framelength) ? (frame_length - imgsensor_info.slim_cap.framelength): 0;
            imgsensor.frame_length = imgsensor_info.slim_cap.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            break;
	case MSDK_SCENARIO_ID_CUSTOM4:
            frame_length = imgsensor_info.hd_gis_video.pclk / framerate * 10 / imgsensor_info.hd_gis_video.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.hd_gis_video.framelength) ? (frame_length - imgsensor_info.hd_gis_video.framelength): 0;
            imgsensor.frame_length = imgsensor_info.hd_gis_video.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            break;
        default:  //coding with  preview scenario by default
            frame_length = imgsensor_info.pre.pclk / framerate * 10 / imgsensor_info.pre.linelength;
            spin_lock(&imgsensor_drv_lock);
            imgsensor.dummy_line = (frame_length > imgsensor_info.pre.framelength) ? (frame_length - imgsensor_info.pre.framelength) : 0;
            imgsensor.frame_length = imgsensor_info.pre.framelength + imgsensor.dummy_line;
            imgsensor.min_frame_length = imgsensor.frame_length;
            spin_unlock(&imgsensor_drv_lock);
            //set_dummy();
            LOG_INF("error scenario_id = %d, we use preview scenario \n", scenario_id);
            break;
    }
    return ERROR_NONE;
}


static kal_uint32 get_default_framerate_by_scenario(MSDK_SCENARIO_ID_ENUM scenario_id, MUINT32 *framerate)
{
    LOG_INF("scenario_id = %d\n", scenario_id);

    switch (scenario_id) {
        case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
            *framerate = imgsensor_info.pre.max_framerate;
            break;
        case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
            *framerate = imgsensor_info.normal_video.max_framerate;
            break;
        case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
            *framerate = imgsensor_info.cap.max_framerate;
            break;
        case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
            *framerate = imgsensor_info.hs_video.max_framerate;
            break;
        case MSDK_SCENARIO_ID_SLIM_VIDEO:
            *framerate = imgsensor_info.slim_video.max_framerate;
            break;
	case MSDK_SCENARIO_ID_CUSTOM1:
            *framerate = imgsensor_info.zsd.max_framerate;
            break;
	case MSDK_SCENARIO_ID_CUSTOM2:
            *framerate = imgsensor_info.hd_4k_video.max_framerate;
            break;
	case MSDK_SCENARIO_ID_CUSTOM3:
            *framerate = imgsensor_info.slim_cap.max_framerate;
            break;
	case MSDK_SCENARIO_ID_CUSTOM4:
            *framerate = imgsensor_info.hd_gis_video.max_framerate;
            break;
        default:
            break;
    }

    return ERROR_NONE;
}

static kal_uint32 set_test_pattern_mode(kal_bool enable)
{
    LOG_INF("enable: %d\n", enable);

    if (enable) {
        write_cmos_sensor(0x30D8, 0x10);
        write_cmos_sensor(0x0600, 0x00);
        write_cmos_sensor(0x0601, 0x02);
    } else {
        write_cmos_sensor(0x30D8, 0x00);
    }
    spin_lock(&imgsensor_drv_lock);
    imgsensor.test_pattern = enable;
    spin_unlock(&imgsensor_drv_lock);
    return ERROR_NONE;
}

static kal_uint32 feature_control(MSDK_SENSOR_FEATURE_ENUM feature_id,
                             UINT8 *feature_para,UINT32 *feature_para_len)
{
    UINT16 *feature_return_para_16=(UINT16 *) feature_para;
    UINT16 *feature_data_16=(UINT16 *) feature_para;
    UINT32 *feature_return_para_32=(UINT32 *) feature_para;
    UINT32 *feature_data_32=(UINT32 *) feature_para;
    unsigned long long *feature_data=(unsigned long long *) feature_para;
    unsigned long long *feature_return_para=(unsigned long long *) feature_para;

    SENSOR_WINSIZE_INFO_STRUCT *wininfo;
    MSDK_SENSOR_REG_INFO_STRUCT *sensor_reg_data=(MSDK_SENSOR_REG_INFO_STRUCT *) feature_para;

    printk("feature_id = %d\n", feature_id);
    switch (feature_id) {
        case SENSOR_FEATURE_GET_PERIOD:
            *feature_return_para_16++ = imgsensor.line_length;
            *feature_return_para_16 = imgsensor.frame_length;
            *feature_para_len=4;
            break;
        case SENSOR_FEATURE_GET_PIXEL_CLOCK_FREQ:
            LOG_INF("feature_Control imgsensor.pclk = %d,imgsensor.current_fps = %d\n", imgsensor.pclk,imgsensor.current_fps);
            *feature_return_para_32 = imgsensor.pclk;
            *feature_para_len=4;
            break;
        case SENSOR_FEATURE_SET_ESHUTTER:
            set_shutter(*feature_data);
            break;
        case SENSOR_FEATURE_SET_NIGHTMODE:
            night_mode((BOOL) *feature_data);
            break;
        case SENSOR_FEATURE_SET_GAIN:
            set_gain((UINT16) *feature_data);
            break;
        case SENSOR_FEATURE_SET_FLASHLIGHT:
            break;
        case SENSOR_FEATURE_SET_ISP_MASTER_CLOCK_FREQ:
            break;
        case SENSOR_FEATURE_SET_REGISTER:
            write_cmos_sensor(sensor_reg_data->RegAddr, sensor_reg_data->RegData);
            break;
        case SENSOR_FEATURE_GET_REGISTER:
            sensor_reg_data->RegData = read_cmos_sensor(sensor_reg_data->RegAddr);
            break;
        case SENSOR_FEATURE_GET_LENS_DRIVER_ID:
            // get the lens driver ID from EEPROM or just return LENS_DRIVER_ID_DO_NOT_CARE
            // if EEPROM does not exist in camera module.
            *feature_return_para_32=LENS_DRIVER_ID_DO_NOT_CARE;
            *feature_para_len=4;
            break;
        case SENSOR_FEATURE_SET_VIDEO_MODE:
            set_video_mode(*feature_data);
            break;
        case SENSOR_FEATURE_CHECK_SENSOR_ID:
            get_imgsensor_id(feature_return_para_32);
            break;
        case SENSOR_FEATURE_SET_AUTO_FLICKER_MODE:
            set_auto_flicker_mode((BOOL)*feature_data_16,*(feature_data_16+1));
            break;
        case SENSOR_FEATURE_SET_MAX_FRAME_RATE_BY_SCENARIO:
            set_max_framerate_by_scenario((MSDK_SCENARIO_ID_ENUM)*feature_data, *(feature_data+1));
            break;
        case SENSOR_FEATURE_GET_DEFAULT_FRAME_RATE_BY_SCENARIO:
            get_default_framerate_by_scenario((MSDK_SCENARIO_ID_ENUM)*(feature_data), (MUINT32 *)(uintptr_t)(*(feature_data+1)));
            break;
        case SENSOR_FEATURE_SET_TEST_PATTERN:
            set_test_pattern_mode((BOOL)*feature_data);
            break;
        case SENSOR_FEATURE_GET_TEST_PATTERN_CHECKSUM_VALUE: //for factory mode auto testing
            *feature_return_para_32 = imgsensor_info.checksum_value;
            *feature_para_len=4;
            break;
        case SENSOR_FEATURE_SET_FRAMERATE:
            LOG_INF("current fps :%d\n", (UINT32)*feature_data);
            spin_lock(&imgsensor_drv_lock);
            imgsensor.current_fps = *feature_data;
            spin_unlock(&imgsensor_drv_lock);
            break;
        case SENSOR_FEATURE_SET_HDR:
            LOG_INF("ihdr enable :%d\n", (BOOL)*feature_data);
            spin_lock(&imgsensor_drv_lock);
            imgsensor.ihdr_en = (BOOL)*feature_data;
            spin_unlock(&imgsensor_drv_lock);
            break;
        case SENSOR_FEATURE_GET_CROP_INFO:
            LOG_INF("SENSOR_FEATURE_GET_CROP_INFO scenarioId:%d\n", (UINT32)*feature_data);

            wininfo = (SENSOR_WINSIZE_INFO_STRUCT *)(uintptr_t)(*(feature_data+1));

            switch (*feature_data_32) {
                case MSDK_SCENARIO_ID_CAMERA_CAPTURE_JPEG:
		    if (cpu_chip_is_mt6795m())
			memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[9],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
		    else
			memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[1],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
                    break;
                case MSDK_SCENARIO_ID_VIDEO_PREVIEW:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[2],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
                    break;
                case MSDK_SCENARIO_ID_HIGH_SPEED_VIDEO:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[3],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
                    break;
                case MSDK_SCENARIO_ID_SLIM_VIDEO:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[4],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
                    break;
		case MSDK_SCENARIO_ID_CUSTOM1:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[5],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
                    break;
		case MSDK_SCENARIO_ID_CUSTOM2:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[7],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
                    break;
		case MSDK_SCENARIO_ID_CUSTOM3:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[6],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
                    break;
		case MSDK_SCENARIO_ID_CUSTOM4:
                    memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[8],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
                    break;
                case MSDK_SCENARIO_ID_CAMERA_PREVIEW:
                default:
		    if (cpu_chip_is_mt6795m())
			memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[10],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
		    else
			memcpy((void *)wininfo,(void *)&imgsensor_winsize_info[0],sizeof(SENSOR_WINSIZE_INFO_STRUCT));
                    break;
            }
	    break;
        case SENSOR_FEATURE_SET_IHDR_SHUTTER_GAIN:
            LOG_INF("SENSOR_SET_SENSOR_IHDR LE=%d, SE=%d, Gain=%d\n",(UINT16)*feature_data,(UINT16)*(feature_data+1),(UINT16)*(feature_data+2));
            ihdr_write_shutter_gain((UINT16)*feature_data,(UINT16)*(feature_data+1),(UINT16)*(feature_data+2));
            break;
        default:
            break;
    }

    return ERROR_NONE;
}   /*  feature_control()  */

static SENSOR_FUNCTION_STRUCT sensor_func = {
    open,
    get_info,
    get_resolution,
    feature_control,
    control,
    close
};

UINT32 IMX220_LITEON_MIPI_RAW_SensorInit(PSENSOR_FUNCTION_STRUCT *pfFunc)
{
    /* To Do : Check Sensor status here */
    if (pfFunc!=NULL)
        *pfFunc=&sensor_func;
    return ERROR_NONE;
}   /*  IMX220_LITEON_MIPI_RAW_SensorInit  */
