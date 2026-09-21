# Field Control Network Configuration

## Purpose

This document defines the persistent configuration used by native ESP-IDF
Field Control Network modules.

FCN separates two related concepts:

1. Board capability
2. Module configuration

Board capability describes what the physical hardware can provide.

Module configuration describes how a particular FCN node is intended to
operate.

Configuration must never create capabilities that the physical board does
not possess.

---

# Configuration Storage

FCN configuration is stored in ESP32 Non-Volatile Storage (NVS).

Current NVS namespace:

    wifi

The namespace name is historical. It now contains configuration beyond
Wi-Fi settings and may eventually be renamed or migrated.

Current keys:

    ssid
    password
    agentIp
    agentPort
    moduleId
    modName
    inCount
    outCount
    ethEn
    wifiFb

Configuration behavior is implemented by the shared:

    fcn_config

component.

---

# FCN Configuration Structure

The common configuration structure currently contains:

    wifi_ssid
    wifi_password
    agent_ip
    agent_port
    module_id
    module_name
    input_count
    output_count
    ethernet_enabled
    wifi_fallback_enabled

Current maximum string lengths:

    Wi-Fi SSID:       32 characters
    Wi-Fi password:   64 characters
    Agent IPv4:       15 characters
    Module name:      31 characters

The agent port is stored as a 16-bit unsigned integer.

The module ID and I/O counts are currently stored as 8-bit unsigned values.

---

# Boot Configuration Mode

FCN provides a serial configuration window during startup.

During normal boot, the operator has approximately:

    5 seconds

to request configuration mode.

Current activation:

    Press 'c' then ENTER

When configuration mode is not requested, the module continues normal
startup using its stored configuration.

Conceptually:

    boot
      |
      v
    load configuration
      |
      v
    configuration window
      |
      +---- no request ----> normal startup
      |
      +---- 'c' -----------> configuration console

---

# Configuration Console

The configuration console allows the operator to inspect or modify the
module configuration.

Current actions include:

    S -> save configuration
    C -> cancel configuration

Saving writes the updated configuration to NVS.

After saving, the configuration is reloaded so subsequent startup behavior
uses the persisted values.

Canceling leaves the previously stored configuration unchanged.

---

# Wi-Fi Configuration

Stored fields:

    wifi_ssid
    wifi_password

NVS keys:

    ssid
    password

Wi-Fi credentials are used when the board's network provider enables Wi-Fi.

A board may use Wi-Fi as:

- Its primary network
- An Ethernet fallback
- A future user-selected network mode

The common configuration component stores the credentials but does not
implement the physical Wi-Fi interface.

---

# micro-ROS Agent Configuration

Stored fields:

    agent_ip
    agent_port

NVS keys:

    agentIp
    agentPort

These identify the micro-ROS UDP agent used by the FCN module.

Example:

    Agent IP:   10.0.0.100
    Agent port: 8888

The common `fcn_microros` component consumes these settings when configuring
its UDP transport.

---

# Module Identity

Stored fields:

    module_id
    module_name

NVS keys:

    moduleId
    modName

The module ID is the primary FCN protocol address.

Example:

    module_id = 4

ROS messages may therefore contain:

    4;1;ON

and:

    4;R:0,0,0,0;I:0

The module name provides a human-readable identity for configuration and
future controller functionality.

The current micro-ROS node name is generated from the numeric module ID.

Example:

    fcn_module_4

The configured module name is not currently used as the ROS node name.

---

# Configured I/O Counts

Stored fields:

    input_count
    output_count

NVS keys:

    inCount
    outCount

These values describe the logical I/O capability that the FCN module exposes
to higher layers.

For example:

    GLEDOPTO GL-C-618WL
        input_count  = 1
        output_count = 4

A base Waveshare module may use:

    input_count  = 8
    output_count = 8

A Waveshare module with an eight-output expansion may use:

    input_count  = 8
    output_count = 16

The configured counts are used by the FCN ROS layer when validating commands
and constructing `/modulereturn`.

---

# Board Capability Versus Configuration

The configured I/O count and the physical board capability are related but
are not the same thing.

For example, the GLEDOPTO board currently provides:

    physical outputs = 4
    physical inputs  = 1

A configuration such as:

    output_count = 8

would therefore describe outputs that the physical provider cannot supply.

Similarly, a Waveshare configuration requesting sixteen outputs is only
valid when an appropriate expansion provider is available.

This leads to an important FCN rule:

> Configuration may select available capability, but it must not invent
> hardware capability.

---

# Current Validation Status

Basic configuration validation exists in the common configuration layer.

However, full validation between stored configuration and actual board
capability is not yet complete.

This is an explicit FCN hardening item.

Future validation should reject or safely constrain configurations such as:

    GLEDOPTO output_count > 4
    GLEDOPTO input_count  > 1

and should distinguish between:

    onboard capability
    detected expansion capability
    configured logical capability

The firmware should not advertise or accept commands for unavailable I/O.

---

# Ethernet Configuration

Stored field:

    ethernet_enabled

NVS key:

    ethEn

This setting indicates whether Ethernet operation is requested.

The setting does not itself prove that the physical board contains a
supported Ethernet provider.

For example:

    Waveshare ESP32-S3-ETH-8DI-8RO
        Ethernet provider implemented

    GLEDOPTO GL-C-618WL
        Ethernet provider not yet characterized

Board capability must therefore be checked in addition to the stored
configuration.

---

# Wi-Fi Fallback Configuration

Stored field:

    wifi_fallback_enabled

NVS key:

    wifiFb

This setting is primarily relevant to boards supporting Ethernet and Wi-Fi.

On the Waveshare implementation the intended policy is:

    Ethernet preferred
            |
            v
    wait for Ethernet IP
            |
       +----+----+
       |         |
    IP obtained  timeout
       |         |
       v         v
    Ethernet    Wi-Fi fallback

The current fallback delay is approximately five seconds.

The GLEDOPTO implementation currently uses Wi-Fi directly because its
Ethernet hardware has not yet been characterized for FCN.

---

# Default Configuration

The shared `fcn_config` component provides default configuration values when
appropriate.

Defaults are useful for initial development and recovery, but production FCN
nodes should not rely on invalid or ambiguous defaults.

A configuration should eventually be considered valid only when all
required settings are internally consistent and compatible with the board.

---

# Configuration Consumers

The common configuration is consumed by several FCN subsystems.

Conceptually:

    NVS
     |
     v
  fcn_config
     |
     +----------> fcn_network
     |
     +----------> fcn_microros
     |
     +----------> I/O capability
     |
     +----------> application startup

This keeps persistent storage details out of the individual subsystems.

---

# Configuration Ownership

The intended responsibility boundaries are:

## fcn_config

Responsible for:

- Loading persistent values
- Saving persistent values
- Defaults
- Basic value validation
- Comparing configuration
- Determining whether Wi-Fi credentials are available

## Board Profile / Providers

Responsible for:

- Physical board capability
- Actual GPIO assignments
- Available network hardware
- Available onboard I/O
- Physical expansion support

## Application Startup

Responsible for:

- Combining stored configuration with board capability
- Rejecting impossible combinations
- Starting the appropriate providers

## fcn_microros

Responsible for:

- Using the validated module identity
- Using validated I/O counts
- Using the configured agent address
- Exposing the resulting logical capability to ROS

---

# Configuration Safety

Invalid configuration should fail safely.

Examples include:

- Invalid agent address
- Invalid agent port
- Impossible input count
- Impossible output count
- Requested Ethernet without a supported provider
- Wi-Fi fallback requested without usable Wi-Fi credentials
- Invalid or reserved module identity

A configuration error should not cause an uncontrolled output state.

Output providers should establish their defined startup state independently
of whether networking or micro-ROS subsequently succeeds.

---

# Future Hardening

Configuration hardening should include:

- Validate configured I/O counts against board capability
- Validate expansion-dependent capability
- Validate module ID range
- Validate agent IPv4 address
- Validate agent port
- Validate network-mode combinations
- Validate required Wi-Fi credentials
- Detect incomplete NVS records
- Distinguish missing configuration from corrupt configuration
- Report configuration errors clearly
- Prevent invalid configuration from reaching hardware providers
- Consider configuration format/version identification

These changes should be implemented incrementally and tested against both
current FCN hardware families.

---

# Current Known Configurations

## GLEDOPTO GL-C-618WL

Typical tested capability:

    inputs:   1
    outputs:  4
    network:  Wi-Fi

## Waveshare ESP32-S3-ETH-8DI-8RO

Base configuration:

    inputs:   8
    outputs:  8
    network:  Ethernet with optional Wi-Fi fallback

Expanded configuration:

    inputs:   8
    outputs:  16
    network:  Ethernet with optional Wi-Fi fallback

The sixteen-output configuration requires the external eight-output provider
to be present.

---

# Design Principle

Persistent configuration describes how an FCN module should operate.

The board profile describes what the hardware can actually do.

The final runtime capability is the valid intersection of those two:

    stored configuration
            +
    physical capability
            |
            v
    validated FCN capability

No higher FCN layer should be asked to operate against capability that has
not passed this validation.
