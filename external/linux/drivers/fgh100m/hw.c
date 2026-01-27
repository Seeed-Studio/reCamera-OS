/*
 * Copyright 2017-2023 Morse Micro
 *
 */

#include <linux/types.h>
#include <linux/gpio.h>
#include "morse.h"
#include "debug.h"
#include "hw.h"
#include "pager_if.h"
#include "bus.h"

static int hw_reload_after_stop __read_mostly = 5;
module_param(hw_reload_after_stop, int, 0644);
MODULE_PARM_DESC(hw_reload_after_stop,
"Reload HW after a stop notification. Abort if stop events are less than this seconds apart (-1 to disable)");

int morse_hw_irq_enable(struct morse *mors, u32 irq, bool enable)
{
	u32 irq_en, irq_en_addr = irq < 32 ? MORSE_REG_INT1_EN(mors) : MORSE_REG_INT2_EN(mors);
	u32 irq_clr_addr = irq < 32 ? MORSE_REG_INT1_CLR(mors) : MORSE_REG_INT2_CLR(mors);
	u32 mask = irq < 32 ? (1 << irq) : (1 << (irq - 32));

	morse_claim_bus(mors);
	morse_reg32_read(mors, irq_en_addr, &irq_en);
	if (enable)
		irq_en |= (mask);
	else
		irq_en &= ~(mask);
	morse_reg32_write(mors, irq_clr_addr, mask);
	morse_reg32_write(mors, irq_en_addr, irq_en);
	morse_release_bus(mors);

	return 0;
}

void morse_hw_stop_work(struct work_struct *work)
{
	struct morse *mors = container_of(work, struct morse, hw_stop);

	if (!mors->started) {
		dev_err(mors->dev, "HW already stopped\n");
		return;
	}

	if (hw_reload_after_stop > 0 &&
	    (ktime_get_seconds() - mors->last_hw_stop) < hw_reload_after_stop) {
		/* HW reload was attempted twice in rapid succession - abort to prevent thrashing */
		dev_err(mors->dev,
			"Automatic HW reload aborted due to retry in < %ds\n",
			hw_reload_after_stop);
		return;
	}

	mutex_lock(&mors->lock);
	if (!morse_coredump_new(mors, MORSE_COREDUMP_REASON_CHIP_INDICATED_STOP))
		set_bit(MORSE_STATE_FLAG_DO_COREDUMP, &mors->state_flags);

	set_bit(MORSE_STATE_FLAG_CHIP_UNRESPONSIVE, &mors->state_flags);
	mors->last_hw_stop = ktime_get_seconds();
	mutex_unlock(&mors->lock);
	schedule_work(&mors->driver_restart);
}

static void to_host_hw_stop_irq_handle(struct morse *mors)
{
	dev_err(mors->dev, "HW has stopped%s\n",
		(hw_reload_after_stop < 0) ? " (ignoring)" : "");

	if (hw_reload_after_stop < 0)
		return;

	schedule_work(&mors->hw_stop);
}

int morse_hw_irq_handle(struct morse *mors)
{
	u32 status1 = 0;
#if defined(CONFIG_MORSE_DEBUG_IRQ)
	int i;
#endif

	morse_reg32_read(mors, MORSE_REG_INT1_STS(mors), &status1);

	if (status1 & MORSE_CHIP_IF_IRQ_MASK_ALL)
		mors->cfg->ops->chip_if_handle_irq(mors, status1);
	if (status1 & MORSE_INT_BEACON_VIF_MASK_ALL)
		morse_beacon_irq_handle(mors, status1);
	if (status1 & MORSE_INT_NDP_PROBE_REQ_PV0_VIF_MASK_ALL)
		morse_ndp_probe_req_resp_irq_handle(mors, status1);
	if (status1 & MORSE_INT_HW_STOP_NOTIFICATION)
		to_host_hw_stop_irq_handle(mors);

	morse_reg32_write(mors, MORSE_REG_INT1_CLR(mors), status1);

#if defined(CONFIG_MORSE_DEBUG_IRQ)
	mors->debug.hostsync_stats.irq++;
	for (i = 0; i < ARRAY_SIZE(mors->debug.hostsync_stats.irq_bits); i++) {
		if (status1 & BIT(i))
			mors->debug.hostsync_stats.irq_bits[i]++;
	}
#endif

	return status1 ? 1 : 0;
}

int morse_hw_irq_clear(struct morse *mors)
{
	morse_claim_bus(mors);
	morse_reg32_write(mors, MORSE_REG_INT1_CLR(mors), 0xFFFFFFFF);
	morse_reg32_write(mors, MORSE_REG_INT2_CLR(mors), 0xFFFFFFFF);
	morse_release_bus(mors);
	return 0;
}

int morse_hw_reset(int reset_pin)
{
	int ret = gpio_request(reset_pin, "morse-reset-ctrl");

	if (ret < 0) {
		MORSE_PR_ERR(FEATURE_ID_DEFAULT, "Failed to acquire reset gpio. Skipping reset.\n");
		return ret;
	}

	pr_info("Resetting Morse Chip\n");
	gpio_direction_output(reset_pin, 0);
	mdelay(20);
	/* setting gpio as float to avoid forcing 3.3V High */
	gpio_direction_input(reset_pin);
	pr_info("Done\n");

	gpio_free(reset_pin);

	return ret;
}

bool is_otp_xtal_wait_supported(struct morse *mors)
{
	int ret;
	u32 otp_word2;
	u32 otp_xtal_wait;

	if (MORSE_REG_OTP_DATA_WORD(mors, 0) == 0)
		/* Device doesn't support OTP (probably an FPGA) */
		return true;

	if (MORSE_REG_OTP_DATA_WORD(mors, 2) != 0) {
		morse_claim_bus(mors);
		ret = morse_reg32_read(mors, MORSE_REG_OTP_DATA_WORD(mors, 2), &otp_word2);
		morse_release_bus(mors);
		if (ret < 0) {
			MORSE_ERR(mors, "OTP data2 value read failed: %d\n", ret);
			return false;
		}
		otp_xtal_wait = (otp_word2 & MM610X_OTP_DATA2_XTAL_WAIT_POS);
		if (!otp_xtal_wait) {
			ret = -1;
			MORSE_ERR(mors, "OTP xtal wait bits not set\n");
			return false;
		}
		return true;
	}
	return false;
}

bool morse_hw_is_valid_chip_id(u32 chip_id, u32 *valid_chip_ids)
{
	int i;

	if (chip_id == CHIP_ID_END) {
		MORSE_WARN_ON_ONCE(FEATURE_ID_DEFAULT, 1);
		return false;
	}

	for (i = 0; valid_chip_ids[i] != CHIP_ID_END; i++)
		if (chip_id == valid_chip_ids[i])
			return true;
	return false;
}

int morse_hw_regs_attach(struct morse_hw_cfg *cfg, u32 chip_id)
{
	int ret = 0;
	u32 id = MORSE_DEVICE_GET_CHIP_ID(chip_id);
	/* MM6108XX should already have the regs attached to the config */
	switch (id) {
	case (MM8108XX_ID):
		cfg->regs = &mm8108_regs;
		break;
	}
	return ret;
}

int morse_hw_enable_stop_notifications(struct morse *mors, bool enable)
{
	return morse_hw_irq_enable(mors, MORSE_INT_HW_STOP_NOTIFICATION_NUM, enable);
}

int morse_chip_cfg_detect_and_init(struct morse *mors, struct morse_chip_series *mors_chip_series)
{
	int ret = 0;
	u32 chip_id = 0;

	morse_claim_bus(mors);
	ret = morse_reg32_read(mors, mors_chip_series->chip_id_address, &chip_id);
	morse_release_bus(mors);
	if (ret < 0) {
		MORSE_ERR(mors, "%s: Failed to access HW (errno:%d)", __func__, ret);
		return ret;
	}

	ret = morse_chip_cfg_init(mors, chip_id);

	return ret;
}

int morse_chip_cfg_init(struct morse *mors, u32 chip_id)
{
	int ret = 0;

	mors->chip_id = chip_id;

	switch (chip_id) {
	case(MM8108B0_ID):
	case(MM8108B1_ID):
	case(MM8108B2_ID):
	case(MM8108B0_FPGA_ID):
	case(MM8108B1_FPGA_ID):
	case(MM8108B2_FPGA_ID):
		mors->cfg = &mm8108_cfg;
		mors->cfg->regs = &mm8108_regs;
		break;
	case(MM6108A0_ID):
	case(MM6108A1_ID):
	case(MM6108A2_ID):
		mors->cfg = &mm6108_cfg;
		break;
	default:
		return -ENODEV;
	}

	return ret;
}
