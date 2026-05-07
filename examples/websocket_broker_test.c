/* websocket_broker_test.c
 *
 * Simple test to verify WebSocket support in wolfMQTT Broker
 */

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include "wolfmqtt/mqtt_broker.h"
#include <stdio.h>
#include <signal.h>

static int gRunning = 1;

static void sig_handler(int signo)
{
    if (signo == SIGINT) {
        gRunning = 0;
    }
}

int main(int argc, char** argv)
{
    MqttBroker broker;
    int rc;
    word16 mqtt_port = 1883;
    word16 ws_port = 8080;
    
    (void)argc;
    (void)argv;
    
    printf("=== wolfMQTT Broker WebSocket Test ===\n\n");
    
    /* Initialize broker */
    rc = MqttBroker_Init(&broker);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("MqttBroker_Init failed: %d\n", rc);
        return rc;
    }
    
    /* Set MQTT port */
    broker.port = mqtt_port;
    
    /* Start MQTT listener */
    rc = MqttBroker_Start(&broker);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("MqttBroker_Start failed: %d\n", rc);
        MqttBroker_Free(&broker);
        return rc;
    }
    
    printf("MQTT listener started on port %d\n", mqtt_port);
    
#ifdef ENABLE_MQTT_WEBSOCKET
    /* Start WebSocket listener */
    rc = MqttBroker_StartWebSocket(&broker, ws_port);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("MqttBroker_StartWebSocket failed: %d\n", rc);
        MqttBroker_Free(&broker);
        return rc;
    }
    
    printf("WebSocket listener started on port %d\n", ws_port);
#else
    printf("WebSocket support is NOT enabled\n");
    printf("Rebuild with: zig build -Dwebsocket=true\n");
#endif
    
    printf("\nPress Ctrl+C to stop...\n\n");
    
    /* Setup signal handler */
    signal(SIGINT, sig_handler);
    
    /* Run broker */
    while (gRunning) {
        rc = MqttBroker_Step(&broker);
        if (rc != MQTT_CODE_SUCCESS) {
            printf("MqttBroker_Step failed: %d\n", rc);
            break;
        }
        
        /* Small delay */
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);  /* 10ms */
#endif
    }
    
    printf("\nShutting down...\n");
    
    /* Cleanup */
    MqttBroker_Free(&broker);
    
    printf("Done.\n");
    return 0;
}
