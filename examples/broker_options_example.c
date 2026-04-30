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
    printf("默认最大客户端数: %d\n", broker.max_clients);
    printf("默认缓冲区大小: RX=%d, TX=%d\n",
           broker.rx_buf_sz, broker.tx_buf_sz);

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

    printf("\n=== 示例2: 小型嵌入式设备配置 ===\n");

    /* 使用 InitEx 展示显式网络层控制 */
    MqttBrokerNet_Init(&net);
    MqttBroker_InitEx(&broker, &net);

    /* 针对资源受限设备优化 - 直接修改字段 */
    broker.max_clients = 4;              /* 少量客户端 */
    broker.max_subs = 16;                /* 少量订阅 */
    broker.rx_buf_sz = 2048;             /* 较小的缓冲区 */
    broker.tx_buf_sz = 2048;
    broker.max_retained = 8;             /* 较少的保留消息 */
    
    printf("嵌入式配置已应用:\n");
    printf("  最大客户端: %d\n", broker.max_clients);
    printf("  缓冲区大小: %d 字节\n", broker.rx_buf_sz);
    
    MqttBroker_Free(&broker);
}

/* 示例3: 自定义配置 - 高性能服务器
 * 使用 InitEx 展示显式网络层控制
 */
void example_high_performance_server(void)
{
    MqttBroker broker;
    MqttBrokerNet net;

    printf("\n=== 示例3: 高性能服务器配置 ===\n");

    /* 使用 InitEx 展示显式网络层控制 */
    MqttBrokerNet_Init(&net);
    MqttBroker_InitEx(&broker, &net);
    
    /* 针对高负载场景优化 - 直接修改字段 */
    broker.max_clients = 100;            /* 支持大量客户端 */
    broker.max_subs = 500;               /* 大量订阅 */
    broker.max_retained = 100;           /* 更多保留消息 */
    broker.rx_buf_sz = 16384;            /* 更大的缓冲区 */
    broker.tx_buf_sz = 16384;
    broker.timeout_ms = 3000;            /* 更长的超时时间 */
    
    printf("高性能配置已应用:\n");
    printf("  最大客户端: %d\n", broker.max_clients);
    printf("  最大订阅: %d\n", broker.max_subs);
    printf("  缓冲区大小: %d 字节\n", broker.rx_buf_sz);
    
    MqttBroker_Free(&broker);
}

/* 示例4: 初始化前预配置
 * 展示如何在调用 Init 之前设置配置
 */
void example_pre_config(void)
{
    MqttBroker broker;

    printf("\n=== 示例4: 初始化前预配置 ===\n");

    /* 在调用 Init 之前直接设置字段 */
    broker.max_clients = 10;
    broker.max_subs = 50;
    broker.rx_buf_sz = 4096;
    broker.tx_buf_sz = 4096;
    
    /* 调用 Init 会保留预设置的值（除了必要的初始化） */
    MqttBroker_Init(&broker);
    
    printf("预配置已保留:\n");
    printf("  最大客户端: %d\n", broker.max_clients);
    printf("  缓冲区大小: %d 字节\n", broker.rx_buf_sz);
    
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
    example_pre_config();
    
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