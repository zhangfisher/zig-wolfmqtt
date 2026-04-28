/* broker_options_example.c
 *
 * Broker运行时配置选项使用示例
 */

#include <stdio.h>
#include "wolfmqtt/mqtt_broker.h"

#if !defined(WOLFMQTT_WOLFIP) && !defined(WOLFMQTT_BROKER_CUSTOM_NET)

/* 示例1: 使用默认配置（简化API） */
void example_default_config(void)
{
    MqttBroker broker;

    printf("=== 示例1: 使用默认配置 ===\n");

    /* 使用简化的Init API，自动初始化默认网络层 */
    MqttBroker_Init(&broker);

    /* 直接使用默认配置启动 */
    printf("使用默认配置启动broker...\n");
    printf("默认最大客户端数: %d\n", broker.options.max_clients);
    printf("默认缓冲区大小: RX=%d, TX=%d\n",
           broker.options.rx_buf_sz, broker.options.tx_buf_sz);

    /* 清理 */
    MqttBroker_Free(&broker);
}

/* 示例2: 自定义配置 - 小型嵌入式设备
 * 注意：此示例使用 InitEx 展示如何显式控制网络层初始化
 * 在实际应用中，如果使用默认网络层，可以使用简化的 Init API
 */
void example_embedded_device(void)
{
    MqttBroker broker;
    MqttBrokerNet net;
    BrokerOptions opts;

    printf("\n=== 示例2: 小型嵌入式设备配置 ===\n");

    /* 使用 InitEx 展示显式网络层控制 */
    MqttBrokerNet_Init(&net);
    MqttBroker_InitEx(&broker, &net);

    /* 获取当前选项 */
    MqttBroker_GetOptions(&broker, &opts);
    
    /* 针对资源受限设备优化 */
    opts.max_clients = 4;              /* 少量客户端 */
    opts.max_subs = 16;                /* 少量订阅 */
    opts.rx_buf_sz = 2048;             /* 较小的缓冲区 */
    opts.tx_buf_sz = 2048;
    opts.max_retained = 8;             /* 较少的保留消息 */
    
    /* 应用配置 */
    if (MqttBroker_SetOptions(&broker, &opts) == MQTT_CODE_SUCCESS) {
        printf("嵌入式配置已应用:\n");
        printf("  最大客户端: %d\n", opts.max_clients);
        printf("  缓冲区大小: %d 字节\n", opts.rx_buf_sz);
    }
    
    MqttBroker_Free(&broker);
}

/* 示例3: 自定义配置 - 高性能服务器
 * 使用 InitEx 展示显式网络层控制
 */
void example_high_performance_server(void)
{
    MqttBroker broker;
    MqttBrokerNet net;
    BrokerOptions opts;

    printf("\n=== 示例3: 高性能服务器配置 ===\n");

    /* 使用 InitEx 展示显式网络层控制 */
    MqttBrokerNet_Init(&net);
    MqttBroker_InitEx(&broker, &net);
    
    /* 获取当前选项 */
    MqttBroker_GetOptions(&broker, &opts);
    
    /* 针对高负载场景优化 */
    opts.max_clients = 100;            /* 支持大量客户端 */
    opts.max_subs = 500;               /* 大量订阅 */
    opts.max_retained = 100;           /* 更多保留消息 */
    opts.rx_buf_sz = 16384;            /* 更大的缓冲区 */
    opts.tx_buf_sz = 16384;
    opts.timeout_ms = 3000;            /* 更长的超时时间 */
    
    /* 应用配置 */
    if (MqttBroker_SetOptions(&broker, &opts) == MQTT_CODE_SUCCESS) {
        printf("高性能配置已应用:\n");
        printf("  最大客户端: %d\n", opts.max_clients);
        printf("  最大订阅: %d\n", opts.max_subs);
        printf("  缓冲区大小: %d 字节\n", opts.rx_buf_sz);
    }
    
    MqttBroker_Free(&broker);
}

/* 示例4: 验证和错误处理
 * 使用 InitEx 展示显式网络层控制
 */
void example_validation(void)
{
    MqttBroker broker;
    MqttBrokerNet net;
    BrokerOptions opts;
    int rc;

    printf("\n=== 示例4: 配置验证和错误处理 ===\n");

    /* 使用 InitEx 展示显式网络层控制 */
    MqttBrokerNet_Init(&net);
    MqttBroker_InitEx(&broker, &net);
    
    /* 测试无效配置 */
    MqttBroker_GetOptions(&broker, &opts);
    opts.rx_buf_sz = 0;  /* 无效值 */
    
    rc = MqttBroker_SetOptions(&broker, &opts);
    if (rc == MQTT_CODE_ERROR_BAD_ARG) {
        printf("✓ 正确拒绝了无效的缓冲区大小(0)\n");
    }
    
    /* 恢复有效值 */
    opts.rx_buf_sz = 4096;
    opts.max_clients = 0;  /* 无效值 */
    
    rc = MqttBroker_SetOptions(&broker, &opts);
    if (rc == MQTT_CODE_ERROR_BAD_ARG) {
        printf("✓ 正确拒绝了无效的客户端数(0)\n");
    }
    
    /* 测试运行时修改保护 */
    broker.running = 1;  /* 模拟运行状态 */
    rc = MqttBroker_SetOptions(&broker, &opts);
    if (rc == MQTT_CODE_ERROR_MUTEX) {
        printf("✓ 正确阻止了运行时的配置修改\n");
    }
    broker.running = 0;
    
    MqttBroker_Free(&broker);
}

int main(void)
{
    printf("========================================\n");
    printf("  Broker运行时配置选项使用示例\n");
    printf("========================================\n\n");
    
    example_default_config();
    example_embedded_device();
    example_high_performance_server();
    example_validation();
    
    printf("\n========================================\n");
    printf("  所有示例执行完毕\n");
    printf("========================================\n");
    
    return 0;
}

#else
int main(void)
{
    printf("此示例需要POSIX套接字支持\n");
    return 0;
}
#endif
