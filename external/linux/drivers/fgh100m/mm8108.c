/*
 * Copyright 2017-2023 Morse Micro
 *
 */

#include <linux/firmware.h>
#include <linux/delay.h>

#include "hw.h"
#include "morse.h"
#include "firmware.h"
#include "mac.h"
#include "bus.h"
#include "debug.h"
#include "yaps.h"

#define MM8108_REG_HOST_MAGIC_VALUE		0xDEADBEEF
#define MM8108_LPHY_AON_RAM_ENABLE_VAL		0x0024a424
#define MM8108_REG_RESET_VALUE			0xDEAD

/* This should be at a fixed location for a family of chipset */
#define MM8108_REG_CHIP_ID			0x00002d20 /* Chip ID */

/* These can change but need to add them to hw_regs structure and dynamically attach it */
#define MM8108_REG_SDIO_DEVICE_ADDR		0x0000207C /* apps_hal_r_system_sdio_device*/
/* offset of apps_hal_rw_system_sdio_device_tl_burst_sel_fn2 */
#define MM8108_REG_SDIO_DEVICE_BURST_OFFSET	9
#define MM8108_REG_PLL_ADDR			0x00002108 /* system_digpll_cfg_enable */
#define MM8108_REG_PLL_ENABLE_OFFSET		0
#define MM8108_REG_PLL_ENABLE_MASK		BIT(0)
#define MM8108_PLL_ENABLE			1
#define MM8108_REG_PLL_GOOD_LOCK_MASK		BIT(14)
#define MM8108_REG_SYS_RAM_POWER_ADDR		0x00002124	/* apps_hal_w_system_ram_power */

/* Generates IRQ to the target */
#define MM8108_REG_TRGR_BASE			0x00003c00 /* HostSync addr */
#define MM8108_REG_INT_BASE			0x00003c50 /* Hostsync reg*/
#define MM8108_REG_MSI				0x00004100 /* Clint */

#define MM8108_REG_MANIFEST_PTR_ADDRESS		0x00002d40 /* SW Manifest Pointer */
#define MM8108_REG_APPS_BOOT_ADDR		0x00002084 /* APPS core boot address */
#define MM8108_REG_RESET			0x000020AC /* Digital Reset */
#define MM8108_REG_AON_ADDR			0x00002114 /* system_ao_mem_in */
#define MM8108_REG_AON_LATCH_ADDR		0x00405020 /* radio_rf_ao_cfg_ao_latch */
#define MM8108_REG_AON_LATCH_MASK		0x1
#define MM8108_REG_AON_RESET_USB_VALUE		0x8
#define MM8108_APPS_MAC_DMEM_ADDR_START		0x00100000 /* DTCM */

#define MM8108_SPI_INTER_BLOCK_DELAY_BURST16_NS	4800
#define MM8108_SPI_INTER_BLOCK_DELAY_BURST8_NS	8000
#define MM8108_SPI_INTER_BLOCK_DELAY_BURST4_NS	15000
#define MM8108_SPI_INTER_BLOCK_DELAY_BURST2_NS	30000
#define MM8108_SPI_INTER_BLOCK_DELAY_BURST0_NS	58000

#define MM8108_FW_BASE				"mm8108"

static const char *mm810x_get_hw_version(u32 chip_id)
{
	switch (chip_id) {
	case MM8108B0_FPGA_ID:
		return "MM8108-B0-FPGA";
	case MM8108B0_ID:
		return "MM8108-B0";
	case MM8108B1_FPGA_ID:
		return "MM8108-B1-FPGA";
	case MM8108B1_ID:
		return "MM8108-B1";
	case MM8108B2_FPGA_ID:
		return "MM8108-B2-FPGA";
	case MM8108B2_ID:
		return "MM8108-B2";
	}
	return "unknown";
}

static char *mm810x_get_revision_string(u32 chip_id)
{
	u8 chip_rev = MORSE_DEVICE_GET_CHIP_REV(chip_id);

	switch (chip_rev) {
	case MM8108B0_REV:
		return MM8108B0_REV_STRING;
	case MM8108B1_REV:
		return MM8108B1_REV_STRING;
	case MM8108B2_REV:
		return MM8108B2_REV_STRING;
	default:
		return "??";
	}
}

static const char *mm810x_get_fw_variant_string(void)
{
	const char *fw_variant = "";

	if (is_fullmac_mode())
		fw_variant = MORSE_FW_FULLMAC_STRING;
	else if (is_thin_lmac_mode())
		fw_variant = MORSE_FW_THIN_LMAC_STRING;
	else if (is_virtual_sta_test_mode())
		fw_variant = MORSE_FW_VIRTUAL_STA_STRING;

	return fw_variant;
}

static char *mm810x_get_fw_path(u32 chip_id)
{
	/* Get all the strings required to construct the fw bin name */
	const char *revision_string = mm810x_get_revision_string(chip_id);
	const char *fw_variant_string = mm810x_get_fw_variant_string();

	return kasprintf(GFP_KERNEL,
			 MORSE_FW_DIR "/" MM8108_FW_BASE "%s%s" FW_ROM_LINKED_STRING MORSE_FW_EXT,
			 revision_string,
			 fw_variant_string);
}

static u8 mm810x_get_wakeup_delay_ms(u32 chip_id)
{
	/* MM8108 takes < 5ms to be active */
	return 10;
}

static u32 mm810x_get_burst_mode_inter_block_delay_ns(const u8 burst_mode)
{
	int ret;

	switch (burst_mode) {
	case SDIO_WORD_BURST_SIZE_16:
		ret = MM8108_SPI_INTER_BLOCK_DELAY_BURST16_NS;
		break;
	case SDIO_WORD_BURST_SIZE_8:
		ret = MM8108_SPI_INTER_BLOCK_DELAY_BURST8_NS;
		break;
	case SDIO_WORD_BURST_SIZE_4:
		ret = MM8108_SPI_INTER_BLOCK_DELAY_BURST4_NS;
		break;
	case SDIO_WORD_BURST_SIZE_2:
		ret = MM8108_SPI_INTER_BLOCK_DELAY_BURST2_NS;
		break;
	default:
		ret = MM8108_SPI_INTER_BLOCK_DELAY_BURST0_NS;
		break;
	}

	return ret;
}

static int mm810x_enable_burst_mode(struct morse *mors, const u8 burst_mode)
{
	u32 reg32_value;
	int ret = mm810x_get_burst_mode_inter_block_delay_ns(burst_mode);

	MORSE_WARN_ON(FEATURE_ID_DEFAULT, !mors);

	/* We should perform a read, modify & write here, since it is the safest option. */
	morse_claim_bus(mors);
	if (morse_reg32_read(mors, MM8108_REG_SDIO_DEVICE_ADDR, &reg32_value)) {
		ret = -EPERM;
		goto exit;
	}

	reg32_value &= ~(u32)(SDIO_WORD_BURST_MASK << MM8108_REG_SDIO_DEVICE_BURST_OFFSET);
	reg32_value |= (u32)(burst_mode << MM8108_REG_SDIO_DEVICE_BURST_OFFSET);

	MORSE_INFO(mors, "Setting Burst mode to %d Writing 0x%08X to the register\n",
		   burst_mode, reg32_value);

	if (morse_reg32_write(mors, MM8108_REG_SDIO_DEVICE_ADDR, reg32_value)) {
		ret = -EPERM;
		goto exit;
	}

exit:
	morse_release_bus(mors);

	if (ret < 0)
		MORSE_PR_ERR(FEATURE_ID_DEFAULT, "%s failed\n", __func__);

	return ret;
}

static int mm810x_pre_load_prepare(struct morse *mors)
{
	u32 write_value;
	u32 confirm_value;
	int ret = 0;

	MORSE_WARN_ON(FEATURE_ID_DEFAULT, !mors);

	morse_claim_bus(mors);

	if (morse_reg32_read(mors, MM8108_REG_PLL_ADDR, &write_value)) {
		ret = -EPERM;
		goto exit;
	}

	write_value &= ~(u32)(MM8108_REG_PLL_ENABLE_MASK << MM8108_REG_PLL_ENABLE_OFFSET);
	write_value |= (u32)(MM8108_PLL_ENABLE << MM8108_REG_PLL_ENABLE_OFFSET);

	/*
	 * TODO: SW-11980: Cleanup this code after A0 EOL
	 * We currently enable digital PLL in bootrom starting A2. This host/driver
	 * code will only be needed for A0, otherwise we will fail to load the firmware
	 */
	MORSE_INFO(mors, "Enabling Digital PLL\n");

	if (morse_reg32_write(mors, MM8108_REG_PLL_ADDR, write_value)) {
		ret = -EPERM;
		goto exit;
	}

	/* Wait for the PLL to lock */
	mdelay(5);

	/* Check to see if PLL is locked */
	if (morse_reg32_read(mors, MM8108_REG_PLL_ADDR, &confirm_value) ||
	    !(confirm_value & MM8108_REG_PLL_GOOD_LOCK_MASK)) {
		/*
		 * SW-11980
		 * Digital PLL should be locked here, but if not we will resume anyway.
		 * Firmware will re-configure the XTAL later and check again for the locking
		 * signal before proceeding
		 */
		WARN_ONCE(1, "Digital PLL is not locked. Continue anyway!\n");
	}

	MORSE_INFO(mors, "Enabling LPHY AON RAM\n");

	if (morse_reg32_write(mors, MM8108_REG_SYS_RAM_POWER_ADDR,
			      MM8108_LPHY_AON_RAM_ENABLE_VAL)) {
		ret = -EPERM;
		goto exit;
	}

exit:
	morse_release_bus(mors);

	if (ret < 0)
		MORSE_PR_ERR(FEATURE_ID_DEFAULT, "%s failed with error %d\n", __func__, ret);

	return ret;
}

static int mm810x_digital_reset(struct morse *mors)
{
	u32 chip_id;
	int ret = 0;

	morse_claim_bus(mors);

	if (mors->bus_type == MORSE_HOST_BUS_TYPE_USB) {
#ifdef CONFIG_MORSE_USB
		ret = morse_usb_ndr_reset(mors);
#endif
		goto usb_reset;
	}

	if (MORSE_REG_RESET(mors) != 0)
		ret = morse_reg32_write(mors, MORSE_REG_RESET(mors), MORSE_REG_RESET_VALUE(mors));

usb_reset:
	/* SDIO needs some time after reset */
	if (sdio_reset_time > 0)
		msleep(sdio_reset_time);

	/* SW-10325 WAR: dummy read to fix the read/write failures after digital reset on SPI */
	morse_reg32_read(mors, MORSE_REG_CHIP_ID(mors), &chip_id);

	morse_release_bus(mors);

	if (!ret)
		mors->chip_was_reset = true;

	return ret;
}

static int mm810x_pre_coredump_hook(struct morse *mors, enum morse_coredump_method method)
{
	int ret = 0;

	if (method == COREDUMP_METHOD_USERSPACE_SCRIPT)
		return ret;
	/* We need disable SDIO tilelink bursting for register reads to work from the driver. */
	if (mors->bus_ops->config_burst_mode)
		mors->bus_ops->config_burst_mode(mors, false);

	return ret;
}

static int mm810x_post_coredump_hook(struct morse *mors, enum morse_coredump_method method)
{
	int ret = 0;

	if (method == COREDUMP_METHOD_USERSPACE_SCRIPT)
		return ret;

	if (mors->bus_ops->config_burst_mode)
		mors->bus_ops->config_burst_mode(mors, true);

	return ret;
}

const struct morse_hw_regs mm8108_regs = {
	/* Register address maps */
	.irq_base_address = MM8108_REG_INT_BASE,
	.trgr_base_address = MM8108_REG_TRGR_BASE,

	/* Reset */
	.cpu_reset_address = MM8108_REG_RESET,
	.cpu_reset_value = MM8108_REG_RESET_VALUE,

	/* Pointer to manifest */
	.manifest_ptr_address = MM8108_REG_MANIFEST_PTR_ADDRESS,

	/* Trigger SWI */
	.msi_address = MM8108_REG_MSI,
	.msi_value = 0x1,
	/* Firmware */
	.magic_num_value = MM8108_REG_HOST_MAGIC_VALUE,

	/*
	 *  Don't set the clock enables to the cores before RAM is loaded,
	 *      otherwise you will have a bad time.
	 *      As MAC FW is being loaded, it will straight away attempt to read memory
	 *      hammering the Memory system, preventing the SDIO Controller from writing memory
	 */
	.early_clk_ctrl_value = 0,

	/* OTP data base address */
	/*
	 * TODO: MM-4868
	 * Not yet implemented for MM8108 so skip this for now
	 */
	.otp_data_base_address = 0,

	.pager_base_address = MM8108_APPS_MAC_DMEM_ADDR_START,

	/* AON registers */
	.aon_latch = MM8108_REG_AON_LATCH_ADDR,
	.aon_latch_mask = MM8108_REG_AON_LATCH_MASK,
	.aon_reset_usb_value = MM8108_REG_AON_RESET_USB_VALUE,
	.aon = MM8108_REG_AON_ADDR,
	.aon_count = 2,

	/* hart0 boot address */
	.boot_address = MM8108_REG_APPS_BOOT_ADDR,
};

struct morse_hw_cfg mm8108_cfg = {
	.regs = NULL,
	.chip_id_address = MM8108_REG_CHIP_ID,
	.ops = &morse_yaps_ops,
	.bus_double_read = false,
	.enable_short_bcn_as_dtim = true,
	.valid_chip_ids = {
		MM8108B0_FPGA_ID,
		MM8108B0_ID,
		MM8108B1_FPGA_ID,
		MM8108B1_ID,
		MM8108B2_FPGA_ID,
		MM8108B2_ID,
		CHIP_ID_END
	},
	.enable_sdio_burst_mode = mm810x_enable_burst_mode,
	.pre_load_prepare = mm810x_pre_load_prepare,
	.digital_reset = mm810x_digital_reset,
	.get_ps_wakeup_delay_ms = mm810x_get_wakeup_delay_ms,
	.get_hw_version = mm810x_get_hw_version,
	.get_fw_path = mm810x_get_fw_path,
	.pre_coredump_hook = mm810x_pre_coredump_hook,
	.post_coredump_hook = mm810x_post_coredump_hook,
};

struct morse_chip_series mm81xx_chip_series = {
	.chip_id_address = MM8108_REG_CHIP_ID
};

/* B0 ROM_LINKED */
MODULE_FIRMWARE(MORSE_FW_DIR "/" MM8108_FW_BASE
				 MM8108B0_REV_STRING
				 FW_ROM_LINKED_STRING
				 MORSE_FW_EXT);

/* B1 ROM_LINKED */
MODULE_FIRMWARE(MORSE_FW_DIR "/" MM8108_FW_BASE
				 MM8108B1_REV_STRING
				 FW_ROM_LINKED_STRING
				 MORSE_FW_EXT);

/* B2 ROM_LINKED */
MODULE_FIRMWARE(MORSE_FW_DIR "/" MM8108_FW_BASE
				 MM8108B2_REV_STRING
				 FW_ROM_LINKED_STRING
				 MORSE_FW_EXT);

/* B0 Fullmac */
MODULE_FIRMWARE(MORSE_FW_DIR "/" MM8108_FW_BASE
				 MM8108B0_REV_STRING
				 MORSE_FW_FULLMAC_STRING
				 FW_ROM_LINKED_STRING
				 MORSE_FW_EXT);

/* B1 Fullmac */
MODULE_FIRMWARE(MORSE_FW_DIR "/" MM8108_FW_BASE
				 MM8108B1_REV_STRING
				 MORSE_FW_FULLMAC_STRING
				 FW_ROM_LINKED_STRING
				 MORSE_FW_EXT);

/* B2 Fullmac */
MODULE_FIRMWARE(MORSE_FW_DIR "/" MM8108_FW_BASE
				 MM8108B2_REV_STRING
				 MORSE_FW_FULLMAC_STRING
				 FW_ROM_LINKED_STRING
				 MORSE_FW_EXT);
