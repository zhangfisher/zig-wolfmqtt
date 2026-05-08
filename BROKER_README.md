# MQTT Broker - ARM 嵌入式版本

## 文件信息

- **文件名**: `mqtt_broker-musleabihf`
- **架构**: ARM EABI (musleabihf)
- **文件大小**: ~110KB
- **链接方式**: 静态链接
- **优化模式**: ReleaseSmall (体积优化)

## 目标平台

此二进制文件适用于：
- ARMv7 架构嵌入式设备
- 运行 musl libc 的 Linux 系统
- 典型设备: Raspberry Pi、BeagleBone Black 等

## 已启用功能

### 核心功能
- ✅ MQTT V5 协议支持
- ✅ MQTT V3.1.1 向后兼容
- ✅ QoS 0/1/2 支持

### 传输层
- ✅ TCP/TLS 支持
- ✅ **WebSocket 传输** (新增)
- ✅ 多线程网络处理

### Broker 功能
- ✅ **消息遗嘱 (LWT)** - 客户端异常断开时发布遗嘱消息
- ✅ **用户名/密码认证** - 连接时验证客户端身份
- ✅ **主题通配符** - 支持 `+` 和 `#` 通配符订阅
- ✅ **保留消息** - 服务器保留最后一条消息
- ✅ 会话持久化 (Clean Session=0)
- ✅ **主题别名** (MQTT V5) - 减少数据包大小
- ✅ 遗嘱延迟间隔 (MQTT V5)

### API 接口
- ✅ **HTTP REST API** - 动态配置和管理
  - 客户端管理（踢出、统计）
  - 消息发布接口
  - Broker 重置和统计

### 调试和日志
- ✅ 详细日志输出
- ✅ 连接统计信息
- ✅ 错误诊断支持

## 内存模式

- **动态内存分配** - 无固定大小限制
- 自动扩展以适应客户端数量
- 自动垃圾回收

## 部署说明

### 1. 文件传输
将 `mqtt_broker-musleabihf` 上传到目标设备：

```bash
# 使用 scp
scp mqtt_broker-musleabihf user@device:/home/user/

# 或使用其他方式（FTP、U盘等）
```

### 2. 设置执行权限
```bash
chmod +x mqtt_broker-musleabihf
```

### 3. 运行 Broker

**基本运行**:
```bash
./mqtt_broker-musleabihf
```

**指定参数**:
```bash
# 指定监听端口（默认 1883）
./mqtt_broker-musleabihf -p 1883

# 启用 WebSocket（端口 8080）
./mqtt_broker-musleabihf --websocket-port 8080

# 指定客户端最大数量
./mqtt_broker-musleabihf --max-clients 100

# 后台运行
./mqtt_broker-musleabihf --daemon
```

### 4. 查看帮助
```bash
./mqtt_broker-musleabihf --help
```

## 命令行选项

```
选项:
  -p <port>              MQTT 端口 (默认: 1883)
  --websocket-port <ws>  WebSocket 端口 (默认: 8080)
  --max-clients <n>      最大客户端数
  --max-subs <n>         最大订阅数
  --max-retained <n>     最大保留消息数
  --require-auth         强制认证
  --allow-anonymous      允许匿名连接
  -d, --daemon           后台运行
  -v, --verbose          详细输出
  -h, --help            显示帮助
```

## 配置示例

### 1. 基本配置
```bash
./mqtt_broker-musleabihf -p 1883 --websocket-port 8080
```

### 2. 生产环境（后台运行）
```bash
./mqtt_broker-musleabihf \
  -p 1883 \
  --websocket-port 8080 \
  --max-clients 500 \
  --max-subs 1000 \
  --daemon \
  -v
```

### 3. 开发调试（前台运行）
```bash
./mqtt_broker-musleabihf \
  -p 1883 \
  --websocket-port 8080 \
  --max-clients 10 \
  --verbose
```

## 功能验证

### MQTT 连接测试
```bash
# 使用 mosquitto客户端
mosquitto_pub -h localhost -p 1883 -t "test/topic" -m "Hello"
mosquitto_sub -h localhost -p 1883 -t "test/topic"

# WebSocket 浏览器测试
# ws://broker-ip:8080/mqtt
```

### HTTP API 测试
```bash
# 查看统计信息
curl http://broker-ip:8081/api/stats

# 发布消息（简单）
curl -X POST http://broker-ip:8081/api/publish/test/topic \
  -d "Hello from API"

# 发布消息（带参数）
curl -X POST "http://broker-ip:8081/api/publish/test/topic?qos=1&retain=true" \
  -d "Retained message"

# 查看客户端列表
curl http://broker-ip:8081/api/clients

# 查询主题订阅者
curl http://broker-ip:8081/api/topics/test/topic

# 查询配置
curl http://broker-ip:8081/api/configs

# 批量更新配置
curl -X POST http://broker-ip:8081/api/configs \
  -H "Content-Type: application/x-www-form-urlencoded" \
  -d "log_level=0&max_clients=100"

# 重置统计
curl -X POST http://broker-ip:8081/api/reset

# 踢出客户端
curl -X POST http://broker-ip:8081/api/kick/client_id
```

**注意**：HTTP API 默认端口是 **8081**（不是 8080，8080 用于 WebSocket）

## 性能特点

- **体积优化**: ~110KB (ReleaseSmall)
- **内存效率**: 动态分配，无静态数组限制
- **并发支持**: 多线程网络 I/O
- **协议优化**: 支持 MQTT V5 性能增强特性

## 故障排查

### 权限错误
```bash
# 确保可执行权限
chmod +x mqtt_broker-musleabihf

# 如果遇到 "Permission denied"
sudo ./mqtt_broker-musleabihf
```

### 端口占用
```bash
# 检查端口占用
netstat -tuln | grep 1883
netstat -tuln | grep 8080

# 更换端口
./mqtt_broker-musleabihf -p 1884
```

### 架构不匹配
```bash
# 检查设备架构
uname -m

# 应显示: armv7l 或类似
# 如果显示其他架构（如 x86_64），需要为该架构重新构建
```

## 重新构建

如需修改功能或优化选项，请查看项目根目录的构建脚本：

```bash
# 使用构建脚本
bash build_broker.sh

# 或手动构建
zig build broker -Dstatic-link=true --release=small
```

## 技术支持

- MQTT Broker 功能已全面启用
- 所有功能特性均为默认开启
- 无需编译时宏开关控制

生成时间: 2026-05-08
构建工具: Zig 0.16+
