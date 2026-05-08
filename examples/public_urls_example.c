/* public_urls_example.c
 * 
 * 示例：如何配置 HTTP API 免认证 URL
 */

#include <stdio.h>
#include "wolfmqtt/mqtt_broker.h"

int main(void)
{
    MqttBroker broker;
    MqttBrokerApiContext api_ctx;
    int rc;
    
    /* 定义免认证 URL 列表 */
    const char* public_urls[] = {
        "/",              /* 首页 */
        "/login.html",    /* 登录页面 */
        "/register.html", /* 注册页面 */
        "/health",        /* 健康检查端点 */
        "/status",        /* 状态页面 */
        "/favicon.ico"    /* 网站图标 */
    };
    int public_url_count = sizeof(public_urls) / sizeof(public_urls[0]);
    
    printf("=== wolfMQTT Broker Public URLs Example ===\n\n");
    
    /* 初始化 broker */
    rc = MqttBroker_Init(&broker);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("Failed to initialize broker: %d\n", rc);
        return 1;
    }
    
    /* 启动 HTTP API on port 8080 */
    rc = MqttBrokerApi_Init(&broker, &api_ctx, 8080);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("Failed to initialize HTTP API: %d\n", rc);
        MqttBroker_Free(&broker);
        return 1;
    }
    
    printf("HTTP API started on port 8080\n\n");
    
    /* /login.html 已默认为免认证 URL */
    printf("Default public URL: /login.html (already configured)\n\n");
    
    /* 添加更多免认证 URL（会替换默认配置） */
    rc = MqttBrokerApi_SetPublicUrls(&api_ctx, public_urls, public_url_count);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("Failed to set public URLs: %d\n", rc);
        MqttBrokerApi_Free(&api_ctx);
        MqttBroker_Free(&broker);
        return 1;
    }
    
    printf("Public URLs configured (%d paths):\n", public_url_count);
    for (int i = 0; i < public_url_count; i++) {
        printf("  - %s\n", public_urls[i]);
    }
    printf("\n");
    
    printf("访问以下 URL 不需要认证：\n");
    for (int i = 0; i < public_url_count; i++) {
        printf("  http://localhost:8080%s\n", public_urls[i]);
    }
    printf("\n");
    
    printf("访问其他 API 端点需要 HTTP Basic 认证：\n");
    printf("  curl -u admin:password http://localhost:8080/api/stats\n");
    printf("\n");
    
    /* 这里可以运行 broker 主循环 */
    /* Broker_Run(&broker); */
    
    printf("Press Enter to exit...\n");
    getchar();
    
    /* 清理资源 */
    MqttBrokerApi_Free(&api_ctx);
    MqttBroker_Free(&broker);
    
    printf("Cleanup complete.\n");
    
    return 0;
}
