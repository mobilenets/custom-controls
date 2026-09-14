# WAVESHARE-IDF-007 — FCN Persistent Configuration

This bundle adds a reusable FCN configuration subsystem.

## Current fields

- Wi-Fi SSID
- Wi-Fi password
- micro-ROS agent IP
- micro-ROS agent port
- module ID
- module name
- number of digital inputs
- number of outputs
- Ethernet enabled
- Wi-Fi fallback enabled

## Storage

Settings are stored in ESP-IDF NVS.

The historical FCN namespace `wifi` is retained so the project can preserve
existing key naming where practical.

## Console behavior

The console edits a working copy of the current configuration.

- Existing settings are shown on one page.
- Changed values display `*`.
- Wi-Fi passwords are never printed.
- `S` validates, saves, and reloads.
- `C` discards the working copy without writing NVS.

## Suggested main.c integration

```c
#include "fcn_config.h"
#include "fcn_config_console.h"

static fcn_config_t g_config;

void app_main(void)
{
    ESP_ERROR_CHECK(fcn_config_load(&g_config));

    // During the boot configuration window, if the user presses 'c':
    //
    // fcn_config_console_run(&g_config);
    //
    // Continue normal initialization from g_config after the console exits.
}
```

The serial "press c within 5 seconds" boot-window helper is intentionally not
included here yet. It can be added as a thin layer around this configuration
subsystem without changing the storage or editor.