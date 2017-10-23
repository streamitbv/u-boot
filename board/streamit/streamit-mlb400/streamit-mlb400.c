/*
 * (C) Copyright 2015 Google, Inc
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */

#include <common.h>
#include <asm/gpio.h>
#include <spl.h>

#define LONE_PRESS_TIME		1500//ms
#define KEY_LONG_PRESS		-1
#define KEY_SHORT_PRESS		1

#if defined(CONFIG_RKCHIP_RK3126)
#define KEY_ADC_CN		2
#elif defined(CONFIG_RKCHIP_RK322XH)
#define KEY_ADC_CN		0
#else
#define KEY_ADC_CN		1
#endif

#define SARADC_BASE					0xFF100000

#define read_XDATA(address) 		(*((uint16_t volatile*)(unsigned long)(address)))
#define read_XDATA32(address)		(*((uint32_t volatile*)(unsigned long)(address)))
#define write_XDATA(address, value) 	(*((uint16_t volatile*)(unsigned long)(address)) = value)
#define write_XDATA32(address, value)	(*((uint32_t volatile*)(unsigned long)(address)) = value)

typedef enum{
	KEY_NULL,
	KEY_AD,      // AD°´¼ü
	KEY_INT,
	KEY_REMOTE,
}KEY_TYPE;

typedef struct
{
	const char *name;   /* name of the fdt property defining this */
	unsigned int gpio;      /* GPIO number, or FDT_GPIO_NONE if none */
	uint8_t flags;       /* FDT_GPIO_... flags */
}gpio_conf;


typedef struct
{
	uint32_t index;
	uint32_t keyValueLow;
	uint32_t keyValueHigh;
	uint32_t data;
	uint32_t stas;
	uint32_t ctrl;
}adc_conf;

typedef struct
{
	const char *name;   /* name of the fdt property defining this */
	unsigned int gpio;      /* GPIO number, or FDT_GPIO_NONE if none */
	uint8_t flags;       /* FDT_GPIO_... flags */
	volatile uint32_t  pressed_state;
	volatile uint32_t  press_time;
}int_conf;

typedef struct {
	KEY_TYPE type;
	union{
		adc_conf    adc;
		int_conf    ioint;
	}key;
}key_config;

static key_config	key_recovery;

static int GetPortState(key_config *key)
{
	uint32_t tt;
	uint32_t hCnt = 0;
	adc_conf* adc = &key->key.adc;
	int_conf* ioint = &key->key.ioint;

	if (key->type == KEY_AD) {
		for (tt = 0; tt < 10; tt++) {
			/* read special gpio port value. */
			uint32_t value;
			uint32_t timeout = 0;

			write_XDATA32(adc->ctrl, 0);
			udelay(5);
			write_XDATA32(adc->ctrl, 0x0028 | (adc->index));
			udelay(5);
			do {
				value = read_XDATA32(adc->ctrl);
				timeout++;
			} while((value & 0x40) == 0);
			value = read_XDATA32(adc->data);
			//printf("adc key = %d\n",value);
			//DRVDelayUs(1000);
			if ((value <= adc->keyValueHigh) && (value >= adc->keyValueLow))
				hCnt++;
		}
		write_XDATA32(adc->ctrl, 0);
		return (hCnt > 8);
	} else if (key->type == KEY_INT) {
		int state;

		state = gpio_get_value(key->key.ioint.gpio);
		if (ioint->pressed_state == 0) { // active low
			return !state;
		} else {
			return state;
		}
	}

	return 0;
}

static void RecoveryKeyInit(void)
{
	key_recovery.type = KEY_AD;
	key_recovery.key.adc.index = KEY_ADC_CN;
	key_recovery.key.adc.keyValueLow = 0;
	key_recovery.key.adc.keyValueHigh = 30;
	key_recovery.key.adc.data = SARADC_BASE + 0;
	key_recovery.key.adc.stas = SARADC_BASE + 4;
	key_recovery.key.adc.ctrl = SARADC_BASE + 8;
}

void key_init(void)
{
	memset(&key_recovery, 0, sizeof(key_config));

	RecoveryKeyInit();
}

int rk_board_late_init(void) {
	key_init();
    if (GetPortState(&key_recovery)) {
		setenv("preboot", "setenv preboot; rockusb 0 mmc 0");
    }
    return 0;
}

void board_boot_order(u32 *spl_boot_list)
{
	/* eMMC prior to sdcard. */
	spl_boot_list[1] = BOOT_DEVICE_MMC2;
	spl_boot_list[0] = BOOT_DEVICE_MMC1;
}
