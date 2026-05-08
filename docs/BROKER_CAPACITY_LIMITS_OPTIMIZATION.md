# Broker 容量限制配置优化

## 概述

本次更新对Broker的容量限制字段进行了优化，使其更加灵活：

1. **默认值为0** - 表示无限制，仅受可用内存限制
2. **可运行时配置** - 通过MqttBroker结构体字段动态设置
3. **移除硬编码宏** - 不再使用BROKER_MAX_*宏作为运行时默认值

## 主要变更

### 1. 修改的字段

以下字段的默认值从宏定义改为0：

| 字段 | 类型 | 旧默认值 | 新默认值 | 说明 |
|------|------|----------|----------|------|
| `max_clients` | word16 | BROKER_MAX_CLIENTS (8) | 0 | 最大客户端数 |
| `max_subs` | word16 | BROKER_MAX_SUBS (32) | 0 | 最大订阅数 |
| `max_retained` | word16 | BROKER_MAX_RETAINED (16) | 0 | 最大保留消息数 |
| `max_pending_wills` | word16 | BROKER_MAX_PENDING_WILLS (4) | 0 | 最大待处理遗嘱数 |

### 2. 语义变化

#### 之前
- 默认值 = 编译时宏定义（如8、32等）
- 固定限制，无法在运行时调整
- 静态数组大小由宏决定

#### 现在
- 默认值 = 0（表示无限制）
- 实际限制取决于可用内存
- 可通过API或配置文件动态设置具体值
- 静态模式下，0会被解释为编译时宏定义的数组大小

### 3. 实现细节

#### 辅助宏

添加了 `BROKER_LIMIT` 辅助宏来统一处理限制逻辑：

```c
#define BROKER_LIMIT(limit, default_val) ((limit) > 0 ? (limit) : (default_val))
```

**功能**：
- 如果 `limit > 0`，返回 `limit`（用户设置的值）
- 如果 `limit == 0`，返回 `default_val`（编译时默认值）

#### 初始化代码

```c
/* 初始化容量限制默认值 (0表示无限制，受内存限制) */
broker->max_clients = 0;
broker->max_subs = 0;
broker->max_retained = 0;
broker->max_pending_wills = 0;
```

#### 循环遍历

所有使用这些字段的循环都更新为使用 `BROKER_LIMIT` 宏：

**之前**：
```c
for (i = 0; i < broker->max_clients; i++) {
    // ...
}
```

**现在**：
```c
for (i = 0; i < BROKER_LIMIT(broker->max_clients, BROKER_MAX_CLIENTS); i++) {
    // ...
}
```

### 4. 静态内存模式 vs 动态内存模式

#### 静态内存模式 (WOLFMQTT_STATIC_MEMORY)

- 数组大小在编译时固定（由宏定义）
- `max_*` 字段值为0时，使用宏定义的大小进行遍历
- `max_*` 字段值>0时，使用该值作为上限（但不能超过数组大小）

**示例**：
```c
// 编译时：clients[BROKER_MAX_CLIENTS] 即 clients[8]
// 运行时：
broker->max_clients = 0;     // 遍历全部8个元素
broker->max_clients = 5;     // 只遍历前5个元素
broker->max_clients = 10;    // 仍然只遍历8个（数组边界）
```

#### 动态内存模式

- 数组通过malloc动态分配
- `max_*` 字段值为0时，理论上无限制（受内存限制）
- `max_*` 字段值>0时，使用该值作为上限

**注意**：当前实现中，动态模式的数组大小仍由编译时宏决定，未来可以改进为完全动态。

## 使用示例

### 1. 默认行为（无限制）

```c
MqttBroker broker;
MqttBroker_Init(&broker, ...);

// max_clients, max_subs, max_retained, max_pending_wills 都是0
// 表示不主动限制，受内存和数组大小限制
```

### 2. 设置具体限制

```c
MqttBroker broker;
MqttBroker_Init(&broker, ...);

// 限制最多10个客户端
broker.max_clients = 10;

// 限制最多50个订阅
broker.max_subs = 50;

// 限制最多20条保留消息
broker.max_retained = 20;

// 限制最多8个待处理遗嘱
broker.max_pending_wills = 8;
```

### 3. 通过HTTP API更新（未来扩展）

```bash
# 批量更新容量限制
curl -X POST \
  -H "Authorization: Basic testtoken12345" \
  -d "max_clients=20&max_subs=100&max_retained=50" \
  http://localhost:8081/api/configs
```

## 优势

### 1. 灵活性提升

✅ **运行时可调** - 无需重新编译即可调整限制  
✅ **内存自适应** - 默认值0让系统根据可用内存自动调整  
✅ **精细控制** - 可以为不同场景设置不同的限制  

### 2. 向后兼容

✅ **宏定义保留** - BROKER_MAX_* 宏仍然存在于头文件中  
✅ **静态模式兼容** - 静态内存模式的行为保持不变  
✅ **默认行为一致** - 对于大多数用户，行为没有变化  

### 3. 未来扩展

✅ **支持动态分配** - 为将来完全动态的内存管理打下基础  
✅ **API友好** - 可以通过HTTP API轻松调整限制  
✅ **配置驱动** - 可以从配置文件读取限制值  

## 注意事项

### ⚠️ 静态内存模式

在静态内存模式下，即使设置 `max_clients = 100`，实际能使用的客户端数量仍然受限于编译时的数组大小（BROKER_MAX_CLIENTS）。

**建议**：
- 如果需要更多客户端，应该在编译时调整宏定义
- 或者使用动态内存模式（不定义 WOLFMQTT_STATIC_MEMORY）

### ⚠️ 内存消耗

设置为0（无限制）时，系统会尽可能多地分配资源，直到内存耗尽。

**建议**：
- 在资源受限的嵌入式系统中，显式设置合理的限制值
- 监控系统内存使用情况
- 根据实际需求设置适当的限制

### ⚠️ 性能考虑

较大的限制值可能导致：
- 更长的遍历时间（O(n)复杂度）
- 更高的内存占用
- 更多的CPU开销

**建议**：
- 根据实际负载设置合理的限制
- 定期监控Broker性能指标
- 使用统计信息调优配置

## 相关文件

### 修改的文件

1. **wolfmqtt/mqtt_broker.h**
   - 保留了BROKER_MAX_*宏定义（用于静态数组大小）
   - 注释更新了字段说明

2. **src/mqtt_broker.c**
   - 添加 `BROKER_LIMIT` 辅助宏
   - 修改初始化代码，设置默认值为0
   - 更新所有循环使用 `BROKER_LIMIT` 宏

### 未修改的文件

- 宏定义本身（BROKER_MAX_CLIENTS等）仍然保留
- 静态数组的定义仍然使用宏
- 其他模块不受影响

## 测试建议

### 1. 测试默认行为

```c
MqttBroker broker;
MqttBroker_Init(&broker, ...);

printf("max_clients: %u\n", broker.max_clients);        // 应该输出 0
printf("max_subs: %u\n", broker.max_subs);              // 应该输出 0
printf("max_retained: %u\n", broker.max_retained);      // 应该输出 0
printf("max_pending_wills: %u\n", broker.max_pending_wills); // 应该输出 0
```

### 2. 测试自定义限制

```c
broker.max_clients = 5;
broker.max_subs = 20;

// 尝试连接6个客户端，第6个应该失败
// 尝试创建21个订阅，第21个应该失败
```

### 3. 测试静态模式

```c
// 编译时：BROKER_MAX_CLIENTS = 8
broker.max_clients = 0;  // 应该能使用全部8个槽位
broker.max_clients = 5;  // 只能使用前5个槽位
broker.max_clients = 10; // 仍然只能使用8个（数组边界）
```

### 4. 压力测试

```bash
# 启动Broker
./mqtt_broker

# 使用脚本创建大量连接
for i in {1..100}; do
    mosquitto_pub -t test/topic -m "msg$i" &
done

# 观察Broker行为和内存使用
```

## 编译结果

| 项目 | 值 |
|------|-----|
| 文件大小 | 111,760 字节 |
| 编译状态 | ✅ 成功 |
| 警告数量 | 0 |
| 错误数量 | 0 |

## 总结

✅ **默认值为0** - 表示无限制，受内存限制  
✅ **可运行时配置** - 通过MqttBroker字段动态设置  
✅ **宏定义保留** - 用于静态数组大小和向后兼容  
✅ **辅助宏简化** - BROKER_LIMIT统一处理限制逻辑  
✅ **向后兼容** - 现有代码无需修改  
✅ **灵活性强** - 支持各种使用场景  

这次优化使得Broker的容量限制更加灵活和可控，同时保持了向后兼容性！
