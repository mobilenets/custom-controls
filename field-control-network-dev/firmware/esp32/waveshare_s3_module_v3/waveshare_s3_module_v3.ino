#include <Arduino.h>
#include <micro_ros_arduino.h>
#include <Preferences.h>
#include <Wire.h>

#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rcl/context.h>   // for rcl_context_get_rmw_context
#include <rmw_microros/rmw_microros.h>
#include <std_msgs/msg/string.h>
#include <Adafruit_NeoPixel.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <ETH.h>
#include <SPI.h>
#include <WiFi.h>
#include <Network.h>   // for Network.onEvent(...)
static bool eth_connected = false;
static bool wifi_connected = false;
static bool entities_created = false;
static rmw_context_t * g_rmw_context = nullptr;

static uint32_t last_net_ok_ms      = 0;
static uint32_t last_agent_ok_ms    = 0;
static uint32_t last_loop_ok_ms     = 0;
static uint32_t last_wifi_retry_ms  = 0;
static uint8_t  agent_fail_count    = 0;

static bool ever_connected = false;

// =============================
// Build options
// =============================
#define INCLUDE_RS485_CODE  1     // compile RS485 code in/out
#define RS485_RUNTIME_DETECT 1    // probe at boot and decide if present

// Waveshare W5500 (per wiki)
static constexpr int ETH_PHY_ADDR = 1;
static constexpr int ETH_CS_PIN   = 16;
static constexpr int ETH_IRQ_PIN  = 12;
static constexpr int ETH_RST_PIN  = -1;   // no reset pin shown on the wiki table

static constexpr int ETH_SPI_MOSI = 13;
static constexpr int ETH_SPI_MISO = 14;
static constexpr int ETH_SPI_SCK  = 15;

// =============================
// Waveshare board pins
// =============================
// support for rgb led
static constexpr uint8_t RGB_LED_PIN = 38;
static constexpr uint8_t RGB_LED_COUNT = 1;   // single WS2812 on-board
Adafruit_NeoPixel statusLed(RGB_LED_COUNT, RGB_LED_PIN, NEO_RGB + NEO_KHZ800);
static inline void led_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
  statusLed.setPixelColor(0, statusLed.Color(r, g, b));
  statusLed.show();
}
static inline void led_agent_disconnected() { led_set_rgb(255, 0, 0); } // RED
static inline void led_agent_connected()    { led_set_rgb(0, 255, 0); } // GREEN

// I2C for onboard relay expander (Waveshare headers showed these)
static constexpr uint8_t I2C_SCL_PIN = 41;
static constexpr uint8_t I2C_SDA_PIN = 42;

// Onboard DI (Waveshare wiki: DI1..DI8 are GPIO4..GPIO11)
static constexpr uint8_t NUM_INPUTS = 8;
static constexpr uint8_t inputPins[NUM_INPUTS] = {4,5,6,7,8,9,10,11};

// Onboard relay expander (TCA9554/PCA9554 compatible)
static constexpr uint8_t EXIO_ADDR  = 0x20;
static constexpr uint8_t REG_OUTPUT = 0x01;
static constexpr uint8_t REG_POLAR  = 0x02;
static constexpr uint8_t REG_CONFIG = 0x03;

// If relay polarity is inverted, flip this
static constexpr bool ACTIVE_LOW_ONBOARD = false;

// =============================
// RS485 (expansion relay) pins
// =============================
#if INCLUDE_RS485_CODE
static constexpr int RS485_TX_PIN    = 17;
static constexpr int RS485_RX_PIN    = 18;
static constexpr int RS485_DE_RE_PIN = 2;    // change if needed
static constexpr uint32_t RS485_BAUD = 9600;
static constexpr uint8_t  RS485_SLAVE_ID = 1;
static constexpr uint16_t RS485_TIMEOUT_MS = 200;
static constexpr uint16_t RS485_TURN_US = 200;
#endif

// =============================
// network functions
// =============================

static int g_udp_sock = -1;
static struct sockaddr_in g_agent_addr;

static bool udp_open_transport(struct uxrCustomTransport * /*transport*/) {
  g_udp_sock = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_udp_sock < 0) return false;

  // Optional: bind to any local port
  struct sockaddr_in local_addr{};
  local_addr.sin_family = AF_INET;
  local_addr.sin_port = htons(0);
  local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  lwip_bind(g_udp_sock, (struct sockaddr*)&local_addr, sizeof(local_addr));

  // Receive timeout (so reads don’t block forever)
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 200000; // 200ms
  lwip_setsockopt(g_udp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  // ✅ ADD THIS: Send timeout (prevents lwip_send() wedging forever)
  struct timeval tvs;
  tvs.tv_sec = 0;
  tvs.tv_usec = 200000; // 200ms
  lwip_setsockopt(g_udp_sock, SOL_SOCKET, SO_SNDTIMEO, &tvs, sizeof(tvs));

  // "Connect" UDP socket to agent so send/recv are tied to that endpoint
  if (lwip_connect(g_udp_sock, (struct sockaddr*)&g_agent_addr, sizeof(g_agent_addr)) != 0) {
    lwip_close(g_udp_sock);
    g_udp_sock = -1;
    return false;
  }

  return true;
}


static bool udp_close_transport(struct uxrCustomTransport * /*transport*/) {
  if (g_udp_sock >= 0) {
    lwip_close(g_udp_sock);
    g_udp_sock = -1;
  }
  return true;
}

static size_t udp_write_transport(struct uxrCustomTransport * /*transport*/,
                                  const uint8_t *buf, size_t len, uint8_t * /*err*/) {
  if (g_udp_sock < 0) return 0;
  int sent = lwip_send(g_udp_sock, buf, (int)len, 0);
  return (sent < 0) ? 0 : (size_t)sent;
}

static size_t udp_read_transport(struct uxrCustomTransport * /*transport*/,
                                 uint8_t *buf, size_t len, int timeout_ms, uint8_t * /*err*/) {
  if (g_udp_sock < 0) return 0;

  // Set per-call timeout
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  lwip_setsockopt(g_udp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  int recvd = lwip_recv(g_udp_sock, buf, (int)len, 0);
  return (recvd < 0) ? 0 : (size_t)recvd;
}

static bool set_agent_endpoint(const char* ip_str, uint16_t port) {
  memset(&g_agent_addr, 0, sizeof(g_agent_addr));
  g_agent_addr.sin_family = AF_INET;
  g_agent_addr.sin_port = htons(port);

  uint32_t a = inet_addr(ip_str);     // dotted IPv4 only
  if (a == INADDR_NONE) return false; // invalid
  g_agent_addr.sin_addr.s_addr = a;
  return true;
}

bool wifi_connect_with_timeout(const char* ssid, const char* pass, uint32_t timeout_ms) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, pass);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeout_ms) {
    delay(50);
  }
  wifi_connected = (WiFi.status() == WL_CONNECTED);

  if (wifi_connected) {
    Serial.print("WiFi IP: ");
    Serial.println(WiFi.localIP());
  }
  return wifi_connected;
}
static void transport_reset() {
  udp_close_transport(nullptr);
  delay(50);
  
}




// =============================
// ROS topics + message schema
// =============================
// Cmd:   /actionrequest_u8   [module_id, relay_id(1..16), desired_state(0/1)]
// State: /modulereturn_u8    [module_id, r1..rN, i1..i8]  where N=8 or 16

// =============================
// micro-ROS lifecycle (your style)
// =============================
enum agent_state_t {
  WAITING_AGENT,
  AGENT_AVAILABLE,
  AGENT_CONNECTED,
  AGENT_DISCONNECTED
};

agent_state_t state;
unsigned long disconnectedAt = 0;

#define RCCHECK(fn) do { rcl_ret_t rc = (fn); if (rc != RCL_RET_OK) return false; } while(0)
#define EXECUTE_EVERY_N_MS(MS, X) do { static int64_t last = 0; if (uxr_millis() - last > (MS)) { X; last = uxr_millis(); } } while(0)

// =============================
// Config (Preferences)
// =============================
Preferences prefs;
String ssid, password, agentIp, agentPort, moduleIdStr;


#define WIFI_PASS_MAX   64
#define AGENT_IP_MAX    16


// =============================
// IO state
// =============================
static constexpr uint8_t ONBOARD_RELAYS = 8;
static bool rs485_present = false;
static uint8_t relay_count = ONBOARD_RELAYS; // becomes 16 if rs485 present

// Track states for publishing (truthy enough; onboard readback optional)
static bool relayStates[16] = {0};      // indices 0..15 represent relays 1..16
static bool inputStates[NUM_INPUTS] = {0};
static bool lastInputStates[NUM_INPUTS] = {0};

// Onboard relays are driven as a mask
static uint8_t onboard_mask = 0x00;

// =============================
// micro-ROS objects/messages
// =============================
rcl_allocator_t allocator;
rclc_support_t support;
rcl_node_t node;
rcl_timer_t timer;
rclc_executor_t executor;
rcl_publisher_t pub;
rcl_subscription_t sub;

std_msgs__msg__String pub_msg;
std_msgs__msg__String sub_msg;

// fixed buffers, max size when rs485 present: 1 + 16 + 8 = 25
//static uint8_t pub_data[25];
#define PUB_MSG_BUFFER_SIZE 256
static char pub_msg_buffer[PUB_MSG_BUFFER_SIZE];
#define SUB_MSG_BUFFER_SIZE 128
static char sub_msg_buffer[SUB_MSG_BUFFER_SIZE];

// =============================
// Helpers
// =============================
static inline bool get_module_id_u8(uint8_t &out_id) {
  if (moduleIdStr.length() == 0) return false;
  int v = moduleIdStr.toInt();
  if (v < 0 || v > 255) return false;
  out_id = (uint8_t)v;
  return true;
}

String readLine() {
  String input;
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (input.length()) break;
      } else {
        input += c;
        Serial.print(c);
      }
    }
  }
  Serial.println();
  return input;
}

void enterConfigurationMode() {
  Serial.println("\n=== Configuration Mode ===");

  Serial.print("Wi-Fi SSID: ");
  ssid = readLine();
  Serial.print("Wi-Fi Password: ");
  password = readLine();
  Serial.print("Agent IP: ");
  agentIp = readLine();
  Serial.print("Agent Port: ");
  agentPort = readLine();
  Serial.print("Module ID (0-255): ");
  moduleIdStr = readLine();

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.putString("agentIp", agentIp);
  prefs.putString("agentPort", agentPort);
  prefs.putString("moduleId", moduleIdStr);
  prefs.end();

  Serial.println("Configuration saved.\n");
}

void loadConfiguration() {
  prefs.begin("wifi", true);
  ssid = prefs.getString("ssid", "");
  password = prefs.getString("password", "");
  agentIp = prefs.getString("agentIp", "");
  agentPort = prefs.getString("agentPort", "");
  moduleIdStr = prefs.getString("moduleId", "");
  prefs.end();
}



void onNetworkEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      ETH.setHostname("waveshare-s3");
      eth_connected = false;
      // setStatusLed(false); // red
      break;

    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH Link Up");
      break;

    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("ETH Got IP: ");
      Serial.println(ETH.localIP());
      Serial.print("Gateway: ");
      Serial.println(ETH.gatewayIP());
      eth_connected = true;
      // setStatusLed(true); // green
      break;

    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      // setStatusLed(false); // red
      break;

    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      // setStatusLed(false); // red
      break;

    default:
      break;
  }
}

static inline bool link_ok() {
  return eth_connected || (WiFi.status() == WL_CONNECTED);
}

static inline bool ip_ok() {
  if (eth_connected) {
    return ETH.localIP() != IPAddress(0, 0, 0, 0);
  }
  return (WiFi.status() == WL_CONNECTED) &&
         (WiFi.localIP() != IPAddress(0, 0, 0, 0));
}

static inline bool network_ok() {
  return link_ok() && ip_ok();
}

static inline void note_loop_ok() {
  last_loop_ok_ms = millis();
}

static inline void note_net_ok() {
  last_net_ok_ms = millis();
}

static inline void note_agent_ok() {
  last_agent_ok_ms = millis();
  agent_fail_count = 0;
}

static void teardown_microros() {
  if (entities_created) {
    if (g_rmw_context != nullptr) {
      rmw_uros_set_context_entity_destroy_session_timeout(g_rmw_context, 0);
    }
    destroy_entities();
    entities_created = false;
  }

  udp_close_transport(nullptr);
  g_rmw_context = nullptr;
}
static void rearm_microros_transport() {
  udp_close_transport(nullptr);
  delay(50);

  rmw_uros_set_custom_transport(
    false,
    nullptr,
    udp_open_transport,
    udp_close_transport,
    udp_write_transport,
    udp_read_transport
  );
}
static void check_last_resort_reboot() {
  const uint32_t now = millis();

  // If the main loop itself appears dead for too long, reboot
  if (now - last_loop_ok_ms > 180000UL) {
    Serial.println("Loop stalled > 3 minutes. Restarting.");
    Serial.flush();
    ESP.restart();
  }

  // If network has been bad for too long, reboot
  if (now - last_net_ok_ms > 180000UL) {
    Serial.println("Network unavailable > 3 minutes. Restarting.");
    Serial.flush();
    ESP.restart();
  }

  // If we had connected before, but agent has been unreachable too long, reboot
  if (ever_connected && (now - last_agent_ok_ms > 180000UL)) {
    Serial.println("Agent unreachable > 3 minutes. Restarting.");
    Serial.flush();
    ESP.restart();
  }
}
static void recover_wifi_if_needed() {
  const uint32_t now = millis();

  if (eth_connected) return;  // prefer ethernet, don't fight it
  if (WiFi.status() == WL_CONNECTED) return;

  if (now - last_wifi_retry_ms < 5000) return;
  last_wifi_retry_ms = now;

  Serial.println("WiFi down, retrying...");

  WiFi.disconnect();
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (wifi_connect_with_timeout(ssid.c_str(), password.c_str(), 5000)) {
    Serial.print("WiFi recovered: ");
    Serial.println(WiFi.localIP());
    note_net_ok();
  }
}


// =============================
// I2C expander (onboard relays)
// =============================
void I2C_Init() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);
}

bool I2C_Write(uint8_t dev, uint8_t reg, const uint8_t* data, uint32_t len) {
  Wire.beginTransmission(dev);
  Wire.write(reg);
  for (uint32_t i=0;i<len;i++) Wire.write(data[i]);
  return (Wire.endTransmission(true) == 0);
}

bool I2C_Read(uint8_t dev, uint8_t reg, uint8_t* data, uint32_t len) {
  Wire.beginTransmission(dev);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  uint32_t got = Wire.requestFrom((int)dev, (int)len, (int)true);
  if (got != len) return false;
  for (uint32_t i=0;i<len;i++) data[i] = Wire.read();
  return true;
}

static inline uint8_t apply_onboard_polarity(uint8_t m) {
  return ACTIVE_LOW_ONBOARD ? (uint8_t)~m : m;
}
static inline uint8_t remove_onboard_polarity(uint8_t v) {
  return ACTIVE_LOW_ONBOARD ? (uint8_t)~v : v;
}

bool exio_init_outputs() {
  // 1) Set output latch to a safe state first (ALL OFF)
  uint8_t safe_out = apply_onboard_polarity(0x00);   // logical off mask
  if (!I2C_Write(EXIO_ADDR, REG_OUTPUT, &safe_out, 1)) return false;

  // 2) No inversion inside chip
  uint8_t pol = 0x00;
  if (!I2C_Write(EXIO_ADDR, REG_POLAR, &pol, 1)) return false;

  // 3) Now configure pins as outputs
  uint8_t cfg = 0x00; // all outputs
  if (!I2C_Write(EXIO_ADDR, REG_CONFIG, &cfg, 1)) return false;

  return true;
}

bool exio_set_mask(uint8_t mask) {
  uint8_t out = apply_onboard_polarity(mask);
  return I2C_Write(EXIO_ADDR, REG_OUTPUT, &out, 1);
}

bool exio_get_mask(uint8_t *mask_out) {
  if (!mask_out) return false;
  uint8_t out = 0;
  if (!I2C_Read(EXIO_ADDR, REG_OUTPUT, &out, 1)) return false;
  *mask_out = remove_onboard_polarity(out);
  return true;
}

void onboard_set_relay(uint8_t relay_id_1_to_8, bool on) {
  uint8_t bit = relay_id_1_to_8 - 1;
  if (on) onboard_mask |=  (1u << bit);
  else    onboard_mask &= ~(1u << bit);

  exio_set_mask(onboard_mask);
  relayStates[bit] = on;
}

// Restore onboard relays: hardware truth first, else flash fallback
void onboard_all_off_now() {
  onboard_mask = 0x00;           // logical OFF for all channels
  exio_set_mask(onboard_mask);   // will invert if ACTIVE_LOW_ONBOARD==true
  for (int i = 0; i < 8; i++) relayStates[i] = false;
}

void restore_onboard_relays() {
  prefs.begin("relays", false);
  uint8_t flash_mask = prefs.getUChar("mask_onb", 0x00);

  uint8_t hw_mask = 0;
  bool got_hw = exio_get_mask(&hw_mask);
  onboard_mask = got_hw ? hw_mask : flash_mask;

  exio_set_mask(onboard_mask);

  for (int i=0;i<8;i++) relayStates[i] = (onboard_mask & (1u<<i)) ? 1 : 0;
  prefs.end();
}

// Save onboard mask (simple; you can debounce later)
void persist_onboard_mask() {
  prefs.begin("relays", false);
  //prefs.putUChar("mask_onb", onboard_mask);
  prefs.end();
}

// =============================
// Inputs scan
// =============================
void scanInputs() {
  for (int i=0;i<NUM_INPUTS;i++) {
    bool v = digitalRead(inputPins[i]);
    if (v != lastInputStates[i]) {
      inputStates[i] = v;
      lastInputStates[i] = v;
    }
  }
}

// =============================
// RS485 Modbus (optional)
// =============================
#if INCLUDE_RS485_CODE
static HardwareSerial& RS485 = Serial1;

static uint16_t modbus_crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i=0;i<len;i++) {
    crc ^= data[i];
    for (int j=0;j<8;j++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
  }
  return crc;
}

static inline void rs485_dir_tx(bool tx) {
  digitalWrite(RS485_DE_RE_PIN, tx ? HIGH : LOW);
  delayMicroseconds(RS485_TURN_US);
}

void rs485_begin() {
  pinMode(RS485_DE_RE_PIN, OUTPUT);
  rs485_dir_tx(false);
  RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  RS485.setTimeout(RS485_TIMEOUT_MS);
}

bool modbus_read_coils_8(uint8_t slave_id, uint16_t start_addr, uint8_t* out_byte) {
  // Request: [id][0x01][addr_hi][addr_lo][qty_hi][qty_lo][crc_lo][crc_hi]
  uint8_t req[8];
  req[0]=slave_id; req[1]=0x01;
  req[2]=(uint8_t)(start_addr>>8); req[3]=(uint8_t)(start_addr&0xFF);
  req[4]=0x00; req[5]=0x08;
  uint16_t crc = modbus_crc16(req,6);
  req[6]=(uint8_t)(crc&0xFF); req[7]=(uint8_t)(crc>>8);

  while (RS485.available()) RS485.read();

  rs485_dir_tx(true);
  RS485.write(req, sizeof(req));
  RS485.flush();
  rs485_dir_tx(false);

  // Response: [id][0x01][bytecount=1][data][crc_lo][crc_hi]
  uint8_t resp[6];
  size_t n=0;
  uint32_t start = millis();
  while ((millis()-start) < RS485_TIMEOUT_MS && n < sizeof(resp)) {
    if (RS485.available()) {
      int b = RS485.read();
      if (b >= 0) resp[n++] = (uint8_t)b;
    }
  }
  if (n < 6) return false;
  uint16_t got = (uint16_t)resp[4] | ((uint16_t)resp[5]<<8);
  uint16_t calc = modbus_crc16(resp,4);
  if (got != calc) return false;
  if (resp[0] != slave_id || resp[1] != 0x01 || resp[2] != 0x01) return false;
  *out_byte = resp[3];
  return true;
}

bool modbus_write_single_coil(uint8_t slave_id, uint16_t coil_addr, bool on) {
  // Value: FF00 on, 0000 off
  uint8_t req[8];
  req[0]=slave_id; req[1]=0x05;
  req[2]=(uint8_t)(coil_addr>>8); req[3]=(uint8_t)(coil_addr&0xFF);
  req[4]= on ? 0xFF : 0x00;
  req[5]= 0x00;
  uint16_t crc = modbus_crc16(req,6);
  req[6]=(uint8_t)(crc&0xFF); req[7]=(uint8_t)(crc>>8);

  while (RS485.available()) RS485.read();

  rs485_dir_tx(true);
  RS485.write(req, sizeof(req));
  RS485.flush();
  rs485_dir_tx(false);

  // Response is echo of request (8 bytes) — we’ll read 8 and validate CRC
  uint8_t resp[8];
  size_t n=0;
  uint32_t start = millis();
  while ((millis()-start) < RS485_TIMEOUT_MS && n < sizeof(resp)) {
    if (RS485.available()) {
      int b = RS485.read();
      if (b >= 0) resp[n++] = (uint8_t)b;
    }
  }
  if (n < 8) return false;
  uint16_t got = (uint16_t)resp[6] | ((uint16_t)resp[7]<<8);
  uint16_t calc = modbus_crc16(resp,6);
  if (got != calc) return false;
  // minimal check
  return resp[0]==slave_id && resp[1]==0x05;
}

void rs485_set_relay(uint8_t relay_id_9_to_16, bool on) {
  uint8_t idx = relay_id_9_to_16 - 9; // 0..7
  if (!rs485_present) return;
  if (modbus_write_single_coil(RS485_SLAVE_ID, (uint16_t)idx, on)) {
    relayStates[8 + idx] = on; // assumed truth until we poll
  }
}

bool rs485_detect_and_sync() {
  uint8_t coils = 0;
  if (!modbus_read_coils_8(RS485_SLAVE_ID, 0x0000, &coils)) return false;

  // update relayStates[8..15]
  for (int i=0;i<8;i++) relayStates[8+i] = (coils & (1u<<i)) ? 1 : 0;
  return true;
}
#endif

// =============================
// Unified relay setter
// =============================
void setRelay(uint8_t relay_id_1_to_16, bool on) {
  if (relay_id_1_to_16 < 1) return;

  if (relay_id_1_to_16 <= 8) {
    onboard_set_relay(relay_id_1_to_16, on);
    //persist_onboard_mask(); // simple persistence; you can debounce later
    return;
  }

#if INCLUDE_RS485_CODE
  if (relay_id_1_to_16 <= 16 && rs485_present) {
    rs485_set_relay(relay_id_1_to_16, on);
    return;
  }
#endif
}

// =============================
// ROS callbacks
// =============================
//void timer_callback(rcl_timer_t*, int64_t) {
  void timer_callback(){
  uint8_t mid = 0;
  if (!get_module_id_u8(mid)) return;

#if INCLUDE_RS485_CODE
  EXECUTE_EVERY_N_MS(1000, {
    if (rs485_present) rs485_detect_and_sync();
  });
#endif

  relay_count = rs485_present ? 16 : 8;

  int idx = 0;

  // module_id
  idx += snprintf(
    pub_msg_buffer + idx,
    PUB_MSG_BUFFER_SIZE - idx,
    "%u;R:",
    mid
  );

  // relay states
  for (int i = 0; i < relay_count; i++) {
    idx += snprintf(
      pub_msg_buffer + idx,
      PUB_MSG_BUFFER_SIZE - idx,
      "%u%s",
      relayStates[i] ? 1 : 0,
      (i < relay_count - 1) ? "," : ""
    );
  }

  // inputs
  idx += snprintf(
    pub_msg_buffer + idx,
    PUB_MSG_BUFFER_SIZE - idx,
    ";I:"
  );

  for (int i = 0; i < NUM_INPUTS; i++) {
    idx += snprintf(
      pub_msg_buffer + idx,
      PUB_MSG_BUFFER_SIZE - idx,
      "%u%s",
      inputStates[i] ? 1 : 0,
      (i < NUM_INPUTS - 1) ? "," : ""
    );
  }

  pub_msg.data.data = pub_msg_buffer;
  pub_msg.data.size = idx;
  pub_msg.data.capacity = PUB_MSG_BUFFER_SIZE;
  rcl_publish(&pub, &pub_msg, NULL);
}


void sub_callback(const void* msgin) {
  const std_msgs__msg__String* in = (const std_msgs__msg__String*)msgin;

  // Safely build a String from data+size (handles non-null-terminated)
  String payload;
  payload.reserve(in->data.size + 1);
  for (size_t i = 0; i < in->data.size; i++) payload += (char)in->data.data[i];
  payload.trim();

  // Expected: "<moduleId>;<relayIndex>;<ON|OFF>"
  int s1 = payload.indexOf(';');
  int s2 = payload.indexOf(';', s1 + 1);
  if (s1 < 0 || s2 < 0) return;

  String target = payload.substring(0, s1);
  if (target != moduleIdStr) return;

  int relay_id = payload.substring(s1 + 1, s2).toInt();
  String cmd = payload.substring(s2 + 1);
  cmd.trim();
  cmd.toUpperCase();

  // Determine supported relay range dynamically
  uint8_t max_relays = rs485_present ? 16 : 8;
  if (relay_id < 1 || relay_id > (int)max_relays) return;

  if (cmd == "ON" || cmd == "1") {
    setRelay((uint8_t)relay_id, true);
  } else if (cmd == "OFF" || cmd == "0") {
    setRelay((uint8_t)relay_id, false);
  }
}


// =============================
// micro-ROS entity lifecycle
// =============================
bool create_entities() {
  allocator = rcl_get_default_allocator();

  // Fixed sub buffer
  sub_msg.data.data = sub_msg_buffer;
  sub_msg.data.capacity = SUB_MSG_BUFFER_SIZE;
  sub_msg.data.size = 0;

  RCCHECK(rclc_support_init(&support, 0, NULL, &allocator));
  RCCHECK(rclc_node_init_default(&node, "waveshare_s3_module", "", &support));

  // Save rmw_context for timeout control during teardown/reconnect
  g_rmw_context = rcl_context_get_rmw_context(&support.context);
  if (g_rmw_context != nullptr) {
    // Optional: keep creation snappy too
    rmw_uros_set_context_entity_creation_session_timeout(g_rmw_context, 1000);
    rmw_uros_set_context_entity_destroy_session_timeout(g_rmw_context, 1000);
  }

  RCCHECK(rclc_publisher_init_best_effort(
    &pub, &node,
    ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String),
    "modulereturn"
  ));

  RCCHECK(rclc_subscription_init_best_effort(
    &sub, &node,
    ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String),
    "actionrequest"
  ));

  //RCCHECK(rclc_timer_init_default(&timer, &support, RCL_MS_TO_NS(1000), timer_callback));

  RCCHECK(rclc_executor_init(&executor, &support.context, 2, &allocator));
  //RCCHECK(rclc_executor_add_timer(&executor, &timer));
  RCCHECK(rclc_executor_add_subscription(&executor, &sub, &sub_msg, sub_callback, ON_NEW_DATA));

  return true;
}

void destroy_entities() {
  rcl_publisher_fini(&pub, &node);
  rcl_subscription_fini(&sub, &node);
  //rcl_timer_fini(&timer);
  rclc_executor_fini(&executor);
  rcl_node_fini(&node);
  rclc_support_fini(&support);
  //memset(&publisher, 0, sizeof(publisher)); // etc
}

// =============================
// Setup / Loop
// =============================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("Reset reason: %d\n", (int)esp_reset_reason());


  statusLed.begin();
  statusLed.setBrightness(40);     // 0-255, keep it modest
  led_agent_disconnected();         // default RED until connected

  // Inputs
  for (int i=0;i<NUM_INPUTS;i++) pinMode(inputPins[i], INPUT_PULLUP);

  // Onboard relays init ASAP
  I2C_Init();
  exio_init_outputs();
  onboard_all_off_now();   // force OFF every boot

#if INCLUDE_RS485_CODE
  rs485_begin();

#if RS485_RUNTIME_DETECT
  rs485_present = rs485_detect_and_sync();
#else
  rs485_present = true;
#endif

  if (rs485_present) Serial.println("RS485 expansion detected.");
  else Serial.println("RS485 expansion NOT detected (running 8 relays only).");
#endif

  // --- Ethernet (W5500) ---
  Network.onEvent(onNetworkEvent);

  // Use the W5500 SPI pins on this board (MOSI=13, MISO=14, SCK=15, CS=16)
  SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI, ETH_CS_PIN);

  // Start W5500
  ETH.begin(ETH_PHY_W5500, ETH_PHY_ADDR, ETH_CS_PIN, ETH_IRQ_PIN, ETH_RST_PIN, SPI);


  // Config mode
  Serial.println("Press 'c' in 5 seconds to enter config mode...");
  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {
    if (Serial.available() && Serial.read() == 'c') {
      enterConfigurationMode();
      break;
    }
  }

  loadConfiguration();
  ssid.trim(); password.trim(); agentIp.trim(); agentPort.trim(); moduleIdStr.trim();

  if (ssid.length()==0 || password.length()==0 || agentIp.length()==0 || agentPort.length()==0 || moduleIdStr.length()==0) {
    Serial.println("Missing config. Hold 'c' on boot to configure.");
    while (true) delay(1000);
  }


  // Prefer ETH; fallback to WiFi
  Serial.println("Waiting for Ethernet IP (preferred)...");
  unsigned long t0 = millis();
  while (!eth_connected && (millis() - t0) < 3000) {  // 3 seconds
    delay(50);
  }

  if (eth_connected) {
    Serial.print("Using Ethernet. IP=");
    Serial.println(ETH.localIP());
  } else {
    Serial.println("Ethernet not ready. Falling back to WiFi...");
    if (!wifi_connect_with_timeout(ssid.c_str(), password.c_str(), 10000)) {
      Serial.println("WiFi failed too. Not starting micro-ROS.");
      //while (true) delay(1000);
      Serial.flush();
      ESP.restart();
    }
    Serial.println("Using WiFi.");
  }

  set_agent_endpoint(agentIp.c_str(), (uint16_t)agentPort.toInt());

  if (udp_open_transport(nullptr)) {
   const char* msg = "udp_test";
   size_t sent = udp_write_transport(nullptr, (const uint8_t*)msg, strlen(msg), nullptr);
   Serial.print("UDP test sent bytes: "); Serial.println((int)sent);
   udp_close_transport(nullptr);
  }


  Serial.print("Agent IP: '"); Serial.print(agentIp); Serial.print("' Port: "); Serial.println(agentPort);

  if (!set_agent_endpoint(agentIp.c_str(), (uint16_t)agentPort.toInt())) {
    Serial.println("ERROR: Agent IP invalid (must be dotted IPv4).");
    while (true) delay(1000);
  }

  rmw_uros_set_custom_transport(
   false,
    nullptr,
    udp_open_transport,
   udp_close_transport,
    udp_write_transport,
   udp_read_transport
  );

  Serial.println("micro-ROS UDP transport set (ETH preferred, WiFi fallback).");

  uint32_t now = millis();
  last_loop_ok_ms   = now;
  last_net_ok_ms    = now;
  last_agent_ok_ms  = now;
  last_wifi_retry_ms = 0;
  agent_fail_count  = 0;
  ever_connected    = false;



  disconnectedAt = millis();
  state = WAITING_AGENT;
}

void loop() {
  //Serial.println(state);
  switch (state) {
    case WAITING_AGENT: {
      led_agent_disconnected();
      note_loop_ok();

      if (network_ok()) {
        note_net_ok();
      } else {
        recover_wifi_if_needed();
        //check_last_resort_reboot();
        delay(100);
        break;
      }

      static uint8_t ok_count = 0;

      if (rmw_uros_ping_agent(200, 1) == RMW_RET_OK) {
        note_agent_ok();

        if (++ok_count >= 3) {
          ok_count = 0;
          Serial.println("Agent detected (stable).");
          state = AGENT_AVAILABLE;
        }
      } else {
        ok_count = 0;
      }

      //check_last_resort_reboot();
      delay(100);
      break;
    }


    case AGENT_AVAILABLE: {
      note_loop_ok();

      rearm_microros_transport();

      if (create_entities()) {
        entities_created = true;
        ever_connected = true;

        note_net_ok();
        note_agent_ok();

        Serial.println("Entities created. Connected.");
        led_agent_connected();
        state = AGENT_CONNECTED;
      } else {
        Serial.println("Create entities failed, returning to WAITING_AGENT.");
        teardown_microros();
        state = WAITING_AGENT;
      }

      break;
    }


    case AGENT_CONNECTED: {
      note_loop_ok();

      const uint32_t now = millis();
      static uint32_t last_inputs_ms = 0;
      static uint32_t last_pub_ms    = 0;
      static uint32_t last_ping_ms   = 0;
      static uint32_t last_spin_ms   = 0;

      if (network_ok()) {
        note_net_ok();
      } else {
        Serial.println("Network lost.");
        state = AGENT_DISCONNECTED;
        led_agent_disconnected();
        break;
      }

      if (now - last_inputs_ms >= 50) {
        last_inputs_ms = now;
        scanInputs();
      }

    #if INCLUDE_RS485_CODE
      static uint32_t last_rs485_ms = 0;
      if (now - last_rs485_ms >= 1000) {
        last_rs485_ms = now;
        if (rs485_present) {
          rs485_detect_and_sync();
        }
      }
    #endif

      if (now - last_pub_ms >= 1000) {
        last_pub_ms = now;
        timer_callback();   // your manual publish function
      }

      if (now - last_ping_ms >= 1000) {
        last_ping_ms = now;

        if (rmw_uros_ping_agent(200, 1) == RMW_RET_OK) {
          note_agent_ok();
        } else {
          agent_fail_count++;
          if (agent_fail_count >= 3) {
            Serial.println("Agent lost!");
            state = AGENT_DISCONNECTED;
            led_agent_disconnected();
            break;
          }
        }
      }

      if (now - last_spin_ms >= 5) {
        last_spin_ms = now;

        uint32_t t0 = millis();
        rclc_executor_spin_some(&executor, RCL_MS_TO_NS(2));
        uint32_t dt = millis() - t0;

        if (dt > 250) {
          Serial.printf("WARN: spin_some slow (%lu ms)\n", (unsigned long)dt);
        }
      }

      //check_last_resort_reboot();
      break;
    }

    case AGENT_DISCONNECTED: {
      note_loop_ok();

      teardown_microros();
      delay(300);

      agent_fail_count = 0;
      state = WAITING_AGENT;

      //check_last_resort_reboot();
      break;
    }
  }
}
