# WebSocket Support for wolfMQTT Broker

## Overview

This document describes the WebSocket support implementation for the wolfMQTT Broker using the wslay library.

## Features

- **WebSocket Server**: Supports MQTT over WebSocket connections
- **No TLS Required**: WebSocket implementation does not require TLS (plain ws://)
- **RFC 6455 Compliant**: Full WebSocket handshake implementation
- **Binary Frame Support**: MQTT messages transmitted as WebSocket binary frames
- **Independent Implementation**: Self-contained SHA-1 and Base64 implementations (no external crypto dependencies)

## Build Configuration

### Using Zig Build

To enable WebSocket support, use the `--websocket` flag:

```bash
zig build -Dwebsocket=true -Dbroker=true
```

### Compile-time Macros

When WebSocket is enabled, the following macro is defined:
- `ENABLE_MQTT_WEBSOCKET`

## API Usage

### Starting WebSocket Listener

```c
#include "wolfmqtt/mqtt_broker.h"

int main(void)
{
    MqttBroker broker;
    int rc;
    
    // Initialize broker
    rc = MqttBroker_Init(&broker);
    if (rc != MQTT_CODE_SUCCESS) {
        return rc;
    }
    
    // Start plain MQTT listener (port 1883)
    broker.port = 1883;
    rc = MqttBroker_Start(&broker);
    if (rc != MQTT_CODE_SUCCESS) {
        return rc;
    }
    
#ifdef ENABLE_MQTT_WEBSOCKET
    // Start WebSocket listener (port 8080)
    rc = MqttBroker_StartWebSocket(&broker, 8080);
    if (rc != MQTT_CODE_SUCCESS) {
        MqttBroker_Free(&broker);
        return rc;
    }
#endif
    
    // Run broker
    printf("Broker running on port 1883 (MQTT) and 8080 (WebSocket)\n");
    rc = MqttBroker_Run(&broker);
    
    // Cleanup
    MqttBroker_Free(&broker);
    return rc;
}
```

## Client Connection Examples

### JavaScript (Browser/Node.js)

```javascript
const mqtt = require('mqtt');

// Connect via WebSocket
const client = mqtt.connect('ws://localhost:8080/mqtt', {
    clientId: 'web-client-1'
});

client.on('connect', function () {
    console.log('Connected via WebSocket');
    
    client.subscribe('test/topic', function (err) {
        if (!err) {
            client.publish('test/topic', 'Hello from WebSocket!');
        }
    });
});

client.on('message', function (topic, message) {
    console.log('Received:', message.toString());
});
```

### Python (paho-mqtt)

```python
import paho.mqtt.client as mqtt

def on_connect(client, userdata, flags, rc):
    print("Connected via WebSocket")
    client.subscribe("test/topic")

def on_message(client, userdata, msg):
    print(f"Received: {msg.payload.decode()}")

client = mqtt.Client(client_id="python-ws-client")
client.on_connect = on_connect
client.on_message = on_message

# Connect via WebSocket
client.ws_set_options(path="/mqtt")
client.connect("localhost", 8080, 60)

client.loop_forever()
```

## Architecture

```
┌─────────────────────────────────────────┐
│         MQTT Broker                      │
├─────────────────────────────────────────┤
│   ┌──────────────┐  ┌────────────────┐  │
│   │ TCP Listener │  │ WS Listener    │  │
│   │ (port 1883)  │  │ (port 8080)    │  │
│   └──────┬───────┘  └───────┬────────┘  │
│          │                   │           │
│          └───────┬───────────┘           │
│                  ▼                       │
│   ┌──────────────────────────────┐       │
│   │   Client Connection Manager  │       │
│   └──────────┬───────────────────┘       │
│              │                           │
│     ┌────────┴────────┐                  │
│     ▼                 ▼                  │
│  ┌────────┐    ┌──────────────┐         │
│  │TCP     │    │WebSocket     │         │
│  │Client  │    │Client(wslay) │         │
│  └────────┘    └──────────────┘         │
└─────────────────────────────────────────┘
```

## Implementation Details

### WebSocket Handshake (RFC 6455)

1. Client sends HTTP Upgrade request with `Sec-WebSocket-Key`
2. Server validates headers:
   - `Upgrade: websocket`
   - `Connection: Upgrade`
   - `Sec-WebSocket-Key` (24 bytes base64)
3. Server generates `Sec-WebSocket-Accept`:
   ```
   accept_key = base64(sha1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
   ```
4. Server responds with HTTP 101 Switching Protocols

### Data Flow

1. **Receiving**: 
   - Socket data → wslay event context → Binary frame extraction → MQTT packet processing
   
2. **Sending**:
   - MQTT packet → wslay binary frame → Socket write

### Key Components

- **mqtt_websocket.h/c**: Independent WebSocket module
- **SHA-1 Implementation**: Custom implementation (RFC 3174)
- **Base64 Encoding**: Custom implementation (RFC 4648)
- **wslay Integration**: Event-based WebSocket framing library

## Testing

### Test with MQTT.js

```bash
npm install mqtt
node -e "
const mqtt = require('mqtt');
const client = mqtt.connect('ws://localhost:8080/mqtt');
client.on('connect', () => {
  console.log('Connected!');
  client.subscribe('test');
  client.publish('test', 'Hello WebSocket!');
  client.end();
});
"
```

### Test with websocat

```bash
# Install websocat
cargo install websocat

# Connect and send MQTT CONNECT packet
echo -ne '\x10\x0e\x00\x04MQTT\x04\x02\x00\x3c\x00\x0atest-client' | \
  websocat -n1 ws://localhost:8080/mqtt
```

## Limitations

- **No TLS Support**: Currently only supports plain WebSocket (ws://), not secure WebSocket (wss://)
- **Single Subprotocol**: Only supports `mqtt` subprotocol
- **No Compression**: WebSocket per-message compression not implemented

## Future Enhancements

- Add WSS (WebSocket Secure) support with wolfSSL
- Support WebSocket extensions (compression)
- Add configurable WebSocket options (max frame size, timeouts)
- Support multiple subprotocols

## References

- [RFC 6455 - The WebSocket Protocol](https://datatracker.ietf.org/doc/html/rfc6455)
- [wslay Library](https://github.com/tatsuhiro-t/wslay)
- [MQTT over WebSocket Specification](http://docs.oasis-open.org/mqtt/mqtt-over-websocket/v1.0/mqtt-over-websocket-v1.0.html)
