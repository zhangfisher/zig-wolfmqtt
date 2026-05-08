# Broker 统一日志系统设计方案

## 问题背景

当前 wolfMQTT Broker 相关模块使用不同的日志实现：

1. **mqtt_broker.c**: 内联 `broker_log()` 函数 + `WBLOG_*` 宏
2. **mqtt_broker_api.c**: 内联 `api_log()` 函数 + `BA_LOG_*` 宏  
3. **mqtt_broker_transport.c**: `fprintf(stderr, ...)` + `WBLOG_*` 宏
4. **mqtt_websocket.c**: `Log_Output()` + `WS_LOG_*` 宏

**问题**:
- ❌ 代码重复（每个模块都实现自己的日志逻辑）
- ❌ 行为不一致（transport 使用 fprintf，其他使用回调）
- ❌ 维护困难（修改日志功能需要改多个文件）
- ❌ MQTT 发布逻辑分散

## 解决方案：创建 mqtt_broker_logger 模块

### 架构设计

```
wolfmqtt/mqtt_broker_logger.h  (公共头文件)
├── BrokerLogger_Init()         - 初始化
├── BrokerLogger_Log()          - 核心日志函数
└── BROKER_LOG_* 宏             - 统一日志宏

src/mqtt_broker_logger.c       (实现文件)
├── 线程局部递归保护
├── 日志级别检查
├── 回调调用
└── MQTT 发布

使用关系:
┌─────────────────────┐
│  mqtt_broker.c      │ ──→ #include "mqtt_broker_logger.h"
│  - 使用 WBLOG_*     │      (WBLOG_* 映射到 BROKER_LOG_*)
└─────────────────────┘

┌─────────────────────┐
│  mqtt_broker_api.c  │ ──→ #include "mqtt_broker_logger.h"
│  - 使用 BA_LOG_*    │      (BA_LOG_* 映射到 BROKER_LOG_*)
└─────────────────────┘

┌──────────────────────────┐
│  mqtt_broker_transport.c │ ──→ #include "mqtt_broker_logger.h"
│  - 使用 WBLOG_*          │      (移除 fprintf，使用统一宏)
└──────────────────────────┘

┌─────────────────────┐
│  mqtt_websocket.c   │ ──→ 可选使用
│  - 使用 WS_LOG_*    │      (保持独立，因为可能无 broker 上下文)
└─────────────────────┘
```

### 优势

✅ **代码复用**: 所有 Broker 模块共享同一实现  
✅ **行为一致**: 统一的日志级别控制、回调、MQTT 发布  
✅ **易于维护**: 修改日志功能只需改一个文件  
✅ **向后兼容**: 保留 WBLOG/BA_LOG 宏作为别名  
✅ **性能优化**: 集中的递归保护，避免重复检查  

## 实施步骤

### Phase 1: 创建核心模块 ✅

已创建：
- `wolfmqtt/mqtt_broker_logger.h` - 公共接口
- `src/mqtt_broker_logger.c` - 核心实现

特性：
- 线程局部递归保护 (`s_log_publishing`)
- 日志级别过滤
- 回调调用
- MQTT 发布到 `$sys/broker/logs`

### Phase 2: 更新各模块使用新日志系统

#### 2.1 mqtt_broker.c
**状态**: 部分完成
- ✅ 已包含 `mqtt_broker_logger.h`
- ⏳ 需要将 `broker_log()` 改为调用 `BrokerLogger_Log()`
- ⏳ `WBLOG_*` 宏可以保留作为兼容性层

**建议**: 
```c
// 方式 1: 直接替换（推荐）
#define WBLOG_DBG(b, ...)   BROKER_LOG_DBG(b, __VA_ARGS__)
#define WBLOG_INFO(b, ...)  BROKER_LOG_INFO(b, __VA_ARGS__)
// ...

// 方式 2: 删除 broker_log()，直接使用 BROKER_LOG_*
// 需要全局搜索替换所有 WBLOG_* 为 BROKER_LOG_*
```

#### 2.2 mqtt_broker_api.c
**状态**: 待更新
- ❌ 仍使用自己的 `api_log()` 实现
- ❌ 需要包含 `mqtt_broker_logger.h`
- ❌ 需要将 `BA_LOG_*` 映射到 `BROKER_LOG_*`

**修改**:
```c
// 删除旧的 api_log() 实现和 BA_LOG_* 宏定义

// 添加
#include "wolfmqtt/mqtt_broker_logger.h"

// 保持 API 特定的宏名（便于识别），但映射到统一实现
#ifdef WOLFMQTT_BROKER_LOG
    #define BA_LOG_DBG(b, ...)   BROKER_LOG_DBG(b, __VA_ARGS__)
    #define BA_LOG_INFO(b, ...)  BROKER_LOG_INFO(b, __VA_ARGS__)
    // ...
#else
    #define BA_LOG_DBG(b, ...)
    // ...
#endif
```

#### 2.3 mqtt_broker_transport.c
**状态**: 待更新
- ❌ 使用 `fprintf(stderr, ...)` 
- ❌ 有自己的 `WBLOG_*` 宏定义
- ❌ 需要包含 `mqtt_broker_logger.h`

**修改**:
```c
// 删除旧的 WBLOG_* 宏定义（第 33-47 行）

// 添加
#include "wolfmqtt/mqtt_broker_logger.h"

// 原有的 WBLOG_* 调用会自动使用新的实现
```

#### 2.4 mqtt_websocket.c
**状态**: 保持不变
- ✅ 已使用 `Log_Output()`
- ✅ 保持独立（WebSocket 可能在无 broker 上下文使用）
- ℹ️ 如果与 broker 紧密集成，可选使用 `BROKER_LOG_*`

### Phase 3: 构建系统集成

#### 3.1 build.zig
需要在 broker 可执行文件中添加 `mqtt_broker_logger.c`:

```zig
// build/entries/broker.zig
broker_root.addCSourceFile(.{ .file = b.path("src/mqtt_broker_logger.c") });
```

#### 3.2 Makefile.am / CMakeLists.txt
同样需要添加源文件。

### Phase 4: 测试验证

1. **编译测试**: 确保所有模块正确链接
2. **功能测试**: 验证日志输出、级别控制、MQTT 发布
3. **性能测试**: 确认递归保护正常工作
4. **回归测试**: 确保现有功能不受影响

## 兼容性考虑

### 向后兼容宏

为了平滑迁移，保留旧宏名作为别名：

```c
// mqtt_broker_logger.h
#define WBLOG_DBG   BROKER_LOG_DBG
#define WBLOG_INFO  BROKER_LOG_INFO
#define WBLOG_WARN  BROKER_LOG_WARN
#define WBLOG_ERR   BROKER_LOG_ERR
#define WBLOG_FATAL BROKER_LOG_FATAL

#define BA_LOG_DBG   BROKER_LOG_DBG
#define BA_LOG_INFO  BROKER_LOG_INFO
// ...
```

这样现有代码无需修改即可工作。

### 逐步迁移策略

1. **第一阶段**: 创建新模块，保留旧实现（并行运行）
2. **第二阶段**: 将旧宏映射到新实现（通过 #define）
3. **第三阶段**: 逐步删除旧实现代码
4. **第四阶段**: 清理，移除兼容性宏（下一个大版本）

## 性能分析

### 开销对比

| 项目 | 旧实现（分散） | 新实现（统一） |
|------|--------------|--------------|
| 代码大小 | ~150 行 × 3 模块 = 450 行 | ~100 行（集中） |
| 二进制大小 | 重复代码增加体积 | 减少约 200-300 字节 |
| 运行时开销 | 每个模块独立检查 | 集中检查，略优 |
| 递归保护 | 每处单独实现 | 统一实现，更可靠 |

### 递归保护改进

**旧实现**: 
- mqtt_broker.c 有自己的 `s_log_publishing`
- 其他模块没有保护 → 潜在风险

**新实现**:
- 统一的 `s_log_publishing`（线程局部）
- 所有模块共享 → 更安全

## 未来扩展

### 可能的增强

1. **日志过滤**: 按模块、主题过滤日志
2. **异步日志**: 队列缓冲，后台线程发布
3. **日志轮转**: 文件大小限制、自动归档
4. **远程配置**: 通过 HTTP API 动态调整日志级别
5. **结构化日志**: JSON 格式，便于解析

### 示例：添加模块标签

```c
typedef enum {
    BROKER_LOG_MODULE_CORE,
    BROKER_LOG_MODULE_API,
    BROKER_LOG_MODULE_TRANSPORT,
    BROKER_LOG_MODULE_WEBSOCKET
} BrokerLogModule;

void BrokerLogger_LogEx(MqttBroker* broker, BrokerLogModule module, 
                        LogLevel level, const char* format, ...);

// 使用
BROKER_LOG_INFO_EX(broker, BROKER_LOG_MODULE_API, "Request received");
```

## 总结

### 已完成
✅ 创建了 `mqtt_broker_logger.h/c` 核心模块  
✅ 实现了统一的日志接口  
✅ 包含递归保护、级别控制、MQTT 发布  

### 待完成
⏳ 更新 `mqtt_broker_api.c` 使用新系统  
⏳ 更新 `mqtt_broker_transport.c` 使用新系统  
⏳ 在构建系统中添加新源文件  
⏳ 全面测试验证  

### 建议
1. **立即行动**: 更新 transport 模块（最简单，收益最大）
2. **短期计划**: 更新 API 模块，删除重复代码
3. **长期计划**: 考虑是否将 WebSocket 也纳入统一日志

这种设计既保持了向后兼容性，又为未来的日志系统扩展打下了良好基础。
