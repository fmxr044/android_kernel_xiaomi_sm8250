# munch（sm8250）MIUI / HyperOS 专用内核

- 仓库：`fmxr044/android_kernel_xiaomi_sm8250`
- 分支：`miui-hyperos`
- 定位：**只服务 MIUI / HyperOS**。纯 AOSP / 类原生继续用 `master`，两边的取舍不再强行合并成一份。

底座是 4.19.325 那棵（能正常开机的 lineage 树），KernelSU 走 CI 现场集成，构建用 clang 23.0.1（clang-r614150）+ LLD，无 LTO。

---

## 一、所有适配都在源码里，不靠编译期打补丁

以前的 workflow 里有个「7. MIUI 适配」步骤，用 sed/python 在 CI 里临时改代码——那个已经删掉。现在改的东西全部 committed 在源码里，clone 下来直接编就是最终形态，不依赖 CI 的运行时 sed，也不依赖某个 defconfig 开关。

### 1. 显示 / 背光（黑屏的直接成因）

**显示栈整份换成小米 MIUI 版的 `msm` 显示栈**（来自同平台 4.19.325 的 ki 内核，实测能在 munch 上点亮）。

做法是在 `techpack/display/msm/` 里**就地替换**：59 个文件用 MIUI 版内容覆盖，新增 `xiaomi_frame_stat.c/.h`，
`Makefile` 只补一行 `xiaomi_frame_stat.o`（include 路径保持 `msm` 不变）。这样仓库里不会出现两份 17 万行的重复显示栈，
`techpack/display/Makefile` 也不用再按开关二选一。整套差异约 **2500 行**。

MIUI 栈带来的关键能力（lineage 那版 `msm` 没有）：

| 能力 | 位置 | 作用 |
|---|---|---|
| ESD 中断有效性判定 | `sde/sde_connector.c` | 先判 `esd_err_enabled` / 面板是否初始化 / power_mode 是否 ON·LP1·LP2，不满足直接丢弃。开机动画结束、息屏、DozeSuspend 瞬间的毛刺不再被当成 panel dead（原来的表现就是"有画面但全黑"） |
| `mi_dsi_panel_*` 一整套面板 MI 接口 | `dsi/dsi_panel_mi.c`、`dsi_display_mi.c` | MIUI 亮度/dimming/DC/Doze 服务的入口，lineage 版是残缺的子集 |
| FOD LHBM（指纹区提亮） | `dsi/mi_disp_lhbm.c`、白点校准 | HyperOS 的指纹低亮提亮链路，含 `fod_lhbm_white_update_flag` 防护 |
| smart FPS 统计 | `xiaomi_frame_stat.c` | MIUI 的变帧率/刷新率联动 |
| DC 调光降级 | `dsi_panel_mi.c` `DISPPARAM_DC_ON/OFF` | 先发 DC 命令，失败自动降级 DIMMING |
| 关 ESD 中断前 `synchronize_irq()` | `dsi_panel_mi.c` | 避免 unprepare 时序被还在跑的处理函数打断 |

此外保留了我们自己补的一处健壮性修复（MIUI 原版没有）：

| 文件 | 改动 | 作用 |
|---|---|---|
| `techpack/display/msm/sde/sde_connector.c` | 亮度克隆热器件注册失败时**不再注销背光设备** | 原逻辑注册失败会把 `panel0-backlight` 一起注销，`/sys/class/backlight` 直接空掉，MIUI 亮度服务无处可写 → 物理无背光 |
| `drivers/video/backlight/backlight.c` | 补回 `brightness_clone` 属性 | MIUI 亮度服务读写"用户期望亮度备份值"，没有这个节点初始化阶段就放弃设亮度 |

配套依赖（MIUI 栈要的，本树原来没有）：

| 文件 | 改动 |
|---|---|
| `drivers/gpu/drm/drm_sysfs_mi.c`、`drm_ioctl_mi.c`、`drm_internal_mi.h` | 小米 drm sysfs/ioctl 补丁（`connector_kdev`、`color_transform_pcc_cfg`、MI ioctl 号段），`drivers/gpu/drm/Makefile` 用 mi 版替换上游 `drm_sysfs.o` / `drm_ioctl.o` |
| `fs/file.c` + `include/linux/file.h` | 补 `get_unused_fd_start_flags()`（`sde/sde_fence.c` 用它申请带 CLOEXEC 的 fd） |

### 2. 触控

`drivers/input/touchscreen/focaltech_3658u/focaltech_core.c`：按用户提供的原厂版本对齐游戏模式、触摸阈值、边缘过滤参数。

### 3. CPU 频率

`drivers/cpufreq/qcom-cpufreq-hw.c`：换成参考树那份（1/0 频点注册开关），与 ki 内核同源。

### 4. KABI / 模块

- `include/linux/mm_types.h`：`mm_struct` 里补 `__kabi_pad_mm_rb_lock` 占位，`->pgd` 钉在 **0x48**（与原厂 4.19.157 一致）。vendor 模块是按这个偏移写死编译的，漂 8 字节就会拿错页表指针直接 panic。
- 同文件加**编译期断言** `static_assert(offsetof(struct mm_struct, pgd) == 0x48)`：偏移一漂，CI 编译阶段就 fail，不用等刷到手机上炸。
- `CONFIG_MODVERSIONS=y`：defconfig 和 fragment 里都开着，产物 vermagic 带 `modversions`，运行时加载自编 .ko 用得上。

---

## 二、为什么"只刷内核本体、不换 dtb/dtbo"也能起来

munch 的设备树是**叠加**结构：

- boot.img 里装的是基础 `kona-v2.1.dtb`（只有 SoC 平台那层，不含面板/背光属性）
- dtbo 分区里是 `munch-sm8250-overlay.dtbo`，munch 自己的面板节点、`mdss_mdp` 属性都在这层

核对过原厂 dtbo：`mi,panel-id` 有 13 处（含 L11R 的 `"L11R", "8\b"`）、`mi,mdss-dsi-panel-dc-type` 19 处、`fragment@69` 给 `mdss_mdp` 补的 `#cooling-cells` 也在。也就是说 **MIUI 亮度服务要的那些 DT 属性，原厂 dtbo 本来就带**，内核侧不需要我们再塞一遍。

所以两条路都成立：

1. **只刷 Image.gz**（推荐）：dtb / dtbo 保持原厂，DT 属性全用 ROM 自带的，代码侧的自适应逻辑照常生效。
2. 连我们自己编的 dtbo 一起刷：`arch/arm64/boot/dts/vendor/qcom/munch-sm8250.dtsi`、`dsi-panel-l11r-38-08-0a-dsc-cmd.dtsi` 里已经把这些属性补齐（`#cooling-cells`、`panel-id`、`dc-type`），行为与原厂一致。

代码侧对"属性缺失"也做了兜底：拿不到 `#cooling-cells` 就只放弃亮度克隆热器件、背光照留；拿不到 `dc-type` 就走"DC 命令失败自动降级"那条路。不会因为 DT 少一项就黑屏。

---

## 三、配置级适配（`arch/arm64/configs/vendor/munch.config`）

在 `vendor/munch_defconfig` 之后合并，优先级最高，编进 `out/.config` 的是最终值。重点项：

| 项 | 值 | 说明 |
|---|---|---|
| `CNSS_QCA6390` / `WIFI_THREE_ANTENNA` | y | WiFi 芯片识别 + 三天线，缺了 WiFi 直接不出 |
| `QCA_CLD_WLAN`(`profile qca6390`) | y | 驱动内建，不靠 .ko |
| `EXT4_ENCRYPTION` / `FS_ENCRYPTION` / `ECRYPT_FS` | y | /data 加密，缺了卡开机 |
| `INPUT_AW8697_HAPTIC` | y | munch 用的是 aw8697@5A |
| `INPUT_AW86927_HAPTIC` | **n** | 与 aw8697 的 ringbuffer 全局符号同名，两颗一起 =y 会 duplicate symbol 链接失败（本次实测踩到） |
| `QPNP_FG_GEN4` | n | 与 bq27z561 抢 power_supply |
| `FUEL_GAUGE_BQ27Z561_MUNCH` / `QPNP_SMB5_MUNCH` / `PD_POLICY_MANAGER_MUNCH` | y | 充电/电量通路 |
| `FB` | y | 关机充电界面 / 开机 logo |
| `LTO_NONE` | y | 与原厂一致，关 LTO |
| `MODVERSIONS` | y | 运行时加载自编 .ko |

启动强相关项与原厂实跑配置（从原厂 Image 的 IKCONFIG 抽出来的那份）逐条对过，全部一致：`ARM64_4K_PAGES=y`、`ARM64_VA_BITS=39`、`ARM64_PA_BITS=48`、不覆盖 bootloader 的 cmdline、`CPU_FREQ_DEFAULT_GOV_PERFORMANCE=y`、`SCHED_WALT=y`。

`.github/scripts/check-config.sh` 在编译前逐项核对，缺一项直接 fail——kconfig 对不存在的符号是静默丢弃的，光写文件不算数。

---

## 四、构建

workflow：`.github/workflows/build_kernel.yml`（`MIUI/HyperOS Kernel (munch)`）

- clang 固定 `clang-r614150`（23.0.1），复用仓库 clang 缓存，版本不符自动补
- `LLVM=1` + `LLVM_IAS=1`
- `KCFLAGS=-w`
- 先 `make vendor/munch_defconfig vendor/munch.config`，再跑 `check-config.sh`，然后 `make Image.gz`，dtb/dtbo 单独出
- 产物：`Image.gz` + `kona-v2.1.dtb`，zip 带 sha256

### 本地实测记录（2026-11-27）

同一套环境离线复现（clang 23.0.1 / LLD / LLVM_IAS=1）：

- `vendor/munch_defconfig vendor/munch.config` → 通过
- `check-config.sh` → `CONFIG_CHECK_OK: 全部关键项已按源码 fragment 生效`
- `make Image.gz` → 通过，Image 51.4 MiB，Image.gz 20.5 MiB
- 版本串：`4.19.325-perf-ga1a695b9c4e8`
- vermagic：`SMP preempt mod_unload modversions aarch64`
- `mm_struct->pgd` 的编译期断言通过（说明偏移确实是 0x48）
- `make dtbs` → `munch-sm8250-overlay.dtbo` 正常产出，overlay 里含 `mi,panel-id`(L11R) / `mi,mdss-dsi-panel-dc-type` / `#cooling-cells`

### 显示栈移植后的复验（2026-11-27 下午）

换成 MIUI 显示栈之后重新整编（同一套 clang 23.0.1 / LLD / 无 LTO）：

- `make O=out vendor/munch_defconfig vendor/munch.config` → 通过
- `make O=out -j8 Image.gz` → **BUILD_EXIT=0**，Image.gz 20.57 MiB
- vmlinux 里核对到 MIUI 栈特征符号：`sde_kms`、`dsi_panel_mi`、`mi_disp_lhbm`、`smart_fps_value`
  （`smart_fps_value` 是 lineage 那版 `msm` 里没有的，能对上就证明编的是 MIUI 栈，不是旧的）
- vermagic：`SMP preempt mod_unload modversions aarch64`
- Image.gz sha256 前缀 `f6db7f4a199ec4…`

---

## 五、没搬的东西（说明原因，不是漏）

- ki 的 `usb/pd/policy_engine_munch.c`（munch 专用 PD 充电策略引擎，配 `pd_policy_manager_munch.c`）：
  它属于充电策略层，跟显示/亮屏无关；本树已有 `CONFIG_USB_PD_POLICY=y` 那套，两套同时编会抢同一批 `usbpd_*` 导出符号。
  只在移植过程中作为依赖临时拷过，最终没入库（备份在 `port-ref/usbpd/` 之外不随仓库走）。

- ki 的 `exposure_adjustment`（DSPP PCC 防闪 DC 调光）：依赖它自己那套 `exposure_adjustment.c/h` 与 PCC 寄存器表，属于画质增强，跟"能不能开机、亮不亮"无关，先不掺进来减少变量。
- `ki_profile` / Rust 充电限流 / Ki-Settings WebUI：那是 ki 的自有功能层，与本分支目标无关。
- AnyKernel3 打包：改成 `Image.gz` 直装，避免打包器默认带 dtb 把 ROM 的 dtbo 顶掉。
- `uname` 伪装：ki 用它让原厂 vendor 模块认版本，但原厂模块的 symbol CRC 跟本树本来就不匹配，伪装了也加载不上；munch 这套功能全是内建的，用不着。

---

## 六、刷入与验证

1. 只刷 `Image.gz`（推荐）：dtb / dtbo 不动
2. 开机后先看背光和亮度：
   ```
   adb shell ls /sys/class/backlight/
   adb shell cat /sys/class/backlight/panel0-backlight/brightness_clone
   ```
3. 看显示有没有再报 panel dead：
   ```
   adb shell dmesg | grep -iE "esd|panel_dead|backlight|clone"
   ```
4. WiFi 起不来时抓：
   ```
   adb shell dmesg | grep -iE "cnss|wlan|qca|cld|icnss"
   ```
