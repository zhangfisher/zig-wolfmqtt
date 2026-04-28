/* c.h - C 头文件封装 (仅 Linux) */

/* 定义编译宏 */
#define HAVE_CONFIG_H 1
#define BUILDING_ZIG 1
#define WOLFMQTT_V5 1 
#define WOLFMQTT_PROPERTY_CB 1 
#define WOLFMQTT_DISCONNECT_CB 1 
#define WOLFMQTT_NONBLOCK 1 
#define WOLFMQTT_DEBUG_CLIENT 1  

/* wolfMQTT 客户端 */
#include "wolfmqtt/mqtt_client.h"
#include "wolfmqtt/mqtt_packet.h"
#include "wolfmqtt/mqtt_socket.h"  