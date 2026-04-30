/* MQTT 5.0 主题别名自动管理示例
 *
 * 演示如何使用 MqttTopicAliasManager 自动管理主题别名
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "wolfmqtt/mqtt_topic_alias.h"

#ifdef WOLFMQTT_V5

/* 回调函数示例 - 记录别名创建 */
static int on_alias_created(word16 alias, const char* topic, void* ctx)
{
    printf("[ALIAS] 创建: alias=%d, topic=%s\n", alias, topic);
    return 0;
}

/* 回调函数示例 - 记录别名移除 */
static int on_alias_removed(word16 alias, const char* topic, void* ctx)
{
    printf("[ALIAS] 移除: alias=%d, topic=%s\n", alias, topic);
    return 0;
}

/* 示例 1: 基本用法 - 自动管理 */
void example_basic_usage(void)
{
    printf("\n=== 示例 1: 基本用法 ===\n");
    
    MqttTopicAliasManager manager;
    word16 alias;
    
    /* 使用默认配置初始化 */
    MqttTopicAlias_InitDefault(&manager);
    
    /* 设置回调 */
    MqttTopicAlias_SetCallbacks(&manager, on_alias_created, on_alias_removed, NULL);
    
    /* 注册主题 - 自动分配别名 */
    MqttTopicAlias_Register(&manager, "sensors/temperature/room1", 28, &alias);
    printf("主题 'sensors/temperature/room1' -> 别名 %d\n", alias);
    
    MqttTopicAlias_Register(&manager, "sensors/humidity/room1", 26, &alias);
    printf("主题 'sensors/humidity/room1' -> 别名 %d\n", alias);
    
    /* 再次注册相同主题 - 应返回相同别名 */
    MqttTopicAlias_Register(&manager, "sensors/temperature/room1", 28, &alias);
    printf("主题 'sensors/temperature/room1' -> 别名 %d (重复)\n", alias);
    
    /* 查找别名对应的主题 */
    const char* topic;
    word16 len;
    if (MqttTopicAlias_Lookup(&manager, 1, &topic, &len) == 0) {
        printf("别名 1 -> 主题 '%.*s'\n", len, topic);
    }
    
    /* 获取统计信息 */
    word16 active;
    word32 total;
    MqttTopicAlias_GetStats(&manager, &active, &total);
    printf("活跃别名: %d, 总创建: %d\n", active, total);
    
    /* 清理 */
    MqttTopicAlias_DeInit(&manager);
}

/* 示例 2: 自定义配置 */
void example_custom_config(void)
{
    printf("\n=== 示例 2: 自定义配置 ===\n");
    
    MqttTopicAliasManager manager;
    MqttTopicAliasConfig config;
    
    /* 自定义配置 */
    memset(&config, 0, sizeof(config));
    config.max_aliases = 5;        /* 最多 5 个别名 */
    config.auto_assign = 1;        /* 自动分配 */
    config.use_lru = 1;            /* 使用 LRU 淘汰 */
    config.ttl_seconds = 300;      /* 5 分钟过期 */
    
    MqttTopicAlias_Init(&manager, &config, NULL, 0);
    
    word16 alias;
    int i;
    
    /* 注册多个主题，超过最大数量会触发 LRU 淘汰 */
    for (i = 0; i < 7; i++) {
        char topic[64];
        snprintf(topic, sizeof(topic), "sensor/data/channel_%d", i);
        
        MqttTopicAlias_Register(&manager, topic, strlen(topic), &alias);
        printf("注册: %s -> 别名 %d\n", topic, alias);
    }
    
    /* 查看统计 */
    word16 active;
    word32 total;
    MqttTopicAlias_GetStats(&manager, &active, &total);
    printf("活跃别名: %d (最大 5), 总创建: %d\n", active, total);
    
    MqttTopicAlias_DeInit(&manager);
}

/* 示例 3: 使用 TTL 自动清理 */
void example_ttl_cleanup(void)
{
    printf("\n=== 示例 3: TTL 自动清理 ===\n");
    
    MqttTopicAliasManager manager;
    MqttTopicAliasConfig config;
    
    memset(&config, 0, sizeof(config));
    config.max_aliases = 10;
    config.ttl_seconds = 2;  /* 2 秒过期 */
    
    MqttTopicAlias_Init(&manager, &config, NULL, 0);
    
    word16 alias;
    MqttTopicAlias_Register(&manager, "temp/sensor1", 12, &alias);
    MqttTopicAlias_Register(&manager, "temp/sensor2", 12, &alias);
    
    printf("初始状态:\n");
    word16 active;
    MqttTopicAlias_GetStats(&manager, &active, NULL);
    printf("  活跃别名: %d\n", active);
    
    /* 模拟时间流逝 */
    MqttTopicAlias_UpdateTime(&manager, 0);
    
    /* 等待 3 秒（模拟） */
    MqttTopicAlias_UpdateTime(&manager, 3);
    
    /* 清理过期别名 */
    int cleaned = MqttTopicAlias_CleanupExpired(&manager);
    printf("3 秒后清理: 移除了 %d 个过期别名\n", cleaned);
    
    MqttTopicAlias_GetStats(&manager, &active, NULL);
    printf("清理后活跃别名: %d\n", active);
    
    MqttTopicAlias_DeInit(&manager);
}

/* 示例 4: 手动管理别名 */
void example_manual_management(void)
{
    printf("\n=== 示例 4: 手动管理 ===\n");
    
    MqttTopicAliasManager manager;
    MqttTopicAlias_InitDefault(&manager);
    
    word16 alias;
    
    /* 注册一些别名 */
    MqttTopicAlias_Register(&manager, "topic/A", 7, &alias);
    printf("注册 topic/A -> 别名 %d\n", alias);
    word16 alias_a = alias;
    
    MqttTopicAlias_Register(&manager, "topic/B", 7, &alias);
    printf("注册 topic/B -> 别名 %d\n", alias);
    word16 alias_b = alias;
    
    /* 手动移除某个别名 */
    MqttTopicAlias_Remove(&manager, alias_a);
    printf("手动移除别名 %d\n", alias_a);
    
    /* 清空所有别名 */
    MqttTopicAlias_Clear(&manager);
    printf("清空所有别名\n");
    
    word16 active;
    MqttTopicAlias_GetStats(&manager, &active, NULL);
    printf("清空后活跃别名: %d\n", active);
    
    MqttTopicAlias_DeInit(&manager);
}

/* 示例 5: 与 MqttClient 集成 */
void example_integration_with_client(void)
{
    printf("\n=== 示例 5: 与 MQTT Client 集成 ===\n");
    printf("(这是概念示例，实际使用需要完整的 MQTT 客户端)\n");
    
    /*
     * 在实际应用中，您可以这样集成：
     * 
     * 1. 在 MqttClient 结构中添加管理器：
     *    typedef struct _MqttClient {
     *        ...
     *    #ifdef WOLFMQTT_V5
     *        MqttTopicAliasManager alias_manager;
     *    #endif
     *    } MqttClient;
     * 
     * 2. 初始化时：
     *    MqttTopicAlias_InitDefault(&client->alias_manager);
     * 
     * 3. 发布消息时自动使用别名：
     *    word16 alias;
     *    if (MqttTopicAlias_Register(&client->alias_manager, 
     *                                 publish->topic_name,
     *                                 strlen(publish->topic_name),
     *                                 &alias) == 0) {
     *        // 首次发送：包含完整主题名 + 别名属性
     *        MqttProp* prop = MqttClient_PropsAdd(&publish->props);
     *        prop->type = MQTT_PROP_TOPIC_ALIAS;
     *        prop->data_short = alias;
     *        
     *        // 后续发送：仅使用别名，主题名为空
     *        // publish->topic_name = "";
     *    }
     * 
     * 4. 接收消息时解析别名：
     *    MqttProp* prop = MqttProps_Find(msg->props, MQTT_PROP_TOPIC_ALIAS);
     *    if (prop && msg->topic_name[0] == '\0') {
     *        // 使用别名查找完整主题名
     *        const char* full_topic;
     *        word16 len;
     *        if (MqttTopicAlias_Lookup(&client->alias_manager,
     *                                   prop->data_short,
     *                                   &full_topic, &len) == 0) {
     *            // 使用 full_topic
     *        }
     *    }
     */
    
    printf("详细集成方法请参考文档\n");
}

int main(void)
{
    printf("MQTT 5.0 主题别名自动管理示例\n");
    printf("==============================\n");
    
    example_basic_usage();
    example_custom_config();
    example_ttl_cleanup();
    example_manual_management();
    example_integration_with_client();
    
    printf("\n=== 所有示例完成 ===\n");
    
    return 0;
}

#else

int main(void)
{
    printf("WOLFMQTT_V5 未启用，无法运行示例\n");
    return 0;
}

#endif /* WOLFMQTT_V5 */
