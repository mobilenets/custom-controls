# FCN ESP-IDF Build, Flash, and Basic Test

## Purpose

This document provides the basic development procedure for building,
flashing, monitoring, and performing a quick functional test of native
ESP-IDF Field Control Network firmware.

It is intended as a practical development reference rather than a complete
ESP-IDF installation guide.

---

# Development Environment

Current known-good development environment:

    Ubuntu 22.04
    ESP-IDF 6.0

Activate ESP-IDF before building:

    source ~/esp-idf-6.0/export.sh

Verify:

    idf.py --version

Expected major version:

    ESP-IDF v6.0

---

# Repository Location

Current FCN repository:

    ~/custom-controls/field-control-network-dev

Native ESP-IDF projects:

    firmware/esp-idf/

Current major projects include:

    esp32_cam
    waveshare_s3_module
    wroom_module

Shared FCN components:

    firmware/esp-idf/common_components/

---

# GLEDOPTO GL-C-618WL

Project:

    firmware/esp-idf/wroom_module

ESP-IDF target:

    esp32

Typical programming port:

    /dev/ttyUSB0

Build:

    cd ~/custom-controls/field-control-network-dev/firmware/esp-idf/wroom_module

    source ~/esp-idf-6.0/export.sh

    idf.py build

Flash and monitor:

    idf.py -p /dev/ttyUSB0 flash monitor

Exit the ESP-IDF monitor with:

    Ctrl+]

---

# Waveshare ESP32-S3-ETH-8DI-8RO

Project:

    firmware/esp-idf/waveshare_s3_module

ESP-IDF target:

    esp32s3

Typical programming port:

    /dev/ttyACM0

Build:

    cd ~/custom-controls/field-control-network-dev/firmware/esp-idf/waveshare_s3_module

    source ~/esp-idf-6.0/export.sh

    idf.py build

Flash and monitor:

    idf.py -p /dev/ttyACM0 flash monitor

Exit monitor:

    Ctrl+]

---

# Verify Target Before Flashing

The two primary FCN boards currently use different ESP32 processor targets.

    GLEDOPTO:
        esp32

    Waveshare:
        esp32s3

When changing or creating a project target, ESP-IDF provides:

    idf.py set-target esp32

or:

    idf.py set-target esp32s3

Do not routinely run `set-target` on an already configured known-good
project merely as part of the build procedure.

Changing the target can regenerate configuration and build state.

Use it when establishing or deliberately changing a project target.

---

# micro-ROS Target Check

Before troubleshooting unexplained micro-ROS crashes, verify that the
micro-ROS component was built for the same target as the application.

Required mapping:

    GLEDOPTO:
        application = esp32
        micro-ROS   = esp32

    Waveshare:
        application = esp32s3
        micro-ROS   = esp32s3

A target mismatch may still compile and link successfully.

It can then fail at runtime with an IllegalInstruction exception.

See:

    docs/development/microros_target_builds.md

for the full explanation.

---

# Serial Configuration

During normal FCN boot there is an approximately five-second configuration
window.

To enter configuration mode:

    press c
    press ENTER

The configuration console can be used to set items such as:

- Wi-Fi credentials
- micro-ROS agent address
- micro-ROS agent port
- Module ID
- Module name
- Input count
- Output count
- Network options

Save using the configuration console's:

    S

option.

Cancel using:

    C

See:

    docs/protocols/configuration.md

for configuration details.

---

# Expected Network Startup

## GLEDOPTO

Current implementation uses Wi-Fi.

A successful startup should eventually show:

    Wi-Fi connected
    DHCP address obtained

Development example:

    IP address: 10.0.0.117

The actual address depends on the network's DHCP server.

---

## Waveshare

Current preferred network order:

    W5500 Ethernet
          |
          v
    Wi-Fi fallback

When Ethernet is available, it should normally be used.

If Ethernet fails to obtain an IP and Wi-Fi fallback is enabled, the module
should start Wi-Fi after the fallback interval.

---

# Expected micro-ROS Startup

Once the FCN node has a usable network address, the common micro-ROS layer
should attempt to find the configured agent.

A normal sequence includes messages equivalent to:

    Agent detected

    Creating ROS session...

    ROS node created: fcn_module_<moduleId>

    Subscribed to /actionrequest

    Publishing /modulereturn

    State -> AGENT_CONNECTED

The exact ESP-IDF logging format may change, but the state progression should
remain equivalent.

---

# Basic ROS Acceptance Test

The following provides a quick functional test after flashing a module.

The ROS 2 host and micro-ROS agent must already be running.

---

## 1. Monitor Module Heartbeats

Open a ROS 2 terminal and run:

    ros2 topic echo \
      --qos-reliability best_effort \
      /modulereturn

A GLEDOPTO module configured as module 4 may produce:

    data: 4;R:0,0,0,0;I:0

A base Waveshare module may produce:

    data: 6;R:0,0,0,0,0,0,0,0;I:0,0,0,0,0,0,0,0

Verify that:

- The expected module ID appears.
- The expected number of outputs appears.
- The expected number of inputs appears.
- Messages continue at approximately one-second intervals.

---

## 2. Turn One Output ON

Example for module 4, output 1:

    ros2 topic pub --once \
      --qos-reliability best_effort \
      /actionrequest \
      std_msgs/msg/String \
      "{data: '4;1;ON'}"

Expected `/modulereturn`:

    4;R:1,0,0,0;I:0

Also verify the physical output.

---

## 3. Turn the Output OFF

    ros2 topic pub --once \
      --qos-reliability best_effort \
      /actionrequest \
      std_msgs/msg/String \
      "{data: '4;1;OFF'}"

Expected return:

    4;R:0,0,0,0;I:0

Verify the physical output is OFF.

---

## 4. Verify Inputs

Activate an appropriate physical input.

Verify that the corresponding logical state changes in `/modulereturn`.

For the currently characterized GLEDOPTO input:

    inactive:
        I:0

    active:
        I:1

Do not apply an external voltage to an input unless its electrical input
rating is known.

The GLEDOPTO IO13 safe external voltage range has not yet been characterized.

---

# Agent Reconnection Test

The common FCN micro-ROS implementation is designed to recover from loss of
the micro-ROS agent.

With the module connected:

1. Verify `/modulereturn` is being published.
2. Stop the micro-ROS agent.
3. Observe the FCN serial monitor.
4. Verify agent loss is detected.
5. Verify ROS entities are destroyed.
6. Restart the micro-ROS agent.
7. Verify the FCN module discovers it again.
8. Verify ROS entities are recreated.
9. Verify `/modulereturn` resumes.

The ESP32 should not require a reboot to recover from normal agent loss.

This behavior has been demonstrated on both the Waveshare and GLEDOPTO FCN
implementations.

---

# Network Reconnection Test

Where practical:

1. Start the FCN module normally.
2. Verify DHCP and ROS operation.
3. Interrupt the active network connection.
4. Observe network state.
5. Restore the connection.
6. Verify IP connectivity returns.
7. Verify micro-ROS communication returns.

For the Waveshare board, Ethernet/Wi-Fi failover should also be tested when
changes are made to the network provider.

---

# Basic Post-Build Checklist

After meaningful firmware changes, verify at minimum:

    [ ] Project builds without errors

    [ ] Correct ESP-IDF target is being used

    [ ] Correct target-specific micro-ROS component is being used

    [ ] Board boots without reset loop

    [ ] Outputs begin in their defined startup state

    [ ] Network obtains an IP address

    [ ] micro-ROS agent is detected

    [ ] ROS entities are created

    [ ] /modulereturn appears

    [ ] Reported module ID is correct

    [ ] Reported I/O counts are correct

    [ ] /actionrequest ON works

    [ ] /actionrequest OFF works

    [ ] Physical output agrees with reported output state

    [ ] Physical inputs report correctly

    [ ] Agent loss is detected

    [ ] Agent reconnect works

---

# Git Check Before Commit

From the repository:

    cd ~/custom-controls/field-control-network-dev

or from the outer repository as appropriate, inspect:

    git status
    git diff

Do not commit build artifacts or unrelated changes simply because they are
present in the working tree.

Review the exact files being staged before committing.

Example:

    git diff --cached

Only create a checkpoint after the intended build and physical tests have
passed.

---

# Known Development Ports

Current commonly observed programming ports:

    GLEDOPTO GL-C-618WL
        /dev/ttyUSB0

    Waveshare ESP32-S3-ETH-8DI-8RO
        /dev/ttyACM0

These device names are not guaranteed.

If a device does not appear where expected, inspect the available serial
devices rather than assuming the hardware has failed.

---

# Useful Principle

For FCN development, a successful build is only the first test.

A meaningful known-good checkpoint should establish the complete path:

    source
      |
      v
    build
      |
      v
    flash
      |
      v
    boot
      |
      v
    physical I/O
      |
      v
    network
      |
      v
    micro-ROS
      |
      v
    ROS command/state
      |
      v
    reconnect behavior

This is especially important because target-specific or hardware-specific
problems may not appear until well after compilation and linking succeed.