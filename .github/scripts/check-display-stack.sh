#!/usr/bin/env bash
# MIUI/HyperOS 内核 —— 显示栈自检
#
# 为什么需要这个：
#   本分支的黑屏问题就是显示栈不对造成的。仓库里有 techpack/display/msm
#   和（历史遗留的）其它显示实现，一旦顶层 Makefile 或 obj 列表被改回旧栈，
#   编出来的 Image 依然是"能开机但不亮"的样子，而且要刷到手机上才发现。
#   所以这里在编完之后直接核对 vmlinux 里有没有 MIUI 栈的特征符号，
#   没有就 fail，省得一小时编译白费。
#
# 用法：
#   bash .github/scripts/check-display-stack.sh out/vmlinux
#
# 退出码：0 = MIUI 显示栈已编入；1 = 没编进去

set -u
VMLINUX="${1:-out/vmlinux}"

if [ ! -f "$VMLINUX" ]; then
  echo "ERROR: 找不到 $VMLINUX"
  exit 1
fi

# MIUI 显示栈特征符号：lineage 那版 msm 里没有这些
REQUIRED_SYMBOLS="
sde_kms
dsi_panel_mi
mi_disp_lhbm
smart_fps_value
"

missing=""
for s in $REQUIRED_SYMBOLS; do
  if strings -a "$VMLINUX" | grep -q "$s"; then
    echo "  OK    $s"
  else
    echo "  MISS  $s"
    missing="$missing $s"
  fi
done

if [ -n "$missing" ]; then
  echo "DISPLAY_STACK_FAIL: vmlinux 里缺 MIUI 显示栈符号:$missing"
  echo "检查 techpack/display/msm（应为 MIUI 版）、techpack/display/Makefile 的 obj 行"
  exit 1
fi

echo "DISPLAY_STACK_OK: MIUI 显示栈已编入"
