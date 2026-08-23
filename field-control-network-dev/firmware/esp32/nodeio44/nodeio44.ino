#include <micro_ros_arduino.h>

#include <micro_ros_arduino.h>


#include <micro_ros_arduino.h>
#include <Preferences.h>

#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>

#include "module_msgs/msg/module_status.h"
#include "module_msgs/msg/module_command.h"

// ======================================================
// ---------------- Pin Definitions ---------------------
// ======================================================

#define NUM_RELAYS 4
#define NUM_INPUTS 4

const uint8_t relayPins[NUM_RELAYS] = {21, 19, 18, 5};
const uint8_t inputPins[NUM_INPUTS] = {32, 33, 26, 27};

// ======================================================
// ---------------- Configuration -----------------------
// ======================================================

#define WIFI_SSID_MAX   32
#define WIFI_PASS_MAX   64
#define AGENT_IP_MAX    16

uint8_t module_id   = 1;
uint8_t module_type = 3;   // defined externally (XML enum)

uint16_t agent_port = 8888;

static char wifi_ssid[WIFI_SSID_MAX];
static char wifi_pass[WIFI_PASS_MAX];
static char agent_ip[AGENT_IP_MAX];

bool checkSerialConfig(uint32_t timeout_ms);
void enterConfigurationMode();
String readLine();

bool checkSerialConfig(uint32_t timeout_ms) {
  Serial.println();
  Serial.println("Press 'c' within 5 seconds to enter configuration...");

  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'c' || c == 'C') {
        return true;
      }
    }
    delay(10);
  }
  return false;
}

void enterConfigurationMode(Preferences &prefs) {
  Serial.println("\n=== Configuration Mode ===");

  prefs.begin("wifi", false);

  Serial.print("WiFi SSID: ");
  String ssid = readLine();
  prefs.putString("ssid", ssid);

  Serial.print("WiFi Password: ");
  String pass = readLine();
  prefs.putString("password", pass);

  Serial.print("Agent IP: ");
  String ip = readLine();
  prefs.putString("agentIp", ip);

  Serial.print("Agent Port (default 8888): ");
  String port = readLine();
  prefs.putUInt("agentPort", port.toInt());

  Serial.print("Module ID (uint8): ");
  module_id = readLine().toInt();
  prefs.putUChar("moduleId", module_id);

  Serial.print("Module Type (uint8): ");
  module_type = readLine().toInt();
  prefs.putUChar("moduleType", module_type);

  prefs.end();

  Serial.println("\nConfiguration saved. Rebooting...");
  delay(1000);
  ESP.restart();
}
void printCurrentConfig() {
  Serial.println("\n--- Current Configuration ---");
  Serial.print("WiFi SSID: "); Serial.println(wifi_ssid);
  Serial.print("WiFi Password: "); Serial.println(wifi_pass);
  Serial.print("Agent IP: "); Serial.println(agent_ip);
  Serial.print("Agent Port: "); Serial.println(agent_port);
  Serial.print("Module ID: "); Serial.println(module_id);
  Serial.print("Module Type: "); Serial.println(module_type);
  Serial.println("-----------------------------\n");
}


Preferences prefs;

// ======================================================
// ---------------- micro-ROS Objects -------------------
// ======================================================

rcl_allocator_t allocator;
rclc_support_t support;
rcl_node_t node;
rcl_publisher_t publisher;
rcl_subscription_t subscriber;
rcl_timer_t timer;
rclc_executor_t executor;

module_msgs__msg__ModuleStatus pub_msg;
module_msgs__msg__ModuleCommand sub_msg;

// ======================================================
// ---------------- State Tracking ----------------------
// ======================================================

bool relayState[NUM_RELAYS] = {false};

bool inputState[NUM_INPUTS]     = {false};
bool lastInputState[NUM_INPUTS] = {false};
unsigned long lastDebounce[NUM_INPUTS] = {0};

#define DEBOUNCE_MS 30
volatile bool inputChanged = false;

// ======================================================
// ---------------- Agent State -------------------------
// ======================================================

enum agent_state_t {
  WAITING_AGENT,
  AGENT_AVAILABLE,
  AGENT_CONNECTED,
  AGENT_DISCONNECTED
};

agent_state_t agent_state = WAITING_AGENT;

// ======================================================
// ---------------- Helpers -----------------------------
// ======================================================

#define RCCHECK(fn) { if ((fn) != RCL_RET_OK) return false; }

#define EXECUTE_EVERY_MS(ms, code)           \
  do {                                      \
    static uint32_t last = 0;               \
    if (millis() - last >= ms) {            \
      last = millis();                      \
      code;                                 \
    }                                       \
  } while (0)

String readLine() {
  String input;
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (input.length() > 0) break;
      } else {
        input += c;
        Serial.print(c); // echo
      }
    }
    delay(10);
  }
  Serial.println();
  return input;
}

// ======================================================
// ---------------- Relay Control -----------------------
// ======================================================

void setRelay(uint8_t index, bool on) {
  if (index < NUM_RELAYS) {
    digitalWrite(relayPins[index], on ? HIGH : LOW);
    relayState[index] = on;
  }
}

// ======================================================
// ---------------- Input Polling -----------------------
// ======================================================

void pollInputs() {
  unsigned long now = millis();

  for (int i = 0; i < NUM_INPUTS; i++) {
    bool reading = digitalRead(inputPins[i]);

    if (reading != lastInputState[i]) {
      lastDebounce[i] = now;
      lastInputState[i] = reading;
    }

    if ((now - lastDebounce[i]) > DEBOUNCE_MS) {
      if (reading != inputState[i]) {
        inputState[i] = reading;
        inputChanged = true;
      }
    }
  }
}

// ======================================================
// ---------------- Publishing --------------------------
// ======================================================

void publishStatus() {
  pub_msg.module_id   = module_id;
  pub_msg.module_type = module_type;

  for (int i = 0; i < NUM_INPUTS; i++) {
    pub_msg.inputs[i] = inputState[i];
  }

  for (int i = 0; i < NUM_RELAYS; i++) {
    pub_msg.outputs[i] = relayState[i];
  }

  rcl_publish(&publisher, &pub_msg, NULL);
}

// ======================================================
// ---------------- Callbacks ---------------------------
// ======================================================

void timer_callback(rcl_timer_t *, int64_t) {
  publishStatus();
  inputChanged = false;
}

void sub_callback(const void * msgin) {
  Serial.println(">>> ModuleCommand received <<<");
  const module_msgs__msg__ModuleCommand * cmd =
    (const module_msgs__msg__ModuleCommand *) msgin;

    // --- NEW: print incoming message ---
    Serial.print("Received ModuleCommand for module_id: ");
    Serial.println(cmd->module_id);
    Serial.print("Outputs: ");
    for (int i = 0; i < 8; i++) {  // assuming outputs[8] in your message
      Serial.print(cmd->outputs[i]);
      if (i < 7) Serial.print(", ");
    }
    Serial.println();

    Serial.print("Callback fired: module_id="); Serial.println(cmd->module_id);
    Serial.print("Outputs: ");
    for (int i=0;i<8;i++) {
      Serial.print(cmd->outputs[i]);
      Serial.print(" ");
    }
  Serial.println();
   // --- END new print ---

  if (cmd->module_id != module_id) {
    return; // not for this module
  }

  for (int i = 0; i < NUM_RELAYS; i++) {
    setRelay(i, cmd->outputs[i]);
  }
}

// ======================================================
// ---------------- micro-ROS Entities ------------------
// ======================================================

bool create_entities() {
  allocator = rcl_get_default_allocator();

  // --- Initialize support ---
  RCCHECK(rclc_support_init(&support, 0, NULL, &allocator));

  // --- Create node ---
  RCCHECK(rclc_node_init_default(&node, "esp32_module", "", &support));

  // --- Create publisher ---
  RCCHECK(rclc_publisher_init_best_effort(
      &publisher,
      &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(module_msgs, msg, ModuleStatus),
      "modulereturn"
  ));

  // --- Create subscriber with Best-Effort QoS ---
  RCCHECK(rclc_subscription_init_best_effort(
      &subscriber,
      &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(module_msgs, msg, ModuleCommand),
      "/actionrequest"
  ));

  // --- Create timer ---
  RCCHECK(rclc_timer_init_default(
      &timer,
      &support,
      RCL_MS_TO_NS(1000),
      timer_callback
  ));

  // --- Create executor ---
  RCCHECK(rclc_executor_init(&executor, &support.context, 2, &allocator));
  RCCHECK(rclc_executor_add_timer(&executor, &timer));
  RCCHECK(rclc_executor_add_subscription(
      &executor, &subscriber, &sub_msg, &sub_callback, ON_NEW_DATA
  ));

  Serial.println("micro-ROS entities created with Best-Effort subscriber.");
  return true;
}


void destroy_entities() {
  rcl_publisher_fini(&publisher, &node);
  rcl_subscription_fini(&subscriber, &node);
  rcl_timer_fini(&timer);
  rclc_executor_fini(&executor);
  rcl_node_fini(&node);
  rclc_support_fini(&support);
}

// ======================================================
// ---------------- Setup & Loop ------------------------
// ======================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  if (checkSerialConfig(5000)) {
    enterConfigurationMode(prefs);
  }

  for (int i = 0; i < NUM_RELAYS; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
  }

  for (int i = 0; i < NUM_INPUTS; i++) {
    pinMode(inputPins[i], INPUT); // GPIO 32–39 have no pullups
    inputState[i] = digitalRead(inputPins[i]);
    lastInputState[i] = inputState[i];
  }

  prefs.begin("wifi", true);
  prefs.getString("ssid", "").toCharArray(wifi_ssid, WIFI_SSID_MAX);
  prefs.getString("password", "").toCharArray(wifi_pass, WIFI_PASS_MAX);
  prefs.getString("agentIp", "").toCharArray(agent_ip, AGENT_IP_MAX);
  module_id   = prefs.getUChar("moduleId", 1);
  module_type = prefs.getUChar("moduleType", 3);
  agent_port  = prefs.getUInt("agentPort", 8888);
  prefs.end();

  // <-- NEW LINE: print current config for verification
  printCurrentConfig();

  set_microros_wifi_transports(
    wifi_ssid,
    wifi_pass,
    agent_ip,
    agent_port
  );

  agent_state = WAITING_AGENT;
}

void loop() {
    static uint32_t last = 0;
    if (millis() - last > 1000) {
      last = millis();
      Serial.println("Executor alive");
    }

  pollInputs();

  switch (agent_state) {

    case WAITING_AGENT:
      EXECUTE_EVERY_MS(500, {
        if (rmw_uros_ping_agent(1000, 3) == RMW_RET_OK)
          agent_state = AGENT_AVAILABLE;
      });
      break;

    case AGENT_AVAILABLE:
      if (create_entities())
        agent_state = AGENT_CONNECTED;
      else {
        destroy_entities();
        delay(1000);
        agent_state = WAITING_AGENT;
      }
      break;

    case AGENT_CONNECTED:
      rclc_executor_spin_some(&executor, RCL_MS_TO_NS(100));
      // heartbeat print
      static uint32_t last = 0;
      if (millis() - last > 2000) {
          last = millis();
         Serial.println("Spin executed, waiting for messages...");
      }
      EXECUTE_EVERY_MS(1000, {
        if (rmw_uros_ping_agent(1000, 3) != RMW_RET_OK)
          agent_state = AGENT_DISCONNECTED;
      });

      if (inputChanged) {
        publishStatus();
        inputChanged = false;
      }
      break;

    case AGENT_DISCONNECTED:
      destroy_entities();
      delay(500);
      agent_state = WAITING_AGENT;
      break;
  }
}
