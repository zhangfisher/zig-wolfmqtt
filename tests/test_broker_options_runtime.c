/* test_broker_options_runtime.c
 *
 * 测试程序演示broker运行时配置的使用
 */

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include "wolfmqtt/mqtt_types.h"
#include "wolfmqtt/mqtt_broker.h"
#include <stdio.h>
#include <string.h>

#if !defined(WOLFMQTT_WOLFIP) && !defined(WOLFMQTT_BROKER_CUSTOM_NET)

/* 打印选项信息 */
static void print_options(const char* label, const BrokerOptions* opts)
{
    printf("\n%s:\n", label);
    printf("  缓冲区大小配置:\n");
    printf("    接收缓冲区: %d 字节\n", opts->rx_buf_sz);
    printf("    发送缓冲区: %d 字节\n", opts->tx_buf_sz);
    printf("    超时时间: %d 毫秒\n", opts->timeout_ms);
    printf("    监听队列长度: %d\n", opts->listen_backlog);
    
    printf("\n  容量限制:\n");
    printf("    最大客户端数: %d\n", opts->max_clients);
    printf("    最大订阅数: %d\n", opts->max_subs);
    printf("    最大保留消息数: %d\n", opts->max_retained);
    printf("    最大待处理遗嘱数: %d\n", opts->max_pending_wills);
    
    printf("\n  字符串长度限制:\n");
    printf("    客户端ID: %d 字符\n", opts->max_client_id_len);
    printf("    用户名: %d 字符\n", opts->max_username_len);
    printf("    密码: %d 字符\n", opts->max_password_len);
    printf("    主题过滤器: %d 字符\n", opts->max_filter_len);
    printf("    主题名称: %d 字符\n", opts->max_topic_len);
    printf("    消息负载: %d 字节\n", opts->max_payload_len);
    printf("    遗嘱负载: %d 字节\n", opts->max_will_payload_len);
}

int main(void)
{
    MqttBroker broker;
    BrokerOptions custom_opts;
    int rc;

    printf("=== Broker运行时配置选项测试 ===\n");

    /* 初始化broker（使用默认网络回调） */
    rc = MqttBroker_Init(&broker);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("初始化broker失败: %d\n", rc);
        return 1;
    }
    
    /* 获取默认选项 */
    BrokerOptions default_opts;
    rc = MqttBroker_GetOptions(&broker, &default_opts);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("获取选项失败: %d\n", rc);
        return 1;
    }
    print_options("默认选项", &default_opts);
    
    /* 创建自定义选项 */
    memcpy(&custom_opts, &default_opts, sizeof(BrokerOptions));
    
    /* 修改一些值 */
    custom_opts.max_clients = 16;        /* 从默认的8增加到16 */
    custom_opts.max_subs = 64;           /* 从默认的32增加到64 */
    custom_opts.rx_buf_sz = 8192;        /* 更大的接收缓冲区 */
    custom_opts.tx_buf_sz = 8192;        /* 更大的发送缓冲区 */
    custom_opts.timeout_ms = 2000;       /* 更长的超时时间 */
    
    print_options("自定义选项", &custom_opts);
    
    /* 应用自定义选项（必须在启动broker之前） */
    rc = MqttBroker_SetOptions(&broker, &custom_opts);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("设置选项失败: %d\n", rc);
        return 1;
    }
    
    /* 验证选项已应用 */
    BrokerOptions verified_opts;
    rc = MqttBroker_GetOptions(&broker, &verified_opts);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("验证选项失败: %d\n", rc);
        return 1;
    }
    
    printf("\n验证已应用的选项...\n");
    if (verified_opts.max_clients == 16 &&
        verified_opts.max_subs == 64 &&
        verified_opts.rx_buf_sz == 8192) {
        printf("✓ 选项成功应用！\n");
    } else {
        printf("✗ 选项验证失败！\n");
        return 1;
    }
    
    /* 尝试在运行时更改选项（应该失败） */
    broker.running = 1;  /* 模拟运行状态 */
    rc = MqttBroker_SetOptions(&broker, &custom_opts);
    if (rc == MQTT_CODE_ERROR_BAD_ARG) {
        printf("✓ 正确阻止了运行时的选项更改\n");
    } else {
        printf("✗ 应该在运行时拒绝选项更改\n");
    }
    broker.running = 0;
    
    /* 清理 */
    MqttBroker_Free(&broker);
    
    printf("\n=== 测试完成 ===\n");
    return 0;
}
#else
int main(void)
{
    printf("此测试需要POSIX套接字（不支持WOLFMQTT_WOLFIP或WOLFMQTT_BROKER_CUSTOM_NET）\n");
    return 0;
}
#endif
