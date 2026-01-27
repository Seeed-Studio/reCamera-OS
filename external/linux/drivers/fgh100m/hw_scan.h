#ifndef _MORSE_HW_SCAN_H_
#define _MORSE_HW_SCAN_H_

/*
 * Copyright 2024 Morse Micro
 */

#include "morse.h"

/**
 * Parameters for a HW scan.
 */
struct morse_hw_scan_params {
	/** HW which initiated the scan */
	struct ieee80211_hw *hw;
	/** VIF which initiated the scan */
	struct ieee80211_vif *vif;

	/** Has valid scan SSID */
	bool has_directed_ssid;

	/** Dwell time for each channel in scan */
	u32 dwell_time_ms;

	/**
	 * Time to dwell on home channel in between channels during a scan, to allow traffic
	 * to still pass.
	 * If 0, don't return to home in between scan channels
	 */
	u32 dwell_on_home_ms;

	/** True to start scan - False to stop scan */
	bool start;

	/** Emit survey results on scan */
	bool survey;

	/** Store HW scan parameters, for use in a following standby enter */
	bool store;

	/** Filled out probe request */
	struct sk_buff *probe_req;

	/** Number of channels in @ref channels, must not exceed @ref allocated_chans */
	u16 num_chans;

	/** Max allocated channels in @ref channels */
	u16 allocated_chans;

	/** List of channels */
	struct {
		/** The 802.11ah channel */
		const struct morse_dot11ah_channel *channel;
		/** Index into @ref powers_qdbm for the power of this channel */
		u8 power_idx;
	} *channels;

	/** List of possible powers */
	s32 *powers_qdbm;
	/** Number of powers in @ref powers_qdbm */
	u8 n_powers;
	/** Force probe requests to send at 1MHz despite primary channel config */
	bool use_1mhz_probes;
};

/**
 * State enum for HW scan
 */
enum morse_hw_scan_state {
	/** HW scan not running */
	HW_SCAN_STATE_IDLE,
	/** HW scan currently running */
	HW_SCAN_STATE_RUNNING,
	/** HW scan has been aborted, awaiting FW to clean up */
	HW_SCAN_STATE_ABORTING,
};

/**
 * HW scan context structure
 */
struct morse_hw_scan {
	/** Current state of HW scan */
	enum morse_hw_scan_state state;
	/** Completion for syncing cancel_hw_scan and actually finishing the scan */
	struct completion scan_done;
	/** Pointer to last command. */
	struct morse_hw_scan_params *params;
	/** Work to timeout uncompleted scans */
	struct delayed_work timeout;
};

/** forward declare */
struct morse_cmd_hw_scan_req;

/**
 * hw_scan_is_supported - Check if hardware scan in supported and enabled
 *
 * @mors: Global morse struct
 *
 * Return: true on success, false on failure
 */
bool hw_scan_is_supported(struct morse *mors);

/**
 * hw_scan_saved_config_has_ssid - Check that the hardware scan parameters
 *				do not contain a wildcard SSID.
 *
 * @mors: Global morse struct
 *
 * Return: true on success, false on failure
 */
bool hw_scan_saved_config_has_ssid(struct morse *mors);

/**
 * hw_scan_is_idle - Check if the scan done event has been received.
 *
 * @mors: Global morse struct
 *
 * Return: true on success, false on failure
 */
bool hw_scan_is_idle(struct morse *mors);

/**
 * morse_ops_cancel_hw_scan - mac80211 op for .cancel_hw_scan. Cancels a currently running
 * scan, and waits for the FW to send a done
 *
 * @hw: ieee80211_hw to operate on
 * @vif: vif to operate on
 */
void morse_ops_cancel_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif);

/**
 * morse_ops_hw_scan - mac80211 op for .hw_scan. Schedules a HW scan with the firmware.
 *
 * @hw: ieee80211_hw to operate on
 * @vif: vif to operate on
 * @hw_req: parameters for scan request
 * Return: 0 if success, otherwise error code
 */
int morse_ops_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_scan_request *hw_req);

/**
 * morse_hw_scan_insert_tlvs - Insert hw scan command TLVs into @ref buf.
 * Helper function for generating the hw scan command.
 * Params must be initialised before calling this function.
 * @warning This function does not check that the buffer is big enough.
 *
 * @params: hw scan params to generate the TLVs from
 * @buf: buffer to insert TLVs to
 * Return: Pointer to end of the TLVs inserted into the buffer
 */
u8 *morse_hw_scan_insert_tlvs(struct morse_hw_scan_params *params, u8 *buf);

/**
 * morse_hw_scan_dump_scan_cmd - Dump a filled out scan command
 *
 * @mors: Morse structure
 * @cmd: command to dump
 */
void morse_hw_scan_dump_scan_cmd(struct morse *mors, struct morse_cmd_hw_scan_req *cmd);

/**
 * morse_hw_scan_get_command_size - Get the size required for the command which would be generated
 * by the passed in params.
 *
 * @params: params to get the size for.
 * Return: the size of the potentially generated command.
 */
size_t morse_hw_scan_get_command_size(struct morse_hw_scan_params *params);

/**
 * morse_hw_scan_done_event - process a HW scan done event from the firmware
 *
 * @hw: ieee80211_hw the scan was operating on
 */
void morse_hw_scan_done_event(struct ieee80211_hw *hw);

/**
 * morse_hw_scan_init - Initalise the hw scan structure.
 *
 * @mors: morse context
 */
void morse_hw_scan_init(struct morse *mors);

/**
 * morse_hw_scan_destroy - Deinitialise and free the hw scan structure
 *
 * @mors: morse context
 */
void morse_hw_scan_destroy(struct morse *mors);

/**
 * morse_hw_scan_finish - Forcibly complete a hw scan without waiting for
 *                        the firmware to complete gracefully. Typically
 *                        called on driver restart.
 *
 * @mors: morse context
 */
void morse_hw_scan_finish(struct morse *mors);

#endif  /* !_MORSE_HW_SCAN_H_ */
