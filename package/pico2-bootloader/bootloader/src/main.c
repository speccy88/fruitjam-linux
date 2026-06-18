#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#include "helpers.h"
#include "qmi.h"

#include "image.h"

#ifdef PICO_SDK
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#else
#include "rp2350-map.h"

#include "pads.h"
#include "resets.h"
#include "clocks.h"
#include "ticks.h"
#include "uart.h"
#include "gpio.h"

#include "ctype_local.h"
#include "stdio_local.h"
#include "string_local.h"
#endif

#ifndef PICO2_BOOTLOADER_LED_PIN
#define PICO2_BOOTLOADER_LED_PIN		2
#endif
#ifndef PICO2_BOOTLOADER_PSRAM_CS_PIN
#define PICO2_BOOTLOADER_PSRAM_CS_PIN	19
#endif
#ifndef PICO2_BOOTLOADER_UART_INDEX
#define PICO2_BOOTLOADER_UART_INDEX	0
#endif
#ifndef PICO2_BOOTLOADER_UART_TX_PIN
#define PICO2_BOOTLOADER_UART_TX_PIN	0
#endif
#ifndef PICO2_BOOTLOADER_UART_RX_PIN
#define PICO2_BOOTLOADER_UART_RX_PIN	1
#endif
#ifndef PICO2_BOOTLOADER_LED_ACTIVE_LOW
#define PICO2_BOOTLOADER_LED_ACTIVE_LOW	0
#endif
#ifndef PICO2_BOOTLOADER_RESCUE_WATCHDOG_MS
#define PICO2_BOOTLOADER_RESCUE_WATCHDOG_MS 0
#endif

#define WATCHDOG_RESCUE_MAGIC	0x6ab73121u
#define WATCHDOG_BOOTSEL_VECTOR_MAGIC	0xb007c0d3u
#define WATCHDOG_BOOTSEL_TYPE		2u

#define LED_PIN			PICO2_BOOTLOADER_LED_PIN
#define RP2350_XIP_CSI_PIN	PICO2_BOOTLOADER_PSRAM_CS_PIN

#define PSRAM_LOCATION (0x11000000U)

/* Linker variable for the end of binary image */
#ifdef PICO_SDK
extern size_t __flash_binary_end[];
#else
extern size_t __payload_load_start[];
#endif

#define is_pow2(x) (((x) != 0) && (((x) & ((x) - 1)) == 0))
#define size_ceil(size, align)							\
	({									\
		static_assert(is_pow2(align), "Alignment not a power of 2");	\
		((size) + (align) - 1) & ~((align) - 1);			\
	})

static inline void set_uart_pinmux(void);
static inline void set_xip_cs1_pinmux(void);
static inline void set_sd_spi_pinmux(void);

static int wait_for_input(const char *msg);
static void hexdump(const void *data, size_t size);

static inline const void* get_payload_start_address(void);
static int psram_setup_and_test(size_t *psram_size);
static int test_executability(void* addr);
static void jump_to_kernel(void *ram_addr, const void *data_addr);
static void rescue_watchdog_check(void);
static void rescue_watchdog_arm(void);

int main(void) {
	int ret = 0;
	uint32_t jump_ret, data;
	size_t device_tree_size, psram_size, kernel_size = 0;
	void *ram_addr = (void *)PSRAM_LOCATION;
	const void *data_addr;

#ifdef PICO_SDK
	stdio_init_all();
#else
	xosc_init();

	set_reset(RESETS_PLL_USB, 0);
	set_reset(RESETS_PLL_SYS, 0);
	set_vreg_1v30();
	delay(1000000);
	set_usb_pll();
	set_sys_pll();

	set_ref_clock_xosc();
	set_sys_clock_pll_sys();
	set_usb_clock_pll_usb();
	set_adc_clock_pll_usb();
	set_perif_clock_clk_sys();
	set_hstx_clock_clk_sys();

	riscv_ticks_init(CLK_REF / MHZ);

#if PICO2_BOOTLOADER_UART_INDEX == 0
	set_reset(RESETS_UART0, 0);
#else
	set_reset(RESETS_UART1, 0);
#endif
	set_reset(RESETS_SPI0, 0);
	set_reset(RESETS_SPI1, 0);

	set_uart_pinmux();
	set_sd_spi_pinmux();

	set_config(LED_PIN, PADS_CLEAR);
	set_pinfunc(LED_PIN, GPIO_FUNC_SIO);
	uart_init();

	SIO_BASE[0x38/4] = BIT(LED_PIN); // OE_SET
#if PICO2_BOOTLOADER_LED_ACTIVE_LOW
	SIO_BASE[0x18/4] = BIT(LED_PIN); // OUT_CLR
#else
	SIO_BASE[0x14/4] = BIT(LED_PIN); // OUT_SET
#endif
#endif

	puts("\n\n\nRP2350 Bootloader starting...\n");
	rescue_watchdog_check();
	ret = psram_setup_and_test(&psram_size);
	if (ret)
		goto exit;

	data_addr = get_payload_start_address();
	if (!data_addr) {
		printf("Failed to get ROM address is invalid\n");
		goto exit;
	}

	printf("\nRom dump:\n");
	hexdump(data_addr, 0x20);

	device_tree_size = get_devicetree_size(data_addr);

	if (device_tree_size) {
		kernel_size = get_kernel_size(data_addr + device_tree_size);
		if(!kernel_size)
			printf("Kernel not found.\n");
	} else {
		printf("Device Tree not found.\n");
	}

	if ((device_tree_size + kernel_size) > psram_size) {
		printf("Data size 0x%04x is larger than PSRAM size 0x%04x\n",
		       (device_tree_size + kernel_size), psram_size);
		goto exit;
	}

	printf("\nCoping Kernel to RAM...");
	memcpy(ram_addr, data_addr + device_tree_size, kernel_size);
	printf("\rRam dump:              \n");
	hexdump(ram_addr, 0x20);

	if (kernel_size)
		jump_to_kernel(ram_addr, data_addr);

	ret = wait_for_input("Press y to jump to PSRAM...\r");
	if (ret == 'y' || ret == 'Y') {
		data = (uint32_t)printf;
		jump_ret = ((uint32_t (*)(void *))ram_addr)((void *)data);
		printf("Jump to PSRAM returned 0x%08x\n", jump_ret);
	}

exit:
	wait_for_input("Press any key to reset.\r");
	puts("Resetting...");

	return 0;
}

#ifndef PICO_SDK
static inline void set_uart_pinmux(void) {
	set_config(PICO2_BOOTLOADER_UART_TX_PIN, PADS_CLEAR);
	set_config(PICO2_BOOTLOADER_UART_RX_PIN, PADS_IE);
	set_pinfunc(PICO2_BOOTLOADER_UART_TX_PIN, GPIO_FUNC_UART);
	set_pinfunc(PICO2_BOOTLOADER_UART_RX_PIN, GPIO_FUNC_UART);
}

static inline void set_xip_cs1_pinmux(void) {
	set_config(RP2350_XIP_CSI_PIN, PADS_CLEAR);
	set_pinfunc(RP2350_XIP_CSI_PIN, GPIO_FUNC_XIP_CS1);
}

static inline void set_sd_spi_pinmux(void) {
	set_config(34, PADS_CLEAR); /* SD SCK */
	set_config(35, PADS_CLEAR); /* SD MOSI */
	set_config(36, PADS_IE);    /* SD MISO */
	set_config(39, PADS_CLEAR); /* SD CS */
	set_pinfunc(34, GPIO_FUNC_SPI);
	set_pinfunc(35, GPIO_FUNC_SPI);
	set_pinfunc(36, GPIO_FUNC_SPI);
	set_pinfunc(39, GPIO_FUNC_SPI);
}
#else
static inline void set_sd_spi_pinmux(void) {
	gpio_set_function(34, GPIO_FUNC_SPI);
	gpio_set_function(35, GPIO_FUNC_SPI);
	gpio_set_function(36, GPIO_FUNC_SPI);
	gpio_set_function(39, GPIO_FUNC_SPI);
}
#endif

static int wait_for_input(const char *msg) {
	int ret;

	while (1) {
		printf(msg);
#ifdef PICO_SDK
		ret = getchar_timeout_us(0);
		if (ret != PICO_ERROR_TIMEOUT) {
#else
		ret = uart_getc();
		if (ret != -1) {
#endif
			puts("");
			return ret;
		}
		delay(500000);
	}
}

static inline const void* get_payload_start_address(void) {
#ifdef PICO_SDK
	size_t payload_load = (size_t)&__flash_binary_end;
#else
	size_t payload_load = (size_t)&__payload_load_start;
#endif

	return (const void*)size_ceil(payload_load, 0x10);
}

static int psram_setup_and_test(size_t *psram_size) {
#ifdef PICO_SDK
	gpio_set_function(RP2350_XIP_CSI_PIN, GPIO_FUNC_XIP_CS1);
#else
	set_xip_cs1_pinmux();
#endif
	*psram_size = setup_psram();

	if (!*psram_size) {
		puts("PSRAM setup failed");
		return -1;
	}

	printf("PSRAM setup complete. PSRAM size 0x%lX (%d)", *psram_size, *psram_size);
	puts("");

	return test_executability((size_t *)(PSRAM_LOCATION + (*psram_size) - 4));
}

static int test_executability(void* addr) {
	const uint16_t ret_inst = 0x8082;
	size_t addr_aligned = ((size_t)addr) & ~1;
	volatile uint16_t* addr_ptr = (volatile uint16_t*)addr_aligned;

	mb();
	*addr_ptr = ret_inst;
	mb();

	printf("Jumping to 0x%08x, aligned from 0x%08x\n", addr_aligned, (size_t)addr);
	printf("Function pointers: 0x%02x 0x%02x\n", ((uint8_t*)addr_ptr)[0], ((uint8_t*)addr_ptr)[1]);

	if (*addr_ptr != ret_inst) {
		printf("ERROR: Expected 0x%04x, got 0x%04x\n", ret_inst, *addr_ptr);
		return -1;
	}

	((void (*)(void))addr_aligned)();

	return 0;
}

static void jump_to_kernel(void *ram_addr, const void *data_addr) {
	typedef void (*image_entry_arg_t)(unsigned long hart, const void *dtb);
	image_entry_arg_t image_entry = (image_entry_arg_t)ram_addr;

	printf("\nJumping to kernel at 0x%08x and DT at 0x%08x\n", ram_addr, data_addr);
	printf("If you are using USB serial, please connect over the hardware serial port.\n");
	rescue_watchdog_arm();
	image_entry(0, data_addr);
}

#ifndef PICO_SDK
#define WATCHDOG_CTRL_REG		0
#define WATCHDOG_LOAD_REG		1
#define WATCHDOG_REASON_REG		2
#define WATCHDOG_SCRATCH4_REG		7
#define WATCHDOG_SCRATCH6_REG		9
#define WATCHDOG_SCRATCH7_REG		10
#define WATCHDOG_CTRL_ENABLE		BIT(30)
#define WATCHDOG_CTRL_PAUSE_MASK	(BIT(24) | BIT(25) | BIT(26))
#define WATCHDOG_LOAD_MAX		0x00ffffffu
#define PSM_WDSEL_REG			2
#define PSM_WDSEL_BITS			0x01ffffffu
#define PSM_WDSEL_ROSC			BIT(2)
#define PSM_WDSEL_XOSC			BIT(3)

#define BOOTROM_ENTRY_OFFSET		0x7dfcu
#define BOOTROM_RT_FLAG_FUNC_RISCV	0x0001u
#define BOOTROM_REBOOT_TYPE_BOOTSEL	0x0002u
#define BOOTROM_REBOOT_NO_RETURN	0x0100u
#define BOOTROM_TABLE_CODE(c1, c2)	((uint32_t)(c1) | ((uint32_t)(c2) << 8))
#define BOOTROM_FUNC_RESET_USB_BOOT	BOOTROM_TABLE_CODE('U', 'B')
#define BOOTROM_FUNC_REBOOT		BOOTROM_TABLE_CODE('R', 'B')

typedef void *(*rom_table_lookup_fn)(uint32_t code, uint32_t mask);
typedef void (*rom_reset_usb_boot_fn)(uint32_t gpio_activity_mask,
				      uint32_t disable_interface_mask);
typedef int (*rom_reboot_fn)(uint32_t flags, uint32_t delay_ms,
			     uint32_t p0, uint32_t p1);

static void watchdog_disable_rescue(void)
{
	WATCHDOG_BASE[WATCHDOG_CTRL_REG] &= ~WATCHDOG_CTRL_ENABLE;
	WATCHDOG_BASE[WATCHDOG_SCRATCH4_REG] = 0;
}

static int watchdog_rescue_fired(void)
{
	return WATCHDOG_BASE[WATCHDOG_REASON_REG] &&
	       WATCHDOG_BASE[WATCHDOG_SCRATCH4_REG] == WATCHDOG_RESCUE_MAGIC;
}

static int watchdog_bootsel_vector_fired(void)
{
	return WATCHDOG_BASE[WATCHDOG_REASON_REG] &&
	       WATCHDOG_BASE[WATCHDOG_SCRATCH4_REG] == WATCHDOG_BOOTSEL_VECTOR_MAGIC &&
	       WATCHDOG_BASE[WATCHDOG_SCRATCH6_REG] == WATCHDOG_BOOTSEL_TYPE &&
	       WATCHDOG_BASE[WATCHDOG_SCRATCH7_REG] == WATCHDOG_BOOTSEL_VECTOR_MAGIC;
}

static void watchdog_start_tick(void)
{
	TICK_BASE[13] = (CLK_REF / MHZ) & 0x1ffu;
	TICK_BASE[12] |= BIT(0);
}

static void *bootrom_func_lookup(uint32_t code)
{
	uint32_t ptr_size = (*(volatile uint8_t *)0x13 == 2) ? 2 : 4;
	uint32_t lookup_entry = BOOTROM_ENTRY_OFFSET - ptr_size;
	rom_table_lookup_fn lookup;

	lookup = (rom_table_lookup_fn)(uintptr_t)*(volatile uint16_t *)lookup_entry;
	if (!lookup)
		return NULL;

	return lookup(code, BOOTROM_RT_FLAG_FUNC_RISCV);
}

static void bootrom_reboot_bootsel(void)
{
	rom_reset_usb_boot_fn reset_usb_boot =
		(rom_reset_usb_boot_fn)bootrom_func_lookup(BOOTROM_FUNC_RESET_USB_BOOT);
	rom_reboot_fn reboot = (rom_reboot_fn)bootrom_func_lookup(BOOTROM_FUNC_REBOOT);
	int ret;

	printf("BOOTSEL ROM funcs: reset_usb_boot=0x%08x reboot=0x%08x\n",
	       (uint32_t)reset_usb_boot, (uint32_t)reboot);

	if (reset_usb_boot) {
		puts("Calling RESET_USB_BOOT...\n");
		reset_usb_boot(0, 0);
		puts("RESET_USB_BOOT returned unexpectedly.\n");
	}

	if (reboot) {
		puts("Calling REBOOT BOOTSEL...\n");
		ret = reboot(BOOTROM_REBOOT_TYPE_BOOTSEL | BOOTROM_REBOOT_NO_RETURN,
			     10, 0, 0);
		printf("REBOOT BOOTSEL returned %d.\n", ret);
	}

	while (1)
		nop();
}

static void rescue_watchdog_check(void)
{
	int rescue_fired = watchdog_rescue_fired();
	int bootsel_fell_through = watchdog_bootsel_vector_fired();

	if (rescue_fired || bootsel_fell_through) {
		if (rescue_fired)
			puts("Linux rescue watchdog fired; entering BOOTSEL.\n");
		else
			puts("BOOTSEL watchdog vector reached bootloader; entering BOOTSEL.\n");
		watchdog_disable_rescue();
		bootrom_reboot_bootsel();
	}

	watchdog_disable_rescue();
}

static void rescue_watchdog_arm(void)
{
	uint32_t load;

	if (!PICO2_BOOTLOADER_RESCUE_WATCHDOG_MS)
		return;

	load = (uint32_t)PICO2_BOOTLOADER_RESCUE_WATCHDOG_MS * 1000u;
	if (load > WATCHDOG_LOAD_MAX)
		load = WATCHDOG_LOAD_MAX;

	watchdog_start_tick();
	WATCHDOG_BASE[WATCHDOG_CTRL_REG] &= ~WATCHDOG_CTRL_ENABLE;
	PSM_BASE[PSM_WDSEL_REG] = PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC | PSM_WDSEL_XOSC);
	WATCHDOG_BASE[WATCHDOG_SCRATCH4_REG] = WATCHDOG_RESCUE_MAGIC;
	WATCHDOG_BASE[WATCHDOG_CTRL_REG] &= ~WATCHDOG_CTRL_PAUSE_MASK;
	WATCHDOG_BASE[WATCHDOG_LOAD_REG] = load;
	WATCHDOG_BASE[WATCHDOG_CTRL_REG] |= WATCHDOG_CTRL_ENABLE;
}
#else
static void watchdog_disable_rescue(void)
{
	watchdog_hw->ctrl &= ~WATCHDOG_CTRL_ENABLE_BITS;
	watchdog_hw->scratch[4] = 0;
}

static int watchdog_rescue_fired(void)
{
	return watchdog_hw->reason &&
	       watchdog_hw->scratch[4] == WATCHDOG_RESCUE_MAGIC;
}

static int watchdog_bootsel_vector_fired(void)
{
	return watchdog_hw->reason &&
	       watchdog_hw->scratch[4] == WATCHDOG_BOOTSEL_VECTOR_MAGIC &&
	       watchdog_hw->scratch[6] == WATCHDOG_BOOTSEL_TYPE &&
	       watchdog_hw->scratch[7] == WATCHDOG_BOOTSEL_VECTOR_MAGIC;
}

static void bootrom_reboot_bootsel(void)
{
	reset_usb_boot(0, 0);
}

static void rescue_watchdog_check(void)
{
	int rescue_fired = watchdog_rescue_fired();
	int bootsel_fell_through = watchdog_bootsel_vector_fired();

	if (rescue_fired || bootsel_fell_through) {
		if (rescue_fired)
			puts("Linux rescue watchdog fired; entering BOOTSEL.\n");
		else
			puts("BOOTSEL watchdog vector reached bootloader; entering BOOTSEL.\n");
		watchdog_disable_rescue();
		bootrom_reboot_bootsel();
	}

	watchdog_disable_rescue();
}

static void rescue_watchdog_arm(void)
{
	if (!PICO2_BOOTLOADER_RESCUE_WATCHDOG_MS)
		return;

	watchdog_enable(PICO2_BOOTLOADER_RESCUE_WATCHDOG_MS, false);
	watchdog_hw->scratch[4] = WATCHDOG_RESCUE_MAGIC;
}
#endif

static void hexdump(const void *data, size_t size) {
	const uint8_t *data_ptr = (const uint8_t *)data;
	size_t i, b;

	for (i = 0; i < size; i++) {
		if (i % 16 == 0)
			printf("%08x  ", (uint32_t)data_ptr + i);
		if (i % 8 == 0)
			printf(" ");

		printf("%02x ", data_ptr[i]);
		if (i % 16 == 15) {
			printf(" |");
			for (b = 0; b < 16; b++){
				if (isprint(data_ptr[i + b - 15]))
					printf("%c", data_ptr[i + b - 15]);
				else
					printf(".");
			}
			printf("|\n");
		}
	}
	printf("%08x\n", 16 + size - (size%16));
}
