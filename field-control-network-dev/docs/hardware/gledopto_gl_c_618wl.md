# GLEDOPTO GL-C-618WL FCN Hardware Profile

## Identification

Manufacturer: GLEDOPTO

Product family: Elite / Advanced WLED Controller

Model: GL-C-618WL

FCN board ID:

    gledopto_gl_c_618wl

ESP-IDF target:

    esp32

This controller is being adapted as a compact Field Control Network node.

---

## Processor

Detected using ESP-IDF/esptool:

    Chip: ESP32-D0WD-V3
    Silicon revision: 3.1
    CPU cores: 2
    Maximum device capability: 240 MHz
    Current FCN runtime frequency: 160 MHz
    Crystal: 40 MHz

The device is a classic ESP32, not an ESP32-S3.

---

## Flash

Physical flash detected:

    4 MB

Current flash configuration:

    Mode: DIO
    Frequency: 40 MHz
    Size: 4 MB

The FCN project currently uses the standard factory application partition
layout.

OTA partitioning has not yet been configured for this board.

---

## FCN I/O

### Amplified Outputs

Four amplified output terminals have been physically mapped.

| FCN Output | ESP32 GPIO | Active State | Verified |
|------------|------------|--------------|----------|
| 1 | GPIO16 | HIGH | Yes |
| 2 | GPIO12 | HIGH | Yes |
| 3 | GPIO4  | HIGH | Yes |
| 4 | GPIO2  | HIGH | Yes |

The board provider presents these through the common `fcn_relay_*` interface.

Although the common API currently uses the term "relay," these are amplified
electronic outputs rather than conventional electromechanical relays.

### Startup Behavior

All four outputs are initialized LOW/OFF before normal FCN operation begins.

Verified boot behavior:

    reset / power-up
          |
          v
    configure output GPIO
          |
          v
    all outputs OFF
          |
          v
    begin normal FCN operation

This SAFE-OFF behavior has been physically verified.

---

## ESP32 Strapping Pins

GPIO2 and GPIO12 are ESP32 strapping pins.

They have operated successfully as output pins after startup on the
GL-C-618WL.

Care must still be taken when attaching external circuitry because loading a
strapping pin during reset can affect ESP32 boot configuration.

---

## Digital Input

The fifth exposed I/O connection has been mapped to:

    GPIO13

FCN configuration:

    Digital input count: 1
    Logical input: 1
    Polarity: active HIGH

Physical test:

    inactive -> /modulereturn I:0
    active   -> /modulereturn I:1

The active-high behavior has been physically verified.

### Electrical Rating

The safe external voltage range of this input has NOT yet been
characterized.

During initial testing the controller was powered through USB and the input
was activated using the board's available V+ connection.

That test establishes logical polarity but does NOT establish that GPIO13 or
its board-level interface can safely tolerate the controller's full external
supply range.

Until the input circuitry is characterized:

> Do not assume IO13 is 12 V or 24 V tolerant.

---

## Networking

### Wi-Fi

Wi-Fi station operation is physically verified.

Example test configuration:

    SSID: configured through FCN NVS
    DHCP: successful

Example assigned address during development:

    IP:      10.0.0.117
    Gateway: 10.0.0.1
    Netmask: 255.255.255.0

The GLEDOPTO network provider currently treats Wi-Fi as the primary network
interface.

### Ethernet

The hardware is believed to provide Ethernet capability, but the Ethernet
controller, interface, GPIO assignments, and FCN driver have not yet been
characterized.

Ethernet must therefore be considered unsupported by the current FCN board
provider.

---

## FCN Configuration

The board uses the common `fcn_config` component and NVS configuration.

A tested configuration includes:

    input_count  = 1
    output_count = 4

The normal FCN serial boot configuration window is available.

During startup:

    Press 'c' then ENTER within 5 seconds

to enter configuration mode.

---

## micro-ROS

The GL-C-618WL successfully runs the common FCN `fcn_microros` component.

Physically verified:

- Wi-Fi connection
- DHCP
- micro-ROS agent detection
- ROS node creation
- `/actionrequest` subscription
- `/modulereturn` publication
- Agent-loss detection
- ROS entity destruction
- Automatic agent rediscovery
- Automatic reconnection

Example node name:

    fcn_module_4

---

## micro-ROS Target Requirement

The GL-C-618WL requires a micro-ROS library built for:

    IDF_TARGET=esp32

A `libmicroros.a` previously built for the ESP32-S3 was accidentally linked
successfully into this project but caused a runtime IllegalInstruction
exception in:

    uxr_nanos()

The failure occurred when micro-ROS initialization began after the network
obtained an IP address.

Changing to the existing micro-ROS component/library built for classic ESP32
resolved the problem.

Therefore:

> Never reuse an ESP32-S3 `libmicroros.a` with the GL-C-618WL.

---

## ROS I/O Verification

The controller was tested as:

    FCN module ID: 4
    Outputs:       4
    Inputs:        1

Normal heartbeat:

    4;R:0,0,0,0;I:0

ROS command:

    4;1;ON

Result:

    4;R:1,0,0,0;I:0

ROS command:

    4;1;OFF

Result:

    4;R:0,0,0,0;I:0

The physical output changed state along with the ROS-reported state.

Input activation was also physically verified:

    inactive -> I:0
    active   -> I:1

This demonstrates the complete FCN path:

    ROS /actionrequest
            |
            v
      fcn_microros
            |
            v
       fcn_relay
            |
            v
    GLEDOPTO provider
            |
            v
       physical I/O
            |
            v
     /modulereturn

---

## Board Provider Files

Current board-specific implementation:

    firmware/esp-idf/wroom_module/
        boards/
            gledopto_gl_c_618wl/
                CMakeLists.txt
                board_profile.h
                output_driver.c
                input_driver.c
                network_driver.c
                status_driver.c

The status provider currently provides the common FCN status interface
without assuming an unidentified physical status LED.

---

## Known Unknowns

The following hardware still needs characterization:

- IO13 safe input-voltage range
- IO13 board-level protection/interface circuitry
- Ethernet controller
- Ethernet pin assignments
- Ethernet driver requirements
- Built-in microphone
- Possible physical status LED
- Other undocumented internal GPIO usage

These should be characterized individually rather than inferred from similar
ESP32 controller products.

---

## Proven Milestone

FCN-IDF-013F marks the known-good hardware validation point for this board.

At that milestone the following were physically demonstrated:

    Native ESP-IDF boot             PASS
    Four amplified outputs          PASS
    SAFE-OFF startup                PASS
    GPIO13 digital input            PASS
    Wi-Fi                           PASS
    DHCP                            PASS
    micro-ROS connection            PASS
    Agent reconnect                 PASS
    /actionrequest ON               PASS
    /actionrequest OFF              PASS
    /modulereturn                   PASS
    Dynamic I/O counts              PASS