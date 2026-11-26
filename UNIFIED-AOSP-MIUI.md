# unified-aosp-miui —— 一份源码 / 一个 Image，同时吃 AOSP 与 MIUI·HyperOS

这个分支从干净的 `master` 拉出来，只做两件事：

1. **把 MIUI/HyperOS 需要的显示适配直接写进源码**（不再靠 CI 里 sed/python 打补丁）
2. **把配置级适配固化进 `arch/arm64/configs/vendor/munch.config`**（源码内的 defconfig fragment）

编出来的产物就是 `Image.gz` + `kona-v2.1.dtb`，不带 AnyKernel3；AOSP 与 MIUI/HyperOS 刷同一个。

---

## 一、显示适配（源码内）

| 文件 | 改动 | 作用 |
|---|---|---|
| `arch/arm64/boot/dts/vendor/qcom/dsi-panel-l11r-38-08-0a-dsc-cmd.dtsi` | `mi,panel-id = "L11R", "8\b"`、`mi,mdss-dsi-panel-dc-type = <0>` | MIUI 亮度服务按 panel-id 匹配面板 |
| `arch/arm64/boot/dts/vendor/qcom/munch-sm8250.dtsi` | `&mdss_mdp { #cooling-cells = <0>; }` | 背光克隆热器件的父节点要求，缺了直接 `-ENODEV` |
| `drivers/video/backlight/backlight.c` | 新增 `brightness_clone` 属性 | MIUI 亮度服务读写的克隆亮度节点 |
| `techpack/display/msm/sde/sde_connector.c` | 克隆热器件注册失败**不再注销**真正的背光设备 | 否则 `/sys/class/backlight` 空掉 → 有画面无背光（全黑） |

---

## 二、配置级适配（源码内）

全部写在 **`arch/arm64/configs/vendor/munch.config`**。
这是 defconfig fragment，`make vendor/munch_defconfig vendor/munch.config` 时在 defconfig **之后**合并，优先级更高 —— 所以这些值就是最终 `out/.config` 的值，**不依赖 CI 里的临时 `scripts/config -e/-d`**。

取值依据两条：

- 原厂实跑 config（从 munch 原厂 `boot.img` 内嵌 IKCONFIG 提取）
- ki 内核（同 sm8250 / 4.19.325，已实测能开机日用）的最终 `.config`

> ⚠️ ki 内核里那批小米私有开关（`XIAOMI_MIUI`、`MILLET`、`MIGT`、`SF_BINDER`、`MI_RECLAIM`、`BOOTUP_RECLAIM`、`RTMM`）是**那棵树自带**的 Kconfig 符号。
> 本树（fmxr044 的 lineage 底座）里**不存在**这些符号，写进 defconfig 会被 kconfig 直接丢弃。所以不能照抄那几张表来"适配"，只能落本树真实存在、且真正影响硬件通路的项。

### 固化清单

**WLAN（munch = QCA6390 + 三天线）**
`CNSS2` / `CNSS_QCA6390` / `CNSS_UTILS` / `WCNSS_MEM_PRE_ALLOC` / `WIFI_THREE_ANTENNA` / `QCA_CLD_WLAN=y` + `QCA_CLD_WLAN_PROFILE="qca6390"` / `CFG80211` / `RFKILL`

> 关键：这棵树把 qcacld 编进内核（AOSP 侧没有 vendor 的 `wlan.ko`），平台层 cnss2 必须带 QCA6390 支持。
> `CNSS_QCA6390=n` 时 cnss2 probe 直接失败 → **WiFi 完全不出**，这是"wifi 全炸"的直接原因。

**存储 / 加密（MIUI·HyperOS 的 /data 与 /metadata）**
`EXT4_FS` / `EXT4_ENCRYPTION` / `FS_ENCRYPTION` / `FS_ENCRYPTION_INLINE_CRYPT` / `ECRYPT_FS` / `KEYS` / `ZRAM` / `ZRAM_WRITEBACK`

> 原厂 `EXT4_ENCRYPTION=y`、`ECRYPT_FS=y`，本树之前是关的。`/data` 走 ext4 + fscrypt 时，关掉会解密失败。

**电量计 / 充电**
`QPNP_FG_GEN4=n`（避免与 bq27z561 抢 power_supply）、`FUEL_GAUGE_BQ27Z561_MUNCH=y`、`QPNP_SMB5_MUNCH=y`、`PD_POLICY_MANAGER_MUNCH=y`（PD 策略走 TI 的 munch 那套；`PD_POLICY_MANAGER` 无后缀保持 n，避免两份 pd_policy_manager 编译后符号重复）

**触感 / 触控**
`INPUT_AW8697_HAPTIC=y`（原厂 y，本树之前 n）、`INPUT_AW86927_HAPTIC=y`、`TOUCHSCREEN_FOCALTECH_3658U=y`、`TOUCHSCREEN_XIAOMI_TOUCHFEATURE=y`

**显示兼容层**
`FB=y`（原厂开；部分 vendor 服务/关机充电界面要 fb0）、`BACKLIGHT_CLASS_DEVICE=y`、`BACKLIGHT_QCOM_SPMI_WLED=y`

**ARM64 勘误（原厂全开，关掉在 kona 上会随机不稳/花屏）**
`ARM64_ERRATUM_1024718 / 1188873 / 1463225 / 1542418 / 1542419 / 819472 / 824069 / 826319 / 827319 / 832075 / 843419 / 845719 = y`

**模块加载（双系统关键）**
`MODULES` / `MODULE_UNLOAD` / `MODVERSIONS=y`（vendor `.ko` 的 vermagic 带 modversions）/ `MODULE_FORCE_LOAD=y` / `MODULE_SIG=n` / `TRIM_UNUSED_KSYMS=n`

**无 LTO**：`LTO_NONE=y`，`LTO_CLANG=n`，`THINLTO=n`

---

## 三、CI 里不再"改配置"，只做核对

`.github/workflows/build_kernel.yml`：

- clang 固定 `clang-r614150`（23.0.1），复用仓库 clang 缓存
- `make O=out vendor/munch_defconfig vendor/munch.config`
- 然后跑 **`.github/scripts/check-config.sh out/.config`**：
  逐项断言上面那些开关是不是真的进了 `out/.config`，缺一项**直接 fail**，不浪费时间编译
  （kconfig 对不存在的符号是静默丢弃的，只写文件不算数，必须核对）
- 产物：`Image.gz` + `dtb` + `out/.config` 一起上传，方便核对

---

## 四、已知取舍

- **qcacld 内置**：AOSP 侧需要（没有 vendor `wlan.ko`）。MIUI 侧 vendor 分区自带 `wlan.ko`，init 去 insmod 时会因为设备已被内置驱动占用而失败，但内置驱动本身仍在工作。若实测发现 MIUI 下 WiFi 异常，可改成 `QCA_CLD_WLAN=m` 并随包提供 `.ko`。
- **`STATIC_USERMODEHELPER_PATH=""`**：保持 lineage 基线（禁用 usermode helper）。ki 内核实测在 MIUI 上也是这个值，能正常开机日用。
- **内核版本串 4.19.325**：原厂 vendor `.ko` 是 4.19.157 编的，vermagic 天然不一致；靠 `MODULE_FORCE_LOAD=y` 兜底。

---

## 五、刷入后若要继续定位黑屏

```bash
adb shell dmesg | grep -iE "sde|dsi|backlight|panel|wled|cnss|wlan" > /sdcard/dmesg.txt
adb shell "ls -l /sys/class/backlight/" 
adb shell "cat /sys/class/backlight/*/brightness"
```
