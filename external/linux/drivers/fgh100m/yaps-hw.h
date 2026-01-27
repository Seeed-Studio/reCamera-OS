/*
 * Copyright 2022 Morse Micro
 *
 */
#ifndef _MORSE_YAPS_HW_H_
#define _MORSE_YAPS_HW_H_

#include <linux/types.h>
#include <linux/crc7.h>

#define MORSE_INT_YAPS_FC_PKT_WAITING_IRQN 0
#define MORSE_INT_YAPS_FC_PACKET_FREED_UP_IRQN 1

struct morse_yaps_hw_table {
	/* Note: no flags actually defined yet, here for future expansion */
	u8 flags;
	u8 padding[3];
	u32 ysl_addr;
	u32 yds_addr;
	u32 status_regs_addr;

	/* Alloc pool sizes */
	u16 tc_tx_pool_size;
	u16 fc_rx_pool_size;
	u8 tc_cmd_pool_size;
	u8 tc_beacon_pool_size;
	u8 tc_mgmt_pool_size;
	u8 fc_resp_pool_size;
	u8 fc_tx_sts_pool_size;
	u8 fc_aux_pool_size;

	/* To chip/from chip queue sizes */
	u8 tc_tx_q_size;
	u8 tc_cmd_q_size;
	u8 tc_beacon_q_size;
	u8 tc_mgmt_q_size;
	u8 fc_q_size;
	u8 fc_done_q_size;

	u16 yaps_reserved_page_size;
	u16 reserved_unused;
} __packed;

struct morse;

int morse_yaps_hw_init(struct morse *mors);
void morse_yaps_hw_yaps_flush_tx_data(struct morse *mors);
void morse_yaps_hw_finish(struct morse *mors);
void morse_yaps_hw_read_table(struct morse *mors, struct morse_yaps_hw_table *tbl_ptr);

#endif /* !_MORSE_YAPS_HW_H_ */
