/*
 * Copyright 2022 Morse Micro.
 *
 */

#include "mmrc_osal.h"


void osal_mmrc_seed_random(void)
{
#ifdef CONFIG_MORSE_RC
	prandom_seed(jiffies);
#else
	srand(time(NULL));
#endif
}

uint32_t osal_mmrc_random_u32(uint32_t max)
{
#ifdef CONFIG_MORSE_RC
	return prandom_u32() % max;
#else
	return rand() % max;
#endif
}
