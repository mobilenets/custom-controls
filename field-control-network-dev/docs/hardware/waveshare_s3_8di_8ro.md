# Waveshare ESP32-S3-ETH-8DI-8RO FCN Hardware Profile

## Identification

Manufacturer: Waveshare

Board:

    ESP32-S3-ETH-8DI-8RO

FCN board ID:

    waveshare_s3_8di_8ro

ESP-IDF target:

    esp32s3

This board is currently one of the primary native ESP-IDF hardware platforms
for the Field Control Network.

It provides onboard relay outputs, digital inputs, Ethernet, Wi-Fi, and an
RS485 interface suitable for optional expansion devices.

---

## Processor

Processor family:

    ESP32-S3

The board is built around the ESP32-S3 and therefore uses the ESP-IDF target:

    esp32s3

This distinction is important because target-specific binary components,
particularly `libmicroros.a`, cannot be shared with classic ESP32 builds.

---

## Flash

Physical flash:

    16 MB

The original ESP-IDF project configuration incorrectly treated the board as
having 2 MB of flash.

The FCN project was corrected to use the physical 16 MB device.

---

## Partition Layout

The Waveshare FCN firmware currently uses a custom OTA-capable partition
table.

Current layout:

    # Name,   Type, SubType, Offset,   Size
    nvs,      data, nvs,     0x9000,   0x6000
    otadata,  data, ota,     0xF000,   0x2000
    phy_init, data, phy,     0x11000,  0x1000
    ota_0,    app,  ota_0,   0x20000,  0x400000
    ota_1,    app,  ota_1,   0x420000, 0x400000
    storage,  data, spiffs,  0x820000, 0x760000

This provides two approximately 4 MB application partitions plus persistent
storage.

---

## Onboard Relay Outputs

The board contains eight onboard relay outputs.

FCN capability:

    Onboard output count: 8

The relays are controlled through an I2C I/O expander.

### I2C Interface

    SDA: GPIO42
    SCL: GPIO41

Relay expander address:

    0x20

Relevant expander registers:

    Output register:   0x01
    Polarity register: 0x02
    Config register:   0x03

The FCN board provider hides these I2C details behind the common
`fcn_relay_*` interface.

---

## Relay Startup Behavior

The onboard relay provider initializes the physical outputs to a known
SAFE-OFF state.

Conceptually:

    processor boot
          |
          v
    initialize I2C
          |
          v
    configure relay expander
          |
          v
    onboard relays OFF
          |
          v
    normal FCN operation

This differs intentionally from physically latching expansion devices, which
may need their existing state preserved rather than overwritten at boot.

---

## Digital Inputs

The board provides eight digital inputs.

FCN input count:

    8

GPIO mapping:

| FCN Input | ESP32-S3 GPIO |
|-----------|----------------|
| 1 | GPIO4  |
| 2 | GPIO5  |
| 3 | GPIO6  |
| 4 | GPIO7  |
| 5 | GPIO8  |
| 6 | GPIO9  |
| 7 | GPIO10 |
| 8 | GPIO11 |

The common FCN input interface presents these as logical input states without
requiring the ROS layer to know the GPIO assignments.

Input monitoring has been physically demonstrated.

---

## Ethernet

The board contains a W5500 Ethernet controller.

FCN uses Ethernet as the preferred network interface on this platform.

### W5500 SPI Mapping

    CS:   GPIO16
    IRQ:  GPIO12
    MOSI: GPIO13
    MISO: GPIO14
    SCLK: GPIO15

PHY address:

    1

The Waveshare board-specific network provider contains the W5500 SPI,
MAC, PHY, interrupt, and event configuration.

These hardware details do not belong in the common `fcn_network` component.

---

## Wi-Fi

The ESP32-S3 Wi-Fi interface is supported as a network fallback.

Current network policy:

    Ethernet -> preferred
    Wi-Fi    -> fallback

The Ethernet interface is initialized and monitored normally.

When Wi-Fi fallback is configured, the firmware prepares the Wi-Fi station
and waits for Ethernet to acquire an IP address.

If Ethernet has not obtained an IP after the fallback interval, Wi-Fi is
started.

Current fallback delay:

    approximately 5 seconds

Ethernet remains preferred when both interfaces are available.

Physical Ethernet/Wi-Fi failover has been demonstrated.

---

## Network Architecture

The Waveshare network implementation provides the common:

    fcn_network

interface.

The higher FCN layers therefore ask questions such as:

    Does the node have an IP address?
    What is its current IP address?

They do not need to know whether that address came from W5500 Ethernet or
ESP32 Wi-Fi.

This allows `fcn_microros` to remain independent of the board's physical
network implementation.

---

## RS485 Interface

The board provides an RS485 interface currently used with an optional
Waveshare eight-channel bi-stable Modbus relay module.

UART configuration:

    UART:      UART1
    TX:        GPIO17
    RX:        GPIO18
    DE/RE:     GPIO2
    Baud rate: 9600
    Format:    8N1

Current tested Modbus slave address:

    1

The RS485 implementation supports:

    Function 0x01 -> Read Coils
    Function 0x05 -> Write Single Coil

CRC16 and response validation are performed by the board/provider layer.

---

## Expansion Architecture

RS485 is NOT part of the public FCN I/O model.

It is only one possible transport used by an output provider.

The architectural rule is:

> Expansion capability describes what I/O is available, not how it is
> connected.

For example, an FCN expansion provider could eventually use:

- RS485 / Modbus
- Direct GPIO
- I2C
- SPI
- UART
- Another microcontroller
- Another future transport

Transport-specific APIs therefore remain below the common FCN I/O interface.

---

## 16-Output Configuration

The Waveshare board has been physically demonstrated with:

    8 onboard relay outputs
    +
    8 RS485 expansion outputs
    =
    16 logical FCN outputs

Logical routing:

    FCN outputs 1-8
        -> onboard I2C relay provider

    FCN outputs 9-16
        -> expansion provider outputs 1-8

For example:

    FCN output 9
        -> expansion relay 1

The ROS controller addresses output 9 without needing to know that the
physical device happens to be connected through Modbus RTU.

---

## Bi-Stable Expansion Behavior

The tested RS485 expansion uses bi-stable/latching relays.

These relays physically retain their state.

For that reason the expansion provider does NOT automatically force all
relays OFF during FCN startup.

Instead:

    FCN startup
        |
        v
    detect expansion
        |
        v
    read physical relay state
        |
        v
    preserve/report existing state

This behavior is intentionally different from the onboard relay SAFE-OFF
policy.

### Safety Restriction

Retained/latching outputs should not automatically be considered suitable for
applications where an unexpected retained ON condition could create motion
or another hazardous state.

Current intended uses include applications such as:

- Lighting
- Suitable sensor circuits
- Suitable control-power circuits

They should not be assumed appropriate for:

- Motors
- Machinery motion
- Actuators
- Valves
- Other loads where retained state could create unsafe operation

---

## Status Indicator

FCN status indication is implemented through the board-specific status
provider.

Status LED GPIO:

    GPIO38

The common `fcn_status` component calls the board provider rather than
directly manipulating this GPIO.

The board status provider is linked using the ESP-IDF `WHOLE_ARCHIVE`
mechanism because the implementation satisfies symbols referenced from the
separate common `fcn_status` static component.

---

## FCN Configuration

The Waveshare uses the common `fcn_config` component.

Stored configuration includes:

- Wi-Fi credentials
- micro-ROS agent address
- micro-ROS agent port
- Module ID
- Module name
- Input count
- Output count
- Ethernet enable
- Wi-Fi fallback enable

The normal serial configuration boot window is supported.

---

## micro-ROS

The Waveshare uses the common:

    fcn_microros

component.

The shared implementation provides:

- Network/IP gating
- Agent discovery
- Agent health checking
- ROS entity creation
- `/actionrequest` subscription
- `/modulereturn` publication
- Entity destruction
- Automatic reconnection

The Waveshare was the first native ESP-IDF FCN platform on which the
bidirectional ROS interface was demonstrated.

---

## micro-ROS Target Requirement

The Waveshare requires a micro-ROS library built for:

    IDF_TARGET=esp32s3

The generated `libmicroros.a` is target-specific.

It must not be replaced with or shared directly with a library built for:

    IDF_TARGET=esp32

The common FCN `fcn_microros` source is portable; the generated micro-ROS
binary library is not.

---

## ROS Protocol Verification

The board has been tested using:

    /actionrequest
    /modulereturn

Example command:

    7;9;ON

For a module configured with 16 outputs, this routes logical output 9 to the
first expansion output.

Example 16-output heartbeat:

    7;R:1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0;I:1,1,1,1,1,1,1,1

An eight-output Waveshare node has also been demonstrated:

    6;R:1,1,0,0,0,0,0,0;I:1,1,1,1,1,1,1,1

This demonstrates that `/modulereturn` is generated dynamically according to
the configured FCN capabilities rather than being fixed to one board layout.

---

## Proven FCN Capabilities

The Waveshare native ESP-IDF implementation has physically demonstrated:

    Native ESP-IDF boot                 PASS
    NVS configuration                   PASS
    8 onboard relay outputs             PASS
    8 digital inputs                    PASS
    Input monitoring                    PASS
    W5500 Ethernet                      PASS
    Wi-Fi fallback                      PASS
    Ethernet/Wi-Fi failover             PASS
    micro-ROS agent connection          PASS
    micro-ROS reconnect                 PASS
    /actionrequest                      PASS
    /modulereturn                       PASS
    Dynamic output reporting            PASS
    RS485 expansion detection           PASS
    Modbus relay read                   PASS
    Modbus relay write                  PASS
    16 logical output routing           PASS

---

## Board Profile

Current board profile constants include:

    FCN_BOARD_NAME
        "Waveshare ESP32-S3-ETH-8DI-8RO"

    FCN_BOARD_ID
        "waveshare_s3_8di_8ro"

    FCN_HAS_WIFI
        1

    FCN_HAS_ETHERNET
        1

    FCN_HAS_RELAYS
        1

    FCN_HAS_DIGITAL_INPUTS
        1

    FCN_HAS_RS485
        1

    FCN_ONBOARD_RELAY_COUNT
        8

    FCN_DIGITAL_INPUT_COUNT
        8

    FCN_RS485_RELAY_COUNT
        8

---

## Design Notes

The Waveshare implementation established several architectural patterns now
used throughout native FCN development:

1. Common FCN interfaces should describe capabilities rather than hardware
   transports.

2. Board-specific providers should contain GPIO, I2C, SPI, Ethernet, RS485,
   and similar hardware knowledge.

3. Higher layers such as `fcn_microros` should operate against common FCN
   interfaces.

4. Configured I/O counts should determine the ROS-visible capability rather
   than hard-coded board assumptions.

5. Output startup policy must reflect the physical behavior of the provider.

6. Network selection and failover belong below the common ROS behavior.

These principles allowed the same common FCN architecture to later run on
the substantially different GLEDOPTO classic ESP32 controller.