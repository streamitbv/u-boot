/*
 * (C) Copyright 2015 Google, Inc
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */

#include <common.h>
#include <asm/gpio.h>
#include <netdev.h>
#include <part.h>

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

int get_mac_address_from_mmc(uint8_t *target) {
    struct blk_desc *mmc = blk_get_dev("mmc", 0);
    if (!mmc) {
        printf("mac: Could not access mmc device\n");
        return 0;
    }
    uint8_t enet_addr_size = 6;
    uint8_t read_buffer[512];
    // 8064 is the first block from the RESERVED1 partition
    ulong blks = blk_dread(mmc, 8064, 1, read_buffer);
    if (blks != 1) {
        printf("mac: Could not read from mmc device\n");
        return 0;
    }
    // Arbitrary header bytes
    if (read_buffer[0] == 0x20 && read_buffer[1] == 0x17) {
        memcpy(target, &read_buffer[2], enet_addr_size);
            printf("Using mac address: %x:%x:%x:%x:%x:%x\n",
            target[0], target[1], target[2], target[3],
             target[4], target[5]);
        return 1;
    }
    else {
        printf("mac: Did not find eth mac address on RESERVED1 partition\n");
        printf("Header: %d %d\n", read_buffer[0], read_buffer[1]);
        return 0;
    }
}

int use_mmc_mac_address() {
    uint8_t ethaddr[6];
    if (!get_mac_address_from_mmc(ethaddr)) {
        return 0;
    }
    if (!is_valid_ethaddr(ethaddr)) {
        printf("mac: Invalid mac address loaded from mmc\n");
        return 0;
    }
    eth_setenv_enetaddr("ethaddr", ethaddr);
    return 1;
}

int rk_board_late_init(void) {
    use_mmc_mac_address();
    key_init();
    if (GetPortState(&key_recovery)) {
		setenv("preboot", "setenv preboot; rockusb 0 mmc 0");
    }
    return 0;
}
