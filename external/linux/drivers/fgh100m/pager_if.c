/*
 * Copyright 2021-2023 Morse Micro
 *
 */

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>

#include "morse.h"
#include "bus.h"
#include "debug.h"
#include "skbq.h"
#include "hw.h"
#include "pager_if.h"
#include "skb_header.h"

static u32 enabled_irqs;

int morse_pager_irq_enable(const struct morse_pager *pager, bool enable)
{
	if (enable)
		enabled_irqs |= MORSE_PAGER_IRQ_MASK(pager->id);
	else
		enabled_irqs &= ~MORSE_PAGER_IRQ_MASK(pager->id);

	return morse_hw_irq_enable(pager->mors, pager->id, enable);
}

int morse_pager_tx_status_irq_enable(struct morse *mors, bool enable)
{
	if (enable)
		enabled_irqs |= MORSE_PAGER_IRQ_BYPASS_TX_STATUS_AVAILABLE;
	else
		enabled_irqs &= ~MORSE_PAGER_IRQ_BYPASS_TX_STATUS_AVAILABLE;

	return morse_hw_irq_enable(mors, MORSE_PAGER_BYPASS_TX_STATUS_IRQ_NUM, enable);
}

int morse_pager_irq_handler(struct morse *mors, u32 status)
{
	int count;
	int ret;
	struct morse_pager *pager;
	struct morse_chip_if_state *chip_if = mors->chip_if;
	bool rx_pend = false;
	bool tx_buffer_return_pend = false;

	for (count = 0; count < chip_if->pager_count; count++) {
		if (!((status & enabled_irqs) & MORSE_PAGER_IRQ_MASK(count)))
			continue;

		pager = &chip_if->pagers[count];

		if (pager->flags & MORSE_PAGER_FLAGS_POPULATED)
			rx_pend |= true;
		else
			tx_buffer_return_pend |= true;
	}

	if (chip_if->tx_status_addr_location &&
	    ((status & enabled_irqs) & MORSE_PAGER_IRQ_BYPASS_TX_STATUS_AVAILABLE)) {
		u32 page;

		ret = morse_reg32_read(mors, chip_if->tx_status_addr_location, &page);
		if (ret == 0) {
			ret = kfifo_put(&chip_if->tx_status_addrs, page);
			MORSE_WARN_ON(FEATURE_ID_DEFAULT, ret == 0);
			/* Kick RX path as this is where tx statuses are processed */
			rx_pend |= true;
		}
	}

	if (rx_pend || tx_buffer_return_pend) {
		if (rx_pend)
			set_bit(MORSE_RX_PEND, &chip_if->event_flags);

		if (tx_buffer_return_pend)
			set_bit(MORSE_PAGE_RETURN_PEND, &chip_if->event_flags);

		queue_work(mors->chip_wq, &mors->chip_if_work);
	}

	return 0;
}

void morse_pager_show(struct morse *mors, struct morse_pager *pager, struct seq_file *file)
{
	seq_printf(file, "flags:0x%01x\n", pager->flags);
}

int morse_pager_init(struct morse *mors, struct morse_pager *pager, int page_size, u8 flags, u8 id)
{
	pager->mors = mors;
	pager->flags = flags;
	pager->page_size_bytes = page_size;
	pager->parent = NULL;
	pager->id = id;

	return 0;
}

void morse_pager_finish(struct morse_pager *pager)
{
}
