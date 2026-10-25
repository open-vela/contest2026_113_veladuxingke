#!/bin/bash
# 语音小智 - 快速编译脚本（修正版）

set -e

PROJECT_ROOT="/home/gfc/桌面/openvela/contest2026_113_veladuxingke"
OPENVELA_ROOT="/home/gfc/桌面/openvela"

echo "================================================"
echo "语音小智 - VAD + MIMO 2.6 在线唤醒"
echo "================================================"
echo ""

# 检查新增文件
echo "[1/4] 检查新增文件..."
files=(
    "app/routine_manager/vad_detector.c"
    "app/routine_manager/vad_detector.h"
    "app/routine_manager/voice_wakeup_service.c"
    "app/routine_manager/voice_wakeup_service.h"
    "app/routine_manager/voice_wakeup_demo.c"
)

for file in "${files[@]}"; do
    if [ -f "$PROJECT_ROOT/$file" ]; then
        echo "  ✓ $file"
    else
        echo "  ✗ $file (缺失)"
        exit 1
    fi
done

echo ""
echo "[2/4] 编译 NuttX..."
cd "$OPENVELA_ROOT"

# 使用项目推荐的编译方式
source build/envsetup.sh
./build.sh contest2026_113_veladuxingke/board/r528s3-dshanpi/configs/nsh/ -j8

if [ $? -ne 0 ]; then
    echo "✗ 编译失败"
    exit 1
fi

echo ""
echo "[3/4] 准备打包环境..."
cd vendor/allwinnertech/lichee
source envsetup.sh
lunch_nuttx r528s3-dshanpi

echo ""
echo "[4/4] 打包镜像..."
pack

if [ $? -ne 0 ]; then
    echo "✗ 打包失败"
    exit 1
fi

echo ""
echo "================================================"
echo "✓ 编译和打包完成！"
echo "================================================"
echo ""

OUT_DIR="$OPENVELA_ROOT/vendor/allwinnertech/lichee/out/r528s3/dshanpi_nand"
IMG_FILE="$OUT_DIR/rtos_nuttx_r528s3-dshanpi_uart0_256Mnand.img"

if [ ! -f "$IMG_FILE" ]; then
    echo "✗ 镜像文件未找到"
    exit 1
fi

echo "镜像路径:"
echo "  $IMG_FILE"
echo ""

# 计算 SHA256 和大小
sha256=$(sha256sum "$IMG_FILE" | awk '{print $1}')
size=$(stat -c%s "$IMG_FILE")

echo "镜像信息:"
echo "  大小: $size bytes ($(($size / 1024 / 1024)) MB)"
echo "  时间: $(stat -c%y "$IMG_FILE" | cut -d. -f1)"
echo "  SHA-256: $sha256"
echo ""

echo "下一步:"
echo "  1. 烧录镜像到板子"
echo "  2. 插入 Micro-TF 卡"
echo "  3. 运行: routine_mgr"
echo "  4. 点击屏幕上的 '离线' 按钮切换到 '在线' 模式"
echo "  5. 说: '你好Vela，现在温度多少？'"
echo ""
echo "注意: 需要配置 MIMO API Key"
echo ""
