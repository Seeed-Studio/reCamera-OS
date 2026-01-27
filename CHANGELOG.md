## 0.2.3 (2026-01-27)

### sg2002_recamera_emmc

- New Features:
    - `action=train` flow: fetch model info via train API v2, download model, upload to device, create cloud app, deploy dashboard flow, auto‑navigate to dashboard when ready.
    - Default dashboard flow moved to `src/utils/flowDefaults.ts` and re‑exported via `src/utils/index.ts`.
    - Train flow enhancements: device type guard, dashboard readiness polling, upload progress, slow‑upload warning, cancel upload (abort).
    - `action=model` flow selection: use `DefaultFlowDataWithDashboard` when `task=classify` and `model_format=cvimodel`, otherwise use default flow; update `model` node fields before deploy.
    - App list UI fixes: overflow handling and tooltip for long names.
    - One‑click deploy script `scripts/deploy.sh`.
    - Train API v2 model info wrappers and types.
    - `action=train` now creates a cloud app (name `classify_<model>`), deploys flow, and redirects in‑tab to dashboard after readiness check.
    - `action=model` logs applyModel response and conditionally auto‑opens dashboard.
    - Redirect handling improved: `redirect_url` encoded; session action cached/cleaned to avoid accidental re‑runs.
    - `sensecraftRequest` refresh now retries on `code=401`.

- Bug Fixes:
    - App name overflow no longer hides edit/delete buttons.
    - Upload flow provides progress, warning, and cancel option.
    - Dashboard jump uses current tab with readiness gating.

## 0.2.2 (2026-01-06)

### sg2002_recamera_emmc

- New Features:
    - Add model conversion feature
    - Enable SSH server by default

- Bug Fixes:
    - Disable cdc-acm mode to resolve the issue of being unable to access devices via USB on Linux
    - Solve the problem of incomplete user password input
    - Solve the problem of model upload failure

## 0.2.1 (2025-09-12)

### sg2002_recamera_emmc

- New Features:
    - File browser
    - SSH on/off
    - Optimize network connection
    - Upgrade node-red to 4.1.0
    - Add CDC support
    - Specified node-reddash@1.26.0
    - Increase ION size to 60M
    - Set TPU max to 700MHz

- Bug Fixes:
    - Resolved 'isp err chk:7271(): CSIBDG A fifo overflow'
    - Resolved some other bugs

## 0.2.0 (2025-03-31)

### sg2002_recamera_emmc

- New Features:
    - Add gimbal automatic calibration
    - Add Node-red gimbal flow

- Bug Fixes:
    - Resolved various bugs in the SSCMA

## 0.1.5 (2025-01-22)

### sg2002_recamera_emmc

- New Features:
    - Added support for device discovery
    - Added support for 63-byte WiFi passwords
    - Added support for audio recording
    - Added ability to enable/disable nodes in the SSCMA Node
    - Introduced a more user-friendly interface for improved interaction

- Bug Fixes:
    - Resolved various bugs in the SSCMA Node
    - Resolved various bugs in the SSCMA Supervisor

If you need any more modifications, feel free to ask!
## 0.1.4 (2024-12-23)

### sg2002_recamera_emmc

- New features:
    - support fip and boot partition auto update
    - support /mnt/system/upgrade.sh start *ota.zip
    - support gimbal (spi-can mcp2518fd)
    - add cvi_pinmux tool
    - use udev instead of mdev
    - udev rules for sd auto mount

- Fix bugs:
    - factory reset failed (if sg2002_recamera_emmc_md5sum.txt is not exist)
    - solve the problem that the pc cannot access the Internet when connected via usb
    - dnsmasq fails to run without wifi

## 0.1.3 (2024-11-08)

### sg2002_recamera_emmc

- New features:
    - support 64G emmc

## 0.1.2 (2024-11-01)

### sg2002_recamera_emmc

- New features:
    - support global.gc operation
    - built-in model files

- Fix bugs:
    - solve some supervisor bugs
    - solve some sscma-node bugs

## 0.1.1 (2024-10-30)

### sg2002_recamera_emmc

- New features:
    - limit node-red to version 3.1.14

- Fix bugs:
    - solve some supervisor bugs
    - fix sd_gen_recovery_image.sh script bugs

## 0.1.0 (2024-10-28)

### sg2002_recamera_emmc

- New features:
    - update ov5647 isp params (denoise)
    - optimize ota

## 0.0.9 (2024-10-26)

### sg2002_recamera_emmc

- New features:
    - enable nodejs --v8-lite-mode (disable WebAssembly)
    - optimize system upgrade operations

- Fix bugs:
    - remove node-red-dashboard from node-red
    - solve some sscma-node bug

## 0.0.8 (2024-10-22)

### sg2002_recamera_emmc

- New features:
    - auto swapon /userdata/.swapfile
    - reduce node-red startup time (skip npm -v)
    - supervior
        - add the operation that AP will automatically turn on or off according to the status of WiFi
        - split the wifi scan into two operations: scan wifi and get scan results
        - add a judgement that an upgrade is in progress

- Fix bugs:
    - solve some supervisor bug
    - solve some sscma-node bug

## 0.0.7 (2024-10-16)

### sg2002_recamera_emmc

- New features:
    - reduce ION_SIZE to 50M
    - kernel support swap
    - kernel support advise syscalls
    - update c-ares to 1.32.2
    - update libuv to 1.48.0
    - update nodejs to 22.8.0
    - update node-red to 4.0.0
    - sd supports hotplug
    - close swupdate auto start (`sudo /usr/lib/swupdate/swupdate.sh`)

- Fix bugs:
    - solve some supervisor bug
    - solve some sscma-node bug

## 0.0.6 (2024-10-12)

### sg2002_recamera_emmc

- New features:
    - sensor auto detection (ov5647/sc530ai)
    - supports restore to factory
    - get mac & sn from efuse
    - add sscma-node program
    - supervisor
        - support https service
        - add service status detection
        - add a feature for file management
        - add wifi password verification operation

- Fix bugs:
    - solve some compilation issues
    - solve some supervisor bugs

## 0.0.5 (2024-09-25)

### sg2002_recamera_emmc

- New features:
    - update node-red interface style
    - supervisor
        - add APIs for uploading models and getting model information
        - allow CORS

- Fix bugs:
    - fix the problem that some configuration files did not exist
    - node-red start failed when reset system
    - solve some supervisor bugs

- Docs:
    - add compilation notes

## 0.0.4 (2024-09-12)

### sg2002_recamera_emmc

- New features:
    - buildin supervisor
    - buildin npm@8.11.0 and node-red@v3.1.11
    - update ov5647 isp params
    - supports rootfs overlay (/bin /etc /lib /home /root /usr /var)
    - add recamera as default user
    - supports sudo
    - upgrade icu to 73-2

- Fix bugs:
    - upgrade.sh checksum failed
    - fixed ov5647 mirror

## 0.0.3 (2024-08-30)

### sg2002_recamera_emmc
- update sdk upstream (6cd7a5b)
- support more buildroot packages (mosquitto/avahi/opkg/live555)
- remove reCamera app
- change ota (not compatible with last version) and supports swupdate
- use ncm replace of rndis
- expand rootfs size to 512M, rootfs default readonly (rootfs_rw (on|off))
- auto mount /dev/mmcblk0p6 to /userdata
- update nodejs to 17.9.1

## 0.0.2 (2024-06-24)

### sg2002_recamera_emmc
- update sdk upstream
- support booting from sd (sg2002_reCamera_0.0.2_emmc_sd_compat.zip)
- support sd recovery (sg2002_reCamera_0.0.2_emmc_recovery.zip)

## 0.0.1 (2024-06-21)

### sg2002_recamera_emmc
- First beta release
- SDK: supported emmc/sdcard/leds/wifi/uart/ethernet/ov5647 sensor
- APP: complete basic functions(Overview/Security/Newwork/Terminal/Setting)
