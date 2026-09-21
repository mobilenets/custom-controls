# micro-ROS Target-Specific Builds

## Purpose

This document records an important build requirement discovered while
porting the Field Control Network firmware between ESP32 families.

The FCN `fcn_microros` source component is shared between boards.

The generated micro-ROS binary library is not.

In particular:

    ESP32    and    ESP32-S3

must use micro-ROS libraries built for their respective ESP-IDF targets.

---

# FCN Architecture

The shared FCN micro-ROS behavior is located under:

    firmware/esp-idf/common_components/fcn_microros/

This component contains FCN-specific ROS behavior such as:

- Agent discovery
- Agent connection
- ROS node creation
- `/actionrequest`
- `/modulereturn`
- Agent health checking
- Entity destruction
- Automatic reconnection

This source code has been demonstrated on both:

    IDF_TARGET=esp32
    IDF_TARGET=esp32s3

The source is therefore portable across the currently supported FCN ESP32
families.

---

# The Important Boundary

The underlying micro-ROS ESP-IDF component generates:

    libmicroros.a

This archive contains compiled target-specific code.

It is therefore NOT portable between ESP32 processor targets.

Correct relationship:

    shared fcn_microros source
              |
        +-----+-----+
        |           |
        v           v
      esp32       esp32s3
        |           |
        v           v
    target-built  target-built
    libmicroros.a libmicroros.a

Incorrect relationship:

    esp32s3 libmicroros.a
              |
              v
        classic ESP32

A successful linker invocation does not prove that the library was built for
the correct processor target.

---

# Failure Discovered During GL-C-618WL Port

The Waveshare FCN project uses:

    IDF_TARGET=esp32s3

The GLEDOPTO GL-C-618WL uses:

    IDF_TARGET=esp32

During the initial GLEDOPTO micro-ROS integration, its project referenced the
micro-ROS component located under the Waveshare ESP32-S3 project.

The application:

    compiled successfully
    linked successfully
    flashed successfully
    booted successfully
    connected to Wi-Fi successfully
    obtained DHCP successfully

The failure appeared only when micro-ROS initialization began.

---

# Runtime Failure

After DHCP, the classic ESP32 generated:

    Guru Meditation Error:
    Core 0 panic'ed (IllegalInstruction)

The program counter was associated with:

    uxr_nanos

The backtrace included functions in the micro-ROS initialization path:

    uxr_nanos
    rmw_init_options_init
    rcl_init_options_init
    configure_init_options
    micro_ros_task

The processor then rebooted and repeated the cycle.

This initially appeared capable of being an FCN source-code or runtime
initialization problem.

It was actually a target-specific binary mismatch.

---

# Diagnosis

Inspection of the micro-ROS ESP-IDF component showed that its CMake build
passes the current:

    IDF_TARGET

into the micro-ROS library build process.

The component generates a target-specific toolchain file.

The two development trees showed:

    esp32_cam/components/micro_ros_espidf_component/
        esp32_toolchain.cmake

containing:

    set(idf_target "esp32")

while:

    waveshare_s3_module/components/micro_ros_espidf_component/
        esp32_toolchain.cmake

contained:

    set(idf_target "esp32s3")

This confirmed that the two micro-ROS component directories had been built
for different processor targets.

---

# Why the Problem Persisted

The micro-ROS build system stores the generated archive inside the component
directory.

The resulting file is:

    libmicroros.a

The build process can consider an existing archive up to date.

Therefore, simply referencing a micro-ROS component directory from another
ESP-IDF project does not guarantee that its existing library was generated
for the new project's target.

The dangerous sequence was effectively:

    Waveshare project
        |
        v
    build libmicroros.a for esp32s3
        |
        v
    archive remains in component directory
        |
        v
    GLEDOPTO project references same component directory
        |
        v
    existing archive reused
        |
        v
    classic ESP32 application links successfully
        |
        v
    IllegalInstruction at runtime

---

# Corrected GLEDOPTO Build

The GLEDOPTO project was changed to use the micro-ROS component already
associated with the classic ESP32 ESP32-CAM development tree.

Current GLEDOPTO component path:

    ../esp32_cam/components/micro_ros_espidf_component

This component had been built for:

    IDF_TARGET=esp32

After making this change, the GLEDOPTO firmware:

    compiled
    linked
    flashed
    booted
    obtained Wi-Fi
    obtained DHCP
    detected the micro-ROS agent
    created its ROS entities
    published /modulereturn
    accepted /actionrequest
    survived agent loss
    automatically reconnected

without the IllegalInstruction failure.

---

# Current Waveshare Build

The Waveshare project uses the micro-ROS component associated with its
ESP32-S3 build.

Required target:

    IDF_TARGET=esp32s3

This target-specific component/library should remain isolated from classic
ESP32 builds.

---

# Current GLEDOPTO Build

The GLEDOPTO project currently uses:

    ../esp32_cam/components/micro_ros_espidf_component

Required target:

    IDF_TARGET=esp32

This arrangement is currently known-good.

The path is functional but should not necessarily be considered the final
FCN dependency layout.

It exists because the ESP32-CAM project already contained a correctly built
classic ESP32 micro-ROS component.

---

# Component Source Versions

During investigation, the micro-ROS component source trees were checked.

Both component copies were at the same source commit:

    4ddd8c26e721662319ed8af981cb7cdc9ae05382

Therefore the runtime difference was not explained by different micro-ROS
source revisions.

The important difference was the generated target-specific build output.

---

# Git Repository Note

The micro-ROS component directories currently behave as independent nested
Git repositories.

They are not registered as normal Git submodules in the outer FCN repository.

Inspection of the outer repository did not show Git submodule entries for
these directories.

This dependency arrangement should eventually be made more explicit.

---

# clean-microros Warning

The micro-ROS ESP-IDF component provides a build target that can clean and
regenerate its library.

For example:

    clean-microros

This must be used carefully when multiple FCN projects reference
target-specific component directories.

Cleaning a known-good ESP32-S3 micro-ROS component while building from an
ESP32 project could replace its generated archive with an ESP32 version.

The reverse is equally dangerous.

Until the dependency layout is hardened:

> Never rebuild a shared micro-ROS component directory for a different
> IDF_TARGET without first verifying which projects depend on that directory.

---

# Verification

The generated toolchain file provides a useful target check.

For a classic ESP32 build, verify that it contains:

    set(idf_target "esp32")

For the Waveshare ESP32-S3 build, verify:

    set(idf_target "esp32s3")

This check is useful, but the longer-term goal should be to make an incorrect
combination impossible rather than relying on manual inspection.

---

# Known-Good Target Mapping

Current development mapping:

| FCN Hardware | ESP-IDF Target | micro-ROS Target |
|--------------|----------------|------------------|
| GLEDOPTO GL-C-618WL | esp32 | esp32 |
| ESP32-CAM | esp32 | esp32 |
| Waveshare ESP32-S3-ETH-8DI-8RO | esp32s3 | esp32s3 |

Never substitute one target's generated `libmicroros.a` for another.

---

# Shared Versus Target-Specific Code

The intended FCN separation is:

## Shared

    common_components/fcn_microros/

Contains FCN behavior and should remain portable.

## Target-Specific

    micro_ros_espidf_component
        generated toolchain
        generated libmicroros.a

Contains or generates processor-specific binary artifacts.

This distinction is important:

> Sharing FCN source code is desirable.
>
> Sharing a generated target-specific binary archive is not.

---

# Hardening Requirement

The current arrangement works, but it still relies too much on directory
selection and developer awareness.

FCN hardening should make the processor target explicit.

A future structure should ensure that:

    esp32 project
        -> can only consume esp32 micro-ROS library

    esp32s3 project
        -> can only consume esp32s3 micro-ROS library

An accidental mismatch should ideally fail during configuration or build
rather than producing a firmware image that crashes at runtime.

---

# Possible Future Approaches

Possible solutions include:

1. Maintain separate dependency/build directories for each ESP-IDF target.

2. Generate `libmicroros.a` into target-specific build locations.

3. Add a CMake/configuration-time target check.

4. Automatically rebuild micro-ROS when `IDF_TARGET` changes.

5. Store target metadata beside generated archives and reject mismatches.

The final solution should preserve the shared FCN `fcn_microros` source
component while isolating target-specific generated artifacts.

---

# Diagnostic Lesson

This failure demonstrated an important embedded-development principle:

> Successful compilation and linking do not prove binary compatibility.

When a firmware application:

    boots normally
    initializes ordinary ESP-IDF services
    connects to the network

but generates an IllegalInstruction exception immediately after entering a
precompiled library, target or ABI compatibility should be considered early
in the investigation.

For FCN specifically, always verify:

    ESP-IDF IDF_TARGET
          =
    micro-ROS library target

before debugging higher-level ROS behavior.