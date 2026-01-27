/** Copyright 2023 Morse Micro*/
#include <linux/bitfield.h>
#include <linux/kernel.h>
#include "morse.h"
#include "debug.h"
#include "mac.h"
#include "command.h"
#include "misc.h"
#include "hw_scan.h"

/* These values were derived from mac80211 scan.c */
/** Default time to dwell on a scan channel */
#define MORSE_HWSCAN_DEFAULT_DWELL_TIME_MS (30)
/** Default time to dwell on a scan channel for passive scan */
#define MORSE_HWSCAN_DEFAULT_PASSIVE_DWELL_TIME_MS (110)
/** Default time to dwell on home channel, in between scan channels */
#define MORSE_HWSCAN_DEFAULT_DWELL_ON_HOME_MS (200)
/** Typical time it takes to send the probe */
#define MORSE_HWSCAN_PROBE_DELAY_MS (30)
/** A margin to account for event/command processing */
#define MORSE_HWSCAN_TIMEOUT_OVERHEAD_MS (2000)

/**
 * Scan TLV header
 */
struct hw_scan_tlv_hdr {
	__le16 tag;
	__le16 len;
} __packed;

/**
 * Tags for scan tlv header
 */
enum hw_scan_tlv_tag {
	HW_SCAN_TLV_TAG_PAD		= 0,
	HW_SCAN_TLV_TAG_PROBE_REQ	= 1,
	HW_SCAN_TLV_TAG_CHAN_LIST	= 2,
	HW_SCAN_TLV_TAG_POWER_LIST	= 3,
	HW_SCAN_TLV_TAG_DWELL_ON_HOME	= 4,
};

/** Scan channel frequency mask */
#define HW_SCAN_CH_LIST_FREQ_KHZ	GENMASK(19, 0)
/**
 * Scan channel bandwidth mask.
 * Encoded as: 0 = 1mhz, 1 = 2mhz, 2 = 4mhz, 3 = 8mhz
 */
#define HW_SCAN_CH_LIST_OP_BW		GENMASK(21, 20)
/**
 * Scan channel primary channel width.
 * Encoded as: 0 = 1mhz, 1 = 2mhz
 */
#define HW_SCAN_CH_LIST_PRIM_CH_WIDTH	BIT(22)
/** Scan channel primary channel index mask */
#define HW_SCAN_CH_LIST_PRIM_CH_IDX	GENMASK(25, 23)
/**
 * Index into power_list for tx power of channel
 */
#define HW_SCAN_CH_LIST_PWR_LIST_IDX	GENMASK(31, 26)

#define MORSE_HWSCAN_DBG(_m, _f, _a...)		morse_dbg(FEATURE_ID_HWSCAN, _m, _f, ##_a)
#define MORSE_HWSCAN_INFO(_m, _f, _a...)	morse_info(FEATURE_ID_HWSCAN, _m, _f, ##_a)
#define MORSE_HWSCAN_WARN(_m, _f, _a...)	morse_warn(FEATURE_ID_HWSCAN, _m, _f, ##_a)
#define MORSE_HWSCAN_ERR(_m, _f, _a...)		morse_err(FEATURE_ID_HWSCAN, _m, _f, ##_a)

/**
 * HW scan TLV for scan channels.
 * Since we can pack a whole channel into just 4 bytes, all US channels (48) only takes
 * (48 * 4) = 192 bytes
 */
struct hw_scan_tlv_channel_list {
	struct hw_scan_tlv_hdr hdr;
	__le32 channels[];
} __packed;

/**
 * HW scan TLV for list of powers matching to channels.
 * The scan channels will have an index into this array which corresponds to the power for the
 * channel.
 */
struct hw_scan_tlv_power_list {
	struct hw_scan_tlv_hdr hdr;
	/**
	 * Array of possible TX powers for channel list.
	 * TX power for a particular channel is:
	 * power_tlv.tx_power_qdbm[MASK_GET(channel_tlv.channels[i], HW_SCAN_CH_LIST_PWR_LIST_IDX)]
	 */
	s32 tx_power_qdbm[];
} __packed;

/**
 * HW scan TLV for probe request template
 */
struct hw_scan_tlv_probe_req {
	struct hw_scan_tlv_hdr hdr;
	/** probe request frame template (including ssids) */
	u8 buf[];
} __packed;

/**
 * HW scan TLV for returning to home channel and dwelling in between scan channels
 */
struct hw_scan_tlv_dwell_on_home {
	struct hw_scan_tlv_hdr hdr;
	/** Time to dwell on home between scan channels */
	__le32 home_dwell_time_ms;
} __packed;

/**
 * morse_hw_scan_pack_tlv_hdr - Generate a TLV header from a given tag and length
 *
 * @tag: Tag to pack
 * @len: Length to pack
 * Return: packed TLV header
 */
static inline struct hw_scan_tlv_hdr morse_hw_scan_pack_tlv_hdr(u16 tag, u16 len)
{
	struct hw_scan_tlv_hdr hdr = {
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(len)
	};
	return hdr;
}

bool hw_scan_is_supported(struct morse *mors)
{
	return (mors->enable_hw_scan && (mors->firmware_flags & MORSE_FW_FLAGS_SUPPORT_HW_SCAN));
}

bool hw_scan_saved_config_has_ssid(struct morse *mors)
{
	struct morse_hw_scan_params *params = mors->hw_scan.params;

	lockdep_assert_held(&mors->lock);

	return params->has_directed_ssid;
}

bool hw_scan_is_idle(struct morse *mors)
{
	struct morse_hw_scan *hw_scan = &mors->hw_scan;

	lockdep_assert_held(&mors->lock);

	return (hw_scan->state == HW_SCAN_STATE_IDLE);
}

/**
 * morse_hw_scan_pack_channel - Pack a channel into a u32 to add to the HW scan channel TLV
 *
 * @chan: dot11ah channel to pack
 * @pwr_idx: Index into power TLV for this channel
 * Return: packed channel
 */
static __le32 morse_hw_scan_pack_channel(const struct morse_dot11ah_channel *chan,
					 u8 pwr_idx)
{
	u32 packed_channel;
	u32 freq_khz = morse_dot11ah_channel_to_freq_khz(chan->ch.hw_value);
	u8 op_bw_mhz = ch_flag_to_chan_bw(chan->ch.flags);
	/* It is expected that the steps in @ref hw_scan_initalise_channel_and_power_lists will
	 * deconstruct any non-primary channels into their primary variants.
	 */
	u8 prim_bw_mhz = min_t(u8, op_bw_mhz, 2);
	u8 prim_ch_idx = 0;

	packed_channel = BMSET(freq_khz, HW_SCAN_CH_LIST_FREQ_KHZ) |
		BMSET(morse_ratecode_bw_mhz_to_bw_index(op_bw_mhz), HW_SCAN_CH_LIST_OP_BW) |
		BMSET(morse_ratecode_bw_mhz_to_bw_index(prim_bw_mhz),
				HW_SCAN_CH_LIST_PRIM_CH_WIDTH) |
		BMSET(prim_ch_idx, HW_SCAN_CH_LIST_PRIM_CH_IDX) |
		BMSET(pwr_idx, HW_SCAN_CH_LIST_PWR_LIST_IDX);

	return cpu_to_le32(packed_channel);
}

/**
 * hw_scan_add_channel_list_tlv - Add channel list TLV to a buffer
 *
 * @buf: Buffer to add the TLVs to
 * @params: HW scan parameters
 * Return: pointer to end of the inserted channel list TLV
 */
static u8 *hw_scan_add_channel_list_tlv(u8 *buf, struct morse_hw_scan_params *params)
{
	int i;
	struct hw_scan_tlv_channel_list *ch_list = (struct hw_scan_tlv_channel_list *)buf;
	struct morse *mors = params->hw->priv;

	ch_list->hdr = morse_hw_scan_pack_tlv_hdr(HW_SCAN_TLV_TAG_CHAN_LIST,
		params->num_chans * sizeof(ch_list->channels[0]));

	MORSE_HWSCAN_DBG(mors, "packing channel list (len: %d)\n", ch_list->hdr.len);

	for (i = 0; i < params->num_chans; i++) {
		const struct morse_dot11ah_channel *chan = params->channels[i].channel;

		ch_list->channels[i] = morse_hw_scan_pack_channel(chan,
								  params->channels[i].power_idx);

		MORSE_HWSCAN_DBG(mors, "[%d] : %08x (freq: %u khz, bw: %d, pwr_idx: %d)\n", i,
			ch_list->channels[i],
			morse_dot11ah_channel_to_freq_khz(chan->ch.hw_value),
			morse_ratecode_bw_index_to_s1g_bw_mhz(BMGET(ch_list->channels[i],
					HW_SCAN_CH_LIST_OP_BW)),
			params->channels[i].power_idx);
	}
	return (u8 *)&ch_list->channels[i];
}

/**
 * hw_scan_add_power_list_tlv - Add power list TLV to a buffer
 *
 * @buf: Buffer to add the TLVs to
 * @params: HW scan parameters
 * Return: pointer to end of the inserted power list TLV
 */
static u8 *hw_scan_add_power_list_tlv(u8 *buf, struct morse_hw_scan_params *params)
{
	int i;
	struct hw_scan_tlv_power_list *pwr_list = (struct hw_scan_tlv_power_list *)buf;
	size_t size = sizeof(pwr_list->tx_power_qdbm[0]) * params->n_powers;
	struct morse *mors = params->hw->priv;

	pwr_list->hdr = morse_hw_scan_pack_tlv_hdr(HW_SCAN_TLV_TAG_POWER_LIST, size);
	MORSE_HWSCAN_DBG(mors, "packing power list (len: %d)\n", pwr_list->hdr.len);

	for (i = 0; i < params->n_powers; i++) {
		pwr_list->tx_power_qdbm[i] = cpu_to_le32(params->powers_qdbm[i]);
		MORSE_HWSCAN_DBG(mors, "[%d] : %d qdBm (%d dBm)\n", i,
				params->powers_qdbm[i], QDBM_TO_DBM(params->powers_qdbm[i]));
	}

	return (u8 *)&pwr_list->tx_power_qdbm[i];
}

/**
 * hw_scan_add_probe_req_tlv - Add probe request TLV to a buffer
 *
 * @buf: Buffer to add the TLVs to
 * @params: HW scan parameters
 * Return: pointer to end of the inserted probe request TLV
 */
static u8 *hw_scan_add_probe_req_tlv(u8 *buf, struct morse_hw_scan_params *params)
{
	struct sk_buff *skb = params->probe_req;
	struct hw_scan_tlv_probe_req *probe_req = (struct hw_scan_tlv_probe_req *)buf;
	struct morse *mors = params->hw->priv;

	probe_req->hdr = morse_hw_scan_pack_tlv_hdr(HW_SCAN_TLV_TAG_PROBE_REQ, skb->len);

	MORSE_HWSCAN_DBG(mors, "packing probe (len: %d)\n", probe_req->hdr.len);

	memcpy(probe_req->buf, skb->data, skb->len);

	return buf + sizeof(*probe_req) + skb->len;
}

/**
 * hw_scan_add_dwell_on_home_tlv() - Add TLV to specify the time to dwell on the home channel
 *					in between scans
 *
 * @buf: Buffer to add the TLV to
 * @params: HW scan parameters
 * Return: pointer to the end of the inserted TLV
 */
static u8 *hw_scan_add_dwell_on_home_tlv(u8 *buf, struct morse_hw_scan_params *params)
{
	struct hw_scan_tlv_dwell_on_home *dwell = (struct hw_scan_tlv_dwell_on_home *)buf;
	struct morse *mors = params->hw->priv;

	dwell->hdr = morse_hw_scan_pack_tlv_hdr(HW_SCAN_TLV_TAG_DWELL_ON_HOME,
		sizeof(*dwell) - sizeof(dwell->hdr));

	MORSE_HWSCAN_DBG(mors, "packing dwell on home (len: %d)\n", dwell->hdr.len);

	dwell->home_dwell_time_ms = cpu_to_le32(params->dwell_on_home_ms);

	return buf + sizeof(*dwell);
}

/**
 * hw_scan_initialise_probe_req - Initialise probe request template for HW scan
 *
 * @params: HW scan params
 * Return: 0 if success, otherwise error code
 */
static int hw_scan_initialise_probe_req(struct morse_hw_scan_params *params,
		struct ieee80211_scan_request *scan_req)
{
	int ret;
	struct morse *mors = params->hw->priv;
	struct sk_buff *probe_req;
	struct cfg80211_scan_request *req = &scan_req->req;
	struct ieee80211_scan_ies *ies = &scan_req->ies;
	u8 ssid_len = 0;
	u8 *ssid = NULL;
	u8 *pos;
	int tx_bw_mhz;
	struct ieee80211_tx_info *info;
	u16 ies_len = ies->len[NL80211_BAND_5GHZ] + ies->common_ie_len;

	if (req->n_ssids) {
		if (req->n_ssids > 1)
			MORSE_HWSCAN_WARN(mors,
				"Multiple SSIDs found when only one supported. Using the first only.\n");
		ssid_len = req->ssids[0].ssid_len;
		ssid = req->ssids[0].ssid;
	}

	probe_req = ieee80211_probereq_get(params->hw, params->vif->addr, ssid, ssid_len, ies_len);
	if (!probe_req)
		return -ENOMEM;

	pos = skb_put(probe_req, ies_len);

	memcpy(pos, ies->common_ies, ies->common_ie_len);
	pos += ies->common_ie_len;

	memcpy(pos, ies->ies[NL80211_BAND_5GHZ], ies->len[NL80211_BAND_5GHZ]);

	info = IEEE80211_SKB_CB(probe_req);
	info->control.vif = params->vif;

	ret = morse_mac_pkt_to_s1g(mors, NULL, &probe_req, &tx_bw_mhz);

	if (!ret)
		params->probe_req = probe_req;
	else
		dev_kfree_skb_any(probe_req);

	return ret;
}

/**
 * channel_is_in_hw_scan_list - Determine if the provided channel (pointer into channel map)
 *                           already exists in the pending HW scan channel list.
 *
 * @params: The HW scan parameters object
 * @chan: The channel to search for
 *
 * Return: true if channel exists
 */
static bool channel_is_in_hw_scan_list(const struct morse_hw_scan_params *params,
				       const struct morse_dot11ah_channel *chan)
{
	int channel;

	for (channel = 0; channel < params->num_chans; channel++) {
		if (params->channels[channel].channel == chan)
			return true;
	}

	return false;
}

/**
 * insert_channel_into_hw_scan_list - Insert new S1G channel into the pending HW scan channel list.
 *
 * @params: The HW scan parameters object
 * @chan: The channel to insert
 *
 * Return: 0 if insertion successful, else error
 */
static int insert_channel_into_hw_scan_list(struct morse_hw_scan_params *params,
					    const struct morse_dot11ah_channel *chan)
{
	if (!params->channels)
		return -EFAULT;

	if (!chan)
		return -EFAULT;

	if (params->num_chans >= params->allocated_chans)
		return -ENOMEM;

	if (channel_is_in_hw_scan_list(params, chan))
		return 0;

	params->channels[params->num_chans].channel = chan;
	params->num_chans++;
	return 0;
}

/**
 * deconstruct_scan_channel_into_scan_list - Given an input S1G channel whose operating bandwidth
 *                                           is larger than the 1 & 2 MHz primaries, deconstruct
 *                                           into primary variants and insert into pending HW scan
 *                                           list.
 *
 * @params: The HW scan parameters object
 * @chan: The channel to deconstruct/insert
 *
 * Return: 0 if insertion successful, else error
 */
static int deconstruct_scan_channel_into_scan_list(struct morse_hw_scan_params *params,
						   const struct morse_dot11ah_channel *chan)
{
	u8 op_bw;
	u8 prim_bw;
	u8 prim_idx;
	struct morse *mors;

	if (!chan || !params) {
		MORSE_WARN_ON(FEATURE_ID_HWSCAN, 1);
		return -EFAULT;
	}

	mors = params->hw->priv;
	op_bw = ch_flag_to_chan_bw(chan->ch.flags);
	if (op_bw <= 2 || op_bw > 8) {
		/* This function shouldn't be called for channels that are outside expected range.
		 * Ignore this.
		 */
		MORSE_WARN_ON(FEATURE_ID_HWSCAN, 1);
		return -EINVAL;
	}

	MORSE_HWSCAN_DBG(mors, "Deconstructing ch %d (%d KHz, %d MHz) into primaries",
			 chan->ch.hw_value, morse_dot11ah_channel_to_freq_khz(chan->ch.hw_value),
			 op_bw);

	/* For each primary variant (1MHz & 2MHz), deconstruct the operating bandwidth into its
	 * constituent primaries and insert into scan list if they don't already exist within it.
	 */
	for (prim_bw = 1; prim_bw <= 2; prim_bw++) {
		for (prim_idx = 0; prim_idx < op_bw; prim_idx += prim_bw) {
			int ret;
			int prim_freq_khz;
			const struct morse_dot11ah_channel *s1g_chan;
			int prim_chan = morse_dot11ah_calc_prim_s1g_chan(op_bw, prim_bw,
									 chan->ch.hw_value,
									 prim_idx);

			MORSE_WARN_ON(FEATURE_ID_HWSCAN, prim_chan < 0);
			if (prim_chan < 0)
				continue;

			prim_freq_khz = morse_dot11ah_channel_to_freq_khz(prim_chan);

			s1g_chan = morse_dot11ah_s1g_freq_to_s1g(KHZ_TO_HZ(prim_freq_khz), prim_bw);
			if (!s1g_chan) {
				MORSE_HWSCAN_ERR(mors,
						 "   ch %d (%d KHz, %d MHz) Error - undefined",
						 prim_chan, prim_freq_khz, prim_bw);
				continue;
			}

			MORSE_HWSCAN_DBG(mors, "   ch %d (%d KHz, %d MHz)",
					 prim_chan, prim_freq_khz, prim_bw);

			ret = insert_channel_into_hw_scan_list(params, s1g_chan);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/**
 * hw_scan_initalise_channel_and_power_lists - Initialise channel and power lists for HW scan
 *
 * @params: HW scan params
 * Return: 0 if success, otherwise error code
 */
static int hw_scan_initalise_channel_and_power_lists(struct morse_hw_scan_params *params,
						     struct cfg80211_scan_request *request)
{
	int i, j;
	struct ieee80211_channel **chans = request->channels;
	int num_pwrs_coarse = 0;
	int last_pwr = INT_MIN;
	int chans_to_allocate = 0;
	bool optimize_channel_list = !params->survey;

	/* should not already be filled.. */
	MORSE_WARN_ON(FEATURE_ID_HWSCAN, params->channels);
	MORSE_WARN_ON(FEATURE_ID_HWSCAN, params->powers_qdbm);

	/* Determine how many channels to allocate for */
	for (i = 0; i < request->n_channels; i++) {
		const struct morse_dot11ah_channel *chan = morse_dot11ah_5g_chan_to_s1g(chans[i]);
		int op_bw;

		if (!chan)
			continue;

		/* 8 and 4 MHz channels will be deconstructed their constituent 1MHz and 2MHz
		 * primary variants. Ensure there is space for them.
		 */
		op_bw = ch_flag_to_chan_bw(chan->ch.flags);
		if (optimize_channel_list && op_bw > 2)
			chans_to_allocate += ((op_bw / 1) + (op_bw / 2));
		else
			chans_to_allocate++;
	}

	params->num_chans = 0;
	params->allocated_chans = 0;
	params->channels = kcalloc(chans_to_allocate, sizeof(*params->channels), GFP_KERNEL);
	if (!params->channels)
		return -ENOMEM;
	params->allocated_chans = chans_to_allocate;

	for (i = 0; i < request->n_channels; i++) {
		const struct morse_dot11ah_channel *chan = morse_dot11ah_5g_chan_to_s1g(chans[i]);

		if (!chan)
			continue;

		if (optimize_channel_list && ch_flag_to_chan_bw(chan->ch.flags) > 2)
			deconstruct_scan_channel_into_scan_list(params, chan);
		else
			insert_channel_into_hw_scan_list(params, chan);
	}

	/* Calculate a rough estimate of number of different channel powers required */
	for (i = 0; i < params->num_chans; i++) {
		const struct morse_dot11ah_channel *chan =  params->channels[i].channel;

		if (chan->ch.max_reg_power != last_pwr) {
			last_pwr = chan->ch.max_reg_power;
			num_pwrs_coarse++;
		}
	}

	params->powers_qdbm = kmalloc_array(num_pwrs_coarse, sizeof(*params->powers_qdbm),
					    GFP_KERNEL);
	if (!params->powers_qdbm)
		return -ENOMEM;
	params->n_powers = 0;

	for (i = 0; i < params->num_chans; i++) {
		const struct morse_dot11ah_channel *chan = params->channels[i].channel;
		s32 power_qdbm = MBM_TO_QDBM(chan->ch.max_reg_power);

		/* Try and find the power in the list */
		for (j = 0; j < params->n_powers; j++)
			if (params->powers_qdbm[j] == power_qdbm)
				break;

		/* Reached the end of the list - add the new power option */
		if (j == params->n_powers) {
			params->powers_qdbm[j] = power_qdbm;
			params->n_powers++;
			if (params->n_powers > num_pwrs_coarse) {
				MORSE_WARN_ON(FEATURE_ID_HWSCAN, 1);
				return -EFAULT;
			}
		}

		/* Give the index of the power level to the channel */
		params->channels[i].power_idx = j;
	}
	return 0;
}

/**
 * hw_scan_clean_up_params - Clean up HW scan params structure
 *
 * @params: hw scan params
 */
static void hw_scan_clean_up_params(struct morse_hw_scan_params *params)
{
	if (params->probe_req)
		dev_kfree_skb_any(params->probe_req);
	kfree(params->channels);
	kfree(params->powers_qdbm);

	params->num_chans = 0;
	params->allocated_chans = 0;
}

size_t morse_hw_scan_get_command_size(struct morse_hw_scan_params *params)
{
	struct hw_scan_tlv_channel_list *ch_list;
	struct hw_scan_tlv_power_list *pwr_list;
	struct hw_scan_tlv_probe_req *probe_req;
	struct hw_scan_tlv_dwell_on_home *dwell;
	struct morse_cmd_hw_scan_req *cmd;
	size_t cmd_size = sizeof(*cmd);

	/* No TLVs if simple abort command */
	if (!params->start)
		return cmd_size;

	cmd_size += struct_size(ch_list, channels, params->num_chans);
	cmd_size += struct_size(pwr_list, tx_power_qdbm, params->n_powers);

	if (params->probe_req)
		cmd_size += struct_size(probe_req, buf, params->probe_req->len);

	if (params->dwell_on_home_ms)
		cmd_size += sizeof(*dwell);

	return cmd_size;
}

u8 *morse_hw_scan_insert_tlvs(struct morse_hw_scan_params *params, u8 *buf)
{
	buf = hw_scan_add_channel_list_tlv(buf, params);

	buf = hw_scan_add_power_list_tlv(buf, params);

	if (params->dwell_on_home_ms)
		buf = hw_scan_add_dwell_on_home_tlv(buf, params);

	if (params->probe_req)
		buf = hw_scan_add_probe_req_tlv(buf, params);

	return buf;
}

void morse_hw_scan_dump_scan_cmd(struct morse *mors, struct morse_cmd_hw_scan_req *cmd)
{
	int i;
	int num_chans;
	int enabled = (cmd->flags & MORSE_HW_SCAN_CMD_FLAGS_START) ? 1 :
			(cmd->flags & MORSE_HW_SCAN_CMD_FLAGS_ABORT) ? 0 : -1;
	struct hw_scan_tlv_hdr *tlv;
	struct hw_scan_tlv_channel_list *ch_list = NULL;
	struct hw_scan_tlv_power_list *pwr_list = NULL;
	struct hw_scan_tlv_dwell_on_home *home_dwell = NULL;
	struct hw_scan_tlv_probe_req *probe = NULL;

	u8 *end = ((u8 *)cmd) + cmd->hdr.len + sizeof(cmd->hdr);

	/* if no logs, just return */
	if (!morse_log_is_enabled(FEATURE_ID_HWSCAN, MORSE_MSG_INFO))
		return;

	MORSE_HWSCAN_INFO(mors, "hw scan: %s",
		enabled == 1 ? "start" : enabled == 0 ? "abort" : "N/A");

	if (enabled != 1)
		return;

	if (cmd->flags & MORSE_HW_SCAN_CMD_FLAGS_SURVEY)
		MORSE_HWSCAN_DBG(mors, "    survey: y\n");

	tlv = (struct hw_scan_tlv_hdr *)cmd->variable;
	while (((u8 *)tlv) < end) {
		if (tlv->tag == HW_SCAN_TLV_TAG_CHAN_LIST)
			ch_list = (struct hw_scan_tlv_channel_list *)tlv;
		else if (tlv->tag == HW_SCAN_TLV_TAG_POWER_LIST)
			pwr_list = (struct hw_scan_tlv_power_list *)tlv;
		else if (tlv->tag == HW_SCAN_TLV_TAG_DWELL_ON_HOME)
			home_dwell = (struct hw_scan_tlv_dwell_on_home *)tlv;
		else if (tlv->tag == HW_SCAN_TLV_TAG_PROBE_REQ)
			probe = (struct hw_scan_tlv_probe_req *)tlv;

		tlv = (struct hw_scan_tlv_hdr *)(((u8 *)tlv) + tlv->len + sizeof(tlv));
	}

	MORSE_HWSCAN_DBG(mors, "    mode: %s\n", (probe) ? "active" : "passive");
	MORSE_HWSCAN_DBG(mors, "    dwell: %u ms\n", le32_to_cpu(cmd->dwell_time_ms));
	MORSE_HWSCAN_DBG(mors, "    home dwell: %u ms\n",
		(home_dwell) ? home_dwell->home_dwell_time_ms : 0);

	if (ch_list)
		num_chans = ch_list->hdr.len / sizeof(ch_list->channels[0]);
	else
		num_chans = 0;
	MORSE_HWSCAN_DBG(mors, "    channels: %u\n", num_chans);

	if (!num_chans || !ch_list)
		return;

	for (i = 0; i < num_chans; i++) {
		u32 packed_chan = le32_to_cpu(ch_list->channels[i]);

		MORSE_HWSCAN_DBG(mors, "[%d] : f:%lu o:%d p:%d i:%ld power:%d mBm\n", i,
			BMGET(packed_chan, HW_SCAN_CH_LIST_FREQ_KHZ),
			morse_ratecode_bw_index_to_s1g_bw_mhz(BMGET(packed_chan,
					HW_SCAN_CH_LIST_OP_BW)),
			morse_ratecode_bw_index_to_s1g_bw_mhz(BMGET(packed_chan,
					HW_SCAN_CH_LIST_PRIM_CH_WIDTH)),
			BMGET(packed_chan, HW_SCAN_CH_LIST_PRIM_CH_IDX),
			(pwr_list) ? QDBM_TO_MBM(pwr_list->tx_power_qdbm[BMGET(packed_chan,
					HW_SCAN_CH_LIST_PWR_LIST_IDX)]) : 0);
	}
}

int morse_ops_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_scan_request *hw_req)
{
	int ret = 0;
	struct morse *mors = hw->priv;
	struct cfg80211_scan_request *req = &hw_req->req;
	struct morse_hw_scan_params *params;
	u32 timeout_ms;

	mutex_lock(&mors->lock);

	MORSE_HWSCAN_DBG(mors, "%s: state %d\n", __func__, mors->hw_scan.state);

	if (!mors->started) {
		MORSE_HWSCAN_WARN(mors, "%s: device not ready yet\n", __func__);
		ret = -ENODEV;
		goto exit;
	}

	switch (mors->hw_scan.state) {
	case HW_SCAN_STATE_IDLE:
		mors->hw_scan.state = HW_SCAN_STATE_RUNNING;
		reinit_completion(&mors->hw_scan.scan_done);
		break;
	case HW_SCAN_STATE_RUNNING:
	case HW_SCAN_STATE_ABORTING:
		ret = -EBUSY;
		goto exit;
	}

	params = mors->hw_scan.params;

	if (!params) {
		params = kzalloc(sizeof(*params), GFP_KERNEL);
		if (!params) {
			ret = -ENOMEM;
			mors->hw_scan.state = HW_SCAN_STATE_IDLE;
			goto exit;
		}

		mors->hw_scan.params = params;
	} else {
		hw_scan_clean_up_params(params);
		memset(params, 0, sizeof(*params));
	}

	params->hw = hw;
	params->vif = vif;
	params->has_directed_ssid = (req->ssids && req->ssids[0].ssid_len > 0);

	if (req->duration)
		params->dwell_time_ms = MORSE_TU_TO_MS(req->duration);
	else if (req->n_ssids == 0)
		params->dwell_time_ms = MORSE_HWSCAN_DEFAULT_PASSIVE_DWELL_TIME_MS;
	else
		params->dwell_time_ms = MORSE_HWSCAN_DEFAULT_DWELL_TIME_MS;

	params->start = true;
	/* We only care about survey records when doing ACS / AP things */
	params->survey = (vif->type == NL80211_IFTYPE_AP);
	/* Return to home between scan channels to allow traffic to still flow */
	params->dwell_on_home_ms = ((vif->type == NL80211_IFTYPE_STATION) &&
		morse_mac_is_sta_vif_associated(vif)) ?	MORSE_HWSCAN_DEFAULT_DWELL_ON_HOME_MS : 0;
	params->use_1mhz_probes = morse_mac_is_1mhz_probe_req_enabled();

	hw_scan_initalise_channel_and_power_lists(params, &hw_req->req);

	/* Only initialise the probe request template if this is an active scan */
	if (req->n_ssids > 0) {
		ret = hw_scan_initialise_probe_req(params, hw_req);
		if (ret)
			MORSE_HWSCAN_ERR(mors, "Failed to init probe req %d\n", ret);
	}

	ret = morse_cmd_hw_scan(mors, params, false);

	if (ret) {
		mors->hw_scan.state = HW_SCAN_STATE_IDLE;
		goto exit;
	}

	timeout_ms = params->dwell_time_ms + params->dwell_on_home_ms;
	if (params->probe_req)
		timeout_ms += MORSE_HWSCAN_PROBE_DELAY_MS;
	timeout_ms *= params->num_chans;
	timeout_ms += MORSE_HWSCAN_TIMEOUT_OVERHEAD_MS;
	MORSE_HWSCAN_DBG(mors, "%s: expecting scan to complete in %u ms\n", __func__, timeout_ms);

	morse_survey_init_usage_records(mors);
	ieee80211_queue_delayed_work(mors->hw,
		&mors->hw_scan.timeout, msecs_to_jiffies(timeout_ms));

exit:
	mutex_unlock(&mors->lock);

	return ret;
}

static void cancel_hw_scan(struct morse *mors)
{
	struct morse_hw_scan_params params = {0};
	int ret;

	mutex_lock(&mors->lock);

	MORSE_HWSCAN_DBG(mors, "%s: state %d\n", __func__, mors->hw_scan.state);

	switch (mors->hw_scan.state) {
	case HW_SCAN_STATE_IDLE:
	case HW_SCAN_STATE_ABORTING:
		/* scan not running */
		mutex_unlock(&mors->lock);
		return;
	case HW_SCAN_STATE_RUNNING:
		mors->hw_scan.state = HW_SCAN_STATE_ABORTING;
		break;
	}

	params.start = false;
	ret = morse_cmd_hw_scan(mors, &params, false);

	mutex_unlock(&mors->lock);

	if (ret ||
		!mors->started ||
		!wait_for_completion_timeout(&mors->hw_scan.scan_done, 1 * HZ)) {
		/* We may have lost the event on the bus, the chip could be wedged, or the cmd
		 * failed for another reason.
		 * Nevertheless, we should call the done event so mac80211 knows to unblock itself
		 */
		struct cfg80211_scan_info info = {
			.aborted = true
		};

		mutex_lock(&mors->lock);
		ieee80211_scan_completed(mors->hw, &info);
		mors->hw_scan.state = HW_SCAN_STATE_IDLE;

		mutex_unlock(&mors->lock);
	}
}

void morse_ops_cancel_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct morse *mors = hw->priv;

	MORSE_HWSCAN_INFO(mors, "hw scan: cancel\n");
	cancel_delayed_work_sync(&mors->hw_scan.timeout);
	cancel_hw_scan(mors);
}

void morse_hw_scan_done_event(struct ieee80211_hw *hw)
{
	struct morse *mors = hw->priv;
	struct cfg80211_scan_info info = {0};

	mutex_lock(&mors->lock);

	MORSE_HWSCAN_INFO(mors, "hw scan: complete\n");
	MORSE_HWSCAN_DBG(mors, "%s: done event (%d)\n", __func__, mors->hw_scan.state);

	switch (mors->hw_scan.state) {
	case HW_SCAN_STATE_IDLE:
		/* Scan has already been stopped. Just continue */
		goto exit;
	case HW_SCAN_STATE_RUNNING:
	case HW_SCAN_STATE_ABORTING:
		mors->hw_scan.state = HW_SCAN_STATE_IDLE;
		info.aborted = (mors->hw_scan.state == HW_SCAN_STATE_ABORTING);
	}

	ieee80211_scan_completed(mors->hw, &info);
exit:
	complete(&mors->hw_scan.scan_done);
	mutex_unlock(&mors->lock);
	cancel_delayed_work_sync(&mors->hw_scan.timeout);
}

static void morse_hw_scan_timeout_work(struct work_struct *work)
{
	struct morse *mors = container_of(work, struct morse, hw_scan.timeout.work);

	MORSE_HWSCAN_ERR(mors, "hw scan: timed out, aborting\n");
	cancel_hw_scan(mors);
}

void morse_hw_scan_init(struct morse *mors)
{
	mors->hw_scan.state = HW_SCAN_STATE_IDLE;
	mors->hw_scan.params = NULL;

	init_completion(&mors->hw_scan.scan_done);
	INIT_DELAYED_WORK(&mors->hw_scan.timeout, morse_hw_scan_timeout_work);
}

void morse_hw_scan_destroy(struct morse *mors)
{
	cancel_delayed_work_sync(&mors->hw_scan.timeout);
	if (mors->hw_scan.params)
		hw_scan_clean_up_params(mors->hw_scan.params);
	kfree(mors->hw_scan.params);
	mors->hw_scan.params = NULL;
}

void morse_hw_scan_finish(struct morse *mors)
{
	struct cfg80211_scan_info info = {
		.aborted = true,
	};
	lockdep_assert_held(&mors->lock);

	if (mors->hw_scan.state == HW_SCAN_STATE_IDLE)
		return;

	ieee80211_scan_completed(mors->hw, &info);
	complete(&mors->hw_scan.scan_done);
	mors->hw_scan.state = HW_SCAN_STATE_IDLE;
	cancel_delayed_work_sync(&mors->hw_scan.timeout);
}
