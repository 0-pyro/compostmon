#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebSocketsServer.h>
#include <PubSubClient.h>

// ==========================================
// ⚙️ CONFIGURATION (Boss: Edit these values!)
// ==========================================
#define WIFI_SSID "moto g34"         // Enter your Wi-Fi Network Name
#define WIFI_PASSWORD "biggiesmalls" // Enter your Wi-Fi Password

#define MQTT_BROKER "broker.hivemq.com" // Online public MQTT broker
#define MQTT_PORT 1883
#define MQTT_TOPIC "compost/sensors"
#define MQTT_TOPIC_MOTOR "compost/motor" // Topic for motor commands from dashboard
#define MQTT_CLIENT_ID "ESP32_Compost_Client"

#define WEBSOCKET_PORT 81
#define MDNS_NAME "compostpro" // Will resolve to http://compostpro.local

// ==========================================
// PIN DEFINITIONS
// ==========================================
#define MQ9_PIN 34
#define MQ137_PIN 35
#define MICS2714_PIN 32

// L298N Motor Driver Pins
#define MOTOR_IN3_PIN 14 // Connected to L298N IN3 (D14)
#define MOTOR_IN4_PIN 27 // Connected to L298N IN4 (D27)

// ==========================================
// GLOBAL OBJECTS & VARIABLES
// ==========================================
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
WebSocketsServer webSocket = WebSocketsServer(WEBSOCKET_PORT);

String unoJson = "{}";
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 2000; // Send telemetry every 2 seconds

unsigned long lastWifiRetry = 0;
unsigned long lastMqttRetry = 0;
bool mdnsStarted = false;
bool motorRunning = false;

// ==========================================
// FORWARD DECLARATIONS
// (required in .cpp — auto-generated in .ino)
// ==========================================
void setMotor(bool enable);
void mqttCallback(char *topic, byte *payload, unsigned int length);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
void checkWiFi();
void checkMQTT();

// ==========================================
// MOTOR CONTROL HELPER (L298N Driver)
// ==========================================
void setMotor(bool enable)
{
  motorRunning = enable;
  if (enable)
  {
    // Run motor forward: IN3 = HIGH, IN4 = LOW
    digitalWrite(MOTOR_IN3_PIN, HIGH);
    digitalWrite(MOTOR_IN4_PIN, LOW);
    Serial.println("[MOTOR] Started (IN3/D14: HIGH, IN4/D27: LOW)");
  }
  else
  {
    // Stop motor: both inputs LOW
    digitalWrite(MOTOR_IN3_PIN, LOW);
    digitalWrite(MOTOR_IN4_PIN, LOW);
    Serial.println("[MOTOR] Stopped (IN3/D14: LOW, IN4/D27: LOW)");
  }
}

// ==========================================
// MQTT CALLBACK — handles incoming messages
// ==========================================
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  // Build a string from the payload bytes
  String message = "";
  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }
  message.trim();

  Serial.print("MQTT message [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  // Handle motor commands arriving from the hosted dashboard via MQTT
  if (String(topic) == MQTT_TOPIC_MOTOR)
  {
    if (message == "MOTOR_ON")
    {
      setMotor(true);
    }
    else if (message == "MOTOR_OFF")
    {
      setMotor(false);
    }

    // Forward the command to all local WebSocket clients as well
    webSocket.broadcastTXT(message);
  }
}

// ==========================================
// WEB SOCKET EVENT HANDLER
// ==========================================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_DISCONNECTED:
    Serial.printf("[%u] WebSocket Client Disconnected\n", num);
    break;

  case WStype_CONNECTED:
  {
    IPAddress ip = webSocket.remoteIP(num);
    Serial.printf("[%u] WebSocket Client Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
    // Send connection acknowledgment
    webSocket.sendTXT(num, "{\"status\":\"connected\",\"device\":\"ESP32_Compost\"}");
    // Sync current motor status with newly connected client
    webSocket.sendTXT(num, motorRunning ? "MOTOR_ON" : "MOTOR_OFF");
  }
  break;

  case WStype_TEXT:
  {
    String msg = String((char *)payload);
    msg.trim();
    Serial.printf("[%u] Received text: %s\n", num, msg.c_str());

    // Handle motor commands arriving from a local WebSocket client
    // and echo back to all WebSocket clients so their UI stays in sync
    if (msg == "MOTOR_ON" || msg == "MOTOR_OFF")
    {
      setMotor(msg == "MOTOR_ON");

      webSocket.broadcastTXT(msg);

      // Also publish motor state to MQTT so remote dashboard stays in sync
      if (mqttClient.connected())
      {
        mqttClient.publish(MQTT_TOPIC_MOTOR, msg.c_str());
      }
    }
  }
  break;

  default:
    break;
  }
}

// ==========================================
// CONNECT / RECONNECT HELPERS (NON-BLOCKING)
// ==========================================
void checkWiFi()
{
  static wl_status_t lastStatus = WL_IDLE_STATUS;

  wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED)
  {
    if (lastStatus != WL_CONNECTED)
    {
      Serial.println("WiFi Connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
    }

    lastStatus = status;
    return;
  }

  if (status == WL_CONNECT_FAILED ||
      status == WL_DISCONNECTED ||
      status == WL_NO_SSID_AVAIL)
  {
    if (millis() - lastWifiRetry > 10000)
    {
      lastWifiRetry = millis();

      Serial.println("Connecting to WiFi...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  lastStatus = status;
}

void checkMQTT()
{
  if (WiFi.status() == WL_CONNECTED && !mqttClient.connected())
  {
    unsigned long now = millis();
    // Try to reconnect every 10 seconds without blocking the loop
    if (now - lastMqttRetry > 10000 || lastMqttRetry == 0)
    {
      lastMqttRetry = now;
      Serial.print("Attempting MQTT connection to: ");
      Serial.println(MQTT_BROKER);
      if (mqttClient.connect(MQTT_CLIENT_ID))
      {
        Serial.println("Connected to online MQTT Broker.");
        // Subscribe to motor command topic so hosted dashboard can control pump/motor
        mqttClient.subscribe(MQTT_TOPIC_MOTOR);
        Serial.println("Subscribed to: " MQTT_TOPIC_MOTOR);
      }
      else
      {
        Serial.print("Failed, rc=");
        Serial.print(mqttClient.state());
        Serial.println(" Will retry in 10s.");
      }
    }
  }
}

// ==========================================
// SETUP
// ==========================================
void setup()
{
  // Configure L298N Motor Driver Pins (IN3 -> D14, IN4 -> D27)
  pinMode(MOTOR_IN3_PIN, OUTPUT);
  pinMode(MOTOR_IN4_PIN, OUTPUT);
  digitalWrite(MOTOR_IN3_PIN, LOW);
  digitalWrite(MOTOR_IN4_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.begin(115200);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  // Initialize hardware serial for communicating with Arduino UNO
  Serial2.begin(9600, SERIAL_8N1, 16, 17);

  // Configure ESP32 ADC Resolution (12-bit: 0 - 4095)
  analogReadResolution(12);

  // Configure MQTT
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback); // Register callback for incoming MQTT messages

  // Start WebSocket Server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  Serial.println("ESP32 Compost Monitor Initialized with L298N Motor Driver");
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop()
{
  // Run connection checks (non-blocking)
  checkWiFi();
  checkMQTT();

  if (WiFi.status() == WL_CONNECTED && !mdnsStarted)
  {
    if (MDNS.begin(MDNS_NAME))
    {
      Serial.println("mDNS responder started: compostpro.local");
      mdnsStarted = true;
    }
  }

  // Keep libraries active
  webSocket.loop();
  if (mqttClient.connected())
  {
    mqttClient.loop();
  }

  // 1. Read JSON telemetry from UNO via UART Serial2 (non-blocking line reading)
  if (Serial2.available())
  {
    unoJson = Serial2.readStringUntil('\n');
    unoJson.trim();
  }

  // 2. Periodically read internal sensors and broadcast/publish combined JSON
  unsigned long currentMillis = millis();
  if (currentMillis - lastSendTime >= sendInterval)
  {
    lastSendTime = currentMillis;

    // Read ESP32 Gas Sensors
    int mq9Raw = analogRead(MQ9_PIN);
    int mq137Raw = analogRead(MQ137_PIN);
    int micsRaw = analogRead(MICS2714_PIN);

    // Convert raw ADC readings to voltages (3.3V reference)
    float mq9Voltage = (mq9Raw * 3.3f) / 4095.0f;
    float mq137Voltage = (mq137Raw * 3.3f) / 4095.0f;
    float micsVoltage = (micsRaw * 3.3f) / 4095.0f;

    // Strip outer braces of Uno JSON to merge
    String inside = "";
    if (unoJson.startsWith("{") && unoJson.endsWith("}"))
    {
      inside = unoJson.substring(1, unoJson.length() - 1);
    }

    // Build unified JSON payload
    String payload = "{";
    if (inside.length() > 0)
    {
      payload += inside + ",";
    }
    payload += "\"mq9_raw\":" + String(mq9Raw);
    payload += ",\"mq9_v\":" + String(mq9Voltage, 3);
    payload += ",\"mq137_raw\":" + String(mq137Raw);
    payload += ",\"mq137_v\":" + String(mq137Voltage, 3);
    payload += ",\"mics2714_raw\":" + String(micsRaw);
    payload += ",\"mics2714_v\":" + String(micsVoltage, 3);
    payload += ",\"motor\":" + String(motorRunning ? 1 : 0);
    payload += "}";

    // Print combined telemetry to local USB Serial (PC debugging)
    Serial.println(payload);

    // Broadcast telemetry to all connected WebSocket clients
    webSocket.broadcastTXT(payload);

    // Publish telemetry to MQTT topic
    if (mqttClient.connected())
    {
      mqttClient.publish(MQTT_TOPIC, payload.c_str());
    }
  }
}
