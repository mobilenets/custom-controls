# Field Control Network Architecture

## Purpose

The Field Control Network (FCN) is a distributed control system intended to
connect multiple embedded I/O nodes to a common ROS 2 control environment.

FCN nodes may use different ESP32 processors, physical I/O hardware, network
interfaces, and expansion devices. These hardware differences are hidden
behind common FCN interfaces so the ROS 2 controller does not need to know
how a particular output or input is physically implemented.

The basic design rule is:

> FCN exposes capabilities. The board/provider layer determines how those
> capabilities are physically implemented.

---

## System Structure

An FCN installation consists of:

- One or more embedded FCN modules
- ROS 2 / micro-ROS communication
- A micro-ROS agent
- A ROS 2 controller
- Optional user interfaces and higher-level logic

Conceptually:

    ROS 2 Controller
           |
      ROS 2 Topics
           |
    micro-ROS Agent
           |
        Network
           |
    +------+------+------+
    |             |      |
 FCN Node      FCN Node  FCN Node
    |             |      |
 Board I/O      Board I/O / Expansion

Every FCN module has a module ID and reports its available I/O through the
same ROS protocol regardless of the underlying hardware.

---

## Firmware Layers

Native ESP-IDF FCN firmware is divided into three primary layers.

### 1. Common FCN Components

Reusable behavior is stored under:

    firmware/esp-idf/common_components/

Current shared components include:

    fcn_config
    fcn_io
    fcn_status
    fcn_network
    fcn_microros

These components should contain as little board-specific knowledge as
possible.

### 2. Board Providers

Board providers implement the common FCN interfaces for particular hardware.

Examples include:

- Waveshare ESP32-S3-ETH-8DI-8RO
- GLEDOPTO GL-C-618WL

A board provider may implement an FCN output using:

- Direct GPIO
- I2C GPIO expander
- SPI device
- RS485 / Modbus
- Another microcontroller
- Other future hardware

The common FCN layer should not need to know which transport is being used.

### 3. Application Startup

The ESP-IDF project initializes configuration, board providers, networking,
and the common FCN services.

The application should primarily assemble capabilities rather than contain
hardware-specific control logic.

---

## I/O Provider Model

FCN treats I/O as logical capabilities.

For example, the public output interface currently uses the historical
`fcn_relay_*` API. A logical FCN output does not necessarily represent a
physical electromechanical relay.

An output may instead be:

- An amplified GPIO output
- An onboard relay
- A remote Modbus relay
- An expansion output
- Another future output provider

Physical protocol details belong below the common FCN I/O interface.

Expansion capability therefore describes what I/O is available, not how the
expansion device is connected.

---

## Output Numbering

FCN presents outputs using one logical numbering space.

For example, a Waveshare module configured with eight onboard outputs and an
eight-channel expansion presents:

    FCN outputs 1-8   -> onboard relay provider
    FCN outputs 9-16  -> expansion provider outputs 1-8

The ROS controller addresses the logical FCN output number and does not need
to know which physical provider handles it.

---

## Networking

FCN networking is exposed through the common `fcn_network` interface.

Individual board providers implement the actual network hardware.

The Waveshare implementation currently supports:

    W5500 Ethernet -> preferred
    Wi-Fi          -> fallback

The GLEDOPTO implementation currently uses:

    Wi-Fi          -> primary

Ethernet support for the GLEDOPTO controller has not yet been characterized.

Network policy belongs in the board/network provider rather than in
micro-ROS behavior.

---

## micro-ROS

The common `fcn_microros` component provides the FCN ROS behavior.

It handles:

- Agent discovery
- Connection
- ROS entity creation
- `/actionrequest`
- `/modulereturn`
- Agent health checking
- Entity destruction
- Automatic reconnection

The same `fcn_microros` source has been demonstrated on both classic ESP32
and ESP32-S3 FCN nodes.

### Important Target Boundary

The FCN `fcn_microros` source code is portable between ESP32 targets.

The generated `libmicroros.a` binary is NOT target-independent.

For example:

    ESP32    -> libmicroros.a built for IDF_TARGET=esp32
    ESP32-S3 -> libmicroros.a built for IDF_TARGET=esp32s3

A library built for ESP32-S3 must not be linked into a classic ESP32
application.

This was physically demonstrated during the GL-C-618WL port: linking an
ESP32-S3 micro-ROS archive into an ESP32 application successfully compiled
and linked but caused an IllegalInstruction exception at runtime in
`uxr_nanos()`.

Target-specific micro-ROS builds must therefore remain explicitly separated.

---

## ROS Protocol

### /actionrequest

Message type:

    std_msgs/String

Format:

    <moduleId>;<outputIndex>;<ON|OFF>

Example:

    4;1;ON

The receiving module ignores commands addressed to other module IDs.

### /modulereturn

Message type:

    std_msgs/String

Format:

    <moduleId>;R:r1,r2,...;I:i1,i2,...

Example:

    4;R:1,0,0,0;I:0

The message is also used as the regular module heartbeat.

Current heartbeat period:

    1 second

FCN ROS topics currently use BEST_EFFORT QoS.

---

## Configuration

Each FCN module stores configuration in NVS.

Current configuration includes:

- Wi-Fi SSID
- Wi-Fi password
- micro-ROS agent IP
- micro-ROS agent port
- Module ID
- Module name
- Input count
- Output count
- Ethernet enable
- Wi-Fi fallback enable

During boot, a serial configuration window allows the operator to enter
configuration mode by pressing `c`.

Configuration behavior is provided by the common `fcn_config` component.

---

## Safety Philosophy

Output startup behavior is determined by the physical characteristics and
intended use of the output provider.

Ordinary non-latching outputs should normally initialize to a known safe
state.

For example, the GLEDOPTO amplified outputs initialize OFF before normal FCN
operation begins.

Bi-stable/latching hardware requires different treatment because the
physical output state can survive processor or power cycles.

The current Waveshare RS485 bi-stable relay expansion is therefore read and
preserved at startup rather than automatically forced OFF.

Bi-stable retained outputs are intended for applications such as lighting
and suitable control-power circuits. They should not be assumed appropriate
for machinery motion, motors, actuators, or valves where an unexpected
retained ON state could create a hazard.

---

## Current Proven Hardware

FCN native ESP-IDF firmware has been physically demonstrated with:

### Waveshare ESP32-S3-ETH-8DI-8RO

- ESP32-S3
- 8 onboard relay outputs
- 8 digital inputs
- W5500 Ethernet
- Wi-Fi fallback
- Optional 8-channel RS485/Modbus relay expansion
- Configurations of 8 and 16 logical outputs demonstrated
- micro-ROS connection and reconnection demonstrated

### GLEDOPTO GL-C-618WL

- Classic ESP32
- 4 amplified outputs
- 1 digital input
- Wi-Fi
- micro-ROS connection and reconnection
- ROS output ON/OFF control
- Dynamic `/modulereturn`
- Active-high digital input reporting

---

## Development Principle

New hardware support should normally be added by implementing board
providers rather than modifying common FCN behavior.

A new board should therefore answer questions such as:

- How are outputs implemented?
- How are inputs implemented?
- What networking hardware is available?
- Is there a physical status indicator?
- What capabilities exist?

The ROS controller should not need to answer those questions.