#!/bin/sh
# 在 ARM 目标设备上运行此脚本检查环境

echo "=== 1. 检查架构 ==="
uname -m
readelf -h /bin/ls 2>/dev/null | grep Machine || echo "readelf not available"

echo -e "\n=== 2. 检查动态链接器 ==="
ls -la /lib/ld-*.so* 2>/dev/null || echo "No ld-* found in /lib"
ls -la /lib/arm-linux-gnueabihf/ld-*.so* 2>/dev/null || echo "No ld-* found in /lib/arm-linux-gnueabihf"

echo -e "\n=== 3. 检查 libc 类型 ==="
ls -la /lib/libc.so* 2>/dev/null | head -5
ls -la /lib/arm-linux-gnueabihf/libc.so* 2>/dev/null | head -5

echo -e "\n=== 4. 检查 mqtt_broker ==="
if [ -f "./mqtt_broker" ]; then
    echo "File exists"
    file ./mqtt_broker
    readelf -l ./mqtt_broker 2>/dev/null | grep "interpreter" || echo "Cannot read interpreter"
    readelf -d ./mqtt_broker 2>/dev/null | grep NEEDED || echo "Cannot read dependencies"
else
    echo "mqtt_broker not found in current directory"
fi

echo -e "\n=== 5. 检查 RPATH ==="
readelf -d ./mqtt_broker 2>/dev/null | grep "RPATH" || echo "No RPATH found"
