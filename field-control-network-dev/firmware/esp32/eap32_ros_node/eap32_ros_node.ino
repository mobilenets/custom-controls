/****************************************************
 *  ESP32 micro-ROS Relay + Input Module
 *
 *  Publishes:
 *    /modulereturn  (std_msgs/String)
 *      "<moduleId>;R:r1,r2,r3,r4;I:i1,i2,i3,i4"
 *
 *  Subscribes:
 *    /actionrequest (std_msgs/String)
 *      "<moduleId>;<relayIndex>;<ON|OFF>"
 *
 ****************************************************/

#include <micro_ros_arduino.h>
#include <Preferences.h>
#include <stdio.h>

#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>
#include <std_msgs/msg/string.h>

/* ==================================================
 *                  Pin Definitions
 * ================================================== */

// Relay outputs
#define RELAY1_PIN 21
#define RELAY2_PIN 19
#define RELAY3_PIN 18
#define RELAY4_PIN 5
#define NUM_RELAYS 4

// Digital inputs (ESP32 input-only pins)
#define INPUT1_PIN 32
#define INPUT2_PIN 33
#define INPUT3_PIN 34
#define INPUT4_PIN 35
#define NUM_INPUTS 4

#define WIFI_SSID_MAX   32
#define WIFI_PASS_MAX   64
#define AGENT_IP_MAX    16

static char wifi_ssid[WIFI_SSID_MAX];
static char wifi_pass[WIFI_PASS_MAX];
static char agent_ip[AGENT_IP_MAX];

/* ==================================================
 *                  Disconnection Timer
 * ================================================== */

unsigned long disconnectedAt = 0;  // global timestamp
/* ==================================================
 *                  Helper Macros
 * ================================================== */

#define RCCHECK(fn) { \
  rcl_ret_t rc = fn; \
  if (rc != RCL_RET_OK) return false; \
}

#define EXECUTE_EVERY_N_MS(MS, X) do {          \
  static int64_t last = 0;                      \
  if (uxr_millis() - last > MS) {               \
    X;                                          \
    last = uxr_millis();                        \
  }                                             \
} while (0)

/* ==================================================
 *               micro-ROS Objects
 * ================================================== */

rcl_allocator_t allocator;
rclc_support_t support;
rcl_node_t node;
rcl_timer_t timer;
rclc_executor_t executor;
rcl_publisher_t publisher;
rcl_subscription_t subscriber;

// Messages
std_msgs__msg__String pub_msg;
std_msgs__msg__String sub_msg;

// Fixed buffers (NO heap usage)
#define SUB_MSG_BUFFER_SIZE 128
static char sub_msg_buffer[SUB_MSG_BUFFER_SIZE];

#define PUB_MSG_BUFFER_SIZE 96
static char pub_msg_buffer[PUB_MSG_BUFFER_SIZE];

/* ==================================================
 *                  Module State
 * ================================================== */

enum agent_state_t {
  WAITING_AGENT,
  AGENT_AVAILABLE,
  AGENT_CONNECTED,
  AGENT_DISCONNECTED
};

agent_state_t state;

Preferences prefs;

// Configuration
String ssid;
String password;
String agentIp;
String agentPort;
String moduleId;

// I/O states
bool relayStates[NUM_RELAYS] = { false, false, false, false };
bool inputStates[NUM_INPUTS] = { false, false, false, false };
bool lastInputStates[NUM_INPUTS] = { false, false, false, false };

/* ==================================================
 *                Relay Control
 * ================================================== */

void setRelay(uint8_t index, bool on) {
  if (index < 1 || index > NUM_RELAYS) return;

  uint8_t pin;

  switch (index) {
    case 1: pin = RELAY1_PIN; break;
    case 2: pin = RELAY2_PIN; break;
    case 3: pin = RELAY3_PIN; break;
    case 4: pin = RELAY4_PIN; break;
  }

  digitalWrite(pin, on ? HIGH : LOW);
  relayStates[index - 1] = on;

  Serial.printf("Relay %d -> %s\n", index, on ? "ON" : "OFF");
}

/* ==================================================
 *              Digital Input Monitoring
 * ================================================== */

void scanInputs() {
  bool current[NUM_INPUTS];

  current[0] = digitalRead(INPUT1_PIN);
  current[1] = digitalRead(INPUT2_PIN);
  current[2] = digitalRead(INPUT3_PIN);
  current[3] = digitalRead(INPUT4_PIN);

  for (int i = 0; i < NUM_INPUTS; i++) {
    if (current[i] != lastInputStates[i]) {
      inputStates[i] = current[i];
      lastInputStates[i] = current[i];
    }
  }
}

/* ==================================================
 *              Configuration Handling
 * ================================================== */

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

  Serial.print("Module ID: ");
  moduleId = readLine();

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.putString("agentIp", agentIp);
  prefs.putString("agentPort", agentPort);
  prefs.putString("moduleId", moduleId);
  prefs.end();

  Serial.println("Configuration saved.\n");
}

void loadConfiguration() {
  prefs.begin("wifi", true);
  ssid = prefs.getString("ssid", "");
  password = prefs.getString("password", "");
  agentIp = prefs.getString("agentIp", "");
  agentPort = prefs.getString("agentPort", "");
  moduleId = prefs.getString("moduleId", "");
  prefs.end();

  Serial.println("\nLoaded configuration:");
  Serial.println("SSID      : " + ssid);
  Serial.println("Agent IP  : " + agentIp);
  Serial.println("Agent Port: " + agentPort);
  Serial.println("Module ID : " + moduleId);
}

/* ==================================================
 *             micro-ROS Callbacks
 * ================================================== */

void timer_callback(rcl_timer_t *, int64_t) {
  Serial.println("moduleId : " + moduleId);
  if (moduleId.length() == 0) {
    Serial.println("Skipping publish: moduleId not set yet");
    
    return;
  }
  snprintf(
    pub_msg_buffer,
    PUB_MSG_BUFFER_SIZE,
    "%s;R:%d,%d,%d,%d;I:%d,%d,%d,%d",
    moduleId.c_str(),
    relayStates[0], relayStates[1],
    relayStates[2], relayStates[3],
    inputStates[0], inputStates[1],
    inputStates[2], inputStates[3]
  );

  pub_msg.data.data = pub_msg_buffer;
  pub_msg.data.size = strlen(pub_msg_buffer);
  pub_msg.data.capacity = PUB_MSG_BUFFER_SIZE;

  Serial.print("Publishing: ");
  Serial.println(pub_msg_buffer);

  if (pub_msg.data.size >= pub_msg.data.capacity) {
    Serial.println("Error: payload exceeds buffer capacity!");
    return;
  }

  rcl_publish(&publisher, &pub_msg, NULL);

}

void sub_callback(const void * msgin) {
  const std_msgs__msg__String * in =
    (const std_msgs__msg__String *)msgin;

  String payload = String(in->data.data).substring(0, in->data.size);

  int s1 = payload.indexOf(';');
  int s2 = payload.indexOf(';', s1 + 1);
  if (s1 < 0 || s2 < 0) return;

  String target = payload.substring(0, s1);
  if (target != moduleId) return;

  uint8_t relay = payload.substring(s1 + 1, s2).toInt();
  String cmd = payload.substring(s2 + 1);

  setRelay(relay, cmd == "ON");
}

/* ==================================================
 *             micro-ROS Lifecycle
 * ================================================== */

bool create_entities() {
  allocator = rcl_get_default_allocator();

  sub_msg.data.data = sub_msg_buffer;
  sub_msg.data.capacity = SUB_MSG_BUFFER_SIZE;
  sub_msg.data.size = 0;

  RCCHECK(rclc_support_init(&support, 0, NULL, &allocator));
  RCCHECK(rclc_node_init_default(&node, "esp32_module_node", "", &support));

  RCCHECK(rclc_publisher_init_best_effort(
    &publisher, &node,
    ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String),
    "modulereturn"
  ));

  RCCHECK(rclc_subscription_init_best_effort(
    &subscriber, &node,
    ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, String),
    "actionrequest"
  ));

  RCCHECK(rclc_timer_init_default(
    &timer, &support, RCL_MS_TO_NS(1000), timer_callback
  ));

  RCCHECK(rclc_executor_init(&executor, &support.context, 2, &allocator));
  RCCHECK(rclc_executor_add_timer(&executor, &timer));
  RCCHECK(rclc_executor_add_subscription(
    &executor, &subscriber, &sub_msg, sub_callback, ON_NEW_DATA
  ));

  Serial.println("micro-ROS entities created.");
  return true;
}

void destroy_entities() {
  rcl_publisher_fini(&publisher, &node);
  rcl_subscription_fini(&subscriber, &node);
  rcl_timer_fini(&timer);
  rclc_executor_fini(&executor);
  rcl_node_fini(&node);
  rclc_support_fini(&support);

  Serial.println("micro-ROS entities destroyed.");
}

/* ==================================================
 *                     Setup
 * ================================================== */

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(RELAY3_PIN, OUTPUT);
  pinMode(RELAY4_PIN, OUTPUT);

  pinMode(INPUT1_PIN, INPUT_PULLUP);
  pinMode(INPUT2_PIN, INPUT_PULLUP);
  pinMode(INPUT3_PIN, INPUT_PULLUP);
  pinMode(INPUT4_PIN, INPUT_PULLUP);

 
    Serial.println("Press 'c' in 5 seconds to enter config mode...");
    unsigned long startTime = millis();
    while (millis() - startTime < 5000) {
      if (Serial.available()) {
        char c = Serial.read();
        if (c == 'c') {
          enterConfigurationMode();
          break;
       }
     }
    }

  loadConfiguration();

  ssid.toCharArray(wifi_ssid, WIFI_SSID_MAX);
  password.toCharArray(wifi_pass, WIFI_PASS_MAX);
  agentIp.toCharArray(agent_ip, AGENT_IP_MAX);
  


  set_microros_wifi_transports(
    wifi_ssid,
    wifi_pass,
    agent_ip,
    agentPort.toInt()
  );

  // Start disconnect watchdog immediately (first boot counts)
  disconnectedAt = millis();
  Serial.println("Disconnect watchdog started (2 min timeout)");

  state = WAITING_AGENT;
}



/* ==================================================
 *                      Loop
 * ================================================== */

void loop() {
  switch (state) {

    case WAITING_AGENT:
      if (rmw_uros_ping_agent(1000, 1) == RMW_RET_OK) {
       Serial.println("Agent detected.");
       state = AGENT_AVAILABLE;
     }

    if (millis() - disconnectedAt > 120000) {
      Serial.println("Agent not available > 2 minutes. Restarting ESP32.");
      Serial.flush();
      ESP.restart();
    }
    delay(500);
    break;

    case AGENT_AVAILABLE:
      if (create_entities()) {
        Serial.println("micro-ROS entities created. Connected.");

        // Reset disconnect watchdog
        disconnectedAt = 0;

        state = AGENT_CONNECTED;
      } else {
        state = WAITING_AGENT;
      }
      break;

    case AGENT_CONNECTED:
      EXECUTE_EVERY_N_MS(50, {
        scanInputs();
      });

      EXECUTE_EVERY_N_MS(1000, {
        if (rmw_uros_ping_agent(1000, 3) != RMW_RET_OK) {
          Serial.println("Agent lost!");
          disconnectedAt = millis();
          state = AGENT_DISCONNECTED;
        }
      });

      rclc_executor_spin_some(&executor, RCL_MS_TO_NS(50));
      break;

    case AGENT_DISCONNECTED:
      destroy_entities();

      EXECUTE_EVERY_N_MS(10000, {
        Serial.println("Agent disconnected...");
      });

      if (millis() - disconnectedAt > 120000) {
        Serial.println("Disconnected > 2 minutes. Restarting ESP32.");
        Serial.flush();
        ESP.restart();
      }

      state = WAITING_AGENT;
      break;
  }
}

