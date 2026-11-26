# munch（sm8250）MIUI / HyperOS 专用内核

- 仓库：`fmxr044/android_kernel_xiaomi_sm8250`
- 分支：`miui-hyperos`
- 定位：**只服务 MIUI / HyperOS**。纯 AOSP / 类原生继续用 `master`，两边的取舍不再强行合并成一份。

底座是 4.19.325 那棵（能正常开机的 lineage 树），KernelSU 走 CI 现场集成，构建用 clang 23.0.1（clang-r614150）+ LLD，无 LTO。

---

## 一、所有适配都在源码里，不靠编译期打补丁

以前的 workflow 里有个「7. MIUI 适配」步骤，用 sed/python 在 CI 里临时改代码——那个已经删掉。现在改的东西全部 committed 在源码里，clone 下来直接编就是最终形态，不依赖 CI 的运行时 sed，也不依赖某个 defconfig 开关。

### 1. 显示 / 背光（黑屏的直接成因）

| 文件 | 改动 | 作用 |
|---|---|---|
| `techpack/display/msm/sde/sde_connector.c` | ESD 中断里先判 `esd_err_enabled` / 面板是否初始化 / power_mode 是否 ON·LP1·LP2，不满足直接丢弃 | 开机动画结束、息屏、DozeSuspend 瞬间的毛刺不再被当成 panel dead（原来的表现就是"有画面但全黑"） |
| 同上 | `power_mode == LP2` 也算活动态 | HyperOS 的 AOD / DozeSuspend 不被误判成面板挂了 |
| 同上 | 亮度克隆热器件注册失败时**不再注销背光设备** | 原来注册失败会把 `panel0-backlight` 一起注销，`/sys/class/backlight` 直接空掉，MIUI 亮度服务无处可写 → 物理无背光 |
| `techpack/display/msm/dsi/dsi_panel_mi.c` | 关 ESD 中断前 `synchronize_irq()` | 避免 unprepare 时序被还在跑的处理函数打断 |
| 同上 | DC 调光不再按 dts 的 `dc-type` 二选一：先发 DC 命令，失败自动降级 DIMMING，最后无条件重写当前亮度 | 面板没配 `dc-type`、或 DC 命令不被支持时，也不会切 DC 后不亮 |
| 同上 | FOD LHBM 白点校准三段入口加 `fod_lhbm_white_update_flag` 防护 | 面板没配白点参数时直接返回，不再拿空命令集去读寄存器（EAGAIN 刷屏 / 极端情况越界） |
| `drivers/video/backlight/backlight.c` | 补回 `brightness_clone` 属性 | MIUI 亮度服务读写"用户期望亮度备份值"，没有这个节点初始化阶段就放弃设亮度 |

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

---

## 五、没搬的东西（说明原因，不是漏）

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
