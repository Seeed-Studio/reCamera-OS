/*
 * Copyright 2017-2023 Morse Micro
 *
 */

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>

#include "debug.h"
#include "command.h"
#include "skbq.h"
#include "mac.h"
#include "skb_header.h"
#include "watchdog.h"
#include "ps.h"
#include "raw.h"
#include "twt.h"
#include "cac.h"
#include "operations.h"
#include "vendor_ie.h"
#include "ocs.h"
#include "mbssid.h"
#include "mesh.h"

#define MM_BA_TIMEOUT (5000)
#define MM_MAX_COMMAND_RETRY 2

/*
 * These timeouts (in msecs) must be kept in sync with the same definitions in the driver.
 */
#define MM_CMD_DEFAULT_TIMEOUT_MS 600
#define MM_CMD_POWERSAVE_TIMEOUT_MS 2000
#define MM_CMD_HEALTH_CHECK_TIMEOUT_MS 1000

enum morse_interface_type {
	MORSE_INTERFACE_TYPE_INVALID = 0,
	MORSE_INTERFACE_TYPE_STA = 1,
	MORSE_INTERFACE_TYPE_AP = 2,
	MORSE_INTERFACE_TYPE_MON = 3,
	MORSE_INTERFACE_TYPE_ADHOC = 4,
	MORSE_INTERFACE_TYPE_MESH = 5,

	MORSE_INTERFACE_TYPE_LAST = MORSE_INTERFACE_TYPE_MESH,
	MORSE_INTERFACE_TYPE_MAX = INT_MAX,
};

struct morse_cmd_resp_cb {
	int ret;
	u32 length;
	struct morse_resp *dest_resp;
};

/* Set driver to chip command timeout: max to wait (in ms) before failing the command */
static u32 default_cmd_timeout_ms __read_mostly = MM_CMD_DEFAULT_TIMEOUT_MS;
module_param(default_cmd_timeout_ms, uint, 0644);
MODULE_PARM_DESC(default_cmd_timeout_ms, "Set default command timeout (in ms)");

static void morse_cmd_init(struct morse *mors, struct morse_cmd_header *hdr,
			   enum morse_commands_id cmd, u16 vif_id, u16 len)
{
	if (len < sizeof(*hdr)) {
		MORSE_ERR_RATELIMITED(mors, "Invalid cmd len %d\n", len);
		return;
	}

	hdr->message_id = cpu_to_le16(cmd);
	hdr->len = cpu_to_le16(len - sizeof(*hdr));
	hdr->vif_id = cpu_to_le16(vif_id);
}

/**
 * morse_cmd_standby_exit_reason_to_str() - Convert standby exit reason to string
 *
 * @reason: Reason for failure
 *
 * return string representing failure
 */
static const char *morse_cmd_standby_exit_reason_to_str(enum morse_standby_mode_exit_reason reason)
{
	switch (reason) {
	case STANDBY_MODE_EXIT_REASON_NONE:
		return "none";
	case STANDBY_MODE_EXIT_REASON_WAKEUP_FRAME:
		return "wake-up frame";
	case STANDBY_MODE_EXIT_REASON_ASSOCIATE:
		return "associate";
	case STANDBY_MODE_EXIT_REASON_EXT_INPUT:
		return "external input";
	case STANDBY_MODE_EXIT_REASON_WHITELIST_PKT:
		return "whitelist pkt";
	case STANDBY_MODE_EXIT_REASON_TCP_CONNECTION_LOST:
		return "tcp connection lost";
	case STANDBY_MODE_EXIT_REASON_HW_SCAN_NOT_ENABLED:
		return "hw scan not enabled";
	case STANDBY_MODE_EXIT_REASON_HW_SCAN_FAILED_TO_START:
		return "hw scan failed to start";
	default:
		return "unknown";
	}
}

static int morse_cmd_tx(struct morse *mors, struct morse_resp *resp,
			struct morse_cmd *cmd, u32 length, u32 timeout, const char *func)
{
	int cmd_len;
	int ret = 0;
	u16 host_id;
	int retry = 0;
	unsigned long wait_ret = 0;
	struct sk_buff *skb;
	struct morse_skbq *cmd_q = mors->cfg->ops->skbq_cmd_tc_q(mors);
	struct morse_cmd_resp_cb *resp_cb;
	DECLARE_COMPLETION_ONSTACK(cmd_comp);

	BUILD_BUG_ON(sizeof(struct morse_cmd_resp_cb) > IEEE80211_TX_INFO_DRIVER_DATA_SIZE);

	if (!cmd_q)
		/* No control pageset, not supported by FW */
		return -ENODEV;

	cmd_len = sizeof(*cmd) + le16_to_cpu(cmd->hdr.len);
	cmd->hdr.flags = cpu_to_le16(MORSE_CMD_REQ);

	mutex_lock(&mors->cmd_wait);
	mors->cmd_seq++;
	if (mors->cmd_seq > MORSE_CMD_HOST_ID_SEQ_MAX)
		mors->cmd_seq = 1;
	host_id = mors->cmd_seq << MORSE_CMD_HOST_ID_SEQ_SHIFT;

	/* Make sure no one enables PS until the command is responded to or timed out */
	morse_ps_disable(mors);

	do {
		cmd->hdr.host_id = cpu_to_le16(host_id | retry);

		skb = morse_skbq_alloc_skb(cmd_q, cmd_len);
		if (!skb) {
			ret = -ENOMEM;
			break;
		}

		memcpy(skb->data, cmd, cmd_len);
		resp_cb = (struct morse_cmd_resp_cb *)IEEE80211_SKB_CB(skb)->driver_data;
		resp_cb->length = length;
		resp_cb->dest_resp = resp;

		MORSE_DBG(mors, "CMD 0x%04x:%04x\n", le16_to_cpu(cmd->hdr.message_id),
			  le16_to_cpu(cmd->hdr.host_id));

		mutex_lock(&mors->cmd_lock);
		mors->cmd_comp = &cmd_comp;
		if (retry > 0)
			reinit_completion(&cmd_comp);
		timeout = timeout ? timeout : default_cmd_timeout_ms;
		ret = morse_skbq_skb_tx(cmd_q, &skb, NULL, MORSE_SKB_CHAN_COMMAND);
		mutex_unlock(&mors->cmd_lock);

		if (ret) {
			MORSE_ERR(mors, "morse_skbq_tx fail: %d\n", ret);
			break;
		}

		wait_ret = wait_for_completion_timeout(&cmd_comp, msecs_to_jiffies(timeout));
		mutex_lock(&mors->cmd_lock);
		mors->cmd_comp = NULL;

		if (!wait_ret) {
			MORSE_INFO(mors, "Try:%d Command %04x:%04x timeout after %u ms\n",
				   retry, le16_to_cpu(cmd->hdr.message_id),
				   le16_to_cpu(cmd->hdr.host_id), timeout);
			ret = -ETIMEDOUT;
		} else {
			ret = (length && resp) ? resp->status : resp_cb->ret;

			MORSE_DBG(mors, "Command 0x%04x:%04x status 0x%08x\n",
				  le16_to_cpu(cmd->hdr.message_id),
				  le16_to_cpu(cmd->hdr.host_id), ret);
			if (ret)
				MORSE_ERR(mors, "Command 0x%04x:%04x error %d\n",
					  le16_to_cpu(cmd->hdr.message_id),
					  le16_to_cpu(cmd->hdr.host_id), ret);
		}
		/* Free the command request */
		spin_lock_bh(&cmd_q->lock);
		morse_skbq_skb_finish(cmd_q, skb, NULL);
		spin_unlock_bh(&cmd_q->lock);
		mutex_unlock(&mors->cmd_lock);

		retry++;
	} while ((ret == -ETIMEDOUT) && retry < MM_MAX_COMMAND_RETRY);

	morse_ps_enable(mors);
	mutex_unlock(&mors->cmd_wait);

	if (ret == -ETIMEDOUT)
		MORSE_ERR(mors, "Command %s %02x:%02x timed out\n",
			  func, le16_to_cpu(cmd->hdr.message_id), le16_to_cpu(cmd->hdr.host_id));
	else if (ret != 0)
		MORSE_ERR(mors, "Command %s %02x:%02x failed with rc %d (0x%x)\n",
			  func, le16_to_cpu(cmd->hdr.message_id),
			  le16_to_cpu(cmd->hdr.host_id), ret, ret);

	return ret;
}

static int morse_cmd_ocs_req(struct morse_vif *mors_vif, struct morse_resp_ocs *resp,
			     struct morse_drv_cmd_ocs *drv_cmd)
{
	int ret;
	struct morse_cmd_ocs cmd;
	struct morse *mors = morse_vif_to_morse(mors_vif);

	/* Prepare request */
	cmd.cmd = *drv_cmd;
	morse_cmd_init(mors, &cmd.cmd.hdr, MORSE_COMMAND_OCS, 0, sizeof(cmd));

	cmd.aid = cpu_to_le16(MORSE_OCS_AID);
	cmd.type = ocs_type;

	ret = morse_cmd_tx(mors, (struct morse_resp *)resp,
			   (struct morse_cmd *)&cmd, sizeof(*resp), 0, __func__);
	if (ret)
		return ret;

	return morse_ocs_cmd_post_process(mors_vif, resp, &cmd);
}

/**
 * morse_cmd_send_wake_action_frame() - Execute command to send wake action frame
 *
 * @mors	Morse chip struct
 * @cmd		Command from morsectrl
 */
static int morse_cmd_send_wake_action_frame(struct morse *mors, struct morse_cmd *cmd)
{
	struct morse_cmd_send_wake_action_frame *cmd_action =
	    (struct morse_cmd_send_wake_action_frame *)cmd;
	return morse_mac_send_vendor_wake_action_frame(mors, cmd_action->dest_addr,
						       cmd_action->payload,
						       cmd_action->payload_size);
}

/**
 * morse_cmd_coredump() - Schedule the restart work, coredump, and wait for
 *                        chip reload.
 *
 * @mors	Morse chip struct
 */
static int morse_cmd_coredump(struct morse *mors)
{
	int ret;
	unsigned long rem;
	/* A core dump typically takes ~30s, applying a x2 buffer for completion */
	const int timeout_ms = 60000;
	DECLARE_COMPLETION_ONSTACK(user_coredump_comp);

	ret = morse_coredump_new(mors, MORSE_COREDUMP_REASON_USER_REQUEST);
	if (ret)
		return ret;

	mors->user_coredump_comp = &user_coredump_comp;
	set_bit(MORSE_STATE_FLAG_DO_COREDUMP, &mors->state_flags);
	schedule_work(&mors->driver_restart);

	mutex_unlock(&mors->lock);
	rem = wait_for_completion_timeout(&user_coredump_comp, msecs_to_jiffies(timeout_ms));
	mutex_lock(&mors->lock);

	mors->user_coredump_comp = NULL;

	/* A value of zero indicates that wait_for_completion_timeout timed out
	 * waiting for completion
	 */
	return rem ? 0 : -1;
}

static void morse_cmd_cac_cfg_get(struct morse *mors, struct morse_vif *mors_vif,
				  struct morse_cmd_cac_cfm *cfm)
{
	struct cac_threshold_change_rules rules;
	int i;

	BUILD_BUG_ON(ARRAY_SIZE(cfm->rule) < ARRAY_SIZE(rules.rule));

	morse_cac_get_rules(mors_vif, &rules, &cfm->rule_tot);
	cfm->rule_tot = rules.rule_tot;

	memset(&cfm->rule, 0, sizeof(cfm->rule));

	for (i = 0; i < cfm->rule_tot && i < ARRAY_SIZE(cfm->rule); i++) {
		struct cac_threshold_change_rule *rule = &rules.rule[i];
		struct cac_cmd_change_rule *cfm_rule = &cfm->rule[i];

		cfm_rule->arfs = rule->arfs;
		cfm_rule->threshold_change = cac_threshold_val2pc(rule->threshold_change);
	}
}

static int morse_cmd_cac_cfg_set(struct morse *mors, struct morse_vif *mors_vif,
				 struct morse_cmd_cac_req *req)
{
	struct cac_threshold_change_rules rules;
	int i;

	if (req->rule_tot > ARRAY_SIZE(rules.rule))
		return -E2BIG;

	if (req->rule_tot == 0)
		return -EINVAL;

	memset(&rules, 0, sizeof(rules));

	rules.rule_tot = req->rule_tot;

	for (i = 0; i < req->rule_tot; i++) {
		struct cac_threshold_change_rule *rule = &rules.rule[i];

		rule->arfs = req->rule[i].arfs;
		rule->threshold_change = cac_threshold_pc2val(req->rule[i].threshold_change);
	}

	morse_cac_set_rules(mors_vif, &rules);

	return 0;
}

static int morse_process_cac_cmd(struct morse *mors, struct morse_vif *mors_vif,
	struct morse_cmd *cmd, struct morse_resp *resp)
{
	struct morse_cmd_cac_req *req = (struct morse_cmd_cac_req *)cmd;
	struct morse_cmd_cac_cfm *cfm = (struct morse_cmd_cac_cfm *)resp;

	resp->hdr.len = 4;

	switch (req->cmd) {
	case CAC_COMMAND_DISABLE:
		return morse_cac_deinit(mors_vif);
	case CAC_COMMAND_ENABLE:
		return morse_cac_init(mors, mors_vif);
	case CAC_COMMAND_CFG_GET:
		resp->hdr.len = sizeof(*cfm) - sizeof(cfm->hdr);
		morse_cmd_cac_cfg_get(mors, mors_vif, cfm);
		return 0;
	case CAC_COMMAND_CFG_SET:
		return morse_cmd_cac_cfg_set(mors, mors_vif, req);
	default:
		return -EINVAL;
	}
}

static int morse_cmd_drv(struct morse *mors, struct ieee80211_vif *vif,
			 struct morse_resp *resp, struct morse_cmd *cmd, u32 length, u32 timeout)
{
	int ret;
	struct morse_vif *mors_vif = ieee80211_vif_to_morse_vif(vif);

	switch (cmd->hdr.message_id) {
	case MORSE_COMMAND_SET_STA_TYPE:
		if (mors_vif) {
			mors->custom_configs.sta_type = cmd->data[0];
			ret = 0;
			resp->hdr.len = 4;
			resp->status = ret;
		} else {
			ret = -EFAULT;
		}
		break;
	case MORSE_COMMAND_SET_ENC_MODE:
		if (mors_vif) {
			mors->custom_configs.enc_mode = cmd->data[0];
			ret = 0;
			resp->hdr.len = 4;
			resp->status = ret;
		} else {
			ret = -EFAULT;
		}
		break;
	case MORSE_COMMAND_SET_LISTEN_INTERVAL:
		if (mors_vif) {
			struct morse_cmd_set_listen_interval *cmd_li =
			    (struct morse_cmd_set_listen_interval *)cmd;

			mors->custom_configs.listen_interval = le16_to_cpu(cmd_li->listen_interval);

			mors->custom_configs.listen_interval_ovr = true;

			MORSE_DBG(mors, "Listen Interval %d\n",
				  mors->custom_configs.listen_interval);

			ret = 0;
			resp->hdr.len = 4;
			resp->status = ret;
		} else {
			ret = -EFAULT;
		}
		break;
	case MORSE_COMMAND_SET_AMPDU:
		mors->custom_configs.enable_ampdu = (cmd->data[0] == 0) ? false : true;
		ret = 0;

		resp->hdr.len = 4;
		resp->status = ret;
		break;
	case MORSE_COMMAND_CONFIG_RAW:
		if (mors_vif) {
			struct morse_cmd_raw_cfg *cmd_raw = (struct morse_cmd_raw_cfg *)cmd;

			ret = morse_raw_process_cmd(mors_vif, cmd_raw);

			resp->hdr.len = 4;
			resp->status = ret;
		} else {
			ret = -EFAULT;
		}
		break;
	case MORSE_COMMAND_TEST_BA:
		if (mors_vif) {
			struct ieee80211_sta *sta;
			struct morse_cmd_test_ba *test_ba = (struct morse_cmd_test_ba *)cmd;

			ret = -EINVAL;
			if (test_ba->tx) {
				/* Must be held while finding and dereferencing sta */
				rcu_read_lock();
				sta = ieee80211_find_sta(vif, test_ba->addr);
				if (!sta) {
					rcu_read_unlock();
					break;
				}

				if (test_ba->start)
					ret = ieee80211_start_tx_ba_session(sta,
									    test_ba->tid,
									    MM_BA_TIMEOUT);
				else
					ret = ieee80211_stop_tx_ba_session(sta, test_ba->tid);
				rcu_read_unlock();
			} else {
				if (test_ba->start)
					break;
				ieee80211_stop_rx_ba_session(vif, test_ba->tid, test_ba->addr);
				ret = 0;
			}
		} else {
			ret = -EFAULT;
		}
		break;
	case MORSE_COMMAND_COREDUMP:
		ret = morse_cmd_coredump(mors);
		resp->hdr.len = 4;
		resp->status = ret;
		break;
	case MORSE_COMMAND_SET_S1G_OP_CLASS:
		mors->custom_configs.channel_info.s1g_operating_class = cmd->data[0];
		mors->custom_configs.channel_info.pri_global_operating_class = cmd->data[1];
		ret = 0;

		resp->hdr.len = 4;
		resp->status = ret;
		break;
	case MORSE_COMMAND_SEND_WAKE_ACTION_FRAME:
		ret = morse_cmd_send_wake_action_frame(mors, cmd);
		resp->hdr.len = 4;
		resp->status = ret;
		break;
	case MORSE_COMMAND_VENDOR_IE_CONFIG: {
		ret = morse_vendor_ie_handle_config_cmd(mors_vif,
					      (struct morse_cmd_vendor_ie_config *)cmd);
		resp->hdr.len = 4;
		resp->status = ret;
		break;
	}
	case MORSE_COMMAND_DRIVER_SET_DUTY_CYCLE: {
		struct morse_cmd_set_duty_cycle_req *req =
		    (struct morse_cmd_set_duty_cycle_req *)cmd;
		mors->custom_configs.duty_cycle = req->duty_cycle;
		/*
		 * When a disable duty cycle command is executed via morsectrl it sends a
		 * duty cycle value of 100%. When this happens set the duty cycle value in
		 * custom config as 0. This enables the driver to use the duty cycle value
		 * mentioned in the regdom.
		 */
		if (req->duty_cycle == 10000)
			mors->custom_configs.duty_cycle = 0;
		mors->duty_cycle = req->duty_cycle;

		cmd->hdr.message_id = MORSE_COMMAND_SET_DUTY_CYCLE;
		ret = morse_cmd_tx(mors, resp, cmd, resp->hdr.len, 0, __func__);
		resp->hdr.len = 4;
		resp->status = ret;
		break;
	}
	case MORSE_COMMAND_TWT_SET_CONF:
		ret = morse_process_twt_cmd(mors, mors_vif, cmd);
		resp->hdr.len = 4;
		resp->status = ret;
		break;
	case MORSE_COMMAND_CAC:
		ret = morse_process_cac_cmd(mors, mors_vif, cmd, resp);
		resp->status = ret;
		break;
	case MORSE_COMMAND_GET_AVAILABLE_CHANNELS:
		ret = morse_cmd_get_available_channels(mors, resp);
		resp->status = ret;
		break;
	case MORSE_COMMAND_SET_ECSA_S1G_INFO:
		if (mors_vif) {
			struct morse_cmd_ecsa *cmd_ecsa = (struct morse_cmd_ecsa *)cmd;

			MORSE_INFO(mors, "ECSA channel info   :\n"
				   " * s1g_global_operating_class    : %d\n"
				   " * s1g_primary_bandwidth         : %d\n"
				   " * s1g_operating_frequency       : %u\n"
				   " * s1g_operating_bandwidth       : %d\n"
				   " * s1g_primary_1MHz_chan_idx     : %d\n"
				   " * primary_global_op_class       : %d\n"
				   " * s1g_cap0                      : %d\n",
				   cmd_ecsa->op_class,
				   cmd_ecsa->prim_bw,
				   cmd_ecsa->op_chan_freq_hz,
				   cmd_ecsa->op_bw_mhz,
				   cmd_ecsa->prim_chan_1mhz_idx, cmd_ecsa->prim_opclass,
				   cmd_ecsa->s1g_cap0);
			mors_vif->ecsa_channel_info.op_chan_freq_hz = cmd_ecsa->op_chan_freq_hz;
			mors_vif->ecsa_channel_info.op_bw_mhz = cmd_ecsa->op_bw_mhz;
			mors_vif->ecsa_channel_info.pri_1mhz_chan_idx =
								cmd_ecsa->prim_chan_1mhz_idx;
			mors_vif->ecsa_channel_info.pri_bw_mhz = cmd_ecsa->prim_bw;
			mors_vif->ecsa_channel_info.s1g_operating_class = cmd_ecsa->op_class;
			mors_vif->ecsa_channel_info.pri_global_operating_class =
			    cmd_ecsa->prim_opclass;
			mors_vif->ecsa_channel_info.s1g_cap0 = cmd_ecsa->s1g_cap0;
			mors_vif->ecsa_channel_info.s1g_cap1 = cmd_ecsa->s1g_cap1;
			mors_vif->ecsa_channel_info.s1g_cap2 = cmd_ecsa->s1g_cap2;
			mors_vif->ecsa_channel_info.s1g_cap3 = cmd_ecsa->s1g_cap3;
			mors_vif->mask_ecsa_info_in_beacon = false;
			ret = 0;
		} else {
			ret = -EFAULT;
		}
		resp->hdr.len = 4;
		resp->status = ret;
		break;
	case MORSE_COMMAND_GET_HW_VERSION:
		ret = morse_cmd_get_hw_version(mors, resp);
		resp->status = ret;
		break;
	case MORSE_COMMAND_MBSSID_INFO:
		ret = morse_command_process_bssid_info(mors_vif, (struct morse_cmd_mbssid *)cmd);
		resp->hdr.len = 4;
		resp->status = ret;
		break;
	case MORSE_COMMAND_OCS_REQ:
		ret = morse_cmd_ocs_req(mors_vif,
					(struct morse_resp_ocs *)resp,
					(struct morse_drv_cmd_ocs *)cmd);
		resp->status = ret;
		break;
	case MORSE_COMMAND_SET_MESH_CONFIG:
		ret = morse_cmd_set_mesh_config(mors_vif, (struct morse_cmd_mesh_config *)cmd,
						NULL);
		resp->hdr.len = 4;
		resp->status = ret;
		break;
	case MORSE_COMMAND_MBCA_SET_CONF:
		ret = morse_cmd_process_mbca_conf(mors_vif, (struct morse_cmd_mbca *)cmd);
		resp->hdr.len = 4;
		resp->status = ret;
		break;
	case MORSE_COMMAND_DYNAMIC_PEERING_SET_CONF:
		ret = morse_cmd_process_dynamic_peering_conf(mors_vif,
					     (struct morse_cmd_dynamic_peering *)cmd);
		resp->hdr.len = 4;
		resp->status = ret;
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

int morse_cmd_resp_process(struct morse *mors, struct sk_buff *skb)
{
	int length, ret = -ESRCH;	/* No such process */
	struct morse_skbq *cmd_q = mors->cfg->ops->skbq_cmd_tc_q(mors);
	struct morse_resp *src_resp = (struct morse_resp *)(skb->data);
	struct sk_buff *cmd_skb = NULL;
	struct morse_cmd_resp_cb *resp_cb;
	struct morse_resp *dest_resp;
	struct morse_cmd *cmd;
	u16 message_id = 0;
	u16 host_id = 0;
	u16 resp_message_id = le16_to_cpu(src_resp->hdr.message_id);
	u16 resp_host_id = le16_to_cpu(src_resp->hdr.host_id);
	bool is_late_response = false;

	MORSE_DBG(mors, "EVT 0x%04x:0x%04x\n", resp_message_id, resp_host_id);

	if (!MORSE_CMD_IS_CFM(src_resp)) {
		ret = morse_mac_event_recv(mors, skb);
		goto exit_free;
	}

	mutex_lock(&mors->cmd_lock);

	cmd_skb = morse_skbq_tx_pending(cmd_q);
	if (cmd_skb) {
		morse_skb_remove_hdr_after_sent_to_chip(cmd_skb);
		cmd = (struct morse_cmd *)cmd_skb->data;
		message_id = le16_to_cpu(cmd->hdr.message_id);
		host_id = le16_to_cpu(cmd->hdr.host_id);
	}

	/*
	 * If there is no pending command or the sequence ID does not match, this is a late response
	 * for a timed out command which has been cleaned up, so just free up the response.
	 * If a command was retried, the response may be from the retry or from the original
	 * command (late response) but not from both because the firmware will silently drop
	 * a retry if it received the initial request. So a mismatched retry counter is treated
	 * as a matched command and response.
	 */
	if (!cmd_skb || message_id != resp_message_id ||
	    (host_id & MORSE_CMD_HOST_ID_SEQ_MASK) != (resp_host_id & MORSE_CMD_HOST_ID_SEQ_MASK)) {
		MORSE_ERR(mors,
			  "Late response for timed out cmd 0x%04x:%04x have 0x%04x:%04x 0x%04x\n",
			  resp_message_id, resp_host_id, message_id, host_id, mors->cmd_seq);
		is_late_response = true;
		goto exit;
	}
	if ((host_id & MORSE_CMD_HOST_ID_RETRY_MASK) !=
	    (resp_host_id & MORSE_CMD_HOST_ID_RETRY_MASK))
		MORSE_INFO(mors, "Command retry mismatch 0x%04x:%04x 0x%04x:%04x\n",
			   message_id, host_id, resp_message_id, resp_host_id);

	resp_cb = (struct morse_cmd_resp_cb *)IEEE80211_SKB_CB(cmd_skb)->driver_data;
	length = resp_cb->length;
	dest_resp = resp_cb->dest_resp;
	if (length >= sizeof(struct morse_resp) && dest_resp) {
		ret = 0;
		length = min_t(int, length, le16_to_cpu(src_resp->hdr.len) +
			       sizeof(struct morse_cmd_header));
		memcpy(dest_resp, src_resp, length);
	} else {
		ret = le32_to_cpu(src_resp->status);
	}

	resp_cb->ret = ret;

exit:
	if (cmd_skb && !is_late_response) {
		/* Complete if not already timed out */
		if (mors->cmd_comp)
			complete(mors->cmd_comp);
	}

	mutex_unlock(&mors->cmd_lock);
exit_free:
	dev_kfree_skb(skb);

	return 0;
}

int morse_cmd_set_channel(struct morse *mors, u32 op_chan_freq_hz,
			  u8 pri_1mhz_chan_idx, u8 op_bw_mhz, u8 pri_bw_mhz, s32 *power_mbm)
{
	int ret;
	struct morse_cmd_set_channel cmd;
	struct morse_resp_set_channel resp;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_SET_CHANNEL, 0, sizeof(cmd));

	/* May be 0xFFFF/0xFFFFFFFF to indicate no change */
	cmd.op_chan_freq_hz = cpu_to_le32(op_chan_freq_hz);
	cmd.op_bw_mhz = op_bw_mhz;
	cmd.pri_bw_mhz = pri_bw_mhz;
	cmd.pri_1mhz_chan_idx = pri_1mhz_chan_idx;
	/* TODO: add other modes as necessary */
	cmd.dot11_mode = DOT11AH_MODE;

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);
	if (!ret)
		*power_mbm = QDBM_TO_MBM(le32_to_cpu(resp.power_qdbm));

	return ret;
}

int morse_cmd_get_current_channel(struct morse *mors, u32 *op_chan_freq_hz,
				  u8 *pri_1mhz_chan_idx, u8 *op_bw_mhz, u8 *pri_bw_mhz)
{
	struct morse_cmd_get_current_channel_req req;
	struct morse_cmd_get_current_channel_cfm cfm;
	int ret;

	morse_cmd_init(mors, &req.hdr, MORSE_COMMAND_GET_CURRENT_CHANNEL, 0, sizeof(req));

	ret = morse_cmd_tx(mors, (struct morse_resp *)&cfm, (struct morse_cmd *)&req,
			   sizeof(cfm), 0, __func__);
	if (ret)
		return ret;

	*op_chan_freq_hz = le32_to_cpu(cfm.operating_channel_freq_hz);
	*pri_1mhz_chan_idx = cfm.primary_1mhz_channel_index;
	*op_bw_mhz = cfm.operating_channel_bw_mhz;
	*pri_bw_mhz = cfm.primary_channel_bw_mhz;

	return 0;
}

int morse_cmd_set_txpower(struct morse *mors, s32 *out_power_mbm, int txpower_mbm)
{
	int ret;
	struct morse_cmd_set_txpower cmd;
	struct morse_resp_set_txpower resp;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_SET_TXPOWER, 0, sizeof(cmd));

	cmd.power_qdbm = cpu_to_le32(MBM_TO_QDBM(txpower_mbm));

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);
	if (ret == 0)
		*out_power_mbm = QDBM_TO_MBM(le32_to_cpu(resp.power_qdbm));

	return ret;
}

int morse_cmd_get_max_txpower(struct morse *mors, s32 *out_power_mbm)
{
	int ret;
	struct morse_cmd_get_max_txpower cmd;
	struct morse_resp_get_max_txpower resp;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_GET_MAX_TXPOWER, 0, sizeof(cmd));

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);
	if (ret == 0)
		*out_power_mbm = QDBM_TO_MBM(le32_to_cpu(resp.power_qdbm));

	return ret;
}

int morse_cmd_set_ps(struct morse *mors, bool enabled, bool enable_dynamic_ps_offload)
{
	int ret;
	struct morse_cmd_set_ps cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_SET_PS, 0, sizeof(cmd));

	cmd.enabled = (u8)enabled;
	cmd.dynamic_ps_offload = (u8)enable_dynamic_ps_offload;

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0,
			   default_cmd_timeout_ms > MM_CMD_POWERSAVE_TIMEOUT_MS ?
			   default_cmd_timeout_ms : MM_CMD_POWERSAVE_TIMEOUT_MS, __func__);

	return ret;
}

int morse_cmd_config_beacon_timer(struct morse *mors, struct morse_vif *morse_vif, bool enabled)
{
	int ret;
	struct morse_cmd_config_bss_beacon cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_BSS_BEACON_CONFIG, morse_vif->id, sizeof(cmd));
	cmd.enable_beaconing = enabled;

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_store_pv1_hc_data(struct morse *mors, struct morse_vif *mors_vif,
				struct ieee80211_sta *sta, u8 *a3, u8 *a4, bool is_store_in_rx)
{
	int ret;
	struct morse_cmd_pv1_hc_data cmd;
	struct morse_resp_pv1_hc_data resp;
	struct morse_sta *mors_sta = (struct morse_sta *)sta->drv_priv;

	memset(&cmd, 0, sizeof(cmd));
	if (a3 || a4)
		cmd.opcode = MORSE_PV1_STORE_A3_A4;

	if (a3) {
		cmd.pv1_hc_store |= MORSE_PV1_CMD_STORE_A3;
		memcpy(cmd.a3, a3, sizeof(cmd.a3));
	}

	if (a4) {
		cmd.pv1_hc_store |= MORSE_PV1_CMD_STORE_A4;
		memcpy(cmd.a4, a4, sizeof(cmd.a4));
	}

	if (is_store_in_rx)
		cmd.pv1_hc_store |= MORSE_PV1_CMD_STORE_RX;

	memcpy(cmd.sta_addr, mors_sta->addr, sizeof(cmd.sta_addr));

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_PV1_HC_INFO_UPDATE, mors_vif->id, sizeof(cmd));
	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);

	return ret;
}

int morse_cmd_add_if(struct morse *mors, u16 *vif_id, const u8 *addr, enum nl80211_iftype type)
{
	int ret;
	struct morse_cmd_add_if cmd;
	struct morse_resp_add_if resp;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_ADD_INTERFACE, 0, sizeof(cmd));

	switch (type) {
	case NL80211_IFTYPE_STATION:
		cmd.type = cpu_to_le32(MORSE_INTERFACE_TYPE_STA);
		break;
	case NL80211_IFTYPE_ADHOC:
		cmd.type = cpu_to_le32(MORSE_INTERFACE_TYPE_ADHOC);
		break;
	case NL80211_IFTYPE_AP:
		cmd.type = cpu_to_le32(MORSE_INTERFACE_TYPE_AP);
		break;
	case NL80211_IFTYPE_MONITOR:
		cmd.type = cpu_to_le32(MORSE_INTERFACE_TYPE_MON);
		break;
	case NL80211_IFTYPE_MESH_POINT:
		cmd.type = cpu_to_le32(MORSE_INTERFACE_TYPE_MESH);
		break;
	default:
		return -EOPNOTSUPP;
	}

	memcpy(cmd.addr, addr, sizeof(cmd.addr));

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);
	if (ret == 0)
		*vif_id = le16_to_cpu(resp.hdr.vif_id);

	return ret;
}

int morse_cmd_rm_if(struct morse *mors, u16 vif_id)
{
	int ret;
	struct morse_cmd_rm_if cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_REMOVE_INTERFACE, vif_id, sizeof(cmd));

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_cfg_bss(struct morse *mors, u16 vif_id, u16 beacon_int, u16 dtim_period, u32 cssid)
{
	int ret;
	struct morse_cmd_cfg_bss cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_BSS_CONFIG, vif_id, sizeof(cmd));

	cmd.beacon_int = cpu_to_le16(beacon_int);
	cmd.cssid = cpu_to_le32(cssid);
	cmd.dtim_period = cpu_to_le16(dtim_period);

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_sta_state(struct morse *mors, struct morse_vif *mors_vif,
			u16 aid, struct ieee80211_sta *sta, enum ieee80211_sta_state state)
{
	int ret;
	struct morse_cmd_sta_state cmd;
	struct morse_resp_sta_state resp;
	struct morse_sta *mors_sta = (struct morse_sta *)sta->drv_priv;

	memset(&cmd, 0, sizeof(cmd));
	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_SET_STA_STATE, mors_vif->id, sizeof(cmd));

	memcpy(cmd.addr, sta->addr, sizeof(cmd.addr));
	cmd.aid = cpu_to_le16(aid);
	cmd.state = cpu_to_le16(state);
	cmd.uapsd_queues = sta->uapsd_queues;
	if (mors_vif->enable_pv1 && mors_sta->pv1_frame_support)
		cmd.flags = MORSE_STA_FLAG_S1G_PV1;

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);

	return ret;
}

int morse_cmd_disable_key(struct morse *mors, struct morse_vif *mors_vif,
			  u16 aid, struct ieee80211_key_conf *key)
{
	int ret;
	struct morse_cmd_disable_key cmd;

	MORSE_DBG(mors, "%s Disabling key for vif (%d):\n"
		  "\tkey->hw_key_idx: %d\n"
		  "\taid (optional): %d\n", __func__, mors_vif->id, key->hw_key_idx, aid);

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_DISABLE_KEY, mors_vif->id, sizeof(cmd));

	cmd.aid = cpu_to_le16(aid);
	cmd.key_idx = key->hw_key_idx;
	cmd.key_type = (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) ?
	    MORSE_KEY_TYPE_PTK : MORSE_KEY_TYPE_GTK;

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_install_key(struct morse *mors, struct morse_vif *mors_vif,
			  u16 aid, struct ieee80211_key_conf *key, enum morse_key_cipher cipher,
			  enum morse_aes_key_length length)
{
	int ret;
	struct morse_cmd_install_key cmd;
	struct morse_resp_install_key resp;

	MORSE_DBG(mors, "%s Installing key for vif (%d):\n"
		  "\tkey->idx: %d\n"
		  "\tkey->cipher: 0x%08x\n"
		  "\tkey->pn: %lld\n"
		  "\tkey->len: %d\n"
		  "\tkey->flags: 0x%08x\n"
		  "\taid (optional): %d\n",
		  __func__,
		  mors_vif->id,
		  key->keyidx,
		  key->cipher, (u64)atomic64_read(&key->tx_pn), key->keylen, key->flags, aid);

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_INSTALL_KEY, mors_vif->id, sizeof(cmd));

	cmd.pn = cpu_to_le64(atomic64_read(&key->tx_pn));
	cmd.aid = cpu_to_le32(aid);
	cmd.cipher = cipher;
	cmd.key_length = length;
	cmd.key_type = (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) ?
	    MORSE_KEY_TYPE_PTK : MORSE_KEY_TYPE_GTK;

	cmd.key_idx = key->keyidx;
	memcpy(&cmd.key[0], &key->key[0], sizeof(cmd.key));

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);

	if (ret == 0) {
		key->hw_key_idx = resp.key_idx;
		MORSE_DBG(mors, "%s Installed key @ hw index: %d\n", __func__, resp.key_idx);
	}

	return ret;
}

int morse_cmd_get_version(struct morse *mors)
{
	int ret;
	struct morse_cmd_get_version cmd;
	struct morse_resp_get_version *resp = NULL;

	memset(&mors->sw_ver, 0, sizeof(mors->sw_ver));

	/* we have to kmalloc otherwise we are bigger than stack allows */
	resp = kmalloc(sizeof(*resp), GFP_KERNEL);
	if (!resp)
		return -ENOSPC;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_GET_VERSION, 0, sizeof(cmd));

	ret = morse_cmd_tx(mors, (struct morse_resp *)resp,
			   (struct morse_cmd *)&cmd, sizeof(*resp), 0, __func__);
	if (ret == 0) {
		int major;
		int minor;
		int patch;

		resp->version[resp->length] = '\0';
		MORSE_INFO(mors,
			   "Morse Driver Version: %s, Morse FW Version: %s\n",
			   DRV_VERSION, resp->version);

		if (sscanf(resp->version, "rel_%d_%d_%d", &major, &minor, &patch) == 3) {
			MORSE_WARN_ON_ONCE(FEATURE_ID_DEFAULT, major > __UINT8_MAX__);
			MORSE_WARN_ON_ONCE(FEATURE_ID_DEFAULT, minor > __UINT8_MAX__);
			MORSE_WARN_ON_ONCE(FEATURE_ID_DEFAULT, patch > __UINT8_MAX__);
			mors->sw_ver.major = major;
			mors->sw_ver.minor = minor;
			mors->sw_ver.patch = patch;
		}

		/* Keep the firmware version string for coredump creation */
		morse_coredump_set_fw_version_str(mors, resp->version);
	}

	kfree(resp);

	return ret;
}

int morse_cmd_get_disabled_channels(struct morse *mors,
				    struct morse_resp_get_disabled_channels *resp,
				    uint resp_len)
{
	struct morse_cmd cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_GET_DISABLED_CHANNELS, 0, sizeof(cmd));

	return morse_cmd_tx(mors, (struct morse_resp *)resp, &cmd, resp_len, 0, __func__);
}

int morse_cmd_cfg_scan(struct morse *mors, bool enabled)
{
	int ret;
	struct morse_cmd_cfg_scan cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_CFG_SCAN, 0, sizeof(cmd));

	cmd.enabled = enabled;

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_get_channel_usage(struct morse *mors, struct morse_survey_rx_usage_record *record)
{
	int ret;
	struct morse_cmd_get_channel_usage cmd;
	struct morse_resp_get_channel_usage resp;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_GET_CHANNEL_USAGE_RECORD, 0, sizeof(cmd));

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);
	if (ret == 0) {
		record->time_listen = le64_to_cpu(resp.time_listen);
		record->time_rx = le64_to_cpu(resp.busy_time);
		record->freq_hz = le32_to_cpu(resp.freq_hz);
		record->bw_mhz = le32_to_cpu(resp.bw_mhz);
		record->noise = resp.noise;
	}

	return ret;
}

static void morse_set_dtim_cts_to_self(bool enable, struct morse_vif *mors_vif)
{
	if (enable)
		MORSE_OPS_SET(&mors_vif->operations, DTIM_CTS_TO_SELF);
	else
		MORSE_OPS_CLEAR(&mors_vif->operations, DTIM_CTS_TO_SELF);
}

int morse_cmd_update_beacon_vendor_ie_oui_filter(struct morse *mors, struct morse_vif *mors_vif)
{
	struct morse_config_oui_filter_req cmd;
	struct vendor_ie_oui_filter_list_item *item;
	u8 idx = 0;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_UPDATE_OUI_FILTER, mors_vif->id, sizeof(cmd));

	spin_lock_bh(&mors_vif->vendor_ie.lock);
	list_for_each_entry(item, &mors_vif->vendor_ie.oui_filter_list, list) {
		if (item->mgmt_type_mask & MORSE_VENDOR_IE_TYPE_BEACON)
			memcpy(cmd.ouis[idx++], item->oui, sizeof(cmd.ouis[idx]));

		if (idx >= ARRAY_SIZE(cmd.ouis))
			break;
	}
	spin_unlock_bh(&mors_vif->vendor_ie.lock);

	cmd.n_ouis = idx;

	return morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);
}

int morse_cmd_cfg_multicast_filter(struct morse *mors, struct morse_vif *mors_vif)
{
	struct morse_cmd_cfg_mcast_filter *cmd;
	struct mcast_filter *filter = mors->mcast_filter;
	u16 filter_list_len = sizeof(filter->addr_list[0]) * filter->count;
	u16 alloc_len = filter_list_len + sizeof(*cmd);
	int ret = 0;

	cmd = kmalloc(alloc_len, GFP_KERNEL);

	morse_cmd_init(mors, &cmd->hdr, MORSE_COMMAND_MCAST_FILTER, mors_vif->id, alloc_len);

	cmd->count = filter->count;
	memcpy(cmd->addr_list, filter->addr_list, filter_list_len);

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)cmd, 0, 0, __func__);
	kfree(cmd);
	return ret;
}

static int morse_cmd_vendor_set_channel(struct morse *mors,
					struct morse_drv_resp_set_channel *drv_resp,
					struct morse_drv_cmd_set_channel *drv_cmd)
{
	int ret;
	struct morse_cmd_set_channel cmd;
	struct morse_resp_set_channel resp;
	struct morse_channel_info *stored_info = &mors->custom_configs.default_bw_info;
	const struct morse_dot11ah_channel *chan_s1g;

	/* Prepare request */
	cmd = drv_cmd->cmd;
	cmd.hdr.len = cpu_to_le16(sizeof(cmd) - sizeof(cmd.hdr));

	resp.resp.hdr.message_id = cmd.hdr.message_id;

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);
	if (ret)
		return ret;

	/* Prepare response */
	*drv_resp = resp.resp;
	drv_resp->hdr.len = cpu_to_le16(sizeof(*drv_resp) - sizeof(drv_resp->hdr));

	if (cmd.op_chan_freq_hz != DEFAULT_FREQUENCY)
		stored_info->op_chan_freq_hz = cmd.op_chan_freq_hz;

	if (cmd.pri_1mhz_chan_idx != DEFAULT_1MHZ_PRIMARY_CHANNEL_INDEX)
		stored_info->pri_1mhz_chan_idx = cmd.pri_1mhz_chan_idx;

	if (cmd.op_bw_mhz != DEFAULT_BANDWIDTH)
		stored_info->op_bw_mhz = cmd.op_bw_mhz;

	if (cmd.pri_bw_mhz != DEFAULT_BANDWIDTH)
		stored_info->pri_bw_mhz = cmd.pri_bw_mhz;

	/* Validate that primary does not exceed operating */
	stored_info->pri_bw_mhz = (stored_info->op_bw_mhz == 1) ? 1 : stored_info->pri_bw_mhz;

	mors->tx_power_mbm = QDBM_TO_MBM(le32_to_cpu(resp.power_qdbm));

	MORSE_INFO(mors,
		   "%s%s: f:%d o:%d p:%d i:%d power:%d mBm\n",
		   __func__, mors->in_scan ? " (scanning)" : "",
		   cmd.op_chan_freq_hz, cmd.op_bw_mhz,
		   cmd.pri_bw_mhz, cmd.pri_1mhz_chan_idx, mors->tx_power_mbm);

	if (!drv_cmd->s1g_chan_power)
		return 0;

	/* Update txpower using S1G max values if possible */
	chan_s1g = morse_dot11ah_s1g_freq_to_s1g(cmd.op_chan_freq_hz, cmd.op_bw_mhz);
	if (chan_s1g)
		morse_mac_set_txpower(mors, chan_s1g->ch.max_reg_power);

	return 0;
}

static int morse_cmd_vendor_force_power_mode(struct morse *mors, struct morse_resp_vendor *resp,
					     const struct morse_cmd_vendor *cmd)
{
	int ret;
	struct morse_cmd_force_power_mode *force_pm = (struct morse_cmd_force_power_mode *)cmd;

	ret = morse_cmd_tx(mors, (struct morse_resp *)resp, (struct morse_cmd *)cmd, sizeof(*resp),
			   0, __func__);

	if (!ret && force_pm->mode == POWER_MODE_HIBERNATE) {
		/* Once forced into hibernation only a digital reset can recover the device */
		pr_warn("%s has been forced into hibernation\n",
			mors->cfg->get_hw_version(mors->chip_id));
		morse_watchdog_pause(mors);
		set_bit(MORSE_STATE_FLAG_CHIP_UNRESPONSIVE, &mors->state_flags);
	}

	return ret;
}

static int morse_cmd_vendor_standby(struct morse *mors,
				struct morse_resp_vendor *resp,
				const struct morse_cmd_vendor *cmd)
{
	int ret;
	struct morse_cmd_standby_mode_req *standby_mode =
		(struct morse_cmd_standby_mode_req *)cmd;
	struct morse_cmd_standby_mode_resp *standby_mode_resp =
		(struct morse_cmd_standby_mode_resp *)resp;

	if (standby_mode->cmd == STANDBY_MODE_CMD_ENTER) {
		/* Validate hw scan config prior to entering standby */
		if (hw_scan_is_supported(mors)) {
			if (!mors->hw_scan.params)
				return -EINVAL;
			else if (!hw_scan_is_idle(mors))
				return -EBUSY;
			else if (!morse_mac_is_sta_vif_associated(mors->hw_scan.params->vif) &&
				 !hw_scan_saved_config_has_ssid(mors))
				return -EINVAL;

			ret = morse_cmd_hw_scan(mors, mors->hw_scan.params, true);

			if (ret)
				goto exit;
		}
	}

	/* Clear the flag that blocks host to firmware TX, before sending standby exit
	 * command to firmware or else the exit command itself will get blocked.
	 */
	if (standby_mode->cmd == STANDBY_MODE_CMD_EXIT &&
		test_bit(MORSE_STATE_FLAG_HOST_TO_CHIP_CMD_BLOCKED, &mors->state_flags))
		clear_bit(MORSE_STATE_FLAG_HOST_TO_CHIP_CMD_BLOCKED, &mors->state_flags);

	ret = morse_cmd_tx(mors, (struct morse_resp *)resp,
			(struct morse_cmd *)cmd, sizeof(*resp), 0, __func__);

	if (ret)
		goto exit;

	if (standby_mode->cmd == STANDBY_MODE_CMD_ENTER) {
		if (hw_scan_is_supported(mors)) {
			set_bit(MORSE_STATE_FLAG_HOST_TO_CHIP_TX_BLOCKED, &mors->state_flags);
			set_bit(MORSE_STATE_FLAG_HOST_TO_CHIP_CMD_BLOCKED, &mors->state_flags);
		}
		morse_watchdog_pause(mors);
	} else if (standby_mode->cmd == STANDBY_MODE_CMD_EXIT) {
		clear_bit(MORSE_STATE_FLAG_HOST_TO_CHIP_TX_BLOCKED, &mors->state_flags);
		clear_bit(MORSE_STATE_FLAG_HOST_TO_CHIP_CMD_BLOCKED, &mors->state_flags);
		morse_watchdog_resume(mors);
	}

	if (!ret && standby_mode->cmd == STANDBY_MODE_CMD_EXIT) {
		u16 vif_id = le16_to_cpu(standby_mode_resp->hdr.vif_id);
		struct morse_cmd_standby_mode_exit *exit = &standby_mode_resp->info;
		bool valid_response = (standby_mode_resp->hdr.len ==
				(sizeof(*standby_mode_resp) - sizeof(standby_mode_resp->hdr)));
		struct ieee80211_vif *vif = morse_get_vif_from_vif_id(mors, vif_id);

		if (valid_response)
			MORSE_ERR(mors, "%s: Standby exited - reason: '%s', STA state %d\n",
				__func__, morse_cmd_standby_exit_reason_to_str(exit->reason),
				exit->sta_state);

		if (vif && vif->type == NL80211_IFTYPE_STATION &&
		    morse_mac_is_sta_vif_associated(vif) &&
		    valid_response && exit->sta_state < IEEE80211_STA_ASSOC)
			ieee80211_connection_loss(vif);
	}

exit:
	return ret;
}

/**
 * morse_cmd_get_set_non_tim_mode() - Get or Set the non-TIM mode
 *
 * @vif: Pointer to virtual interface
 * @resp: Response for the command
 * @cmd: Command request to get or set
 * @is_set_cmd: Value indicating set/get command, 1 for set command and 0 for get command.
 *
 * @return Error code on failure, 0 otherwise
 */
static int morse_cmd_get_set_non_tim_mode(struct ieee80211_vif *vif,
					   struct morse_cmd_param_cfm *resp,
					   const struct morse_cmd_param_req *cmd, bool is_set_cmd)
{
	int ret = 0;
	struct morse_vif *mors_vif;

	if (!vif) {
		ret = -EFAULT;
		goto exit;
	}

	if (vif->type != NL80211_IFTYPE_STATION && vif->type != NL80211_IFTYPE_AP) {
		ret = -EFAULT;
		goto exit;
	}
	mors_vif = ieee80211_vif_to_morse_vif(vif);

	if (is_set_cmd) {
		/* Do not allow disabling non-TIM mode when STAs are associated */
		if (vif->type == NL80211_IFTYPE_AP && cmd->value == 0 && mors_vif->ap->num_stas) {
			ret = -EPERM;
			goto exit;
		}
		mors_vif->enable_non_tim_mode = (cmd->value == 1);
		if (mors_vif->enable_non_tim_mode &&
			MORSE_CAPAB_SUPPORTED(&mors_vif->capabilities, NON_TIM))
			mors_vif->s1g_cap_ie.capab_info[4] |= S1G_CAP4_NON_TIM;
		else
			mors_vif->s1g_cap_ie.capab_info[4] &= ~S1G_CAP4_NON_TIM;
	} else {
		resp->value = mors_vif->enable_non_tim_mode;
	}

exit:
	return ret;
}

/**
 * morse_cmd_vendor_get_params() - Get the parameter value
 *
 * @mors: Global Morse struct
 * @vif: Pointer to virtual interface
 * @cmd: Command request to get
 * @resp: Response for the command
 *
 * @return Error code on failure, 0 otherwise
 */
static int morse_cmd_vendor_get_params(struct morse *mors, struct ieee80211_vif *vif,
		struct morse_cmd_param_cfm *resp, const struct morse_cmd_param_req *cmd)
{
	int ret = 0;

	switch (cmd->param_id) {
	case MORSE_PARAM_ID_NON_TIM_MODE:
		ret = morse_cmd_get_set_non_tim_mode(vif, resp, cmd, false);
		break;
	default:
		resp->value = -1;
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * morse_cmd_vendor_set_params() - Set the parameter value
 *
 * @mors: Global Morse struct
 * @vif: Pointer to virtual interface
 * @cmd: Command request to set
 * @resp: Response for the command
 *
 * @return Error code on failure, 0 otherwise
 */
static int morse_cmd_vendor_set_params(struct morse *mors, struct ieee80211_vif *vif,
		struct morse_cmd_param_cfm *resp, const struct morse_cmd_param_req *cmd)
{
	int ret = 0;

	switch (cmd->param_id) {
	case MORSE_PARAM_ID_NON_TIM_MODE:
		ret = morse_cmd_get_set_non_tim_mode(vif, resp, cmd, true);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * morse_cmd_vendor_get_set_params() - Get or set parameter value in driver or firmware
 *
 * @mors:	Global Morse struct
 * @vif:	Pointer to virtual interface
 * @cmd:	Command request to get or set
 * @resp:	Response for the command
 */
static int morse_cmd_vendor_get_set_params(struct morse *mors, struct ieee80211_vif *vif,
		struct morse_cmd_param_cfm *resp, const struct morse_cmd_param_req *cmd)
{
	int ret = 0;
	bool is_driver_param = false;

	switch (cmd->param_id) {
	case MORSE_PARAM_ID_NON_TIM_MODE:
		is_driver_param = true;
		if (cmd->action == MORSE_PARAM_ACTION_GET)
			ret = morse_cmd_vendor_get_params(mors, vif, resp, cmd);
		else if (cmd->action == MORSE_PARAM_ACTION_SET)
			ret = morse_cmd_vendor_set_params(mors, vif, resp, cmd);
		break;
	default:
		/* Get or set command for the firmware */
		ret = morse_cmd_tx(mors, (struct morse_resp *)resp,
			(struct morse_cmd *)cmd, sizeof(*resp), 0, __func__);
		break;
	}

	if (!ret && is_driver_param) {
		resp->hdr.len = cpu_to_le16(sizeof(*resp) - sizeof(resp->hdr));
		resp->hdr.host_id = cmd->hdr.host_id;
		resp->status = ret;
	}

	return ret;
}

int morse_cmd_vendor(struct morse *mors, struct ieee80211_vif *vif,
		     const struct morse_cmd_vendor *cmd, int cmd_len,
		     struct morse_resp_vendor *resp, int *resp_len)
{
	int ret;
	struct morse_vif *mors_vif = ieee80211_vif_to_morse_vif(vif);

	resp->hdr.message_id = cmd->hdr.message_id;

	if (cmd->hdr.message_id >= MORSE_COMMAND_DRIVER_START &&
	    cmd->hdr.message_id <= MORSE_COMMAND_DRIVER_END) {
		ret = morse_cmd_drv(mors, vif, (struct morse_resp *)resp,
				    (struct morse_cmd *)cmd, sizeof(*resp), 0);
		if (ret)
			MORSE_ERR(mors, "%s error %d\n", __func__, ret);
	} else if (cmd->hdr.message_id == MORSE_COMMAND_SET_CHANNEL) {
		ret = morse_cmd_vendor_set_channel(mors,
				(struct morse_drv_resp_set_channel *)resp,
				(struct morse_drv_cmd_set_channel *)cmd);
	} else if (cmd->hdr.message_id == MORSE_COMMAND_STANDBY_MODE) {
		ret = morse_cmd_vendor_standby(mors, resp, cmd);
	} else if (cmd->hdr.message_id == MORSE_COMMAND_FORCE_POWER_MODE) {
		ret = morse_cmd_vendor_force_power_mode(mors, resp, cmd);
	} else if (cmd->hdr.message_id == MORSE_COMMAND_GET_SET_GENERIC_PARAM) {
		ret = morse_cmd_vendor_get_set_params(mors, vif,
						      (struct morse_cmd_param_cfm *)resp,
						      (struct morse_cmd_param_req *)cmd);
	} else {
		ret = morse_cmd_tx(mors, (struct morse_resp *)resp,
				   (struct morse_cmd *)cmd, sizeof(*resp), 0, __func__);
	}
	if (ret) {
		resp->hdr.host_id = cmd->hdr.host_id;
		resp->status = ret;
		*resp_len = sizeof(struct morse_resp);
		goto exit;
	}
	*resp_len = resp->hdr.len + sizeof(struct morse_cmd_header);

	/** Commands that were successful and need to be post processed */

	switch (cmd->hdr.message_id) {
	case MORSE_COMMAND_SET_CONTROL_RESPONSE:
		{
			struct morse_cmd_cr_bw *cr_cmd = (struct morse_cmd_cr_bw *)cmd;

			if (mors_vif) {
				if (cr_cmd->direction)
					mors_vif->ctrl_resp_in_1mhz_en = cr_cmd->cr_1mhz_en;
				else
					mors_vif->ctrl_resp_out_1mhz_en = cr_cmd->cr_1mhz_en;
			}
		}
		break;
	case MORSE_COMMAND_SET_BSS_COLOR:
		{
			struct morse_cmd_set_bss_color *color_cmd =
			    (struct morse_cmd_set_bss_color *)cmd;

			if (mors_vif)
				mors_vif->bss_color = color_cmd->color;
		}
		break;
	case MORSE_COMMAND_SET_LONG_SLEEP_CONFIG:
		{
			struct morse_cmd_set_long_sleep_config *long_sleep_cmd =
			    (struct morse_cmd_set_long_sleep_config *)cmd;

			if (long_sleep_cmd->enabled)
				(void)morse_watchdog_pause(mors);
			else
				(void)morse_watchdog_resume(mors);
		}
		break;
	case MORSE_COMMAND_SET_CTS_SELF_PS:
		{
			struct morse_cmd_cts_self_ps *cts_self_ps =
			    (struct morse_cmd_cts_self_ps *)cmd;

			WARN_ON_ONCE(!mors_vif);
			if (mors_vif && vif->type == NL80211_IFTYPE_AP)
				morse_set_dtim_cts_to_self(cts_self_ps->enable, mors_vif);
		}
		break;
	case MORSE_COMMAND_GET_SET_GENERIC_PARAM:
		{
			struct morse_cmd_param_req *get_set_cmd = (struct morse_cmd_param_req *)cmd;
			struct morse_cmd_param_cfm *get_set_resp =
			    (struct morse_cmd_param_cfm *)resp;

			if (get_set_cmd->param_id == MORSE_PARAM_ID_EXTRA_ACK_TIMEOUT_ADJUST_US) {
				if (get_set_cmd->action == MORSE_PARAM_ACTION_SET)
					mors->extra_ack_timeout_us = get_set_cmd->value;
				else if (get_set_cmd->action == MORSE_PARAM_ACTION_GET)
					mors->extra_ack_timeout_us = get_set_resp->value;
			}
		}
		break;
	}

exit:
	return ret;
}

int morse_wiphy_cmd_vendor(struct morse *mors,
		     const struct morse_cmd_vendor *cmd, int cmd_len,
		     struct morse_resp_vendor *resp, int *resp_len)
{
	int ret;

	resp->hdr.message_id = cmd->hdr.message_id;

	if (cmd->hdr.message_id == MORSE_COMMAND_COREDUMP) {
		ret = morse_cmd_drv(mors, NULL, (struct morse_resp *)resp,
				    (struct morse_cmd *)cmd, sizeof(*resp), 0);
	} else {
		/* Command not supported yet */
		ret = -ENOTSUPP;
	}
	if (ret) {
		resp->hdr.host_id = cmd->hdr.host_id;
		resp->status = ret;
		*resp_len = sizeof(struct morse_resp);
		goto exit;
	}
	*resp_len = resp->hdr.len + sizeof(struct morse_cmd_header);

exit:
	if (ret)
		MORSE_ERR(mors, "%s: failed (ret:%d)\n", __func__, ret);
	return ret;
}

/* Sets the control response frame bandwidth for the given vif */
int morse_cmd_set_cr_bw(struct morse *mors, struct morse_vif *mors_vif, u8 direction, u8 cr_1mhz_en)
{
	int ret;
	struct morse_cmd_cr_bw cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_SET_CONTROL_RESPONSE, mors_vif->id,
		       sizeof(cmd));

	cmd.cr_1mhz_en = cr_1mhz_en;
	cmd.direction = direction;

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_cfg_qos(struct morse *mors, struct morse_queue_params *params)
{
	int ret;
	struct morse_cmd_cfg_qos cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_SET_QOS_PARAMS, 0, sizeof(cmd));

	cmd.uapsd = params->uapsd;
	cmd.aci = params->aci;
	cmd.aifs = params->aifs;
	cmd.cw_min = cpu_to_le16(params->cw_min);
	cmd.cw_max = cpu_to_le16(params->cw_max);
	cmd.txop = cpu_to_le32(params->txop);

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_set_bss_color(struct morse *mors, struct morse_vif *mors_vif, u8 color)
{
	int ret;
	struct morse_cmd_set_bss_color cmd;
	struct morse_resp_set_bss_color resp;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_SET_BSS_COLOR, mors_vif->id, sizeof(cmd));

	cmd.color = color;

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);

	return ret;
}

int morse_cmd_health_check(struct morse *mors)
{
	int ret;
	struct morse_cmd_health_check cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_HEALTH_CHECK, 0, sizeof(cmd));

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0,
			   default_cmd_timeout_ms > MM_CMD_HEALTH_CHECK_TIMEOUT_MS ?
			   default_cmd_timeout_ms : MM_CMD_HEALTH_CHECK_TIMEOUT_MS, __func__);

	return ret;
}

int morse_cmd_arp_offload_update_ip_table(struct morse *mors, u16 vif_id,
					  int arp_addr_count, u32 *arp_addr_list)
{
	int ret = 0;
	int i;
	struct morse_cmd_arp_offload cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_ARP_OFFLOAD, vif_id, sizeof(cmd));

	memset(cmd.ip_table, 0, sizeof(cmd.ip_table));

	for (i = 0; i < arp_addr_count && i < ARRAY_SIZE(cmd.ip_table); i++)
		cmd.ip_table[i] = arp_addr_list[i];

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_get_capabilities(struct morse *mors, u16 vif_id, struct morse_caps *capabilities)
{
	int ret = 0;
	int i;
	struct morse_get_capabilities_req cmd;
	struct morse_get_capabilities_cfm rsp;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_GET_CAPABILITIES, vif_id, sizeof(cmd));

	ret = morse_cmd_tx(mors, (struct morse_resp *)&rsp,
			   (struct morse_cmd *)&cmd, sizeof(rsp), 0, __func__);
	if (ret != 0)
		return ret;

	capabilities->ampdu_mss = rsp.capabilities.ampdu_mss;
	capabilities->morse_mmss_offset = rsp.morse_mmss_offset;
	capabilities->beamformee_sts_capability = rsp.capabilities.beamformee_sts_capability;
	capabilities->maximum_ampdu_length_exponent =
	    rsp.capabilities.maximum_ampdu_length_exponent;
	capabilities->number_sounding_dimensions = rsp.capabilities.number_sounding_dimensions;
	for (i = 0; i < FW_CAPABILITIES_FLAGS_WIDTH; i++)
		capabilities->flags[i] = le32_to_cpu(rsp.capabilities.flags[i]);

	return ret;
}

int morse_cmd_config_non_tim_mode(struct morse *mors, bool enable, u16 vif_id)
{
	int ret = 0;
	struct morse_cmd_param_req cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_GET_SET_GENERIC_PARAM, vif_id, sizeof(cmd));
	cmd.param_id = MORSE_PARAM_ID_NON_TIM_MODE;
	cmd.action = MORSE_PARAM_ACTION_SET;
	cmd.value = enable;
	cmd.flags = 0;

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_enable_li_sleep(struct morse *mors, u16 listen_interval, u16 vif_id)
{
	int ret;
	struct morse_cmd_li_sleep cmd;

	if (listen_interval == 0)
		return -EINVAL;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_SET_LI_SLEEP, vif_id, sizeof(cmd));
	cmd.listen_interval = cpu_to_le32(listen_interval);
	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_dhcpc_enable(struct morse *mors, u16 vif_id)
{
	int ret;
	struct morse_cmd_dhcpc_req cmd;
	struct morse_cmd_dhcpc_cfm resp;

	if (vif_id == (u16)-1)
		return -ENODEV;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_DHCP_OFFLOAD, vif_id, sizeof(cmd));

	cmd.opcode = cpu_to_le32(MORSE_DHCP_CMD_ENABLE);

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);

	if (ret == 0) {
		if (le32_to_cpu(resp.retcode) == MORSE_DHCP_RET_SUCCESS) {
			MORSE_INFO(mors, "In chip DHCP client enabled\n");
		} else if (le32_to_cpu(resp.retcode) == MORSE_DHCP_RET_ALREADY_ENABLED) {
			/* Client is already enabled, trigger a lease update. */
			MORSE_INFO(mors, "DHCP client already enabled, forcing lease update\n");
			cmd.opcode = cpu_to_le32(MORSE_DHCP_CMD_SEND_LEASE_UPDATE);
			ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
					   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);
		} else {
			MORSE_WARN(mors, "Error enabling in-chip DHCP client %d\n",
				   le32_to_cpu(resp.retcode));
		}
	}

	return ret;
}

static int morse_cmd_twt_agreement_req(struct morse *mors,
				       struct morse_twt_agreement_data *agreement,
				       u16 iface_id, bool validate_only)
{
	int ret = 0;
	struct morse_cmd_install_twt_agreement_req *cmd;
	enum morse_commands_id message_id;

	cmd = kmalloc(sizeof(*cmd) + TWT_MAX_AGREEMENT_LEN, GFP_KERNEL);

	if (validate_only)
		message_id = MORSE_COMMAND_VALIDATE_TWT_AGREEMENT;
	else
		message_id = MORSE_COMMAND_INSTALL_TWT_AGREEMENT;
	morse_cmd_init(mors, &cmd->hdr, message_id, iface_id, sizeof(*cmd) + TWT_MAX_AGREEMENT_LEN);

	cmd->flow_id = (agreement->params.req_type & IEEE80211_TWT_REQTYPE_FLOWID) >>
	    IEEE80211_TWT_REQTYPE_FLOWID_OFFSET;
	cmd->agreement_len = morse_twt_initialise_agreement(agreement, cmd->agreement);

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)cmd, 0, 0, __func__);
	kfree(cmd);
	return ret;
}

int morse_cmd_twt_agreement_validate_req(struct morse *mors,
					 struct morse_twt_agreement_data *agreement, u16 iface_id)
{
	return morse_cmd_twt_agreement_req(mors, agreement, iface_id, true);
}

int morse_cmd_twt_agreement_install_req(struct morse *mors,
					struct morse_twt_agreement_data *agreement, u16 iface_id)
{
	return morse_cmd_twt_agreement_req(mors, agreement, iface_id, false);
}

int morse_cmd_twt_remove_req(struct morse *mors,
			     struct morse_cmd_remove_twt_agreement *twt_remove_cmd, u16 iface_id)
{
	morse_cmd_init(mors, &twt_remove_cmd->hdr, MORSE_COMMAND_REMOVE_TWT_AGREEMENT,
		       iface_id, sizeof(*twt_remove_cmd));

	return morse_cmd_tx(mors, NULL, (struct morse_cmd *)twt_remove_cmd, 0, 0, __func__);
}

int morse_cmd_cfg_ibss(struct morse *mors, u16 vif_id,
		       const u8 *bssid, bool ibss_creator, bool stop_ibss)
{
	int ret;
	struct morse_cmd_cfg_ibss cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_IBSS_CONFIG, vif_id, sizeof(cmd));

	/* If stop_ibss is set, other parameters are ignored and cleared in target */
	memcpy(cmd.ibss_bssid_addr, bssid, sizeof(cmd.ibss_bssid_addr));

	if (stop_ibss) {
		cmd.ibss_cfg_opcode = MORSE_IBSS_CONFIG_CMD_STOP;
	} else {
		if (ibss_creator)
			cmd.ibss_cfg_opcode = MORSE_IBSS_CONFIG_CMD_CREATE;
		else
			cmd.ibss_cfg_opcode = MORSE_IBSS_CONFIG_CMD_JOIN;
	}

	cmd.ibss_probe_filtering = enable_ibss_probe_filtering;

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_cfg_offset_tsf(struct morse *mors, u16 vif_id, s64 offset_tsf)
{
	int ret;
	struct morse_cmd_cfg_offset_tsf cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_SET_OFFSET_TSF, vif_id, sizeof(cmd));

	cmd.offset_tsf = cpu_to_le64(offset_tsf);

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_set_duty_cycle(struct morse *mors,
			     enum duty_cycle_mode mode, int duty_cycle, bool omit_ctrl_resp)
{
	int ret;
	struct morse_cmd_set_duty_cycle_req cmd;
	struct morse_cmd_set_duty_cycle_cfm resp;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_SET_DUTY_CYCLE, 0, sizeof(cmd));

	cmd.duty_cycle = cpu_to_le32(duty_cycle);
	cmd.omit_ctrl_resp = omit_ctrl_resp ? 1 : 0;
	cmd.mode = mode;
	cmd.set_configs =
	    MORSE_DUTY_CYCLE_SET_CFG_DUTY_CYCLE |
	    MORSE_DUTY_CYCLE_SET_CFG_OMIT_CTRL_RESP | MORSE_DUTY_CYCLE_SET_CFG_EXT;

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);

	return ret;
}

int morse_cmd_set_mpsw(struct morse *mors, int min, int max, int window)
{
	int ret;
	struct morse_cmd_set_mpsw_config_req cmd;
	struct morse_cmd_set_mpsw_config_cfm resp;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_MPSW_CONFIG, 0, sizeof(cmd));

	cmd.config.airtime_max_us = cpu_to_le32(max);
	cmd.config.airtime_min_us = cpu_to_le32(min);
	cmd.config.packet_space_window_length_us = cpu_to_le32(window);
	cmd.config.enable = (max > 0 && min > 0);
	cmd.set_configs = MORSE_MPSW_SET_CFG_AIRTIME_BOUNDS |
	    MORSE_MPSW_SET_CFG_PKT_SPACE_WINDOW_LEN | MORSE_MPSW_SET_CFG_ENABLED;

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);

	return ret;
}

int morse_cmd_get_available_channels(struct morse *mors, struct morse_resp *resp)
{
	struct morse_cmd_get_available_channels_cfm *cfm =
	    (struct morse_cmd_get_available_channels_cfm *)resp;
	int num_entries;

	num_entries = morse_dot11ah_fill_channel_list(cfm->channels);

	if (num_entries < 0) {
		resp->hdr.len = 4;
		return num_entries;
	}

	cfm->num_channels = num_entries;

	resp->hdr.len = sizeof(*cfm) - sizeof(cfm->hdr) + (num_entries * sizeof(cfm->channels[0]));

	return 0;
}

int morse_cmd_get_hw_version(struct morse *mors, struct morse_resp *resp)
{
	struct morse_resp_get_hw_version_cfm *cfm = (struct morse_resp_get_hw_version_cfm *)resp;
	struct morse_hw_cfg *cfg = mors->cfg;
	const char *hw_version = "n/a";
	int ret;

	if (!cfg)
		return -ENXIO;

	memset(&cfm->hw_version, 0x0, sizeof(cfm->hw_version));

	if (mors->cfg->get_hw_version)
		hw_version = mors->cfg->get_hw_version(mors->chip_id);

	ret = strscpy(cfm->hw_version, hw_version, sizeof(cfm->hw_version));

	if (ret == -E2BIG)
		MORSE_WARN(mors, "Hardware version string truncated\n");

	resp->hdr.len = cpu_to_le16(sizeof(*cfm) - sizeof(cfm->hdr));

	return 0;
}

int morse_cmd_set_frag_threshold(struct morse *mors, u32 frag_threshold)
{
	int ret;
	struct morse_cmd_set_frag_threshold_req cmd;
	struct morse_cmd_set_frag_threshold_cfm resp;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_SET_FRAG_THRESHOLD, 0, sizeof(cmd));

	cmd.frag_threshold = cpu_to_le32(frag_threshold);

	ret = morse_cmd_tx(mors, (struct morse_resp *)&resp,
			   (struct morse_cmd *)&cmd, sizeof(resp), 0, __func__);

	return ret;
}

int morse_cmd_cfg_mesh(struct morse *mors, struct morse_vif *mors_vif, bool stop_mesh,
		       bool mesh_beaconing)
{
	int ret;
	struct morse_cmd_cfg_mesh cmd;
	struct morse_mesh *mesh = mors_vif->mesh;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_MESH_CONFIG, mors_vif->id, sizeof(cmd));

	if (stop_mesh)
		cmd.mesh_cfg_opcode = MORSE_MESH_CONFIG_CMD_STOP;
	else
		cmd.mesh_cfg_opcode = MORSE_MESH_CONFIG_CMD_START;

	cmd.mesh_beaconing = mesh_beaconing;
	cmd.mbca_config = mesh->mbca.config;
	if (mesh_beaconing) {
		cmd.min_beacon_gap_ms = mesh->mbca.min_beacon_gap_ms;
		cmd.tbtt_adj_timer_interval_ms = mesh->mbca.tbtt_adj_interval_ms;
		cmd.mbss_start_scan_duration_ms = mesh->mbca.mbss_start_scan_duration_ms;
	}
	MORSE_INFO(mors, "%s: cfg=0x%02x, gap=%u, tbtt interval=%u start scan duration=%u\n",
		   __func__,
		   mesh->mbca.config, mesh->mbca.min_beacon_gap_ms, mesh->mbca.tbtt_adj_interval_ms,
		   mesh->mbca.mbss_start_scan_duration_ms);

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_ack_timeout_adjust(struct morse *mors, u16 vif_id, u32 timeout_us)
{
	int ret;
	struct morse_cmd_param_req cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_GET_SET_GENERIC_PARAM, vif_id, sizeof(cmd));

	cmd.param_id = MORSE_PARAM_ID_EXTRA_ACK_TIMEOUT_ADJUST_US;
	cmd.action = MORSE_PARAM_ACTION_SET;
	cmd.value = timeout_us;
	cmd.flags = 0;

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	/* Store ack timeout adjust as it's used in a vendor WAR */
	if (ret == 0)
		mors->extra_ack_timeout_us = le32_to_cpu(timeout_us);

	return ret;
}

int morse_cmd_pv1_set_rx_ampdu_state(struct morse_vif *mors_vif, u8 *sta_addr, u8 tid,
		u16 buf_size, bool ba_session_enable)
{
	int ret;
	struct morse_cmd_pv1_rx_ampdu_state cmd;
	struct morse *mors = morse_vif_to_morse(mors_vif);

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_PV1_SET_RX_AMPDU_STATE,
			mors_vif->id, sizeof(cmd));

	cmd.tid = tid;
	cmd.buf_size = buf_size;
	cmd.ba_session_enable = ba_session_enable;
	memcpy(cmd.addr, sta_addr, ETH_ALEN);

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_configure_page_slicing(struct morse_vif *mors_vif, bool enable)
{
	int ret;
	struct morse *mors = morse_vif_to_morse(mors_vif);
	struct morse_cmd_page_slicing_config cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_CONFIGURE_PAGE_SLICING,
		mors_vif->id, sizeof(cmd));

	cmd.enabled = enable;

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);

	return ret;
}

int morse_cmd_hw_scan(struct morse *mors, struct morse_hw_scan_params *params, bool store)
{
	int ret;
	struct morse_cmd_hw_scan_req *cmd;
	size_t cmd_size;
	u8 *buf;

	cmd_size = morse_hw_scan_get_command_size(params);
	cmd_size = ROUND_BYTES_TO_WORD(cmd_size);

	cmd = kzalloc(cmd_size, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	buf = cmd->variable;

	if (store)
		cmd->flags = MORSE_HW_SCAN_CMD_FLAGS_STORE;
	else
		cmd->flags |= params->start ?
			MORSE_HW_SCAN_CMD_FLAGS_START : MORSE_HW_SCAN_CMD_FLAGS_ABORT;

	if (params->survey)
		cmd->flags |= MORSE_HW_SCAN_CMD_FLAGS_SURVEY;

	if (params->use_1mhz_probes)
		cmd->flags |= MORSE_HW_SCAN_CMD_FLAGS_1MHZ_PROBES;

	cmd->flags = cpu_to_le32(cmd->flags);

	if (params->start) {
		cmd->dwell_time_ms = cpu_to_le32(params->dwell_time_ms);
		buf = morse_hw_scan_insert_tlvs(params, buf);
	}

	morse_cmd_init(mors, &cmd->hdr, MORSE_COMMAND_HW_SCAN, 0, buf - (u8 *)cmd);
	morse_hw_scan_dump_scan_cmd(mors, cmd);
	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)cmd, 0, 0, __func__);
	kfree(cmd);

	return ret;
}

int morse_cmd_set_country(struct morse *mors, const char *country_code)
{
	struct morse_cmd_param_req cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_GET_SET_GENERIC_PARAM, 0, sizeof(cmd));
	cmd.param_id = MORSE_PARAM_ID_COUNTRY;
	cmd.action = MORSE_PARAM_ACTION_SET;
	cmd.flags = 0;
	cmd.value = country_code[0] | country_code[1] << 8;

	return morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);
}

int morse_cmd_set_rts_threshold(struct morse *mors, u32 rts_threshold)
{
	struct morse_cmd_param_req cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_GET_SET_GENERIC_PARAM, 0, sizeof(cmd));
	cmd.param_id = MORSE_PARAM_ID_RTS_THRESHOLD;
	cmd.action = MORSE_PARAM_ACTION_SET;
	cmd.flags = 0;
	cmd.value = rts_threshold;

	return morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);
}

int morse_cmd_start_scan(struct morse *mors,
			 u8 n_ssids,
			 const u8 *ssid, size_t ssid_len,
			 const u8 *extra_ies, size_t extra_ies_len, u32 dwell_time_ms)
{
	struct morse_cmd_start_scan *cmd;
	int ret;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	morse_cmd_init(mors, &cmd->hdr, MORSE_COMMAND_START_SCAN, 0, sizeof(*cmd));

	cmd->dwell_time_ms = dwell_time_ms;
	if (extra_ies_len) {
		extra_ies_len = min(sizeof(cmd->extra_ies), extra_ies_len);
		memcpy(cmd->extra_ies, extra_ies, extra_ies_len);
		cmd->extra_ies_len = extra_ies_len;
	}
	cmd->n_ssids = n_ssids;
	if (ssid_len) {
		ssid_len = min(sizeof(cmd->ssid), ssid_len);
		memcpy(cmd->ssid, ssid, ssid_len);
		cmd->ssid_len = ssid_len;
	}

	ret = morse_cmd_tx(mors, NULL, (struct morse_cmd *)cmd, 0, 0, __func__);

	kfree(cmd);

	return ret;
}

int morse_cmd_abort_scan(struct morse *mors)
{
	struct morse_cmd_abort_scan cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_ABORT_SCAN, 0, sizeof(cmd));

	return morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);
}

int morse_cmd_connect(struct morse *mors, const u8 *ssid, size_t ssid_len,
		      enum nl80211_auth_type auth_type,
		      const u8 *sae_pwd, size_t sae_pwd_len)
{
	struct morse_cmd_connect cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_CONNECT, 0, sizeof(cmd));

	switch (auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		cmd.auth_type = CONNECT_AUTH_TYPE_OPEN;
		break;
	case NL80211_AUTHTYPE_SAE:
		cmd.auth_type = CONNECT_AUTH_TYPE_SAE;
		break;
	case NL80211_AUTHTYPE_AUTOMATIC:
		cmd.auth_type = CONNECT_AUTH_TYPE_AUTOMATIC;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (ssid_len < 1 || ssid_len > sizeof(cmd.ssid))
		return -EINVAL;
	memcpy(cmd.ssid, ssid, ssid_len);
	cmd.ssid_len = ssid_len;

	if (sae_pwd_len > sizeof(cmd.sae_pwd))
		return -EINVAL;
	memcpy(cmd.sae_pwd, sae_pwd, sae_pwd_len);
	cmd.sae_pwd_len = sae_pwd_len;

	return morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);
}

int morse_cmd_disconnect(struct morse *mors)
{
	struct morse_cmd_disconnect cmd;

	morse_cmd_init(mors, &cmd.hdr, MORSE_COMMAND_DISCONNECT, 0, sizeof(cmd));

	return morse_cmd_tx(mors, NULL, (struct morse_cmd *)&cmd, 0, 0, __func__);
}

int morse_cmd_get_connection_state(struct morse *mors, s8 *signal,
				   u32 *connected_time_s, u8 *dtim_period,
				   u16 *beacon_interval_tu)
{
	struct morse_cmd_get_connection_state_req req;
	struct morse_cmd_get_connection_state_cfm cfm;
	s16 signal_from_chip;
	int ret;

	morse_cmd_init(mors, &req.hdr, MORSE_COMMAND_GET_CONNECTION_STATE, 0, sizeof(req));

	ret = morse_cmd_tx(mors, (struct morse_resp *)&cfm, (struct morse_cmd *)&req,
			   sizeof(cfm), 0, __func__);
	if (ret)
		return ret;

	/* The chip gives us a signal indication in dBm as int16_t. */
	signal_from_chip = (s16)le16_to_cpu(cfm.rssi);

	if (signal_from_chip >= S8_MIN && signal_from_chip <= S8_MAX)
		*signal = signal_from_chip;
	else
		*signal = 0;

	*connected_time_s = le32_to_cpu(cfm.connected_time_s);

	if (le16_to_cpu(cfm.dtim_period) <= U8_MAX)
		*dtim_period = le16_to_cpu(cfm.dtim_period);
	else
		*dtim_period = U8_MAX;

	*beacon_interval_tu = le16_to_cpu(cfm.beacon_interval_tu);

	return 0;
}
