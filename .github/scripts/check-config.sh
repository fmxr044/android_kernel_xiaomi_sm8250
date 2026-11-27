#!/usr/bin/env bash
# MIUI/HyperOS 内核 —— 配置级适配自检
#
# 为什么需要这个：
#   arch/arm64/configs/vendor/munch.config 里的开关必须真的进了 out/.config，
#   否则编出来的 Image 刷入后 WiFi 不出、/data 解不开、背光/充电异常。
#   kconfig 对"不存在的符号"是静默丢弃的，所以只写文件不算数，必须编完核对。
#
# 用法：
#   bash .github/scripts/check-config.sh out/.config
#
# 退出码：0 = 全部生效；1 = 有项没生效（CI 直接 fail，省得白编一小时）

set -u
CFG="${1:-out/.config}"

if [ ! -f "$CFG" ]; then
  echo "ERROR: 找不到 $CFG"
  exit 1
fi

# ---- 逐项断言 ----
# 格式：CONFIG_NAME:期望值
#   期望值写 y / m / n / 具体字符串（不要带引号）
REQUIRED="
CNSS2:y
CNSS_QCA6390:y
CNSS_UTILS:y
WCNSS_MEM_PRE_ALLOC:y
WIFI_THREE_ANTENNA:y
QCA_CLD_WLAN:y
QCA_CLD_WLAN_PROFILE:qca6390
CFG80211:y
RFKILL:y
EXT4_FS:y
EXT4_ENCRYPTION:y
FS_ENCRYPTION:y
FS_ENCRYPTION_INLINE_CRYPT:y
ECRYPT_FS:y
KEYS:y
ZRAM:y
ZRAM_WRITEBACK:y
QPNP_FG_GEN4:n
FUEL_GAUGE_BQ27Z561_MUNCH:y
QPNP_SMB5_MUNCH:y
INPUT_AW8697_HAPTIC:n
INPUT_AW86927_HAPTIC:y
TOUCHSCREEN_FOCALTECH_3658U:y
TOUCHSCREEN_XIAOMI_TOUCHFEATURE:y
FB:y
BACKLIGHT_CLASS_DEVICE:y
BACKLIGHT_QCOM_SPMI_WLED:y
TECHPACK_CAMERA_XIAOMI:y
ARM64_LSE_ATOMICS:y
MODULES:y
MODVERSIONS:y
MODULE_FORCE_LOAD:y
MODULE_SIG:n
TRIM_UNUSED_KSYMS:n
LTO_NONE:y
LTO_CLANG:n
THINLTO:n
ARM64_ERRATUM_1542419:y
ARM64_ERRATUM_843419:y
ARM64_ERRATUM_1024718:y
"

get_val() { # $1 = CONFIG_NAME
  local name="$1" line
  if line=$(grep -m1 "^${name}=" "$CFG"); then
    printf '%s' "${line#*=}" | tr -d '"'
  elif grep -qx "# ${name} is not set" "$CFG"; then
    printf 'n'
  else
    printf '__MISSING__'
  fi
}

FAIL=0
printf '%-40s %-22s %s\n' "配置项" "期望" "实际"
printf '%-40s %-22s %s\n' "----------------------------------------" "----------------------" "----------"
while read -r entry; do
  [ -z "$entry" ] && continue
  name="${entry%%:*}"; want="${entry#*:}"
  got="$(get_val "CONFIG_${name}")"
  # 期望 n 时，符号在本树里根本不存在也算满足：不存在就是永远关不掉也开不起，
  # 等价于 disabled。只有期望 y / m / 具体值时，缺符号才算真的没生效。
  if [ "$got" = "$want" ] || { [ "$want" = "n" ] && [ "$got" = "__MISSING__" ]; }; then
    printf '%-40s %-22s %s\n' "$name" "$want" "$got"
  else
    printf '%-40s %-22s %s   <<< 未生效\n' "$name" "$want" "$got"
    FAIL=$((FAIL+1))
  fi
done <<< "$REQUIRED"

echo
echo "===== WLAN 相关实际值 ====="
grep -E '^CONFIG_(CNSS|QCA_CLD_WLAN|WIFI|WLAN|CFG80211|RFKILL)' "$CFG" | sort | head -40

echo
echo "===== 加密 / 供电 / 显示 / 模块 ====="
grep -E '^CONFIG_(EXT4_ENCRYPTION|ECRYPT_FS|FS_ENCRYPTION|ZRAM|QPNP_FG_GEN4|QPNP_SMB5|FUEL_GAUGE|PD_POLICY|FB|BACKLIGHT_QCOM|ARM64_LSE_ATOMICS|MODVERSIONS|MODULE_SIG|LTO_NONE|LTO_CLANG)' "$CFG" | sort

echo
if [ "$FAIL" -ne 0 ]; then
  echo "CONFIG_CHECK_FAIL: $FAIL 项未生效（见上面的 <<< 标记）"
  exit 1
fi
echo "CONFIG_CHECK_OK: 全部关键项已按源码 fragment 生效"
