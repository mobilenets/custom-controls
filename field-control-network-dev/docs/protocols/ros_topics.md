# Field Control Network ROS Topic Protocol

## Purpose

This document defines the ROS 2 message protocol used by Field Control
Network modules.

The protocol is intentionally independent of the physical FCN hardware.

A ROS controller communicates with logical FCN modules and logical I/O
numbers. It does not need to know whether an output is implemented using
GPIO, an I2C expander, RS485, or another hardware provider.

---

## Current Topics

FCN currently defines the following application topics:

    /actionrequest
    /modulereturn
    /environment

All currently use:

    std_msgs/msg/String

The string protocol is intentionally simple so messages can be inspected,
generated, and debugged easily from the ROS 2 command line.

---

# /actionrequest

## Purpose

`/actionrequest` sends an output command from the ROS 2 controller to an
FCN module.

Message type:

    std_msgs/msg/String

Current QoS reliability:

    BEST_EFFORT

---

## Format

    <moduleId>;<outputIndex>;<ON|OFF>

Example:

    4;1;ON

This means:

    module ID:    4
    output index: 1
    requested:    ON

Another example:

    7;9;OFF

This means:

    module ID:    7
    output index: 9
    requested:    OFF

---

## Module Addressing

Every FCN module has a configured module ID.

A module receiving `/actionrequest` first checks the module ID contained in
the message.

Commands addressed to another module are ignored.

This allows all FCN modules to subscribe to the same topic.

For example:

    4;1;ON

is processed by module 4 and ignored by modules 6 and 7.

---

## Output Numbering

Output indexes are one-based.

Therefore:

    first output  = 1
    second output = 2

Output zero is invalid.

The valid output range is determined by the module's configured output
count.

For a four-output module:

    valid:   1-4
    invalid: 0
    invalid: 5 and above

For a sixteen-output module:

    valid:   1-16

The ROS protocol addresses logical FCN outputs rather than physical
provider-local outputs.

For example, on a Waveshare module with an eight-output expansion:

    FCN outputs 1-8
        -> onboard provider

    FCN outputs 9-16
        -> expansion provider

The ROS command remains simply:

    7;9;ON

The controller does not need to know that FCN output 9 is physically
implemented as expansion output 1.

---

## Output State Values

Current accepted output commands are:

    ON
    OFF

These values are case-sensitive in the current implementation.

Malformed or unsupported commands should not change an output.

---

## ROS 2 Command-Line Example

Turn output 1 ON for module 4:

    ros2 topic pub --once \
      --qos-reliability best_effort \
      /actionrequest \
      std_msgs/msg/String \
      "{data: '4;1;ON'}"

Turn it OFF:

    ros2 topic pub --once \
      --qos-reliability best_effort \
      /actionrequest \
      std_msgs/msg/String \
      "{data: '4;1;OFF'}"

---

# /modulereturn

## Purpose

`/modulereturn` reports the current logical I/O state of an FCN module.

It also serves as the regular module heartbeat.

Message type:

    std_msgs/msg/String

Current QoS reliability:

    BEST_EFFORT

Current publication interval:

    approximately 1 second

---

## Format

    <moduleId>;R:<output states>;I:<input states>

Example:

    4;R:1,0,0,0;I:0

This represents:

    module ID: 4

    outputs:
        1 = ON
        2 = OFF
        3 = OFF
        4 = OFF

    inputs:
        1 = inactive

---

## Output State List

The `R:` section contains one state for each configured logical output.

Values:

    0 = OFF
    1 = ON

The number of values is dynamic.

A four-output module may report:

    R:0,0,0,0

An eight-output module may report:

    R:1,1,0,0,0,0,0,0

A sixteen-output module may report:

    R:1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0

The output count therefore describes module capability rather than a fixed
ROS message layout.

---

## Input State List

The `I:` section contains one state for each configured logical input.

Values:

    0 = inactive
    1 = active

The board provider is responsible for translating physical electrical
polarity into these logical FCN states.

The ROS protocol should therefore describe logical state rather than whether
the underlying GPIO is electrically HIGH or LOW.

Example for a one-input module:

    I:0

Example for an eight-input module:

    I:1,1,1,1,1,1,1,1

The number of input values is dynamic according to the module's configured
input count.

---

## Complete Examples

GLEDOPTO module 4:

    4;R:0,0,0,0;I:0

Waveshare module 6:

    6;R:1,1,0,0,0,0,0,0;I:1,1,1,1,1,1,1,1

Waveshare module 7 with sixteen logical outputs:

    7;R:1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0;I:1,1,1,1,1,1,1,1

Multiple modules publish to the same `/modulereturn` topic.

The module ID identifies the source.

---

## Heartbeat Behavior

Each connected FCN module periodically publishes `/modulereturn`.

The current interval is approximately:

    1 second

The heartbeat serves two purposes:

1. Report the latest I/O state.
2. Indicate that the FCN module is still communicating through ROS.

Higher-level software can therefore use heartbeat age when determining
whether a module should be considered online.

A missed BEST_EFFORT heartbeat does not by itself indicate module failure.

---

## ROS 2 Command-Line Example

Monitor FCN module returns:

    ros2 topic echo \
      --qos-reliability best_effort \
      /modulereturn

Example output from several modules:

    data: 4;R:0,0,0,0;I:0
    ---
    data: 6;R:1,1,0,0,0,0,0,0;I:1,1,1,1,1,1,1,1
    ---
    data: 7;R:1,0,0,0,0,0,0,0,1,0,0,0,0,0,0;I:1,1,1,1,1,1,1,1

---

# /environment

## Purpose

`/environment` is reserved for environmental measurements associated with an
FCN module.

Message type:

    std_msgs/msg/String

Current format:

    <moduleId>;T:<temperature>;H:<humidity>

Example:

    3;T:24.6;H:58.2

Where:

    module ID   = 3
    temperature = 24.6
    humidity    = 58.2

The original implementation target uses a DHT22 temperature/humidity sensor.

The planned/current measurement interval for nodes providing this data is
approximately:

    5 seconds

Environmental capability is optional and is not required for an FCN node to
provide normal I/O services.

---

# QoS

The current FCN application topics use:

    BEST_EFFORT

This has been used deliberately on both the embedded micro-ROS nodes and ROS
2 side to avoid reliability-policy mismatches.

The protocol is designed around continuously refreshed state rather than
requiring every heartbeat to arrive.

For example, if one `/modulereturn` heartbeat is lost, the next heartbeat
will again contain the complete current I/O state.

Command reliability and acknowledgement behavior may receive additional
hardening in future FCN versions.

---

# State Versus Events

`/modulereturn` represents current state rather than an event log.

For example:

    4;R:1,0,0,0;I:0

means that output 1 is currently ON.

It does not mean that output 1 has just transitioned ON.

This distinction allows a newly started controller or monitoring client to
reconstruct the current module state from subsequent heartbeat messages.

---

# Provider Independence

ROS-visible I/O is provider-independent.

For example, these may all appear as ordinary FCN outputs:

    direct ESP32 GPIO
    I2C relay expander
    RS485 Modbus relay
    future SPI expansion
    future remote I/O processor

The ROS protocol must not expose transport-specific implementation details
unless a future requirement specifically demands them.

This is a central FCN design rule:

> ROS addresses FCN capabilities, not physical transport mechanisms.

---

# Current Validation

The protocol has been physically demonstrated with multiple FCN modules
operating simultaneously.

Current tested configurations include:

    Module 4
        4 outputs
        1 input
        classic ESP32 / GLEDOPTO

    Module 6
        8 outputs
        8 inputs
        ESP32-S3 / Waveshare

    Module 7
        16 outputs
        8 inputs
        ESP32-S3 / Waveshare with expansion

The same `/actionrequest` and `/modulereturn` protocol is used by all three
configurations.

---

# Future Protocol Considerations

The current String-based protocol is intentionally simple and has proven
useful during development.

Future hardening may consider:

- Explicit command acknowledgement
- Command sequence or transaction IDs
- Error/status reporting
- Module capability reporting
- Protocol version identification
- Message validation
- Stale-command protection
- More structured ROS message types

Any future protocol change should preserve the hardware-independent FCN
capability model.
