/* test_broker_options.c
 *
 * Test program to demonstrate BrokerOptions usage
 */

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include "wolfmqtt/mqtt_broker.h"
#include <stdio.h>
#include <string.h>

#ifdef WOLFMQTT_BROKER

int main(void)
{
    MqttBroker broker;
    const BrokerOptions* options;
    BrokerOptions custom_options;
    int rc;

    printf("=== BrokerOptions Test ===\n\n");

    /* Initialize broker with default network callbacks */
    rc = MqttBroker_Init(&broker);
    if (rc != 0) {
        printf("ERROR: MqttBroker_Init failed with code %d\n", rc);
        return 1;
    }
    printf("Broker initialized successfully\n\n");

    /* Get default options */
    options = MqttBroker_GetOptions(&broker);
    if (options == NULL) {
        printf("ERROR: MqttBroker_GetOptions returned NULL\n");
        return 1;
    }

    printf("Default Options:\n");
    printf("  Feature Flags (0x%02X):\n", options->features);
    printf("    enable_retained:   %s\n", 
           BROKER_HAS_FEATURE(options, BROKER_FEATURE_RETAINED) ? "Yes" : "No");
    printf("    enable_will:       %s\n", 
           BROKER_HAS_FEATURE(options, BROKER_FEATURE_WILL) ? "Yes" : "No");
    printf("    enable_wildcards:  %s\n", 
           BROKER_HAS_FEATURE(options, BROKER_FEATURE_WILDCARDS) ? "Yes" : "No");
    printf("    enable_auth:       %s\n", 
           BROKER_HAS_FEATURE(options, BROKER_FEATURE_AUTH) ? "Yes" : "No");
    printf("    enable_insecure:   %s\n", 
           BROKER_HAS_FEATURE(options, BROKER_FEATURE_INSECURE) ? "Yes" : "No");
    printf("    enable_websocket:  %s\n", 
           BROKER_HAS_FEATURE(options, BROKER_FEATURE_WEBSOCKET) ? "Yes" : "No");
    printf("    enable_debug:      %s\n", 
           BROKER_HAS_FEATURE(options, BROKER_FEATURE_DEBUG) ? "Yes" : "No");
    printf("    enable_v5:         %s\n", 
           BROKER_HAS_FEATURE(options, BROKER_FEATURE_V5) ? "Yes" : "No");
    printf("\n");
    printf("  Buffer Sizes:\n");
    printf("    rx_buf_size:       %d bytes\n", options->rx_buf_size);
    printf("    tx_buf_size:       %d bytes\n", options->tx_buf_size);
    printf("    timeout_ms:        %d ms\n", options->timeout_ms);
    printf("    listen_backlog:    %d\n", options->listen_backlog);
    printf("\n");
    printf("  Limits:\n");
    printf("    max_clients:       %d\n", options->max_clients);
    printf("    max_subscriptions: %d\n", options->max_subscriptions);
    printf("    max_client_id_len: %d\n", options->max_client_id_len);
    printf("    max_username_len:  %d\n", options->max_username_len);
    printf("    max_password_len:  %d\n", options->max_password_len);
    printf("    max_filter_len:    %d\n", options->max_filter_len);
    printf("    max_retained_msgs: %d\n", options->max_retained_msgs);
    printf("    max_topic_len:     %d\n", options->max_topic_len);
    printf("    max_payload_len:   %d\n", options->max_payload_len);
    printf("    max_will_payload_len: %d\n", options->max_will_payload_len);
    printf("    max_pending_wills: %d\n", options->max_pending_wills);
    printf("\n");
    printf("  Logging:\n");
    printf("    log_level:         %d\n", options->log_level);
    printf("\n");

    /* Create custom options */
    printf("Setting custom options...\n");
    MqttBroker_OptionsInitDefaults(&custom_options);
    
    /* Modify some values using bit flags */
    BROKER_DISABLE_FEATURE(&custom_options, BROKER_FEATURE_RETAINED);  /* Disable retained messages */
    BROKER_DISABLE_FEATURE(&custom_options, BROKER_FEATURE_AUTH);      /* Disable authentication */
    BROKER_ENABLE_FEATURE(&custom_options, BROKER_FEATURE_DEBUG);      /* Enable debug logging */
    custom_options.max_clients = 4;          /* Reduce max clients */
    custom_options.log_level = 3;            /* Enable debug logging */
    custom_options.rx_buf_size = 8192;       /* Increase RX buffer */
    custom_options.tx_buf_size = 8192;       /* Increase TX buffer */

    rc = MqttBroker_SetOptions(&broker, &custom_options);
    if (rc != 0) {
        printf("ERROR: MqttBroker_SetOptions failed with code %d\n", rc);
        return 1;
    }
    printf("Custom options set successfully\n\n");

    /* Verify the changes */
    options = MqttBroker_GetOptions(&broker);
    printf("Updated Options (changed values):\n");
    printf("  Feature Flags: 0x%02X (was 0x%02X)\n", 
           options->features, BROKER_FEATURE_DEFAULT);
    printf("  enable_retained:   %s (was Yes)\n", 
           BROKER_HAS_FEATURE(options, BROKER_FEATURE_RETAINED) ? "Yes" : "No");
    printf("  enable_auth:       %s (was Yes)\n", 
           BROKER_HAS_FEATURE(options, BROKER_FEATURE_AUTH) ? "Yes" : "No");
    printf("  enable_debug:      %s (was No)\n", 
           BROKER_HAS_FEATURE(options, BROKER_FEATURE_DEBUG) ? "Yes" : "No");
    printf("  max_clients:       %d (was 8)\n", options->max_clients);
    printf("  log_level:         %d (was 2)\n", options->log_level);
    printf("  rx_buf_size:       %d (was 4096)\n", options->rx_buf_size);
    printf("  tx_buf_size:       %d (was 4096)\n", options->tx_buf_size);
    printf("\n");

    /* Cleanup */
    MqttBroker_Free(&broker);
    printf("Test completed successfully!\n");

    return 0;
}

#else

int main(void)
{
    printf("Broker not enabled. Build with --enable-broker\n");
    return 0;
}

#endif /* WOLFMQTT_BROKER */
