#ifndef _MORSE_COMMAND_H_
#define _MORSE_COMMAND_H_

/*
 * Copyright 2017-2022 Morse Micro
 *
 */
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include "morse.h"

#define MORSE_CMD_REQ	BIT(0)
#define MORSE_CMD_CFM	BIT(1)
#define MORSE_CMD_EVT	BIT(2)
#define MORSE_CMD_RSP	BIT(3)

#define MORSE_CMD_IS_REQ(cmd) ((cmd)->hdr.flags & MORSE_CMD_REQ)
#define MORSE_CMD_IS_CFM(cmd) ((cmd)->hdr.flags & MORSE_CMD_CFM)
#define MORSE_CMD_IS_EVT(cmd) ((cmd)->hdr.flags & MORSE_CMD_EVT)
#define MORSE_CMD_IS_RSP(cmd) ((cmd)->hdr.flags & MORSE_CMD_RSP)

/* Firmware will not change currently set bandwidth */
#define DEFAULT_BANDWIDTH 0xFF

/* Firmware will not change currently set frequency */
#define DEFAULT_FREQUENCY 0xFFFFFFFF

/* Firmware will not change currently set 1mhz channel index */
#define DEFAULT_1MHZ_PRIMARY_CHANNEL_INDEX 0xFF

/* Default IBSS ACK Timeout adjustment in usecs */
#define DEFAULT_MORSE_IBSS_ACK_TIMEOUT_ADJUST_US (1000)

/**
 * The maximum length of a user-specified payload (bytes) for Standby status
 * frames.
 */
#define STANDBY_STATUS_FRAME_USER_PAYLOAD_MAX_LEN (64)

/** Maximum length of extra IEs passed to scan request. */
#define SCAN_EXTRA_IES_MAX_LEN (1022)

/**
 * Maximum length of SAE password accepted by the chip.
 * Note that this is less than the kernel's %SAE_PASSWORD_MAX_LEN constant.
 */
#define MORSE_SAE_PASSWORD_MAX_LEN (100)

/* The max length of the twt agreement sent to the FW */
#define TWT_MAX_AGREEMENT_LEN	(20)

/** Flags of MORSE STA */
#define MORSE_STA_FLAG_S1G_PV1		BIT(0)

/** CAC rule max */
#define CAC_CFG_CHANGE_RULE_MAX		(8)

struct morse_twt_agreement_data;

/**
 *  Host to firmware/driver messages
 *
 * @note you must hardcode the values here, and they must all be unique
 */
enum morse_commands_id {
	MORSE_COMMAND_SET_CHANNEL = 0x0001,
	MORSE_COMMAND_GET_VERSION = 0x0002,
	MORSE_COMMAND_SET_TXPOWER = 0x0003,
	MORSE_COMMAND_ADD_INTERFACE = 0x0004,
	MORSE_COMMAND_REMOVE_INTERFACE = 0x0005,
	MORSE_COMMAND_BSS_CONFIG = 0x0006,
	MORSE_COMMAND_APP_STATS_LOG = 0x0007,
	MORSE_COMMAND_APP_STATS_RESET = 0x0008,
	MORSE_COMMAND_RPG = 0x0009,
	MORSE_COMMAND_INSTALL_KEY = 0x000A,
	MORSE_COMMAND_DISABLE_KEY = 0x000B,
	MORSE_COMMAND_CFG_SCAN = 0x0010,
	MORSE_COMMAND_SET_QOS_PARAMS = 0x0011,
	MORSE_COMMAND_GET_QOS_PARAMS = 0x0012,
	MORSE_COMMAND_GET_FULL_CHANNEL = 0x0013,
	MORSE_COMMAND_SET_STA_STATE = 0x0014,
	MORSE_COMMAND_SET_BSS_COLOR = 0x0015,
	MORSE_COMMAND_SET_PS = 0x0016,
	MORSE_COMMAND_BLOCKACK_DEPRECATED = 0x0017,
	MORSE_COMMAND_MAC_STATS_LOG = 0x000C,
	MORSE_COMMAND_MAC_STATS_RESET = 0x000D,
	MORSE_COMMAND_UPHY_STATS_LOG = 0x000E,
	MORSE_COMMAND_UPHY_STATS_RESET = 0x000F,
	MORSE_COMMAND_HEALTH_CHECK = 0x0019,
	MORSE_COMMAND_SET_CTS_SELF_PS = 0x001A,
	MORSE_COMMAND_GET_CURRENT_CHANNEL = 0x001D,
	MORSE_COMMAND_ARP_OFFLOAD = 0x0020,
	MORSE_COMMAND_SET_LONG_SLEEP_CONFIG = 0x0021,
	MORSE_COMMAND_SET_DUTY_CYCLE = 0x0022,
	MORSE_COMMAND_GET_MAX_TXPOWER = 0x0024,
	MORSE_COMMAND_GET_CAPABILITIES = 0x00025,
	MORSE_COMMAND_INSTALL_TWT_AGREEMENT = 0x00026,
	MORSE_COMMAND_REMOVE_TWT_AGREEMENT = 0x00027,
	MORSE_COMMAND_MPSW_CONFIG = 0x0030,
	MORSE_COMMAND_STANDBY_MODE = 0x0031,
	MORSE_COMMAND_DHCP_OFFLOAD = 0x0032,
	MORSE_COMMAND_UPDATE_OUI_FILTER = 0x0034,
	MORSE_COMMAND_IBSS_CONFIG = 0x0035,
	MORSE_COMMAND_VALIDATE_TWT_AGREEMENT = 0x00036,
	MORSE_COMMAND_SET_FRAG_THRESHOLD = 0x0037,
	MORSE_COMMAND_OCS = 0x0038,
	MORSE_COMMAND_MESH_CONFIG = 0x0039,
	MORSE_COMMAND_SET_OFFSET_TSF = 0x003A,
	MORSE_COMMAND_GET_CHANNEL_USAGE_RECORD = 0x003B,
	MORSE_COMMAND_MCAST_FILTER = 0x003C,
	MORSE_COMMAND_BSS_BEACON_CONFIG = 0x003D,
	MORSE_COMMAND_GET_SET_GENERIC_PARAM = 0x003E,
	MORSE_COMMAND_PV1_HC_INFO_UPDATE = 0x0041,
	MORSE_COMMAND_PV1_SET_RX_AMPDU_STATE = 0x0042,
	MORSE_COMMAND_CONFIGURE_PAGE_SLICING = 0x0043,
	MORSE_COMMAND_HW_SCAN = 0x0044,
	MORSE_COMMAND_FORCE_POWER_MODE = 0x00048,
	MORSE_COMMAND_SET_LI_SLEEP = 0x0049,
	MORSE_COMMAND_GET_DISABLED_CHANNELS = 0x004A,

	/* Fullmac-specific commands starting at 0x0800 */
	MORSE_COMMAND_START_SCAN = 0x0801,
	MORSE_COMMAND_ABORT_SCAN = 0x0802,
	MORSE_COMMAND_CONNECT = 0x0803,
	MORSE_COMMAND_DISCONNECT = 0x0804,
	MORSE_COMMAND_GET_CONNECTION_STATE = 0x0805,

	/* Temporary Commands that may be removed later */
	MORSE_COMMAND_SET_MODULATION = 0x1000,
	MORSE_COMMAND_GET_RSSI = 0x1002,
	MORSE_COMMAND_SET_IFS = 0x1003,
	MORSE_COMMAND_SET_FEM_SETTINGS = 0x1005,
	MORSE_COMMAND_SET_CONTROL_RESPONSE = 0x1009,
	MORSE_COMMAND_SET_PERIODIC_CAL = 0x100A,

	/* Commands to driver */
	MORSE_COMMAND_DRIVER_START = 0xA000,
	MORSE_COMMAND_SET_STA_TYPE = 0xA000,
	MORSE_COMMAND_SET_ENC_MODE = 0xA001,
	MORSE_COMMAND_TEST_BA = 0xA002,
	MORSE_COMMAND_SET_LISTEN_INTERVAL = 0xA003,
	MORSE_COMMAND_SET_AMPDU = 0xA004,
	MORSE_COMMAND_SET_RAW_DEPRECATED = 0xA005,
	MORSE_COMMAND_COREDUMP = 0xA006,
	MORSE_COMMAND_SET_S1G_OP_CLASS = 0xA007,
	MORSE_COMMAND_SEND_WAKE_ACTION_FRAME = 0xA008,
	MORSE_COMMAND_VENDOR_IE_CONFIG = 0xA009,
	MORSE_COMMAND_TWT_SET_CONF = 0xA010,
	MORSE_COMMAND_GET_AVAILABLE_CHANNELS = 0xA011,
	MORSE_COMMAND_SET_ECSA_S1G_INFO = 0xA012,
	MORSE_COMMAND_GET_HW_VERSION = 0xA013,
	MORSE_COMMAND_CAC = 0xA014,
	MORSE_COMMAND_DRIVER_SET_DUTY_CYCLE = 0xA015,
	MORSE_COMMAND_MBSSID_INFO = 0xA016,
	MORSE_COMMAND_OCS_REQ = 0xA017,
	MORSE_COMMAND_SET_MESH_CONFIG = 0xA018,
	MORSE_COMMAND_MBCA_SET_CONF = 0xA019,
	MORSE_COMMAND_DYNAMIC_PEERING_SET_CONF = 0xA020,
	MORSE_COMMAND_CONFIG_RAW = 0xA021,
	MORSE_COMMAND_DRIVER_END,

	/* Event notifications start at 0x4000 */
	MORSE_COMMAND_EVT_STA_STATE = 0x4001,
	MORSE_COMMAND_EVT_BEACON_LOSS = 0x4002,
	MORSE_COMMAND_EVT_SIG_FIELD_ERROR = 0x4003,
	MORSE_COMMAND_EVT_UMAC_TRAFFIC_CONTROL = 0x4004,
	MORSE_COMMAND_EVT_DHCP_LEASE_UPDATE = 0x4005,
	MORSE_COMMAND_EVT_OCS_DONE = 0x4006,
	MORSE_COMMAND_EVT_SCAN_DONE = 0x4007,
	MORSE_COMMAND_EVT_SCAN_RESULT = 0x4008,
	MORSE_COMMAND_EVT_CONNECTED = 0x4009,
	MORSE_COMMAND_EVT_DISCONNECTED = 0x4010,
	MORSE_COMMAND_EVT_HW_SCAN_DONE = 0x4011,
	MORSE_COMMAND_EVT_CHANNEL_USAGE = 0x4012,
	MORSE_COMMAND_EVT_CONNECTION_LOSS = 0x4013,

	/** Test commands start at 0x8000 */
	MORSE_TEST_COMMAND_START_SAMPLEPLAY = 0x8002,
	MORSE_TEST_COMMAND_STOP_SAMPLEPLAY = 0x8003,
	MORSE_TEST_COMMAND_SET_RESPONSE_INDICATION = 0x8007,
	MORSE_TEST_COMMAND_SET_MAC_ACK_TIMEOUT = 0x8008,
	MORSE_TEST_COMMAND_FORCE_ASSERT = 0x800E,
};

/**
 * Error numbers the FW may return from commands
 * Defined here as errno numbers are not portable across architectures
 */
enum morse_cmd_return_code {
	MORSE_RET_SUCCESS	  = 0,
	MORSE_RET_EPERM		  = -1,
	MORSE_RET_ENOMEM	  = -12,
	MORSE_RET_CMD_NOT_HANDLED = -32757,
};

/* Crypto Related types */

#define MORSE_MAX_CRYPTO_KEY_LEN (32)

enum morse_temporal_key_type {
	MORSE_KEY_TYPE_INVALID = 0,
	MORSE_KEY_TYPE_GTK = 1,	/* Group Temporal key */
	MORSE_KEY_TYPE_PTK = 2,	/* Pairwise Temporal key */
	MORSE_KEY_TYPE_IGTK = 3,	/* Integrity Group Temporal key */

	MORSE_KEY_TYPE_LAST = MORSE_KEY_TYPE_IGTK,
};

enum morse_aes_key_length {
	MORSE_AES_KEY_INVALID = 0,
	MORSE_AES_KEY_LENGTH_128 = 1,
	MORSE_AES_KEY_LENGTH_256 = 2,

	MORSE_AES_KEY_LENGTH_LAST = MORSE_AES_KEY_LENGTH_256,
};

enum morse_key_cipher {
	MORSE_KEY_CIPHER_INVALID = 0,
	MORSE_KEY_CIPHER_AES_CCM = 1,
	MORSE_KEY_CIPHER_AES_GCM = 2,
	MORSE_KEY_CIPHER_AES_CMAC = 3,
	MORSE_KEY_CIPHER_AES_GMAC = 4,

	MORSE_KEY_CIPHER_LAST = MORSE_KEY_CIPHER_AES_GMAC,
};

#define MORSE_CMD_HOST_ID_SEQ_MAX	0xfff
#define MORSE_CMD_HOST_ID_RETRY_MASK	0x000f
#define MORSE_CMD_HOST_ID_SEQ_SHIFT	4
#define MORSE_CMD_HOST_ID_SEQ_MASK	0xfff0

struct morse_cmd_header {
	__le16 flags;
	__le16 message_id;	/* from enum morse_commands_id */
	__le16 len;
	__le16 host_id;
	__le16 vif_id;
	__le16 pad;
} __packed;

struct morse_cmd {
	struct morse_cmd_header hdr;
	u8 data[];
} __packed;

struct morse_cmd_test_ba {
	struct morse_cmd_header hdr;
	u8 addr[ETH_ALEN];
	u8 start;
	u8 tx;
	__le32 tid;
} __packed;

struct morse_cmd_set_txpower {
	struct morse_cmd_header hdr;
	__le32 power_qdbm;
} __packed;

struct morse_resp_set_txpower {
	struct morse_cmd_header hdr;
	__le32 status;
	__le32 power_qdbm;
} __packed;

struct morse_cmd_get_max_txpower {
	struct morse_cmd_header hdr;
} __packed;

struct morse_resp_get_max_txpower {
	struct morse_cmd_header hdr;
	__le32 status;
	__le32 power_qdbm;
} __packed;

struct morse_cmd_add_if {
	struct morse_cmd_header hdr;
	u8 addr[ETH_ALEN];
	__le32 type;
} __packed;

struct morse_resp_add_if {
	struct morse_cmd_header hdr;
	__le32 status;
} __packed;

struct morse_cmd_rm_if {
	struct morse_cmd_header hdr;
} __packed;

struct morse_cmd_cfg_bss {
	struct morse_cmd_header hdr;
	__le16 beacon_int;
	__le16 dtim_period;
	u8 __padding[2];
	__le32 cssid;
} __packed;

struct morse_cmd_sta_state {
	struct morse_cmd_header hdr;
	u8 addr[ETH_ALEN];
	__le16 aid;
	__le16 state;
	u8 uapsd_queues;
	u32 flags;
} __packed;

struct morse_cmd_install_key {
	struct morse_cmd_header hdr;
	__le64 pn;
	__le32 aid;
	u8 key_idx;
	u8 cipher;
	u8 key_length;
	u8 key_type;
	u8 __padding[2];
	u8 key[MORSE_MAX_CRYPTO_KEY_LEN];
} __packed;

struct morse_resp_install_key {
	struct morse_cmd_header hdr;
	__le32 status;
	u8 key_idx;
} __packed;

struct morse_cmd_disable_key {
	struct morse_cmd_header hdr;
	__le32 key_type;
	__le32 aid;
	u8 key_idx;
} __packed;

struct morse_resp_sta_state {
	struct morse_cmd_header hdr;
	__le32 status;
} __packed;

struct morse_cmd_config_bss_beacon {
	struct morse_cmd_header hdr;
	bool enable_beaconing;
} __packed;

struct morse_cmd_pv1_hc_data {
	struct morse_cmd_header hdr;
	u8 opcode;
	u8 pv1_hc_store;
	u8 sta_addr[ETH_ALEN];
	u8 a3[ETH_ALEN];
	u8 a4[ETH_ALEN];
} __packed;

struct morse_resp_pv1_hc_data {
	struct morse_cmd_header hdr;
	__le32 status;
} __packed;

/* Used in setting dot11_mode in set_channel */
enum dot11_proto_mode {
	/* 802.11ah S1G mode */
	DOT11AH_MODE,
	/* 802.11b (DSSS only) mode */
	DOT11B_MODE,
	/* 802.11bg (Legacy only) mode */
	DOT11BG_MODE,
	/* 802.11gn (OFDM only) mode */
	DOT11GN_MODE,
	/* 802.11bgn (Full compatibility) mode */
	DOT11BGN_MODE,
};

/* Used between driver and FW */
struct morse_cmd_set_channel {
	struct morse_cmd_header hdr;
	__le32 op_chan_freq_hz;
	u8 op_bw_mhz;
	u8 pri_bw_mhz;
	u8 pri_1mhz_chan_idx;
	u8 dot11_mode;
} __packed;

/* Used between userspace and driver */
struct morse_drv_cmd_set_channel {
	struct morse_cmd_set_channel cmd;
	u8 s1g_chan_power;
} __packed;

/* Used between userspace and driver */
struct morse_drv_resp_set_channel {
	struct morse_cmd_header hdr;
	__le32 status;
} __packed;

/* Used between driver and FW */
struct morse_resp_set_channel {
	struct morse_drv_resp_set_channel resp;
	__le32 power_qdbm;
} __packed;

/**
 * struct morse_cmd_get_current_channel_req - Request message for GET_CURRENT_CHANNEL.
 */
struct morse_cmd_get_current_channel_req {
	struct morse_cmd_header hdr;
} __packed;

/**
 * struct morse_cmd_get_current_channel_cfm - Confirm message for GET_CURRENT_CHANNEL.
 * @operating_channel_freq_hz: Centre frequency of the operating channel in Hz.
 * @operating_channel_bw_mhz: Operating channel bandwidth in MHz.
 * @primary_channel_bw_mhz: Primary channel bandwidth in MHz.
 * @primary_1mhz_channel_index: Index of the 1MHz channel within the operating channel.
 */
struct morse_cmd_get_current_channel_cfm {
	struct morse_cmd_header hdr;
	__le32 status;
	__le32 operating_channel_freq_hz;
	u8 operating_channel_bw_mhz;
	u8 primary_channel_bw_mhz;
	u8 primary_1mhz_channel_index;
} __packed;

struct morse_cmd_cfg_scan {
	struct morse_cmd_header hdr;
	u8 enabled;
} __packed;

struct morse_resp {
	struct morse_cmd_header hdr;
	__le32 status;
	u8 data[];
} __packed;

struct morse_cmd_get_version {
	struct morse_cmd_header hdr;
} __packed;

struct morse_resp_get_version {
	struct morse_cmd_header hdr;
	__le32 status;
	__le32 length;
	u8 version[2048];
} __packed;

struct morse_cmd_get_channel_usage {
	struct morse_cmd_header hdr;
} __packed;

struct morse_resp_get_channel_usage {
	struct morse_cmd_header hdr;
	__le32 status;
	__le64 time_listen;
	__le64 busy_time;
	__le32 freq_hz;
	s8 noise;
	u8 bw_mhz;
} __packed;

struct morse_cmd_set_listen_interval {
	struct morse_cmd_header hdr;
	__le16 listen_interval;
} __packed;

struct morse_cmd_vendor {
	struct morse_cmd_header hdr;
	u8 data[2048];
} __packed;

struct morse_cmd_set_ps {
	struct morse_cmd_header hdr;
	u8 enabled;
	u8 dynamic_ps_offload;
} __packed;

struct morse_resp_vendor {
	struct morse_cmd_header hdr;
	__le32 status;
	u8 data[2048];
} __packed;

struct morse_cmd_cr_bw {
	struct morse_cmd_header hdr;
	u8 direction;
	u8 cr_1mhz_en;
} __packed;

struct cmd_force_assert_req {
	/* Target hart to crash with an intended assert */
	__le32 hart_id;
} __packed;

struct morse_cmd_health_check {
	struct morse_cmd_header hdr;
} __packed;

struct morse_cmd_cts_self_ps {
	struct morse_cmd_header hdr;
	u8 enable;
} __packed;

enum ocs_subcmd {
	OCS_SUBCMD_CONFIG = 1,
	OCS_SUBCMD_STATUS,
};

enum ocs_type {
	OCS_TYPE_QNULL = 0,
	OCS_TYPE_RAW,
};

/* Used between userspace and driver */
struct morse_drv_cmd_ocs {
	struct morse_cmd_header hdr;
	__le32 subcmd;
	__le32 op_chan_freq_hz;
	u8 op_bw_mhz;
	u8 pri_bw_mhz;
	u8 pri_1mhz_chan_idx;
} __packed;

/* Used between driver and FW */
struct morse_cmd_ocs {
	struct morse_drv_cmd_ocs cmd;
	__le16 aid;
	u8 type;
} __packed;

struct morse_resp_ocs {
	struct morse_cmd_header hdr;
	__le32 status;
	u8 running;
} __packed;

struct morse_ocs_done_evt {
	u64 time_listen;
	u64 time_rx;
	s8 noise;
	u8 metric;
} __packed;

struct morse_mesh_peer_addr_evt {
	u8 addr[ETH_ALEN];
} __packed;

struct morse_event {
	struct morse_cmd_header hdr;
	union {
		DECLARE_FLEX_ARRAY(u8, data);
		struct morse_ocs_done_evt ocs_done_evt;
		struct morse_mesh_peer_addr_evt peer_addr_evt;
	};
} __packed;

struct morse_evt_sta_state {
	struct morse_cmd_header hdr;
	u8 addr[ETH_ALEN];
	__le16 aid;
	__le16 state;
} __packed;

struct morse_evt_beacon_loss {
	struct morse_cmd_header hdr;
	__le32 num_bcns;
} __packed;

struct morse_evt_sig_field_error_evt {
	struct morse_cmd_header hdr;
	__le64 start_timestamp;
	__le64 end_timestamp;
} __packed;

/** Enum describing the sources of a traffic control event */
enum umac_traffic_control_source {
	UMAC_TRAFFIC_CONTROL_SOURCE_TWT = BIT(0),
	UMAC_TRAFFIC_CONTROL_SOURCE_DUTY_CYCLE = BIT(1),
};

struct morse_evt_umac_traffic_control {
	struct morse_cmd_header hdr;
	u8 pause_data_traffic;
	u32 sources;
} __packed;

struct morse_evt_dhcp_lease_update {
	struct morse_cmd_header hdr;
	u32 my_ip;
	u32 netmask;
	u32 router;
	u32 dns;
} __packed;

enum scan_result_frame_type {
	SCAN_RESULT_FRAME_TYPE_UNKNOWN = 0,
	SCAN_RESULT_FRAME_TYPE_BEACON = 1,
	SCAN_RESULT_FRAME_TYPE_PROBE_RESPONSE = 2,
};

struct morse_evt_scan_result {
	struct morse_cmd_header hdr;
	__le32 channel_freq_hz;
	u8 bw_mhz;
	u8 frame_type;
	__le16 rssi;
	u8 bssid[ETH_ALEN];
	__le16 beacon_interval;
	__le16 capability_info;
	__le64 tsf;
	__le16 ies_len;
	u8 ies[];
} __packed;

struct morse_evt_scan_done {
	struct morse_cmd_header hdr;
	u8 aborted;
} __packed;

struct morse_evt_connected {
	struct morse_cmd_header hdr;
	u8 bssid[ETH_ALEN];
	__le16 rssi;
} __packed;

struct morse_evt_disconnected {
	struct morse_cmd_header hdr;
} __packed;

struct morse_evt_channel_usage {
	struct morse_cmd_header hdr;
	u64 time_listen;
	u64 busy_time;
	u32 freq_hz;
	s8 noise;
	u8 bw_mhz;
} __packed;

struct morse_evt_connection_loss {
	struct morse_cmd_header hdr;
	u32 reason;
} __packed;

enum morse_cmd_raw_tlv_tag {
	MORSE_RAW_CMD_TAG_SLOT_DEF = 0,
	MORSE_RAW_CMD_TAG_GROUP = 1,
	MORSE_RAW_CMD_TAG_START_TIME = 2,
	MORSE_RAW_CMD_TAG_PRAW = 3,
	MORSE_RAW_CMD_TAG_BCN_SPREAD = 4,
};

/* slot definition is required for new raw configs */
struct morse_cmd_raw_tlv_slot_def {
	u8 tag;
	/** Total length of the RAW window. This / num_slots = slot_duration */
	u32 raw_duration_us;
	/** Number of individual "slots" within the RAW window */
	u8 num_slots;

	u8 cross_slot_bleed;
} __packed;

struct morse_cmd_raw_tlv_group {
	u8 tag;

	/** Start AID for this raw config */
	u16 aid_start;

	/** End AID for this config (inclusive) */
	u16 aid_end;
} __packed;

struct morse_cmd_raw_tlv_start_time {
	u8 tag;
	/** Time the RAW window starts, measured from the end of the frame carrying RPS IE.*/
	u32 start_time_us;
} __packed;

struct morse_cmd_raw_tlv_praw {
	u8 tag;
	u8 periodicity;
	u8 validity;
	u8 start_offset;
	u8 refresh_on_expiry;
} __packed;

struct morse_cmd_raw_tlv_bcn_spread {
	u8 tag;
	u16 max_spread;
	u16 nominal_sta_per_bcn;
} __packed;

union morse_cmd_raw_tlvs {
	u8 tag;
	struct morse_cmd_raw_tlv_slot_def slot_def;
	struct morse_cmd_raw_tlv_group group;
	struct morse_cmd_raw_tlv_start_time start_time;
	struct morse_cmd_raw_tlv_praw praw;
	struct morse_cmd_raw_tlv_bcn_spread bcn_spread;
} __packed;

/**
 * Flags used for describing the operation of a RAW config command.
 */
enum morse_cmd_raw_config_flags {
	/** Enable RAW config at this index */
	RAW_CMD_FLAG_ENABLE = BIT(0),
	/** Delete RAW config at this index */
	RAW_CMD_FLAG_DELETE = BIT(1),
	/** Update RAW config parameters */
	RAW_CMD_FLAG_UPDATE = BIT(2),
};

struct morse_cmd_raw_cfg {
	struct morse_cmd_header hdr;
	/** Flags for this RAW config */
	u32 flags;

	/** ID to reference this RAW config.
	 * If ID already exists, existing config will be overwritten.
	 * ID of 0 is reserved to indicate operate on RAW globally
	 */
	u16 id;

	u8 variable[];
} __packed;

/** CAC threshold change rule */
struct cac_cmd_change_rule {
	/* Threshold in Authentication Request Frames per Second */
	u16 arfs;
	/** Percentage change to apply to threshold if condition is matched */
	s16 threshold_change;
} __packed;

struct morse_cmd_cac_req {
	struct morse_cmd_header hdr;
	/** CAC subcommand */
	u8 cmd;
	/** Number of threshold change rules */
	u8 rule_tot;
	/** Threshold change rule (%) */
	struct cac_cmd_change_rule rule[CAC_CFG_CHANGE_RULE_MAX];
} __packed;

struct morse_cmd_cac_cfm {
	struct morse_cmd_header hdr;
	__le32 status;
	/** Number of threshold change rules */
	u8 rule_tot;
	/** Threshold change rule (%) */
	struct cac_cmd_change_rule rule[CAC_CFG_CHANGE_RULE_MAX];
} __packed;

struct morse_cmd_mbssid {
	struct morse_cmd_header hdr;
	u8 max_bssid_indicator;
	char transmitter_iface[IFNAMSIZ];
} __packed;

struct morse_cmd_ecsa {
	struct morse_cmd_header hdr;
	/** Operating Channel Frequency Hz.
	 * Endianness __le32 op_chan_freq_hz is not considered
	 * here as the cmd is indicated to driver and not chip
	 */
	u32 op_chan_freq_hz;
	u8 op_class;
	u8 prim_bw;
	u8 prim_chan_1mhz_idx;
	u8 op_bw_mhz;
	u8 prim_opclass;
	u8 s1g_cap0;
	u8 s1g_cap1;
	u8 s1g_cap2;
	u8 s1g_cap3;
} __packed;

struct morse_cmd_send_wake_action_frame {
	struct morse_cmd_header hdr;
	u8 dest_addr[ETH_ALEN];
	__le32 payload_size;
	u8 payload[];
} __packed;

enum command_vendor_ie_opcode {
	MORSE_VENDOR_IE_OP_ADD_ELEMENT = 0,
	MORSE_VENDOR_IE_OP_CLEAR_ELEMENTS,
	MORSE_VENDOR_IE_OP_ADD_FILTER,
	MORSE_VENDOR_IE_OP_CLEAR_FILTERS,

	MORSE_VENDOR_IE_OP_MAX = U16_MAX,
	MORSE_VENDOR_IE_OP_INVALID = MORSE_VENDOR_IE_OP_MAX
};

struct morse_cmd_vendor_ie_config {
	struct morse_cmd_header hdr;
	u16 opcode;
	u16 mgmt_type_mask;
	u8 data[];
} __packed;

struct morse_cmd_arp_offload {
	struct morse_cmd_header hdr;
	u32 ip_table[IEEE80211_BSS_ARP_ADDR_LIST_LEN];
} __packed;

struct morse_cmd_set_long_sleep_config {
	struct morse_cmd_header hdr;
	u8 enabled;
} __packed;

enum twt_conf_subcommands {
	TWT_CONF_SUBCMD_CONFIGURE,
	TWT_CONF_SUBCMD_FORCE_INSTALL_AGREEMENT,
	TWT_CONF_SUBCMD_REMOVE_AGREEMENT,
	TWT_CONF_SUBCMD_CONFIGURE_EXPLICIT
};

struct morse_cmd_set_twt_conf {
	__le64 target_wake_time;
	union {
		__le64 wake_interval_us;
		struct {
			__le16 wake_interval_mantissa;
			u8 wake_interval_exponent;
			u8 padding[5];
		} explicit;
	};
	__le32 wake_duration;
	u8 twt_setup_command;
	u8 padding[3];
} __packed;

struct morse_cmd_remove_twt_agreement {
	struct morse_cmd_header hdr;
	u8 flow_id;
} __packed;

struct command_twt_req {
	struct morse_cmd_header hdr;
	/** TWT subcommands, see @ref twt_conf_subcommands */
	u8 cmd;
	/** The flow (twt) identifier for the agreement to set, install or remove */
	u8 flow_id;
	struct morse_cmd_set_twt_conf set_twt_conf;
} __packed;

struct morse_cmd_install_twt_agreement_req {
	struct morse_cmd_header hdr;
	/** The flow (twt) identifier for this agreement */
	u8 flow_id;
	/** The length of the TWT agreement */
	u8 agreement_len;
	/** The TWT agreement data */
	u8 agreement[];
} __packed;

/**
 * struct morse_queue_params - QoS parameters
 *
 * @uapsd: access category status for UAPSD
 * @aci: access category index
 * @aifs: arbitration interframe space [0..255]
 * @cw_min: minimum contention window
 * @cw_max: maximum contention window
 * @txop: maximum burst time in units of usecs, 0 meaning disabled
 */
struct morse_queue_params {
	u8 uapsd;
	u8 aci;
	u8 aifs;
	u16 cw_min;
	u16 cw_max;
	u32 txop;
};

/**
 * struct cmd_cfg_qos - configure QoS command
 *
 * @hdr: command header
 * @aci: access category index
 * @aifs: arbitration interframe space [0..255]
 * @cw_min: minimum contention window
 * @cw_max: maximum contention window
 * @txop: maximum burst time in units of usecs, 0 meaning disabled
 */
struct morse_cmd_cfg_qos {
	struct morse_cmd_header hdr;
	u8 uapsd;
	u8 aci;
	u8 aifs;
	__le16 cw_min;
	__le16 cw_max;
	__le32 txop;
} __packed;

struct morse_cmd_set_bss_color {
	struct morse_cmd_header hdr;
	u8 color;
} __packed;

struct morse_resp_set_bss_color {
	struct morse_cmd_header hdr;
	__le32 status;
} __packed;

struct morse_set_periodic_cal {
	struct morse_cmd_header hdr;
	__le32 periodic_cal_enabled;
} __packed;

struct mm_capabilities {
	/** capability flags */
	__le32 flags[FW_CAPABILITIES_FLAGS_WIDTH];
	/** The minimum A-MPDU start spacing required by firmware.*/
	u8 ampdu_mss;
	/** The beamformee STS capability value */
	u8 beamformee_sts_capability;
	/** Number of sounding dimensions */
	u8 number_sounding_dimensions;
	/** The maximum A-MPDU length. This is the exponent value such that
	 * (2^(13 + exponent) - 1) is the length
	 */
	u8 maximum_ampdu_length_exponent;
} __packed;

struct morse_get_capabilities_req {
	struct morse_cmd_header hdr;
} __packed;

struct morse_get_capabilities_cfm {
	struct morse_cmd_header hdr;
	__le32 status;
	struct mm_capabilities capabilities;
	/** Morse offset to apply to base mmss value */
	u8 morse_mmss_offset;
} __packed;

enum morse_standby_mode_cmd {
	/** The external host is indicating that it's now awake */
	STANDBY_MODE_CMD_EXIT = 0x0,
	/** The external host is indicating that it's going into standby mode */
	STANDBY_MODE_CMD_ENTER,
	/** The external host sets the remote standby server details */
	STANDBY_MODE_CMD_SET_SERVER_DETAILS,
	/**
	 * The external host sets a number of configuration options for
	 * standby mode
	 */
	STANDBY_MODE_CMD_SET_CONFIG,
	/**
	 * The external host provides a payload that gets appended to
	 * status frames
	 */
	STANDBY_MODE_CMD_SET_STATUS_PAYLOAD,
};

struct morse_cmd_standby_mode_exit {
	/** Reason for exiting Standby mode, see @ref morse_standby_mode_exit_reason */
	u8 reason;
	/** Connection state of STA in LMAC to be synced with mac80211 */
	u8 sta_state;
};

struct morse_cmd_standby_set_config {
	/** Interval for transmitting Standby status packets */
	__le32 notify_period_s;
	/** Period of inactivity (traffic directed to STA) before external host triggers standby
	 * mode
	 */
	__le32 inactivity_before_standby_s;
	/** Time for firmware to wait during beacon loss before entering deep sleep in seconds */
	__le32 bss_inactivity_before_deep_sleep_s;
	/** Time for firmware to remain in deep sleep in seconds */
	__le32 deep_sleep_period_s;
	/** The BSSID to monitor for activity (or lack thereof) before entering deep sleep */
	u8 monitor_bssid[ETH_ALEN];
	/** Padding for word aligned access in config. (It may grow in future) */
	u8 __padding[2];
} __packed;

struct morse_cmd_standby_set_server_details {
	/** Source IP address */
	__le32 src_ip;
	/** Destination IP address */
	__le32 dst_ip;
	/** Destination UDP Port */
	__le16 dst_port;
} __packed;

struct morse_cmd_standby_set_status_payload {
	/** The length of the payload */
	__le32 len;
	/** The payload */
	u8 payload[STANDBY_STATUS_FRAME_USER_PAYLOAD_MAX_LEN];
} __packed;

/**
 * Structure for Configuring MM standby mode
 */
struct morse_cmd_standby_mode_resp {
	struct morse_cmd_header hdr;
	/** Standby Mode subcommands, see @ref morse_standby_mode_cmd */
	__le32 status;
	union {
		u8 opaque[0];
		/** Standby mode exit status */
		struct morse_cmd_standby_mode_exit info;
	};
} __packed;

struct morse_cmd_standby_mode_req {
	struct morse_cmd_header hdr;
	/** Standby Mode subcommands, see @ref morse_standby_mode_cmd */
	__le32 cmd;
	union {
		/** Valid for STANDBY_MODE_CMD_SET_CONFIG cmd */
		struct morse_cmd_standby_set_config config;
		/** Valid for STANDBY_MODE_CMD_SET_SERVER_DETAILS cmd */
		struct morse_cmd_standby_set_server_details server_details;
		/** Valid for STANDBY_MODE_CMD_SET_STATUS_PAYLOAD cmd */
		struct morse_cmd_standby_set_status_payload set_payload;
	};
} __packed;

/* morse_standby_mode_exit_reason must be in sync with the reason code table
 * in firmware, @ref standby_mode_exit_reason_t.
 */
enum morse_standby_mode_exit_reason {
	/** No specific reason for exiting standby mode */
	STANDBY_MODE_EXIT_REASON_NONE,
	/** The STA has received the wakeup frame */
	STANDBY_MODE_EXIT_REASON_WAKEUP_FRAME,
	/** The STA needs to associate */
	STANDBY_MODE_EXIT_REASON_ASSOCIATE,
	/** The STAs external input pin has fired */
	STANDBY_MODE_EXIT_REASON_EXT_INPUT,
	/** Whitelisted packet received */
	STANDBY_MODE_EXIT_REASON_WHITELIST_PKT,
	/** TCP connection lost */
	STANDBY_MODE_EXIT_REASON_TCP_CONNECTION_LOST,
	/** HW scan is not enabled */
	STANDBY_MODE_EXIT_REASON_HW_SCAN_NOT_ENABLED,
	/** HW scan failed to start */
	STANDBY_MODE_EXIT_REASON_HW_SCAN_FAILED_TO_START,
};

enum dhcp_offload_opcode {
	/** Enable the DHCP client */
	MORSE_DHCP_CMD_ENABLE = 0,
	/** Do a DHCP discovery and obtain a lease */
	MORSE_DHCP_CMD_DO_DISCOVERY,
	/** Return the current lease */
	MORSE_DHCP_CMD_GET_LEASE,
	/** Clear the current lease */
	MORSE_DHCP_CMD_CLEAR_LEASE,
	/** Trigger a renewal of the current lease */
	MORSE_DHCP_CMD_RENEW_LEASE,
	/** Trigger a rebinding of the current lease */
	MORSE_DHCP_CMD_REBIND_LEASE,
	/** Ask the FW to send a lease update event to the driver */
	MORSE_DHCP_CMD_SEND_LEASE_UPDATE,
	/** Force uint32 */
	DHCP_CMD_LAST = U32_MAX
};

enum dhcp_offload_retcode {
	/** Command completed successfully */
	MORSE_DHCP_RET_SUCCESS = 0,
	/** DHCP Client is disabled */
	MORSE_DHCP_RET_NOT_ENABLED,
	/** DHCP Client is already enabled */
	MORSE_DHCP_RET_ALREADY_ENABLED,
	/** No current bound lease */
	MORSE_DHCP_RET_NO_LEASE,
	/** DHCP client already has a lease */
	MORSE_DHCP_RET_HAVE_LEASE,
	/** DHCP client is currently busy (discovering or renewing) */
	MORSE_DHCP_RET_BUSY,

	/** Force uint32 */
	MORSE_DHCP_RET_LAST = U32_MAX
};

struct morse_cmd_dhcpc_req {
	struct morse_cmd_header hdr;
	__le32 opcode;
} __packed;

struct morse_cmd_dhcpc_cfm {
	struct morse_cmd_header hdr;
	__le32 status;
	__le32 retcode;
	__le32 my_ip;
	__le32 netmask;
	__le32 router;
	__le32 dns;
} __packed;

/**
 * IBSS Opcode for configuring the current state to firmware
 */
enum ibss_config_opcode {
	/** Notifying the creator mode */
	MORSE_IBSS_CONFIG_CMD_CREATE = 0,
	/** Notifying the joining IBSS */
	MORSE_IBSS_CONFIG_CMD_JOIN,
	/** Notifying to leave IBSS */
	MORSE_IBSS_CONFIG_CMD_STOP,

	/** Force uint8 */
	MORSE_IBSS_CONFIG_LAST = 255,
};

/**
 * Structure for configuring the IBSS.
 *
 * @note This command is to configure IBSS parameters like BSSID and others.
 *
 * @hdr: command header
 * @bssid_addr: IBSS BSSID Address
 * @ibss_cfg_opcode: type ibss_config_opcode.
 *    If CMD_CREATE is sent, then firmware will start to generate interrupts.
 *    If CMD_JOIN i sent, then firmware will wait till we receive beacon from IBSS network,
 *      then set its TSF and start the beacon timer to be in sync with network TBTT.
 *    If CMD_STOP is sent, all the parameters will be cleared in target and stop beaconing.
 * @ibss_probe_filtering: Probe request filtering based on the last beacon tx is handled in fw.
 *        When set to 1, probe requests will be dropped in firmware, if this node didn't
 *        transmit the last beacon. Otherwise, all the probe req will be forwarded to host
 *        driver for responding with probe resp.
 */
struct morse_cmd_cfg_ibss {
	struct morse_cmd_header hdr;
	/** BSSID of IBSS generated by mac80211 */
	u8 ibss_bssid_addr[ETH_ALEN];
	/** IBSS Opcode for configuring the current state to firmware */
	u8 ibss_cfg_opcode;
	/** Flag to enable/disable probe req filtering in IBSS mode, based on the last beacon */
	u8 ibss_probe_filtering;
} __packed;

/**
 * Structure for configuring tsf offset for mesh interface
 */
struct morse_cmd_cfg_offset_tsf {
	struct morse_cmd_header hdr;
	__le64 offset_tsf;
} __packed;

struct morse_config_oui_filter_req {
	struct morse_cmd_header hdr;
	u8 n_ouis;
	u8 ouis[MAX_NUM_OUI_FILTERS][OUI_SIZE];
} __packed;

struct morse_cmd_cfg_mcast_filter {
	struct morse_cmd_header hdr;
	u8 count;
	__le32 addr_list[];
} __packed;

enum duty_cycle_mode {
	MORSE_DUTY_CYCLE_MODE_SPREAD = 0,
	MORSE_DUTY_CYCLE_MODE_BURST = 1,
};

enum duty_cycle_config_options {
	MORSE_DUTY_CYCLE_SET_CFG_DUTY_CYCLE = BIT(0),
	MORSE_DUTY_CYCLE_SET_CFG_OMIT_CTRL_RESP = BIT(1),
	MORSE_DUTY_CYCLE_SET_CFG_EXT = BIT(2),
};

struct morse_cmd_set_duty_cycle_req {
	struct morse_cmd_header hdr;
	u8 omit_ctrl_resp;
	__le32 duty_cycle;
	u8 set_configs;
	__le32 burst_record_unit_us;
	u8 mode;
} __packed;

struct morse_cmd_set_duty_cycle_cfm {
	struct morse_cmd_header hdr;
	u8 omit_ctrl_resp;
	__le32 duty_cycle;
	__le32 airtime_remaining_us;
	__le32 burst_window_duration_us;
	__le32 burst_record_unit_us;
	u8 mode;
} __packed;

enum mpsw_config_options {
	MORSE_MPSW_SET_CFG_AIRTIME_BOUNDS = BIT(0),
	MORSE_MPSW_SET_CFG_PKT_SPACE_WINDOW_LEN = BIT(1),
	MORSE_MPSW_SET_CFG_ENABLED = BIT(2)
};

struct mpsw_config {
	__le32 airtime_max_us;
	__le32 airtime_min_us;
	__le32 packet_space_window_length_us;
	u8 enable;
} __packed;

struct morse_cmd_set_mpsw_config_req {
	struct morse_cmd_header hdr;
	struct mpsw_config config;
	u8 set_configs;
} __packed;

struct morse_cmd_set_mpsw_config_cfm {
	struct morse_cmd_header hdr;
	struct mpsw_config config;
} __packed;

struct morse_cmd_get_available_channels_cfm {
	struct morse_cmd_header hdr;
	__le32 status;
	u32 num_channels;
	struct morse_channel channels[];
} __packed;

struct morse_resp_get_hw_version_cfm {
	struct morse_cmd_header hdr;
	__le32 status;
	u8 hw_version[64];
} __packed;

struct morse_cmd_set_frag_threshold_req {
	struct morse_cmd_header hdr;
	u32 frag_threshold;
} __packed;

struct morse_cmd_set_frag_threshold_cfm {
	struct morse_cmd_header hdr;
	u32 frag_threshold;
} __packed;

enum morse_cmd_hw_scan_config_flags {
	MORSE_HW_SCAN_CMD_FLAGS_START		= BIT(0),
	MORSE_HW_SCAN_CMD_FLAGS_ABORT		= BIT(1),
	MORSE_HW_SCAN_CMD_FLAGS_SURVEY		= BIT(2),
	MORSE_HW_SCAN_CMD_FLAGS_STORE		= BIT(3),
	MORSE_HW_SCAN_CMD_FLAGS_1MHZ_PROBES = BIT(4),
};

struct morse_cmd_hw_scan_req {
	struct morse_cmd_header hdr;
	u32 flags;
	u32 dwell_time_ms;
	u8 variable[];
} __packed;

/**
 * Mesh Opcode for configuring the current state to firmware
 */
enum mesh_config_opcode {
	/** Notifying the Start of Mesh */
	MORSE_MESH_CONFIG_CMD_START = 0,
	/** Notifying to leave Mesh */
	MORSE_MESH_CONFIG_CMD_STOP,

	/** Force uint8 */
	MORSE_MESH_CONFIG_LAST = __UINT8_MAX__,
};

/**
 * PV1 opcode for sending store param from Header Compression to firmware
 */
enum pv1_hc_store_opcode {
	/** Notify A3/A4 update to store */
	MORSE_PV1_STORE_A3_A4 = 0,
	/* TODO: Add opcode for CCMP update */

	/** Force uint8 */
	MORSE_PV1_STORE_LAST = __UINT8_MAX__,
};

/**
 * Structure for configuring the Mesh.
 *
 * @note This command is to configure Mesh parameters.
 *
 * @hdr: command header
 * @mesh_cfg_opcode: type mesh_config_opcode.
 *    If CMD_START is sent, then firmware will start to generate beacon interrupts.
 *    If CMD_STOP is sent, all the parameters will be cleared in target and stop beaconing.
 * @mesh_beaconing: To enable/disable Mesh beaconless mode
 * @mbca_config: Configuration to enable/disable MBCA TBTT selection and adjustment.
 */
struct morse_cmd_cfg_mesh {
	struct morse_cmd_header hdr;
	/** Mesh Opcode for configuring the current state to firmware */
	u8 mesh_cfg_opcode;
	/** Flag to enable/disable beaconing in Mesh */
	u8 mesh_beaconing;
	/** Configuration to enable/disable MBCA TBTT selection and adjustment */
	u8 mbca_config;
	/** Minimum gap between our beacons and neighbour beacons */
	u8 min_beacon_gap_ms;
	/** Duration of the scan during start of the MBSS in milliseconds */
	u16 mbss_start_scan_duration_ms;
	/** TBTT adjustment timer interval in milliseconds */
	u16 tbtt_adj_timer_interval_ms;
} __packed;

enum morse_cmd_host_tx_blocked_flags {
	/** Block TX channel for all data and management frames */
	MORSE_CMD_HOST_BLOCK_TX_FRAMES	= BIT(0),
	/** Block TX channel for commands */
	MORSE_CMD_HOST_BLOCK_TX_CMD	= BIT(1),
};

enum morse_param_action {
	MORSE_PARAM_ACTION_SET = 0,
	MORSE_PARAM_ACTION_GET = 1,

	MORSE_PARAM_ACTION_LAST,
	MORSE_PARAM_ACTION_MAX = __UINT8_MAX__,
};

enum morse_param_id {
	MORSE_PARAM_ID_MAX_TRAFFIC_DELIVERY_WAIT_US	= 0,
	MORSE_PARAM_ID_EXTRA_ACK_TIMEOUT_ADJUST_US	= 1,
	MORSE_PARAM_ID_COUNTRY = 13,
	MORSE_PARAM_ID_RTS_THRESHOLD = 14,
	MORSE_PARAM_ID_HOST_TX_BLOCK = 15,
	MORSE_PARAM_ID_NON_TIM_MODE = 17,

	MORSE_PARAM_ID_LAST,
	MORSE_PARAM_ID_MAX = __UINT32_MAX__,
};

struct morse_cmd_pv1_rx_ampdu_state {
	struct morse_cmd_header hdr;
	u8 addr[ETH_ALEN];
	u8 tid;
	u8 ba_session_enable;
	u16 buf_size;
} __packed;

struct morse_cmd_page_slicing_config {
	struct morse_cmd_header hdr;
	/** Page slicing enabled or disabled */
	u8 enabled;
} __packed;

enum power_mode {
	POWER_MODE_SNOOZE,
	POWER_MODE_DEEP_SLEEP,
	POWER_MODE_HIBERNATE,
};

struct morse_cmd_force_power_mode {
	struct morse_cmd_header hdr;
	/** Mode of power to force see @ref power_mode */
	u32 mode;
} __packed;

struct morse_cmd_li_sleep {
	struct morse_cmd_header hdr;
	/** Listen interval value if enabled */
	__le32 listen_interval;
} __packed;

struct morse_cmd_param_req {
	struct morse_cmd_header hdr;
	/** The param to perform the action on [enum morse_param_id] */
	u32 param_id;
	/** The action to take on the param [get | set] */
	u32 action;
	/** Any flags to modify the behaviour of the action (for forward/backward compatibility) */
	u32 flags;
	/** The value to set (only applicable for set actions) */
	u32 value;
} __packed;

struct morse_cmd_param_cfm {
	/** Common command header */
	struct morse_cmd_header hdr;
	/** Common status set by chip on command responses */
	__le32 status;
	/**
	 * Any flags to signal change of interpretation of response
	 * (forwards/backwards compatibility)
	 */
	u32 flags;
	/** The value returned (only applicable for get actions) */
	u32 value;
} __packed;

struct morse_disabled_channel_entry {
	/** Frequency (100 kHz units for compression) */
	__le16 freq_100khz;
	/** Bandwidth (MHz) */
	u8 bw_mhz;
} __packed;

struct morse_resp_get_disabled_channels {
	struct morse_cmd_header hdr;
	__le32 status;
	/** Number of channels in response */
	__le32 n_channels;
	/** Variable length array of disabled channels */
	struct morse_disabled_channel_entry channels[];
} __packed;

struct morse_cmd_start_scan {
	struct morse_cmd_header hdr;
	__le32 dwell_time_ms;
	u8 n_ssids;
	u8 __padding[2];
	u8 ssid_len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	__le16 extra_ies_len;
	u8 extra_ies[SCAN_EXTRA_IES_MAX_LEN];
} __packed;

struct morse_cmd_abort_scan {
	struct morse_cmd_header hdr;
} __packed;

enum connect_auth_type {
	CONNECT_AUTH_TYPE_INVALID = 0,
	CONNECT_AUTH_TYPE_OPEN = 1,
	CONNECT_AUTH_TYPE_OWE = 2,
	CONNECT_AUTH_TYPE_SAE = 3,
	/** Any authentication type may be used when connecting. */
	CONNECT_AUTH_TYPE_AUTOMATIC = 255,
};

struct morse_cmd_connect {
	struct morse_cmd_header hdr;
	u8 auth_type;
	u8 ssid_len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 __padding[3];
	u8 sae_pwd_len;
	u8 sae_pwd[MORSE_SAE_PASSWORD_MAX_LEN];
} __packed;

struct morse_cmd_disconnect {
	struct morse_cmd_header hdr;
} __packed;

struct morse_cmd_get_connection_state_req {
	struct morse_cmd_header hdr;
} __packed;

/**
 * struct morse_cmd_get_connection_state_cfm - Confirm message for GET_CONNECTION_STATE.
 * @hdr: Common command header.
 * @status: Command status.
 * @beacon_interval_tu: Beacon interval in TUs.
 * @dtim_period: DTIM period.
 * @rssi: Signal strength of the most recently received beacon in dBm as signed 16-bit.
 * @connected_time_s: Time since connection was established in seconds.
 */
struct morse_cmd_get_connection_state_cfm {
	struct morse_cmd_header hdr;
	__le32 status;
	__le16 beacon_interval_tu;
	__le16 dtim_period;
	__le16 rssi;
	u8 __padding[2];
	__le32 connected_time_s;
} __packed;

int morse_cmd_set_duty_cycle(struct morse *mors, enum duty_cycle_mode mode,
			     int duty_cycle, bool omit_ctrl_resp);
int morse_cmd_set_mpsw(struct morse *mors, int min, int max, int window);

int morse_cmd_set_ps(struct morse *mors, bool enabled, bool enable_dynamic_ps_offload);
int morse_cmd_set_txpower(struct morse *mors, s32 *out_power, int txpower);
int morse_cmd_get_max_txpower(struct morse *mors, s32 *out_power);
int morse_cmd_add_if(struct morse *mors, u16 *id, const u8 *addr, enum nl80211_iftype type);
int morse_cmd_rm_if(struct morse *mors, u16 id);
int morse_cmd_resp_process(struct morse *mors, struct sk_buff *skb);
int morse_cmd_cfg_bss(struct morse *mors, u16 id, u16 beacon_int, u16 dtim_period, u32 cssid);
int morse_cmd_vendor(struct morse *mors, struct ieee80211_vif *vif,
		     const struct morse_cmd_vendor *cmd, int cmd_len,
		     struct morse_resp_vendor *resp, int *resp_len);
int morse_wiphy_cmd_vendor(struct morse *mors,
		     const struct morse_cmd_vendor *cmd, int cmd_len,
		     struct morse_resp_vendor *resp, int *resp_len);
int morse_cmd_set_channel(struct morse *mors, u32 op_chan_freq_hz,
			  u8 pri_1mhz_chan_idx, u8 op_bw_mhz, u8 pri_bw_mhz, s32 *power_dbm);
int morse_cmd_get_current_channel(struct morse *mors, u32 *op_chan_freq_hz,
				  u8 *pri_1mhz_chan_idx, u8 *op_bw_mhz, u8 *pri_bw_mhz);
int morse_cmd_get_version(struct morse *mors);
int morse_cmd_cfg_scan(struct morse *mors, bool enabled);
int morse_cmd_get_channel_usage(struct morse *mors, struct morse_survey_rx_usage_record *record);

int morse_cmd_sta_state(struct morse *mors, struct morse_vif *mors_vif,
			u16 aid, struct ieee80211_sta *sta, enum ieee80211_sta_state state);
int morse_cmd_disable_key(struct morse *mors, struct morse_vif *mors_vif,
			  u16 aid, struct ieee80211_key_conf *key);
int morse_cmd_install_key(struct morse *mors, struct morse_vif *mors_vif,
			  u16 aid, struct ieee80211_key_conf *key, enum morse_key_cipher cipher,
			  enum morse_aes_key_length length);
int morse_cmd_set_cr_bw(struct morse *mors, struct morse_vif *mors_vif, u8 direction,
			u8 cr_1mhz_en);
int morse_cmd_cfg_qos(struct morse *mors, struct morse_queue_params *params);
int morse_cmd_set_bss_color(struct morse *mors, struct morse_vif *mors_vif, u8 color);
int morse_cmd_health_check(struct morse *mors);
int morse_cmd_arp_offload_update_ip_table(struct morse *mors, u16 vif_id,
					  int arp_addr_count, u32 *arp_addr_list);
int morse_cmd_get_capabilities(struct morse *mors,
			       u16 vif_id, struct morse_caps *capabilities);
/**
 * morse_cmd_config_non_tim_mode() - Configure non-TIM mode.
 *
 * @mors: morse chip struct
 * @enable: enable or disable non-TIM mode
 * @vif_id: interface id
 *
 * @return 0 on success, else error code
 */
int morse_cmd_config_non_tim_mode(struct morse *mors, bool enable, u16 vif_id);
int morse_cmd_enable_li_sleep(struct morse *mors,  u16 listen_interval, u16 vif_id);
int morse_cmd_dhcpc_enable(struct morse *mors, u16 vif_id);
int morse_cmd_twt_agreement_validate_req(struct morse *mors,
					 struct morse_twt_agreement_data *agreement, u16 iface_id);
int morse_cmd_twt_agreement_install_req(struct morse *mors,
					struct morse_twt_agreement_data *agreement, u16 iface_id);
int morse_cmd_twt_remove_req(struct morse *mors,
			     struct morse_cmd_remove_twt_agreement *twt_remove_cmd, u16 iface_id);
int morse_cmd_cfg_ibss(struct morse *mors, u16 id,
		       const u8 *bssid, bool ibss_creator, bool stop_ibss);
int morse_cmd_cfg_offset_tsf(struct morse *mors, u16 vif_id, s64 offset_tsf);
int morse_cmd_config_beacon_timer(struct morse *mors, struct morse_vif *mors_vif, bool enabled);
int morse_cmd_store_pv1_hc_data(struct morse *mors, struct morse_vif *mors_vif,
				 struct ieee80211_sta *sta, u8 *a3, u8 *a4, bool is_store_in_rx);

/**
 * @brief Configure the OUI filter in the FW. If a beacon is received containing a vendor element
 * with an OUI matching one in the filter, it will unconditionally pass the beacon up to the host.
 *
 * @param mors Morse object
 * @param mors_vif morse interface
 * @return 0 on success, else error code
 */
int morse_cmd_update_beacon_vendor_ie_oui_filter(struct morse *mors, struct morse_vif *mors_vif);

int morse_cmd_cfg_multicast_filter(struct morse *mors, struct morse_vif *mors_vif);

int morse_cmd_get_available_channels(struct morse *mors, struct morse_resp *resp);
int morse_cmd_get_hw_version(struct morse *mors, struct morse_resp *resp);

int morse_cmd_set_frag_threshold(struct morse *mors, u32 frag_threshold);

/**
 * morse_cmd_cfg_mesh() -  Configure mesh bss parameters in the firmware.
 *
 * @mors: morse chip struct
 * @mors_vif: pointer to morse interface
 * @stop_mesh: Flag to start or stop the mesh interface.
 * @mesh_beaconing: Flag to enable or disable beaconing.
 *
 * Return: 0 on success, else error code
 */
int morse_cmd_cfg_mesh(struct morse *mors, struct morse_vif *mors_vif, bool stop_mesh,
		       bool mesh_beaconing);

/**
 * morse_cmd_ack_timeout_adjust() - Configure ack timeout in the firmware.
 *
 * @mors: Morse object
 * @vif_id: interface id
 * @timeout_us: ACK timeout adjustment value in usecs
 *
 * Return: 0 on success, else error code
 */
int morse_cmd_ack_timeout_adjust(struct morse *mors, u16 vif_id, u32 timeout_us);

/**
 * morse_cmd_pv1_set_rx_ampdu_state() - Configure RX AMPDU state for PV1 STA in the firmware.
 *
 * @mors_vif: morse interface object
 * @sta_addr: Station address for which BA session is established
 * @tid: TID for which BA session is established
 * @buf_size: A-MPDU reordering buffer size
 * @ba_session_enable: BA session enabled or disabled
 *
 * Return: 0 on success, else error code
 */
int morse_cmd_pv1_set_rx_ampdu_state(struct morse_vif *mors_vif, u8 *sta_addr, u8 tid,
		u16 buf_size, bool ba_session_enable);

/**
 * morse_cmd_configure_page_slicing() - Configure page slicing in target.
 *
 * @mors_vif: morse interface object
 * @enable: enable status of page slicing.
 *
 * Return: 0 on success, else error code
 */
int morse_cmd_configure_page_slicing(struct morse_vif *mors_vif, bool enable);

/**
 * morse_cmd_hw_scan() - Configure HW scanning
 *
 * @mors: Morse structure
 * @params: HW scan parameters
 * @store: Whether to save the HW scan configuration in chip (for standby mode)
 *
 * Return: 0 on success, else error code
 */
int morse_cmd_hw_scan(struct morse *mors, struct morse_hw_scan_params *params, bool store);

/**
 * morse_cmd_get_disabled_channels() - Retrieve channels that are disabled by hardware.
 *
 * @mors: Morse structure
 * @resp: Allocated response to store disabled channels in
 * @resp_len: Size of allocated response buffer.
 *
 * Return: 0 on success, else error code
 */
int morse_cmd_get_disabled_channels(struct morse *mors,
				    struct morse_resp_get_disabled_channels *resp,
				    uint resp_len);
int morse_cmd_set_country(struct morse *mors, const char *country_code);
int morse_cmd_set_rts_threshold(struct morse *mors, u32 rts_threshold);
int morse_cmd_start_scan(struct morse *mors, u8 n_ssids, const u8 *ssid, size_t ssid_len,
			 const u8 *extra_ies, size_t extra_ies_len, u32 dwell_time_ms);
int morse_cmd_abort_scan(struct morse *mors);
int morse_cmd_connect(struct morse *mors, const u8 *ssid, size_t ssid_len,
		      enum nl80211_auth_type auth_type,
		      const u8 *sae_pwd, size_t sae_pwd_len);
int morse_cmd_disconnect(struct morse *mors);
int morse_cmd_get_connection_state(struct morse *mors, s8 *signal,
				   u32 *connected_time_s, u8 *dtim_period,
				   u16 *beacon_interval_tu);

#endif /* !_MORSE_COMMAND_H_ */
