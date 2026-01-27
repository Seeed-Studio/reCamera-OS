#ifndef _MORSE_WIPHY_H_
#define _MORSE_WIPHY_H_

/*
 * Copyright 2017-2023 Morse Micro
 *
 */
#include <net/mac80211.h>

/**
 * morse_wiphy_to_morse -  Look up &struct mors inside &struct wiphy
 *
 * Return: pointer to &struct mors
 */
struct morse *morse_wiphy_to_morse(struct wiphy *wiphy);

/**
 * morse_wiphy_init() -  Init wiphy device
 * @morse: morse device instance
 *
 * Initilise wiphy device
 *
 * Return: 0 success, else error.
 */
int morse_wiphy_init(struct morse *mors);

/**
 * morse_wiphy_register() -  Register wiphy device
 * @morse: morse device instance
 *
 * Register wiphy device
 *
 * Return: 0 success, else error.
 */
int morse_wiphy_register(struct morse *mors);

/**
 * morse_wiphy_create() -  Create wiphy device
 * @priv_size: extra size per structure to allocate
 * @dev: Bus device structure
 *
 * Allocate memory for wiphy device and do basic initialisation.
 *
 * Return: morse device struct, else NULL.
 */
struct morse *morse_wiphy_create(size_t priv_size, struct device *dev);

/**
 * morse_wiphy_stop() -  Stop wiphy device in preparation for chip restart
 * @mors: morse device instance
 */
void morse_wiphy_stop(struct morse *mors);

/**
 * morse_wiphy_restarted() -  Notify wiphy device that chip restarted
 * @mors: morse device instance
 *
 * Device state will be reset and userspace will be informed that the connection was lost.
 */
void morse_wiphy_restarted(struct morse *mors);

/**
 * morse_wiphy_deinit() -  Deinit wiphy device
 * @morse: morse device instance
 *
 * Deinitilise wiphy device
 *
 * Return: None.
 */
void morse_wiphy_deinit(struct morse *mors);

/**
 * morse_wiphy_destroy() -  Destroy wiphy device
 * @morse: morse device instance
 *
 * Free wiphy device
 * Acquires and releases the rtnl lock.
 *
 * Return: None.
 */
void morse_wiphy_destroy(struct morse *mors);

/**
 * morse_wiphy_rx() -  Receive WIPHY (802.3) packet
 * @mors: Morse state struct
 * @skb: SKB packet
 *
 * Passes the packet to upper layers.
 * Must be invoked from process context.
 */
void morse_wiphy_rx(struct morse *mors, struct sk_buff *skb);

/**
 * morse_wiphy_scan_result() -  Process a result from an in-progress scan
 * @mors: morse device instance
 * @result: scan result event data
 *
 * Return: 0 on success, else error.
 */
int morse_wiphy_scan_result(struct morse *mors, struct morse_evt_scan_result *result);

/**
 * morse_wiphy_scan_done() -  Mark scan as complete
 * @mors: morse device instance
 * @aborted: true if the scan terminated without scanning all channels
 */
void morse_wiphy_scan_done(struct morse *mors, bool aborted);

/**
 * morse_wiphy_connected() -  Mark connection as established
 * @mors: morse device instance
 * @bssid: BSSID which the chip is connected to
 */
void morse_wiphy_connected(struct morse *mors, const u8 *bssid);

/**
 * morse_wiphy_connected() -  Mark connection as lost
 * @mors: morse device instance
 */
void morse_wiphy_disconnected(struct morse *mors);

#endif /* !_MORSE_WIPHY_H_ */
