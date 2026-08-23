#include <Arduino.h>
#include <micro_ros_arduino.h>
#include <std_msgs/msg/string.h>

#define LED_PIN 2  // Built-in LED on most ESP32 boards

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);

  // Optional: initialize micro-ROS here if you have ROS2 setup
  // rmw_microros_init();
}

void loop() {
  // Blink LED
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(500);

  // Optional: publish heartbeat to ROS2 topic
  // std_msgs__msg__String msg;
  // msg.data = "FCU-µ alive";
  // publisher.publish(&msg);
}

