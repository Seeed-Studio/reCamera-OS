int cvi_board_init(void)
{
	// WIFI/BT
	PINMUX_CONFIG(AUX0, XGPIOA_30); // BT_REG_ON & WIFI_REG_ON -- gpio510
	PINMUX_CONFIG(JTAG_CPU_TMS, UART1_RTS);
	PINMUX_CONFIG(JTAG_CPU_TCK, UART1_CTS);
	PINMUX_CONFIG(IIC0_SDA, UART1_RX);
	PINMUX_CONFIG(IIC0_SCL, UART1_TX);

	// Camera
	PINMUX_CONFIG(PWR_WAKEUP0, PWR_GPIO_6); // CAM_EN -- gpio358
	PINMUX_CONFIG(PWR_GPIO1, IIC2_SCL); // PWR_GPIO1 -- IIC2_SCL
	PINMUX_CONFIG(PWR_GPIO2, IIC2_SDA); // PWR_GPIO2 -- IIC2_SDA

	// Red & Blue leds
	PINMUX_CONFIG(SPK_EN, XGPIOA_15); // GPIO15/IR_CUT -- gpio495
	PINMUX_CONFIG(GPIO_ZQ, PWR_GPIO_24); // PAD_ZQ -- gpio376
	// White led
	PINMUX_CONFIG(PWR_GPIO0, PWR_GPIO_0); // PWR_GPIO0 -- gpio352

	return 0;
}


#include <common.h>
#include <command.h>

static int do_cvi_otp(struct cmd_tbl* cmdtp, int flag, int argc,
    char* const argv[])
{
    extern int64_t cvi_efuse_read_from_shadow(uint32_t addr);

    int i = 0;
    uint32_t buf[5];
    unsigned char* pu8 = NULL;
    char str[32] = "";
    char* str_get;
    unsigned char save_flag = 0;

    for (i = 0; i < ARRAY_SIZE(buf); i++) {
        buf[i] = cvi_efuse_read_from_shadow(0x40 + i * sizeof(uint32_t));
    }

    str_get = env_get("ethaddr");
    if ((buf[0] != 0x0) && (buf[0] != 0xffffffff)) {
        pu8 = (char*)buf;
        sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x", pu8[0], pu8[1], pu8[2], pu8[3], pu8[4], pu8[5]);
        printf("mac: %s\n", str);
        if ((NULL == str_get) || strcmp(str, str_get)) {
            save_flag = 1;
        	env_set("ethaddr", str);
        }
    } else {
        printf("random mac: %s\n", (NULL == str_get) ? "null" : str_get);
    }

    str_get = env_get("sn");
    pu8 = (char*)&buf[2];
    sprintf(str, "%02X%02X%02X%02X%02X%02X%02X%02X%02X",
        pu8[0], pu8[1], pu8[2], pu8[3], pu8[4], pu8[5], pu8[6], pu8[7], pu8[8]);
    printf("sn: %s\n", str);
    if ((NULL == str_get) || strcmp(str, str_get)) {
        save_flag = 1;
    	env_set("sn", str);
    }

    if (save_flag) {
        env_save();
    }

    return 0;
}

U_BOOT_CMD_COMPLETE(
    cvi_otp, CONFIG_SYS_MAXARGS, 1, do_cvi_otp,
    "load efuse info to env",
    "    - load efuse info to env",
    var_complete);
