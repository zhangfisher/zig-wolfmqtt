#!/bin/sh
# 在 ARM 目标设备上运行此脚本诊断问题

echo "=== 1. CPU 信息 ==="
cat /proc/cpuinfo | grep -E "Processor|CPU|model name|Features" | head -5

echo -e "\n=== 2. 检查动态链接器 ==="
echo "查找 /lib 中的动态链接器:"
find /lib -name "ld-*" -o -name "ld.so*" 2>/dev/null
echo -e "\n查找 /lib/arm-linux-gnueabihf 中的动态链接器:"
find /lib/arm-linux-gnueabihf -name "ld-*" -o -name "ld.so*" 2>/dev/null
echo -e "\n查找 /lib/arm-linux-gnueabi 中的动态链接器:"
find /lib/arm-linux-gnueabi -name "ld-*" -o -name "ld.so*" 2>/dev/null

echo -e "\n=== 3. 检查 mqtt_broker ==="
if [ -f "./mqtt_broker" ]; then
    echo "✓ 文件存在"
    echo "文件类型:"
    file ./mqtt_broker
    echo -e "\n期望的动态链接器:"
    readelf -l ./mqtt_broker 2>/dev/null | grep "interpreter" || echo "无法读取"
    echo -e "\n需要的库:"
    readelf -d ./mqtt_broker 2>/dev/null | grep "NEEDED" || echo "无法读取"
    echo -e "\nRPATH:"
    readelf -d ./mqtt_broker 2>/dev/null | grep "RPATH\|RUNPATH" || echo "无 RPATH"
else
    echo "✗ mqtt_broker 不存在"
fi

echo -e "\n=== 4. 尝试执行并捕获详细错误 ==="
strace -e trace=open,openat,execve ./mqtt_broker 2>&1 | head -20 || ./mqtt_broker 2>&1

echo -e "\n=== 5. 检查 libc ==="
ls -la /lib/libc.so* 2>/dev/null | head -3
ls -la /lib/arm-linux-gnueabihf/libc.so* 2>/dev/null | head -3
ls -la /lib/arm-linux-gnueabi/libc.so* 2>/dev/null | head -3
