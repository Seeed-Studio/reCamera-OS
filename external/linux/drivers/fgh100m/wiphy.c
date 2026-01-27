/*
 * Copyright 2017-2023 Morse Micro
 *
 */

#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/crc32.h>
#include <net/mac80211.h>
#include <linux/rtnetlink.h>
#include <asm/div64.h>

#include "bus.h"
#include "command.h"
#include "morse.h"
#include "mac.h"
#include "ps.h"
#include "utils.h"
#include "wiphy.h"
#include "debug.h"

/**
 * The maximum number of SSIDs we support scanning for in a single scan request.
 */
#define SCAN_MAX_SSIDS 1

/**
 * Number of bytes of extra padding to be inserted at the start of each tx packet.
 *
 * Fullmac firmware needs at least 20 bytes so that it can do 802.3 to 802.11 header
 * translation in place.
 */
#define EXTRA_TX_OFFSET 20

/**
 * morse_wiphy_privid -  privid value for our wiphy
 *
 * This lets us distinguish it from ieee80211_hw owned by mac80211.
 */
static const void *const morse_wiphy_privid = &morse_wiphy_privid;

/*
 * bound_mors_vif - singleton vif in fullmac mode
 *
 * Fullmac only supports one station virtual interface. This is a pointer to its private structure.
 *
 * TODO: refactor private struct layout to allow multiple vifs, remove this global pointer.
 */
static struct morse_vif *bound_mors_vif;

struct morse *morse_wiphy_to_morse(struct wiphy *wiphy)
{
	struct ieee80211_hw *hw;

	/* If we were loaded in fullmac mode, our struct morse is the priv structure in wiphy,
	 * there is no ieee80211_hw.
	 */
	if (wiphy->privid == morse_wiphy_privid)
		return wiphy_priv(wiphy);

	/* In softmac mode, mac80211 has installed struct ieee80211_hw as the priv structure
	 * in wiphy, ours is inside that.
	 */
	hw = wiphy_to_ieee80211_hw(wiphy);
	return hw->priv;
}

/**
 * morse_wiphy_dot11ah_channel_to_5g - Map 802.11ah channel to 5GHz channel.
 */
static struct ieee80211_channel *morse_wiphy_dot11ah_channel_to_5g(struct wiphy *wiphy,
						const struct morse_dot11ah_channel *chan_s1g)
{
	struct ieee80211_supported_band *sband = wiphy->bands[NL80211_BAND_5GHZ];
	int i;

	if (WARN_ON(!chan_s1g))
		return NULL;

	for (i = 0; i < sband->n_channels; i++) {
		if (sband->channels[i].hw_value == chan_s1g->hw_value_map)
			return &sband->channels[i];
	}

	WARN(1, "5GHz channel mapping not defined");
	return NULL;
}

/**
 * morse_wiphy_get_5g_channel - Get 5GHz channel by its channel number.
 */
static struct ieee80211_channel *morse_wiphy_get_5g_channel(struct wiphy *wiphy, int chan_5g)
{
	struct ieee80211_supported_band *sband = wiphy->bands[NL80211_BAND_5GHZ];
	int i;

	for (i = 0; i < sband->n_channels; i++) {
		if (sband->channels[i].hw_value == chan_5g)
			return &sband->channels[i];
	}

	return NULL;
}

static int morse_ndev_open(struct net_device *dev)
{
	struct morse_vif *mors_vif = netdev_priv(dev);
	struct morse *mors = wiphy_priv(mors_vif->wdev.wiphy);
	int ret;

	/* Carrier state is initially off. It will be set on when a connection is established.
	 */
	netif_carrier_off(dev);

	mutex_lock(&mors->lock);

	ret = morse_cmd_set_country(mors, mors->country);
	if (ret == MORSE_RET_CMD_NOT_HANDLED)
		MORSE_WARN(mors, "firmware does not support setting country\n");
	else if (ret)
		goto out;

	ret = morse_cmd_add_if(mors, &mors_vif->id, dev->dev_addr, NL80211_IFTYPE_STATION);
	if (ret)
		goto out;

	mors->started = true;

out:
	mutex_unlock(&mors->lock);

	return ret;
}

static int morse_ndev_close(struct net_device *dev)
{
	struct morse_vif *mors_vif = netdev_priv(dev);
	struct morse *mors = wiphy_priv(mors_vif->wdev.wiphy);
	int ret;

	mutex_lock(&mors->lock);

	ret = morse_cmd_rm_if(mors, mors_vif->id);

	mors->started = false;

	mutex_unlock(&mors->lock);

	return ret;
}

static netdev_tx_t morse_ndev_data_tx(struct sk_buff *skb, struct net_device *dev)
{
	int ret;
	struct morse_vif *mors_vif = netdev_priv(dev);
	struct morse *mors = wiphy_priv(mors_vif->wdev.wiphy);
	struct morse_skbq *mq = mors->cfg->ops->skbq_tc_q_from_aci(mors, MORSE_ACI_BE);

	ret = morse_skbq_skb_tx(mq, &skb, NULL, MORSE_SKB_CHAN_WIPHY);
	if (ret < 0)
		goto tx_err;

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	return NETDEV_TX_OK;

tx_err:
	MORSE_ERR_RATELIMITED(mors, "%s failed with error [%d]\n", __func__, ret);
	dev->stats.tx_dropped++;
	dev->stats.tx_aborted_errors++;

	return NETDEV_TX_BUSY;
}

/** Network device operations vector table */
static const struct net_device_ops mors_netdev_ops = {
	.ndo_open = morse_ndev_open,
	.ndo_stop = morse_ndev_close,
	.ndo_start_xmit = morse_ndev_data_tx,
	.ndo_set_mac_address = eth_mac_addr,
	/*
	 * TBD
	 * Place holder of what we need to do. Do not remove
	 */
	/*
	 * .ndo_set_features       = morse_netdev_set_features,
	 * .ndo_set_rx_mode     = morse_netdev_set_multicast_list,
	 */
};

static void morse_netdev_init(struct net_device *dev, struct morse *mors)
{
	dev->netdev_ops = &mors_netdev_ops;
	dev->watchdog_timeo = 10;
	dev->needed_headroom = ETH_HLEN + sizeof(struct morse_buff_skb_header) +
			       mors->bus_ops->bulk_alignment +
			       mors->extra_tx_offset;
}

/** Ethernet Tool operations */
static const struct ethtool_ops mors_ethtool_ops = {
	/*
	 * TBD
	 * Place holder of what we need to do. Do not remove
	 */
	/*
	 * .get_drvinfo = morse_ethtool__get_drvinfo,
	 * .get_link = morse_ethtool_op_get_link,
	 * .get_strings = morse_ethtool_et_strings,
	 * .get_ethtool_stats = morse_ethtool_et_stats,
	 * .get_sset_count = morse_ethtool_et_sset_count,
	 */
};

static int morse_wiphy_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
{
	struct morse *mors = wiphy_priv(wiphy);
	u32 dwell_time_ms = 0;
	const u8 *ssid = NULL;
	size_t ssid_len = 0;
	int ret;

	/* We configured these limits in struct wiphy. */
	if (request->n_ssids > SCAN_MAX_SSIDS || request->ie_len > SCAN_EXTRA_IES_MAX_LEN) {
		MORSE_WARN_ON_ONCE(FEATURE_ID_DEFAULT, 1);
		return -EFAULT;
	}

	mutex_lock(&mors->lock);

	if (mors->scan_req) {
		ret = -EBUSY;
		goto out;
	}

	/* TODO: obey channels, mac_addr, mac_addr_mask, bssid, scan_width */
	/* TODO: apply a timeout to the scan operation on the driver side */

	if (request->n_ssids) {
		ssid = request->ssids[0].ssid;
		ssid_len = request->ssids[0].ssid_len;
	}
	if (request->duration)
		dwell_time_ms = MORSE_TU_TO_MS(request->duration);

	ret = morse_cmd_start_scan(mors, request->n_ssids, ssid, ssid_len, request->ie,
				   request->ie_len, dwell_time_ms);
	if (ret)
		goto out;

	mors->scan_req = request;

out:
	mutex_unlock(&mors->lock);

	return ret;
}

static void morse_wiphy_abort_scan(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct morse *mors = wiphy_priv(wiphy);
	int ret;

	mutex_lock(&mors->lock);

	if (!mors->scan_req)
		goto out;

	ret = morse_cmd_abort_scan(mors);
	if (ret)
		MORSE_ERR(mors, "failed to abort scan: %d\n", ret);

out:
	mutex_unlock(&mors->lock);
}

static int morse_wiphy_connect(struct wiphy *wiphy, struct net_device *netdev,
			       struct cfg80211_connect_params *sme)
{
	struct morse_vif *mors_vif = bound_mors_vif;
	struct morse *mors = wiphy_priv(wiphy);
	const u8 *sae_pwd = NULL;
	u8 sae_pwd_len = 0;
	int ret;

	mutex_lock(&mors->lock);

	if (test_bit(MORSE_SME_STATE_CONNECTING, &mors_vif->sme_state) ||
	    test_bit(MORSE_SME_STATE_CONNECTED, &mors_vif->sme_state)) {
		ret = -EBUSY;
		goto out;
	}

#if KERNEL_VERSION(5, 11, 0) <= MAC80211_VERSION_CODE
	switch (sme->crypto.sae_pwe) {
	case NL80211_SAE_PWE_UNSPECIFIED:
	case NL80211_SAE_PWE_HASH_TO_ELEMENT:
		break;
	case NL80211_SAE_PWE_HUNT_AND_PECK:
	case NL80211_SAE_PWE_BOTH:
	default:
		/* Only H2E (hash-to-element) is permitted in 802.11ah, hunt and peck
		 * is not supported.
		 */
		ret = -EOPNOTSUPP;
		goto out;
	}
#endif

	if (sme->auth_type == NL80211_AUTHTYPE_SAE) {
		/* SAE offload is mandatory for this driver: if SAE is selected then
		 * the SAE passphrase must also be given.
		 */
#if KERNEL_VERSION(5, 3, 0) <= MAC80211_VERSION_CODE
		if (!sme->crypto.sae_pwd || !sme->crypto.sae_pwd_len) {
			ret = -EINVAL;
			goto out;
		}
		sae_pwd = sme->crypto.sae_pwd;
		sae_pwd_len = sme->crypto.sae_pwd_len;
#else
		ret = -EOPNOTSUPP;
		goto out;
#endif
	}

	/* TODO: obey channel, bssid, bss_select */
	/* TODO: obey cipher suite selection */
	/* TODO: obey controlled port config */
	/* TODO: pass down IEs for association request, bg_scan_period */
	/* TODO: apply a timeout to the connect operation on the driver side */

	ret = morse_cmd_connect(mors, sme->ssid, sme->ssid_len, sme->auth_type, sae_pwd,
				sae_pwd_len);
	if (ret)
		goto out;

	set_bit(MORSE_SME_STATE_CONNECTING, &mors_vif->sme_state);

out:
	mutex_unlock(&mors->lock);

	return ret;
}

static int morse_wiphy_disconnect(struct wiphy *wiphy, struct net_device *ndev, u16 reason_code)
{
	struct morse_vif *mors_vif = bound_mors_vif;
	struct morse *mors = wiphy_priv(wiphy);
	int ret;

	mutex_lock(&mors->lock);

	if (!test_bit(MORSE_SME_STATE_CONNECTING, &mors_vif->sme_state) &&
	    !test_bit(MORSE_SME_STATE_CONNECTED, &mors_vif->sme_state)) {
		ret = -EINVAL;
		goto out;
	}

	ret = morse_cmd_disconnect(mors);

	if (test_and_clear_bit(MORSE_SME_STATE_CONNECTING, &mors_vif->sme_state)) {
		cfg80211_connect_timeout(ndev, /* bssid */ NULL,
					 /* req_ie */ NULL, /* req_ie_len */ 0,
					 GFP_KERNEL
#if KERNEL_VERSION(4, 11, 0) <= MAC80211_VERSION_CODE
					 , NL80211_TIMEOUT_UNSPECIFIED
#endif
		    );
	}

out:
	mutex_unlock(&mors->lock);

	return ret;
}

static int morse_wiphy_get_channel(struct wiphy *wiphy, struct wireless_dev *wdev,
#if KERNEL_VERSION(5, 19, 2) <= MAC80211_VERSION_CODE
			     unsigned int link_id,
#endif
			     struct cfg80211_chan_def *chandef)
{
	struct morse_vif *mors_vif = bound_mors_vif;
	struct morse *mors = wiphy_priv(wiphy);
	int ret;
	u32 op_chan_freq_hz;
	u8 op_bw_mhz;
	u8 pri_bw_mhz;
	u8 pri_1mhz_chan_idx;
	int op_chan_s1g, pri_chan_s1g;
	int op_chan_5g, pri_chan_5g;
	u32 op_freq_5g;
	enum nl80211_chan_width width_5g;

	mutex_lock(&mors->lock);

	/* Only fetch channel information from the chip when it's connected or connecting.
	 * For now, this is overly restrictive -- we could also fetch channel information
	 * when the chip is scanning or even idle. We just need to avoid sending a command
	 * to the chip while MHS is still booting up, because it will cause a command timeout.
	 * TODO: make the driver wait for MHS to boot and then relax this restriction.
	 */
	if (!test_bit(MORSE_SME_STATE_CONNECTING, &mors_vif->sme_state) &&
	    !test_bit(MORSE_SME_STATE_CONNECTED, &mors_vif->sme_state)) {
		ret = -ENODEV;
		goto out;
	}

	ret = morse_cmd_get_current_channel(mors, &op_chan_freq_hz, &pri_1mhz_chan_idx, &op_bw_mhz,
					    &pri_bw_mhz);
	if (ret)
		goto out;

	/* Look up S1G channel numbers based on the channel info we received from the chip.
	 */
	op_chan_s1g = morse_dot11ah_freq_khz_bw_mhz_to_chan(HZ_TO_KHZ(op_chan_freq_hz), op_bw_mhz);
	pri_chan_s1g = morse_dot11ah_calc_prim_s1g_chan(op_bw_mhz, 1, op_chan_s1g,
							pri_1mhz_chan_idx);

	/* Map to 5GHz channel info.
	 */
	op_chan_5g = morse_dot11ah_s1g_chan_to_5g_chan(op_chan_s1g);
	pri_chan_5g = morse_dot11ah_s1g_op_chan_pri_chan_to_5g(op_chan_s1g, pri_chan_s1g);
	op_freq_5g = ieee80211_channel_to_frequency(op_chan_5g, NL80211_BAND_5GHZ);
	switch (op_bw_mhz) {
	case 1:
		width_5g = NL80211_CHAN_WIDTH_20_NOHT;
		break;
	case 2:
		width_5g = NL80211_CHAN_WIDTH_40;
		break;
	case 4:
		width_5g = NL80211_CHAN_WIDTH_80;
		break;
	case 8:
		width_5g = NL80211_CHAN_WIDTH_160;
		break;
	default:
		width_5g = WARN_ON(NL80211_CHAN_WIDTH_20_NOHT);
	}

	chandef->chan = morse_wiphy_get_5g_channel(wiphy, pri_chan_5g);
	chandef->center_freq1 = op_freq_5g;
	chandef->width = width_5g;
	chandef->center_freq2 = 0;

out:
	mutex_unlock(&mors->lock);

	return ret;
}

static int morse_wiphy_get_station(struct wiphy *wiphy, struct net_device *dev, const u8 *mac,
				   struct station_info *sinfo)
{
	struct morse_vif *mors_vif = bound_mors_vif;
	struct morse *mors = wiphy_priv(wiphy);
	int ret;

	sinfo->filled = 0;

	mutex_lock(&mors->lock);

	if (!test_bit(MORSE_SME_STATE_CONNECTED, &mors_vif->sme_state)) {
		ret = -ENODEV;
		goto out;
	}

	ret = morse_cmd_get_connection_state(mors,
					     &sinfo->signal,
					     &sinfo->connected_time,
					     &sinfo->bss_param.dtim_period,
					     &sinfo->bss_param.beacon_interval);
	if (ret == MORSE_RET_CMD_NOT_HANDLED) {
		MORSE_WARN(mors, "firmware does not support fetching connection state\n");
		ret = 0;
		goto out;
	} else if (ret) {
		goto out;
	}

	/* Short slot time is not relevant for 802.11ah, but mac80211 reports this flag
	 * for 5GHz bands, which we are pretending to be. So report it here too for consistency.
	 */
	sinfo->bss_param.flags = BSS_PARAM_FLAGS_SHORT_SLOT_TIME;

	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL) |
			 BIT_ULL(NL80211_STA_INFO_CONNECTED_TIME) |
			 BIT_ULL(NL80211_STA_INFO_BSS_PARAM);

out:
	mutex_unlock(&mors->lock);

	return ret;
}

static int morse_wiphy_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	struct morse *mors = wiphy_priv(wiphy);
	int ret = 0;

	if (changed & WIPHY_PARAM_RTS_THRESHOLD) {
		/* cfg80211 uses (u32)-1 to indicate RTS/CTS disabled, whereas the chip uses 0 to
		 * indicate RTS/CTS disabled.
		 */
		if (wiphy->rts_threshold != U32_MAX) {
			MORSE_DBG(mors, "setting RTS threshold %u\n", wiphy->rts_threshold);
			ret = morse_cmd_set_rts_threshold(mors, wiphy->rts_threshold);
		} else {
			MORSE_DBG(mors, "disabling RTS\n");
			ret = morse_cmd_set_rts_threshold(mors, 0);
		}
		if (ret)
			goto out;
	}
	if (changed & WIPHY_PARAM_FRAG_THRESHOLD) {
		MORSE_DBG(mors, "setting fragmentation threshold %u\n", wiphy->frag_threshold);
		ret = morse_cmd_set_frag_threshold(mors, wiphy->frag_threshold);
		if (ret)
			goto out;
	}

out:
	return ret;
}

static int
morse_wiphy_set_power_mgmt(struct wiphy *wiphy, struct net_device *dev, bool enabled, int timeout)
{
	/* It doesn't make sense to disable powersave offload with fullmac firmware.
	 */
	const bool enable_dynamic_ps_offload = enabled;
	struct morse *mors = wiphy_priv(wiphy);
	int ret;

	if (!morse_mac_ps_enabled(mors))
		return -EOPNOTSUPP;

	mutex_lock(&mors->lock);

	if (mors->config_ps == enabled) {
		ret = 0;
		goto out;
	}

	ret = morse_cmd_set_ps(mors, enabled, enable_dynamic_ps_offload);
	if (ret)
		goto out;

	mors->config_ps = enabled;
	ret = 0;

out:
	mutex_unlock(&mors->lock);
	return ret;
}

static struct cfg80211_ops morse_wiphy_cfg80211_ops = {
	.scan = morse_wiphy_scan,
	.abort_scan = morse_wiphy_abort_scan,
	.connect = morse_wiphy_connect,
	.disconnect = morse_wiphy_disconnect,
	.get_channel = morse_wiphy_get_channel,
	.get_station = morse_wiphy_get_station,
	.set_wiphy_params = morse_wiphy_set_wiphy_params,
	.set_power_mgmt = morse_wiphy_set_power_mgmt,
	/*
	 * TBD
	 * Place holder of what we need to do. Do not remove
	 */
	/*
	 * .add_virtual_intf = morse_wiphy_add_iface,
	 * .del_virtual_intf = morse_wiphy_del_iface,
	 * .change_virtual_intf = mors_wiphy_change_iface,
	 * .join_ibss = mors_wiphy_join_ibss,
	 * .leave_ibss = mors_wiphy_leave_ibss,
	 * .dump_station = mors_wiphy_dump_station,
	 * .set_tx_power = mors_wiphy_set_tx_power,
	 * .get_tx_power = mors_wiphy_get_tx_power,
	 * .add_key = mors_wiphy_add_key,
	 * .del_key = mors_wiphy_del_key,
	 * .get_key = mors_wiphy_get_key,
	 * .set_default_key = mors_wiphy_config_default_key,
	 * .set_default_mgmt_key = mors_wiphy_config_default_mgmt_key,
	 * .suspend = mors_wiphy_suspend,
	 * .resume = mors_wiphy_resume,
	 * .set_pmksa = mors_wiphy_set_pmksa,
	 * .del_pmksa = mors_wiphy_del_pmksa,
	 * .flush_pmksa = mors_wiphy_flush_pmksa,
	 * .start_ap = mors_wiphy_start_ap,
	 * .stop_ap = mors_wiphy_stop_ap,
	 * .change_beacon = mors_wiphy_change_beacon,
	 * .del_station = mors_wiphy_del_station,
	 * .change_station = mors_wiphy_change_station,
	 * .sched_scan_start = mors_wiphy_sched_scan_start,
	 * .sched_scan_stop = mors_wiphy_sched_scan_stop,
	 * .update_mgmt_frame_registrations =
	 mors_wiphy_update_mgmt_frame_registrations,
	 * .mgmt_tx = mors_wiphy_mgmt_tx,
	 * .remain_on_channel = mors_p2p_remain_on_channel,
	 * .cancel_remain_on_channel = mors_wiphy_cancel_remain_on_channel,
	 * .start_p2p_device = mors_p2p_start_device,
	 * .stop_p2p_device = mors_p2p_stop_device,
	 * .crit_proto_start = mors_wiphy_crit_proto_start,
	 * .crit_proto_stop = mors_wiphy_crit_proto_stop,
	 * .tdls_oper = mors_wiphy_tdls_oper,
	 * .update_connect_params = mors_wiphy_update_conn_params,
	 * .set_pmk = mors_wiphy_set_pmk,
	 * .del_pmk = mors_wiphy_del_pmk,
	 */
};

/**
 * morse_wiphy_create() -  Create wiphy device
 * @priv_size: extra size per structure to allocate
 * @dev: Bus device structure
 *
 * Allocate memory for wiphy device and do basic initialisation.
 *
 * Return: morse device struct, else NULL.
 */
struct morse *morse_wiphy_create(size_t priv_size, struct device *dev)
{
	struct wiphy *wiphy;
	struct morse *mors = NULL;

	wiphy = wiphy_new(&morse_wiphy_cfg80211_ops, sizeof(*mors) + priv_size);
	if (!wiphy) {
		dev_err(dev, "wiphy_new failed\r\n");
		return NULL;
	}

	wiphy->max_scan_ssids = SCAN_MAX_SSIDS;
	wiphy->max_scan_ie_len = SCAN_EXTRA_IES_MAX_LEN;
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wiphy->bands[NL80211_BAND_5GHZ] = &mors_band_5ghz;
	wiphy->bands[NL80211_BAND_2GHZ] = NULL;
	wiphy->bands[NL80211_BAND_60GHZ] = NULL;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	set_wiphy_dev(wiphy, dev);

	wiphy->privid = morse_wiphy_privid;
	mors = wiphy_priv(wiphy);
	mors->wiphy = wiphy;

	return mors;
}

static struct wireless_dev *morse_wiphy_interface_add(struct morse *mors, const char *name,
						      unsigned char name_assign_type,
						      enum nl80211_iftype type)
{
	struct net_device *ndev;
	struct morse_vif *mors_vif;

	ndev = alloc_netdev(sizeof(*mors_vif), name, name_assign_type, ether_setup);
	if (!ndev)
		return NULL;

	mors_vif = netdev_priv(ndev);
	mors_vif->wdev.wiphy = mors->wiphy;
	mors_vif->ndev = ndev;
	mors_vif->wdev.netdev = ndev;
	mors_vif->wdev.iftype = type;
	ndev->ieee80211_ptr = &mors_vif->wdev;
	SET_NETDEV_DEV(ndev, wiphy_dev(mors_vif->wdev.wiphy));

	bound_mors_vif = mors_vif;

	memcpy(ndev->perm_addr, mors->macaddr, ETH_ALEN);
#if KERNEL_VERSION(5, 17, 0) > LINUX_VERSION_CODE
	memcpy(ndev->dev_addr, mors->macaddr, ETH_ALEN);
#else
	dev_addr_set(ndev, mors->macaddr);
#endif

	morse_netdev_init(ndev, mors);
	netdev_set_default_ethtool_ops(ndev, &mors_ethtool_ops);

	if (register_netdevice(ndev))
		goto err;

	return &mors_vif->wdev;

err:
	free_netdev(ndev);
	return NULL;
}

/**
 * morse_wiphy_init() -  Init wiphy device
 * @morse: morse device instance
 *
 * Initilise wiphy device
 *
 * Return: 0 success, else error.
 */
int morse_wiphy_init(struct morse *mors)
{
	struct wiphy *wiphy = mors->wiphy;
	/* TODO: ask the chip instead of hardcoding the list here. */
	static const u32 morse_wiphy_cipher_suites[] = {
		WLAN_CIPHER_SUITE_CCMP,
		WLAN_CIPHER_SUITE_AES_CMAC,
	};

	memcpy(wiphy->perm_addr, mors->macaddr, ETH_ALEN);

#if KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_SAE_OFFLOAD);
#endif

	wiphy->cipher_suites = morse_wiphy_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(morse_wiphy_cipher_suites);

	mors->extra_tx_offset = EXTRA_TX_OFFSET;

	return 0;
}

/**
 * morse_wiphy_register() -  Register wiphy device
 * @morse: morse device instance
 *
 * Register wiphy device
 *
 * Return: 0 success, else error.
 */
int morse_wiphy_register(struct morse *mors)
{
	int ret;
	struct wiphy *wiphy = mors->wiphy;
	struct device *dev = wiphy_dev(wiphy);
	struct wireless_dev *wdev;

	ret = wiphy_register(wiphy);
	if (ret < 0)
		dev_info(dev, "wiphy_register fail\r\n");
	else
		dev_info(dev, "wiphy_register success %d\r\n", ret);

	rtnl_lock();

	/* Add an initial station interface */
	wdev = morse_wiphy_interface_add(mors, "wlan%d", NET_NAME_ENUM, NL80211_IFTYPE_STATION);

	rtnl_unlock();

	return ret;
}

void morse_wiphy_stop(struct morse *mors)
{
	struct morse_vif *mors_vif = bound_mors_vif;

	netif_stop_queue(mors_vif->ndev);
}

/**
 * morse_wiphy_cleanup() -  Clean up cfg80211 state on chip shutdown.
 * @morse: morse device instance
 */
static void morse_wiphy_cleanup(struct morse *mors)
{
	const bool disconnect_locally_generated = true;
	struct morse_vif *mors_vif = bound_mors_vif;
	struct net_device *ndev = mors_vif->ndev;

	lockdep_assert_held(&mors->lock);

	netif_carrier_off(ndev);

	if (test_and_clear_bit(MORSE_SME_STATE_CONNECTED, &mors_vif->sme_state)) {
		cfg80211_disconnected(ndev, WLAN_REASON_UNSPECIFIED,
				      NULL, 0, disconnect_locally_generated, GFP_KERNEL);
		morse_ps_disable(mors);
	}

	if (test_and_clear_bit(MORSE_SME_STATE_CONNECTING, &mors_vif->sme_state))
		cfg80211_connect_timeout(ndev, NULL, NULL, 0, GFP_KERNEL
#if KERNEL_VERSION(4, 11, 0) <= MAC80211_VERSION_CODE
					 , NL80211_TIMEOUT_UNSPECIFIED
#endif
					);

	if (mors->scan_req) {
		struct cfg80211_scan_info info = {
			.aborted = true,
		};
		cfg80211_scan_done(mors->scan_req, &info);
		mors->scan_req = NULL;
	}
}

void morse_wiphy_restarted(struct morse *mors)
{
	struct morse_vif *mors_vif = bound_mors_vif;
	struct net_device *ndev = mors_vif->ndev;
	int ret;

	lockdep_assert_held(&mors->lock);

	morse_wiphy_cleanup(mors);

	mors->started = true;

	ret = morse_cmd_set_country(mors, mors->country);
	if (ret == MORSE_RET_CMD_NOT_HANDLED)
		MORSE_WARN(mors, "firmware does not support setting country\n");
	else if (ret)
		MORSE_ERR(mors, "error setting country after restart: %d\n", ret);

	/* Add back the fixed STA VIF, originally added in morse_ndev_open(). */
	ret = morse_cmd_add_if(mors, &mors_vif->id, ndev->dev_addr, NL80211_IFTYPE_STATION);
	if (ret)
		MORSE_ERR(mors, "error adding interface to chip after restart: %d\n", ret);

	netif_wake_queue(mors_vif->ndev);
}

/**
 * morse_wiphy_deinit() -  Deinit wiphy device
 * @morse: morse device instance
 *
 * Deinitilise wiphy device
 *
 * Return: None.
 */
void morse_wiphy_deinit(struct morse *mors)
{
	struct morse_vif *mors_vif = bound_mors_vif;
	struct wiphy *wiphy = mors->wiphy;

	mutex_lock(&mors->lock);
	morse_wiphy_cleanup(mors);
	mutex_unlock(&mors->lock);

	netif_stop_queue(mors_vif->ndev);

	unregister_netdev(mors_vif->ndev);

	if (wiphy->registered)
		wiphy_unregister(wiphy);

	free_netdev(mors_vif->ndev);
	bound_mors_vif = NULL;
}

/**
 * morse_wiphy_destroy() -  Destroy wiphy device
 * @morse: morse device instance
 *
 * Free wiphy device
 *
 * Return: None.
 */
void morse_wiphy_destroy(struct morse *mors)
{
	struct wiphy *wiphy = mors->wiphy;

	WARN(!wiphy, "%s called with null wiphy", __func__);
	if (!wiphy)
		return;

	wiphy_free(wiphy);
}

void morse_wiphy_rx(struct morse *mors, struct sk_buff *skb)
{
	struct morse_vif *mors_vif = bound_mors_vif;
	struct net_device *ndev = mors_vif->ndev;

	skb->dev = ndev;
	skb->protocol = eth_type_trans(skb, ndev);
	ndev->stats.rx_packets++;
	ndev->stats.rx_bytes += skb->len;
#if KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE
	netif_rx(skb);
#else
	netif_rx_ni(skb);
#endif
}

/* Caller must kfree() the returned value on success. */
static u8 *morse_wiphy_translate_prob_resp_ies(u8 *ies_s1g, size_t ies_s1g_len,
					       int *length_11n_out)
{
	struct dot11ah_ies_mask *ies_mask = NULL;
	u8 *ies_11n = NULL;
	int length_11n;
	int ret;

	ies_mask = morse_dot11ah_ies_mask_alloc();
	if (!ies_mask) {
		ret = -ENOMEM;
		goto err;
	}

	ret = morse_dot11ah_parse_ies(ies_s1g, ies_s1g_len, ies_mask);
	if (ret)
		goto err;

	length_11n = morse_dot11ah_s1g_to_probe_resp_ies_size(ies_mask);
	ies_11n = kzalloc(length_11n, GFP_KERNEL);
	if (!ies_11n) {
		ret = -ENOMEM;
		goto err;
	}
	morse_dot11ah_s1g_to_probe_resp_ies(ies_11n, length_11n, ies_mask);

	kfree(ies_mask);
	*length_11n_out = length_11n;
	return ies_11n;

err:
	kfree(ies_mask);
	kfree(ies_11n);
	return ERR_PTR(ret);
}

int morse_wiphy_scan_result(struct morse *mors, struct morse_evt_scan_result *result)
{
	struct wiphy *wiphy = mors->wiphy;
	const struct morse_dot11ah_channel *chan_s1g;
	enum cfg80211_bss_frame_type ftype;
	struct ieee80211_channel *chan_5g;
	struct cfg80211_bss *bss;
	s16 signal_from_chip;
	u8 *ies_11n = NULL;
	int ies_11n_len = 0;
	s32 signal;
	int ret;

	chan_s1g = morse_dot11ah_s1g_freq_to_s1g(le32_to_cpu(result->channel_freq_hz),
						 result->bw_mhz);
	if (!chan_s1g) {
		MORSE_ERR(mors, "scan result channel is invalid: freq %uHz, bw %uMhz\n",
			  le32_to_cpu(result->channel_freq_hz), result->bw_mhz);
		return -EINVAL;
	}

	chan_5g = morse_wiphy_dot11ah_channel_to_5g(wiphy, chan_s1g);

	switch (result->frame_type) {
	case SCAN_RESULT_FRAME_TYPE_BEACON:
		ftype = CFG80211_BSS_FTYPE_BEACON;
		break;
	case SCAN_RESULT_FRAME_TYPE_PROBE_RESPONSE:
		ftype = CFG80211_BSS_FTYPE_PRESP;
		break;
	case SCAN_RESULT_FRAME_TYPE_UNKNOWN:
	default:
		ftype = CFG80211_BSS_FTYPE_UNKNOWN;
	}

	/* The chip gives us a signal indication in dBm as int16_t. */
	signal_from_chip = (s16)le16_to_cpu(result->rssi);

	/* cfg80211 wants the signal in mBm, even though we declare ourselves as SIGNAL_DBM. */
	signal = DBM_TO_MBM((s32)signal_from_chip);

	ies_11n = morse_wiphy_translate_prob_resp_ies(result->ies,
						      le16_to_cpu(result->ies_len), &ies_11n_len);
	if (IS_ERR(ies_11n)) {
		MORSE_INFO_RATELIMITED(mors, "invalid probe response IEs from BSS %pM\n",
				       result->bssid);
		ies_11n = NULL;
	}

	bss = cfg80211_inform_bss(wiphy, chan_5g, ftype, result->bssid,
				  le64_to_cpu(result->tsf), le16_to_cpu(result->capability_info),
				  le16_to_cpu(result->beacon_interval),
				  ies_11n, ies_11n_len, signal, GFP_KERNEL);
	if (bss) {
		MORSE_DBG(mors, "scan added BSS %pM\n", result->bssid);
		cfg80211_put_bss(wiphy, bss);
		ret = 0;
	} else {
		MORSE_ERR(mors, "%s failed to add BSS\n", __func__);
		ret = -ENOMEM;
	}

	kfree(ies_11n);
	return ret;
}

void morse_wiphy_scan_done(struct morse *mors, bool aborted)
{
	struct cfg80211_scan_info info = { 0 };

	mutex_lock(&mors->lock);

	if (!mors->scan_req) {
		MORSE_ERR(mors, "received scan done event but no scan was in progress\n");
		goto exit;
	}

	info.aborted = aborted;
	cfg80211_scan_done(mors->scan_req, &info);

	mors->scan_req = NULL;

exit:
	mutex_unlock(&mors->lock);
}

void morse_wiphy_connected(struct morse *mors, const u8 *bssid)
{
	struct morse_vif *mors_vif = bound_mors_vif;
	struct net_device *ndev = mors_vif->ndev;

	mutex_lock(&mors->lock);

	MORSE_INFO(mors, "connected to BSS %pM\n", bssid);

	WARN_ON(!test_and_clear_bit(MORSE_SME_STATE_CONNECTING, &mors_vif->sme_state));
	set_bit(MORSE_SME_STATE_CONNECTED, &mors_vif->sme_state);

	netif_carrier_on(ndev);

	cfg80211_connect_bss(ndev, bssid, NULL, NULL, 0, NULL, 0,
			     WLAN_STATUS_SUCCESS, GFP_KERNEL
#if KERNEL_VERSION(4, 11, 0) <= MAC80211_VERSION_CODE
			     , /* timeout_reason */ 0
#endif
	    );

/* TODO: this should only be called if we connected with SAE (or OWE?) */
#if KERNEL_VERSION(6, 2, 0) <= LINUX_VERSION_CODE
	cfg80211_port_authorized(ndev, bssid, NULL, 0, GFP_KERNEL);
#elif KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
	cfg80211_port_authorized(ndev, bssid, GFP_KERNEL);
#endif

	morse_ps_enable(mors);

	mutex_unlock(&mors->lock);
}

void morse_wiphy_disconnected(struct morse *mors)
{
	struct morse_vif *mors_vif = bound_mors_vif;
	struct net_device *ndev = mors_vif->ndev;

	mutex_lock(&mors->lock);

	if (WARN_ON(!test_and_clear_bit(MORSE_SME_STATE_CONNECTED, &mors_vif->sme_state)))
		goto exit;

	MORSE_INFO(mors, "disconnected\n");

	morse_ps_disable(mors);

	netif_carrier_off(ndev);

	/* TODO: get reason, deassoc/deauth IEs */
	cfg80211_disconnected(ndev, WLAN_REASON_UNSPECIFIED,
			      /* ie */ NULL, /* ie_len */ 0,
			      /* locally_generated */ false, GFP_KERNEL);

exit:
	mutex_unlock(&mors->lock);
}
