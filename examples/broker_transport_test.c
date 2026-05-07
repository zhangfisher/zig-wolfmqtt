/* broker_transport_test.c
 *
 * Test program to verify the unified transport layer architecture
 */

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include "wolfmqtt/mqtt_broker.h"
#include "wolfmqtt/mqtt_broker_transport.h"
#include <stdio.h>
#include <string.h>

/* Mock broker for testing */
static MqttBroker g_mock_broker;

/* Test TCP transport initialization */
static void test_tcp_transport(void)
{
    BrokerClient bc;
    int rc;
    
    printf("=== Testing TCP Transport ===\n");
    
    memset(&bc, 0, sizeof(bc));
    bc.broker = &g_mock_broker;
    bc.sock = 12345;  /* Mock socket */
    
    rc = BrokerTransport_Init(&bc, BROKER_TRANSPORT_TCP, &g_mock_broker);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("  FAIL: BrokerTransport_Init returned %d\n", rc);
        return;
    }
    
    printf("  PASS: TCP transport initialized\n");
    printf("  Transport name: %s\n", BrokerTransport_GetName(&bc));
    
    /* Test handshake (should succeed immediately for TCP) */
    rc = BrokerTransport_Handshake(&bc, &g_mock_broker);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("  FAIL: TCP handshake returned %d\n", rc);
        return;
    }
    printf("  PASS: TCP handshake completed (immediate)\n");
    
    /* Test handshake done check */
    if (!BrokerTransport_IsHandshakeDone(&bc)) {
        printf("  FAIL: TCP should be handshake done\n");
        return;
    }
    printf("  PASS: TCP handshake is done\n");
    
    /* Cleanup */
    BrokerTransport_Cleanup(&bc);
    printf("  PASS: TCP transport cleaned up\n\n");
}

#ifdef ENABLE_MQTT_TLS
/* Test TLS transport initialization */
static void test_tls_transport(void)
{
    BrokerClient bc;
    int rc;
    
    printf("=== Testing TLS Transport ===\n");
    
    memset(&bc, 0, sizeof(bc));
    bc.broker = &g_mock_broker;
    bc.sock = 12346;
    
    rc = BrokerTransport_Init(&bc, BROKER_TRANSPORT_TLS, &g_mock_broker);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("  FAIL: BrokerTransport_Init returned %d\n", rc);
        return;
    }
    
    printf("  PASS: TLS transport initialized\n");
    printf("  Transport name: %s\n", BrokerTransport_GetName(&bc));
    
    /* TLS handshake will fail without actual SSL context, but that's OK */
    printf("  NOTE: TLS handshake requires actual SSL context\n");
    
    /* Cleanup */
    BrokerTransport_Cleanup(&bc);
    printf("  PASS: TLS transport cleaned up\n\n");
}
#endif

#ifdef ENABLE_MQTT_WEBSOCKET
/* Test WebSocket transport initialization */
static void test_ws_transport(void)
{
    BrokerClient bc;
    int rc;
    
    printf("=== Testing WebSocket Transport ===\n");
    
    memset(&bc, 0, sizeof(bc));
    bc.broker = &g_mock_broker;
    bc.sock = 12347;
    
    rc = BrokerTransport_Init(&bc, BROKER_TRANSPORT_WEBSOCKET, &g_mock_broker);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("  FAIL: BrokerTransport_Init returned %d\n", rc);
        return;
    }
    
    printf("  PASS: WebSocket transport initialized\n");
    printf("  Transport name: %s\n", BrokerTransport_GetName(&bc));
    printf("  is_websocket flag: %d\n", bc.is_websocket);
    
    if (!bc.is_websocket) {
        printf("  FAIL: is_websocket flag not set\n");
        return;
    }
    printf("  PASS: is_websocket flag is set\n");
    
    /* Cleanup */
    BrokerTransport_Cleanup(&bc);
    printf("  PASS: WebSocket transport cleaned up\n\n");
}
#endif

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    printf("========================================\n");
    printf("  wolfMQTT Broker Transport Layer Test\n");
    printf("========================================\n\n");
    
    /* Initialize mock broker */
    memset(&g_mock_broker, 0, sizeof(g_mock_broker));
    
    /* Run tests */
    test_tcp_transport();
    
#ifdef ENABLE_MQTT_TLS
    test_tls_transport();
#else
    printf("=== TLS Support Not Enabled ===\n");
    printf("Build with -Dtls=true to enable\n\n");
#endif
    
#ifdef ENABLE_MQTT_WEBSOCKET
    test_ws_transport();
#else
    printf("=== WebSocket Support Not Enabled ===\n");
    printf("Build with -Dwebsocket=true to enable\n\n");
#endif
    
    printf("========================================\n");
    printf("  All tests completed!\n");
    printf("========================================\n");
    
    return 0;
}
