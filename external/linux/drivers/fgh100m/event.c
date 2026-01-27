/*
 * Copyright 2022-2023 Morse Micro
 *
 */

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>

#include "morse.h"
#include "debug.h"
#include "command.h"
#include "skbq.h"
#include "mac.h"
#include "monitor.h"
#include "skb_header.h"
#include "offload.h"
#include "vendor.h"
#include "ocs.h"
#include "wiphy.h"
#include "hw_scan.h"

/** List of reason codes to use in the `command_connection_loss_evt` event */
enum connection_loss_reason_code {
	CONNECTION_LOSS_REASON_TSF_RESET = 0,
};

static const char *connection_loss_reason_to_str(enum connection_loss_reason_code reason)
{
	switch (reason) {
	case CONNECTION_LOSS_REASON_TSF_RESET:
		return "tsf reset";
	default:
		return "unknown";
	}
}

int morse_mac_event_recv(struct morse *mors, struct sk_buff *skb)
{
	int ret;

	struct morse_event *event = (struct morse_event *)(skb->data);
	u16 event_id = le16_to_cpu(event->hdr.message_id);
	u16 event_iid = le16_to_cpu(event->hdr.host_id);
	u16 event_len = le16_to_cpu(event->hdr.len);
	u16 vif_id = le16_to_cpu(event->hdr.vif_id);

	if (!MORSE_CMD_IS_EVT(event)) {
		ret = -EINVAL;
		goto exit;
	}

	/* For events, iid must be set to 0 */
	if (event_iid != 0) {
		ret = -EINVAL;
		goto exit;
	}

	MORSE_DBG(mors, "EVT 0x%04x LEN %u\n", event_id, event_len);

	switch (event_id) {
	case MORSE_COMMAND_EVT_STA_STATE:
		{
			struct morse_evt_sta_state *sta_state_evt =
			    (struct morse_evt_sta_state *)event;

			MORSE_DBG(mors, "State change event: addr %pM, aid %u, state %u\n",
				  sta_state_evt->addr, sta_state_evt->aid, sta_state_evt->state);

			ret = 0;

			break;
		}
	case MORSE_COMMAND_EVT_BEACON_LOSS:
		{
			struct morse_evt_beacon_loss *bcn_loss_evt =
			    (struct morse_evt_beacon_loss *)event;
			struct ieee80211_vif *vif = morse_get_vif_from_vif_id(mors, vif_id);

			if (vif)
				ieee80211_beacon_loss(vif);

			MORSE_DBG(mors, "Beacon loss event: number of beacons %u, vif id %u\n",
				  bcn_loss_evt->num_bcns, vif_id);

			ret = 0;

			break;
		}
	case MORSE_COMMAND_EVT_SIG_FIELD_ERROR:
		{
			struct morse_evt_sig_field_error_evt *sig_field_error_evt =
			    (struct morse_evt_sig_field_error_evt *)event;

#ifdef CONFIG_MORSE_MONITOR
			if (mors->hw->conf.flags & IEEE80211_CONF_MONITOR)
				morse_mon_sig_field_error(sig_field_error_evt);
#endif

			MORSE_DBG(mors, "Sig field error %llu - %llu\n",
				  sig_field_error_evt->start_timestamp,
				  sig_field_error_evt->end_timestamp);

			ret = 0;

			break;
		}
	case MORSE_COMMAND_EVT_UMAC_TRAFFIC_CONTROL:
		{
			/* Event size from older firmware revisions */
			const int legacy_evt_len = 3;
			struct morse_evt_umac_traffic_control *umac_traffic_control =
			    (struct morse_evt_umac_traffic_control *)event;
			int sources = UMAC_TRAFFIC_CONTROL_SOURCE_TWT;

			if (event->hdr.len > legacy_evt_len)
				/* This event includes the source bitfield (potentially
				 * more sources than just twt).
				 */
				sources = le32_to_cpu(umac_traffic_control->sources);

			ret = morse_mac_traffic_control(mors,
						le16_to_cpu(umac_traffic_control->hdr.vif_id),
						umac_traffic_control->pause_data_traffic,
						sources);
			break;
		}
	case MORSE_COMMAND_EVT_DHCP_LEASE_UPDATE:
		{
			struct morse_evt_dhcp_lease_update *dhcp_lease_update =
			    (struct morse_evt_dhcp_lease_update *)event;

			if (mors->custom_configs.enable_dhcpc_offload)
				ret = morse_offload_dhcpc_set_address(mors, dhcp_lease_update);
			else
				ret = 0;
			break;
		}
	case MORSE_COMMAND_EVT_OCS_DONE:
		{
			struct ieee80211_vif *vif = morse_get_vif_from_vif_id(mors, vif_id);

			ret = morse_evt_ocs_done(ieee80211_vif_to_morse_vif(vif), event);
			break;
		}
	case MORSE_COMMAND_EVT_SCAN_RESULT:
		{
			struct morse_evt_scan_result *scan_result =
			    (struct morse_evt_scan_result *)event;

			ret = morse_wiphy_scan_result(mors, scan_result);

			break;
		}
	case MORSE_COMMAND_EVT_SCAN_DONE:
		{
			struct morse_evt_scan_done *scan_done = (struct morse_evt_scan_done *)event;

			morse_wiphy_scan_done(mors, scan_done->aborted);

			ret = 0;

			break;
		}
	case MORSE_COMMAND_EVT_CONNECTED:
		{
			struct morse_evt_connected *connected = (struct morse_evt_connected *)event;

			morse_wiphy_connected(mors, connected->bssid);

			ret = 0;

			break;
		}
	case MORSE_COMMAND_EVT_DISCONNECTED:
		{
			morse_wiphy_disconnected(mors);

			ret = 0;

			break;
		}
	case MORSE_COMMAND_EVT_HW_SCAN_DONE:
		{
			morse_hw_scan_done_event(mors->hw);

			ret = 0;

			break;
		}
	case MORSE_COMMAND_EVT_CHANNEL_USAGE:
		{
			struct morse_evt_channel_usage *resp =
					(struct morse_evt_channel_usage *)event;
			struct morse_survey_rx_usage_record record;

			record.time_listen = le64_to_cpu(resp->time_listen);
			record.time_rx = le64_to_cpu(resp->busy_time);
			record.freq_hz = le32_to_cpu(resp->freq_hz);
			record.bw_mhz = le32_to_cpu(resp->bw_mhz);
			record.noise = resp->noise;

			morse_survey_add_channel_usage(mors, &record);

			ret = 0;

			break;
		}
	case MORSE_COMMAND_EVT_CONNECTION_LOSS:
		{
			struct morse_evt_connection_loss *conn_loss =
				(struct morse_evt_connection_loss *)event;
			struct ieee80211_vif *vif = morse_get_vif_from_vif_id(mors, vif_id);

			MORSE_ERR(mors, "%s: connection loss observed on vif:%d, reason: '%s'",
				__func__, vif_id, connection_loss_reason_to_str(conn_loss->reason));

			if (vif)
				ieee80211_connection_loss(vif);

			ret = 0;
			break;
		}
	default:
		ret = -EINVAL;
	}

exit:
	return ret;
}
