#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/spinlock.h>

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/wakelock.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <asm/delay.h>
#include <mach/mt_reg_base.h>
#include <mach/eint.h>
#include <mach/eint_drv.h>
#include <mach/irqs.h>
#include <mach/sync_write.h>

/*kpd.h file path: ALPS/mediatek/kernel/include/linux */
#include <linux/kpd.h>
#include "cust_gpio_usage.h"
#include "cust_kpd.h"
#include "cust_eint.h"
#include <mach/mt_gpio.h>
#include <asm/atomic.h>
#include <linux/mutex.h>

#define DEBUG_KEY
#ifndef DEBUG_KEY
	#define printk
#endif

struct gpio_button_data {
	const struct gpio_keys_button *button;
	struct input_dev *input;

	unsigned int timer_debounce;	/* in msecs */
	unsigned int irq;
	spinlock_t lock;
	struct mutex key_mutex;
	bool disabled;
	atomic_t key_pressed;
};

struct gpio_keys_drvdata {
	struct input_dev *input;
	struct mutex disable_lock;
	unsigned int n_buttons;
	int (*enable)(struct device *dev);
	void (*disable)(struct device *dev);
	struct gpio_button_data data[0];
};

typedef enum {
	GPIO_VOLDOWN = 0,
	GPIO_VOLUP,
	GPIO_HALL,
	GPIO_MAX
} GPIO_INDEX;

#define RISING_POLARITY  MT_EINT_POL_POS
#define FALLING_POLARITY MT_EINT_POL_NEG

#define PRESSED  (1)
#define RELEASED (0)
#define CLOSED   (2)
#define REMOVED  (3)

#define FALLING MT_EINT_POL_NEG // 0
#define RISING  MT_EINT_POL_POS // 1
#define LOW     MT_EINT_POL_NEG // 0
#define HIGH    MT_EINT_POL_POS // 1

extern void  external_kpd_eint_set_polarity(int eint, int pol);
extern int  external_kpd_eint_to_irq(int eint);
extern void  external_kpd_eint_init(struct gpio_button_data *bdata);

#define INTERFACE
#ifdef INTERFACE
extern int meizu_sysfslink_register_n(struct device *dev, char *name);
#endif

