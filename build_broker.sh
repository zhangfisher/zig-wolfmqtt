#!/bin/bash
# MQTT Broker 快速构建脚本
# 构建目标: ARM Linux musleabihf (嵌入式设备)

set -e

echo "========================================"
echo "  MQTT Broker 构建脚本"
echo "  目标平台: ARM Linux musleabihf"
echo "========================================"
echo ""

# 清理旧的构建
echo "1. 清理旧构建..."
zig build -p cleanse 2>/dev/null || true

# 构建可执行文件
echo "2. 构建 mqtt_broker-musleabihf..."
zig build broker -Dstatic-link=true --release=small

echo ""
echo "3. 复制可执行文件..."
# 查找最新的构建产物
LATEST_BUILD=$(find .zig-cache -name "mqtt_broker-musleabihf" -type f -printf "%T@ %p\n" | sort -r | head -1 | awk '{print $2}')
if [ -n "$LATEST_BUILD" ]; then
    cp "$LATEST_BUILD" ./mqtt_broker-musleabihf
    echo "   已复制到: ./mqtt_broker-musleabihf"
else
    echo "   警告: 未找到构建产物"
    exit 1
fi

echo ""
echo "4. 构建信息..."
echo "   文件大小: $(du -h mqtt_broker-musleabihf | cut -f1)"
echo "   架构: ARM EABI (musleabihf)"
echo "   链接: 静态链接"
echo ""

echo "========================================"
echo "  ✅ 构建完成！"
echo "========================================"
echo ""
echo "可执行文件: ./mqtt_broker-musleabihf"
echo ""
echo "已启用功能:"
echo "  • MQTT V5 协议"
echo "  • WebSocket 传输"
echo "  • 消息遗嘱 (LWT)"
echo "  • 用户名/密码认证"
echo "  • 主题通配符"
echo "  • 保留消息"
echo "  • HTTP API"
echo ""
