# wolfMQTT 库体积优化指南

## 📊 实测数据对比

| 配置方案 | 库大小 | 减少比例 | 保留功能 |
|---------|--------|---------|---------|
| **默认配置** | **590 KB** | - | 全部功能 |
| 仅禁用错误字符串 | 581 KB | 1.5% | 除错误消息外全部 |
| **最小配置** | **314 KB** | **46.8%** | MQTT 3.1.1, QoS 0-2 |
| **中等配置** | **463 KB** | **21.5%** | MQTT 3.1.1+5.0, 非阻塞 |

## 🎯 三种推荐配置

### 1️⃣ 最小配置 (314 KB) - 适合资源受限设备

```bash
zig build wolfmqtt \
  -Derror-strings=false \
  -Dv5=false \
  -Dmt=false \
  -Ddiscb=false \
  -Dproperty-cb=false \
  -Dbroker=false
```

**特点**:
- ✅ 最小的代码体积
- ✅ 适合 Flash < 512KB 的设备
- ❌ 无 MQTT v5.0
- ❌ 无多线程支持
- ❌ 无详细错误消息

**适用场景**:
- STM32、ESP32 等微控制器
- 内存受限的嵌入式系统
- 只需要基础 MQTT 功能

---

### 2️⃣ 中等配置 (463 KB) - 平衡方案 ⭐推荐

```bash
zig build wolfmqtt \
  -Derror-strings=false \
  -Dv5=true \
  -Dmt=false \
  -Dnonblock=true \
  -Ddiscb=true \
  -Dproperty-cb=false \
  -Dbroker=false
```

**特点**:
- ✅ 支持 MQTT v5.0
- ✅ 非阻塞 I/O
- ✅ 断开回调
- ❌ 无多线程（可在应用层处理）
- ❌ 无详细错误消息

**适用场景**:
- Linux 嵌入式设备（树莓派等）
- IoT 网关
- 需要 v5.0 特性的应用

---

### 3️⃣ 完整配置 (590 KB) - 开发/服务器

```bash
zig build wolfmqtt
# 或使用默认配置
```

**特点**:
- ✅ 所有功能启用
- ✅ 详细的错误消息（便于调试）
- ✅ 多线程支持
- ✅ MQTT v5.0 完整支持

**适用场景**:
- 开发和测试阶段
- 服务器/网关应用
- 桌面应用
- 不需要考虑存储限制的场景

---

## 🔧 各选项影响详解

### 对体积影响最大的选项

| 选项 | 禁用后节省 | 影响的功能 |
|------|-----------|-----------|
| `-Derror-strings=false` | ~10-15 KB | 错误消息文本 |
| `-Dv5=false` | ~100-150 KB | MQTT v5.0 协议支持 |
| `-Dmt=false` | ~20-30 KB | 线程安全锁 |
| `-Dproperty-cb=false` | ~10-15 KB | v5.0 属性回调 |
| `-Ddiscb=false` | ~5-10 KB | 断开连接回调 |
| `-Dbroker=false` | ~0 KB* | Broker 不在库中 |

*注: broker.c 不编译进 libwolfmqtt.a，所以禁用不影响库大小

### 对其他选项的影响

| 选项 | 说明 | 建议 |
|------|------|------|
| `-Dnonblock=true` | 非阻塞 I/O 状态机 | 如需异步操作则保留 |
| `-Dno-timeout=false` | 超时支持 | 通常保留 |
| `-Dstdincap=true` | STDIN 捕获 | 示例程序需要，库不需要 |
| `-Dmqtt-sn=false` | MQTT-SN | 不需要传感器网络则禁用 |

---

## 💡 优化技巧

### 技巧 1: 使用构建预设

在 `build.zig` 中添加预设支持：

```zig
const preset = b.option([]const u8, "preset", "Build preset (min, std, full)") orelse "full";

var opts = options_mod.parseBuildOptions(b);

// 应用预设
if (std.mem.eql(u8, preset, "min")) {
    opts.error_strings = false;
    opts.v5 = false;
    opts.mt = false;
    opts.discb = false;
    opts.property_cb = false;
    opts.broker = false;
} else if (std.mem.eql(u8, preset, "std")) {
    opts.error_strings = false;
    opts.mt = false;
    opts.property_cb = false;
    opts.broker = false;
}
// "full" 使用默认值
```

使用方式：
```bash
zig build wolfmqtt -Dpreset=min
zig build wolfmqtt -Dpreset=std
zig build wolfmqtt -Dpreset=full
```

### 技巧 2: 添加额外的编译器优化标志

修改 `build/entries/wolfmqtt.zig`：

```zig
// 在创建模块后添加
root_module.addCFlag("-ffunction-sections");
root_module.addCFlag("-fdata-sections");
root_module.addCFlag("-Os");  // 优化大小
root_module.addLinkerFlag("-Wl,--gc-sections");  // 移除未使用的段
```

### 技巧 3: 条件编译示例代码

如果您的应用有特定的需求，可以创建自定义构建脚本：

```bash
#!/bin/bash
# build_minimal.sh

zig build wolfmqtt \
  -Dtarget=arm-linux-gnueabihf \
  -Doptimize=ReleaseSmall \
  -Derror-strings=false \
  -Dv5=false \
  -Dmt=false \
  -Dnonblock=false \
  -Ddiscb=false \
  -Dproperty-cb=false \
  -Dstdincap=false \
  -Dbroker=false \
  -Dmqtt-sn=false
```

---

## 📈 进一步优化方向

### 短期（立即可做）

1. ✅ **禁用不需要的功能** - 已测试，可减少 47%
2. 🔧 **启用更激进的优化** - 预计再减少 10-15%
3. 🔧 **使用 LTO（链接时优化）** - 预计再减少 5-10%

### 中期（需要代码修改）

1. 📝 **分离 v5.0 代码到独立文件** - 便于条件编译
2. 📝 **错误字符串外部化** - 可选加载
3. 📝 **模块化架构** - 按需链接模块

### 长期（架构重构）

1. 🏗️ **提供多个库版本**
   - `libwolfmqtt_min.a`
   - `libwolfmqtt_std.a`
   - `libwolfmqtt_full.a`

2. 🏗️ **插件化架构**
   - 核心库 + 可选插件
   - 动态加载特性

---

## 🎓 最佳实践建议

### 对于嵌入式项目

```bash
# 推荐配置
zig build wolfmqtt \
  -Dpreset=min \
  -Dtarget=your-target \
  -Doptimize=ReleaseSmall
```

### 对于 IoT 网关

```bash
# 推荐配置
zig build wolfmqtt \
  -Dpreset=std \
  -Dtarget=your-target \
  -Doptimize=ReleaseFast  # 性能优先
```

### 对于开发环境

```bash
# 推荐配置（便于调试）
zig build wolfmqtt \
  -Dpreset=full \
  -Doptimize=Debug \
  -Ddebug-client=true
```

---

## 📝 总结

**libwolfmqtt.a 超过 500KB 是正常的**，因为：
1. 包含了完整的 MQTT 3.1.1 + 5.0 实现
2. 6,828 行 C 代码的正常编译结果
3. 启用了所有高级特性

**如果需要减小体积**：
- 最小配置：**314 KB**（减少 47%）
- 中等配置：**463 KB**（减少 22%）
- 完整配置：**590 KB**（全部功能）

**最有效的优化**：
1. 禁用错误字符串 (`-Derror-strings=false`)
2. 评估是否需要 v5.0 (`-Dv5=false`)
3. 评估是否需要多线程 (`-Dmt=false`)

选择哪种配置取决于您的具体应用场景和资源限制。
