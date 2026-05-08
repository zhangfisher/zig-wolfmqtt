# epoll max_events 动态配置指南

## 概述

wolfMQTT Broker现在支持动态配置epoll的`max_events`参数，允许根据实际并发连接数优化性能。

## 什么是 max_events？

`max_events`决定了单次`epoll_wait()`调用最多能返回多少个就绪事件。这个参数直接影响：

1. **批处理能力**: 值越大，一次可以处理更多活跃连接
2. **内存占用**: 每个event约12-16字节，影响很小
3. **系统调用次数**: 高负载时，较大的值可以减少`epoll_wait()`调用次数

## 默认值

- **默认值**: 64
- **适用范围**: 适合100以内并发连接的场景
- **最大值**: 4096（内部限制）

## 如何配置

### 方法1: API调用（推荐）

在启动broker之前调用API设置：

```c
#include "wolfmqtt/mqtt_broker.h"

int main() {
    MqttBroker broker;
    
    // 初始化broker
    MqttBroker_Init(&broker);
    
    // 设置epoll max_events为256（适合500+并发连接）
    int rc = MqttBroker_SetEpollMaxEvents(&broker, 256);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("Failed to set epoll max_events: %d\n", rc);
        return -1;
    }
    
    // 配置其他参数...
    broker.port = 1883;
    
    // 启动broker
    MqttBroker_Start(&broker);
    MqttBroker_Run(&broker);
    
    return 0;
}
```

### 方法2: 修改默认宏定义

在编译前修改头文件中的默认值：

```c
// 在 wolfmqtt/mqtt_broker.h 中修改
#ifndef BROKER_EPOLL_MAX_EVENTS_DEFAULT
    #define BROKER_EPOLL_MAX_EVENTS_DEFAULT 256  // 改为需要的值
#endif
```

或者在编译时通过宏定义覆盖：

```bash
# Autoconf
CFLAGS="-DBROKER_EPOLL_MAX_EVENTS_DEFAULT=256" ./configure --enable-broker

# Zig build (需要在build.zig中添加自定义宏)
```

## 推荐配置

根据不同的并发连接数选择合适的值：

| 并发连接数 | 推荐max_events | 说明 |
|-----------|---------------|------|
| < 50      | 64 (默认)     | 默认值已足够 |
| 50-100    | 64-128        | 小幅提升 |
| 100-500   | 128-256       | 显著提升 |
| 500-1000  | 256-512       | 大幅优化 |
| 1000+     | 512-1024      | 高性能场景 |

**经验法则**: `max_events ≈ 并发连接数 / 2`

## 注意事项

### 1. 必须在启动前设置

```c
MqttBroker broker;
MqttBroker_Init(&broker);

// ✅ 正确：在Start之前设置
MqttBroker_SetEpollMaxEvents(&broker, 256);
MqttBroker_Start(&broker);

// ❌ 错误：Start之后不能再修改
MqttBroker_Start(&broker);
MqttBroker_SetEpollMaxEvents(&broker, 256);  // 会返回错误
```

### 2. 有效范围

- **最小值**: 1
- **最大值**: 4096
- 超出范围会返回`MQTT_CODE_ERROR_BAD_ARG`

### 3. 内存影响

每个epoll event结构体约12-16字节：

```
max_events = 64:   ~1KB
max_events = 256:  ~4KB
max_events = 1024: ~16KB
```

内存开销很小，可以放心使用较大值。

### 4. 性能测试建议

在实际部署前进行性能测试：

```c
// 测试不同值的性能
int test_values[] = {64, 128, 256, 512};
for (int i = 0; i < 4; i++) {
    MqttBroker broker;
    MqttBroker_Init(&broker);
    MqttBroker_SetEpollMaxEvents(&broker, test_values[i]);
    
    // 运行压力测试
    // 记录吞吐量、延迟、CPU使用率
    
    MqttBroker_Free(&broker);
}
```

## 完整示例

```c
#include <stdio.h>
#include "wolfmqtt/mqtt_broker.h"

int main() {
    MqttBroker broker;
    int rc;
    
    // 1. 初始化broker
    rc = MqttBroker_Init(&broker);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("Init failed: %d\n", rc);
        return -1;
    }
    
    // 2. 配置epoll max_events（根据预期并发连接数）
    // 假设预期有500个并发连接
    rc = MqttBroker_SetEpollMaxEvents(&broker, 256);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("Set epoll max_events failed: %d\n", rc);
        MqttBroker_Free(&broker);
        return -1;
    }
    
    // 3. 配置其他参数
    broker.port = 1883;
    broker.log_level = LOG_LEVEL_INFO;
    
#ifdef ENABLE_MQTT_TLS
    broker.use_tls = 0;  // 不使用TLS
#endif
    
    // 4. 启动broker
    printf("Starting MQTT broker with epoll max_events=256...\n");
    rc = MqttBroker_Start(&broker);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("Start failed: %d\n", rc);
        MqttBroker_Free(&broker);
        return -1;
    }
    
    // 5. 运行主循环
    MqttBroker_Run(&broker);
    
    // 6. 清理资源
    MqttBroker_Free(&broker);
    
    return 0;
}
```

## 运行时监控

可以通过日志确认配置是否生效：

```
[INFO] epoll initialized (max_events=256)
[INFO] epoll I/O multiplexing enabled
```

如果看到`max_events=64`，说明使用了默认值。

## 常见问题

### Q: 设置太大会有什么副作用？

A: 
- 内存占用略微增加（每1024个events约16KB）
- 理论上没有负面影响，但过大的值可能浪费资源
- 建议根据实际负载调整

### Q: 可以在运行时动态调整吗？

A: 不可以。必须在`MqttBroker_Start()`之前设置，一旦epoll实例创建后就不能修改。

### Q: 如何确定最佳值？

A: 
1. 从默认值64开始
2. 逐步增加到128、256、512
3. 在每个值下进行压力测试
4. 观察吞吐量、延迟和CPU使用率
5. 选择性能最好的值

### Q: 非Linux平台怎么办？

A: 在非Linux平台上，epoll不可用，会自动回退到select模式，此配置无效。

## 总结

- ✅ 默认值64适合大多数场景
- ✅ 高并发场景（100+连接）建议增大到256-512
- ✅ 必须在broker启动前设置
- ✅ 内存开销很小，可以放心使用较大值
- ✅ 通过压力测试确定最佳值

---
**版本**: wolfMQTT 2.0.0  
**更新日期**: 2026-05-08
