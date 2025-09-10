#!/bin/sh
${CVI_SHOPTS}
#
# Start to insert kernel modules
#
insmod /mnt/system/ko/cv181x_sys.ko
insmod /mnt/system/ko/cv181x_base.ko
insmod /mnt/system/ko/cv181x_rtos_cmdqu.ko
insmod /mnt/system/ko/cv181x_fast_image.ko
insmod /mnt/system/ko/cvi_mipi_rx.ko
insmod /mnt/system/ko/snsr_i2c.ko
insmod /mnt/system/ko/cv181x_vi.ko vi_log_lv=1
insmod /mnt/system/ko/cv181x_vpss.ko vpss_log_lv=1
insmod /mnt/system/ko/cv181x_dwa.ko
insmod /mnt/system/ko/cv181x_vo.ko vo_log_lv=1
#insmod /mnt/system/ko/cv181x_mipi_tx.ko
insmod /mnt/system/ko/cv181x_rgn.ko

#insmod /mnt/system/ko/cv181x_wdt.ko
insmod /mnt/system/ko/cv181x_clock_cooling.ko

insmod /mnt/system/ko/cv181x_tpu.ko
insmod /mnt/system/ko/cv181x_vcodec.ko
insmod /mnt/system/ko/cv181x_jpeg.ko
insmod /mnt/system/ko/cvi_vc_driver.ko MaxVencChnNum=9 MaxVdecChnNum=9
#insmod /mnt/system/ko/cv181x_rtc.ko
insmod /mnt/system/ko/cv181x_ive.ko

insmod /mnt/system/ko/cfg80211.ko
insmod /mnt/system/ko/brcmutil.ko
insmod /mnt/system/ko/brcmfmac.ko

echo 3 > /proc/sys/vm/drop_caches
dmesg -n 4

# Shared GPIOs with TF card
[ ! -e /dev/mmcblk1 ] && {
    # gimbal
    [ -x /usr/bin/gimbal ] && {
        /usr/bin/gimbal init > /dev/null
        /usr/bin/gimbal cali > /dev/null&
    }

    # PoE
    [ -z "$(ifconfig can0 2>/dev/null)" ] && {
        PINMUX="/mnt/system/usr/bin/cvi_pinmux"

        $PINMUX -w SD0_CLK/XGPIOA_7 # 487
        $PINMUX -w SD0_CMD/XGPIOA_8 # 488
        $PINMUX -w SD0_D0/UART3_TX #XGPIOA_9 489
        $PINMUX -w SD0_D1/XGPIOA_10 #UART1_TX 490
        $PINMUX -w SD0_D2/XGPIOA_11 #UART1_RX 491
        $PINMUX -w SD0_D3/UART3_RX #XGPIOA_12 492
    }
}

#usb hub control
#/etc/uhubon.sh host

exit $?
