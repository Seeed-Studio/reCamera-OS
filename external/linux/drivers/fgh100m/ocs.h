/*
 * Copyright 2023 Morse Micro
 */

#include "morse.h"
#include "command.h"

#define MORSE_OCS_AID		(AID_LIMIT + 1)	/* Use an unused AID */

int morse_ocs_cmd_post_process(struct morse_vif *mors_vif, const struct morse_resp_ocs *resp,
			       const struct morse_cmd_ocs *cmd);
int morse_evt_ocs_done(struct morse_vif *mors_vif, struct morse_event *event);
