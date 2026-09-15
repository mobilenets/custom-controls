
# Waveshare ESP32-S3 Module

Reserved for the native ESP-IDF port of the FCN
Waveshare ESP32-S3 Ethernet/relay controller.

Development will begin after the ESP32-CAM diagnostic
node establishes the native ESP-IDF baseline.
development has begun. notes below.

FCN Waveshare ESP32-S3 — Development Environment Setup

This firmware is the native ESP-IDF implementation of the Field Control Network node for the Waveshare ESP32-S3-ETH-8DI-8RO. It has been developed and tested with ESP-IDF 6.0 on Ubuntu 22.04.

The current tested firmware supports the board's eight digital inputs, eight onboard relays, W5500 Ethernet, persistent configuration, and automatic Wi-Fi fallback. Ethernet is the preferred network interface; Wi-Fi takes over when Ethernet is unavailable and Ethernet automatically becomes preferred again when it returns.

1. Install the basic development tools

On a clean Ubuntu installation:

sudo apt update
sudo apt upgrade

sudo apt install -y \
    git \
    wget \
    flex \
    bison \
    gperf \
    python3 \
    python3-pip \
    python3-venv \
    cmake \
    ninja-build \
    ccache \
    libffi-dev \
    libssl-dev \
    dfu-util \
    libusb-1.0-0
2. Install ESP-IDF 6.0

The known-good development version for this project is ESP-IDF 6.0.

cd ~
git clone -b v6.0 --recursive https://github.com/espressif/esp-idf.git esp-idf-6.0

cd ~/esp-idf-6.0
./install.sh esp32s3

ESP-IDF must be activated in each new terminal before using idf.py:

source ~/esp-idf-6.0/export.sh

Verify it:

idf.py --version

Expected:

ESP-IDF v6.0
3. Clone the FCN repository
cd ~
git clone https://github.com/mobilenets/custom-controls.git

cd ~/custom-controls/field-control-network-dev/firmware/esp-idf/waveshare_s3_module

The native ESP-IDF firmware is intentionally separated from the older Arduino implementation. Board-specific hardware is kept under the boards/ directory while reusable FCN functions are moving into components/.

The Waveshare driver currently uses W5500 SPI on GPIO 15/13/14 with CS 16 and IRQ 12, while its relay and input hardware is handled independently.

4. Select the ESP32-S3 target

From the Waveshare project directory:

idf.py set-target esp32s3

The physical board has 16 MB flash. Make sure the project configuration reflects the 16 MB device rather than the 2 MB value that may initially be selected.

idf.py menuconfig

Also configure the primary console for USB Serial/JTAG. This is important: logs can appear over /dev/ttyACM0 even when the input side is incorrectly configured for UART0. The FCN interactive configuration console requires USB Serial/JTAG to be the primary console.

5. Build
idf.py build

A successful build ends with:

Project build complete.

Do not routinely use fullclean. Normal incremental builds are considerably faster and are sufficient for normal development.

6. Flash and monitor

The Waveshare board normally appears as:

/dev/ttyACM0

Flash and open the serial monitor with:

idf.py -p /dev/ttyACM0 flash monitor

Exit the ESP-IDF monitor with:

Ctrl+]
7. Configure the FCN node

At boot the firmware provides a short configuration window. Press:

c

within approximately five seconds.

The configuration console allows setting the module identity, I/O counts, micro-ROS agent address, Wi-Fi credentials, and network policy.

Typical configuration:

========== FCN CONFIGURATION ==========

 1. Module ID:          1
 2. Module Name:        FCN Module
 3. Input Count:        8
 4. Output Count:       8

 5. Agent IP:           192.168.0.100
 6. Agent Port:         8888

 7. Wi-Fi SSID:         your-network
 8. Wi-Fi Password:     [configured]
 9. Ethernet Enabled:   YES
10. Wi-Fi Fallback:     YES

---------------------------------------
S. Save and reload
C. Cancel
---------------------------------------

These operational settings are stored in ESP32 NVS rather than hard-coded into the firmware.

Wi-Fi credentials are not required for an Ethernet-only installation.

8. Network behavior

The normal policy is:

             FCN Network Manager
                     |
              Start Ethernet
                     |
             Ethernet usable?
                /         \
              yes          no
               |            |
          ETHERNET       Wi-Fi
          preferred      fallback
               |            |
               +------<-----+
                 Ethernet
                  returns

On startup without a usable Ethernet connection, the firmware waits approximately five seconds before starting Wi-Fi fallback. The fallback task only starts Wi-Fi when Ethernet has failed to acquire an address.

A successful Wi-Fi fallback looks approximately like:

Ethernet unavailable after startup delay
Activating Wi-Fi fallback
Wi-Fi station started
Wi-Fi connected to AP
Wi-Fi DHCP lease acquired
Wi-Fi IP: 10.0.0.x
Wi-Fi Gateway: 10.0.0.1
Wi-Fi Netmask: 255.255.255.0
Active interface: WI-FI
Active IP: 10.0.0.x

If Ethernet subsequently becomes available, it automatically becomes active again. If Ethernet disappears later while Wi-Fi remains connected, the firmware can switch back to Wi-Fi immediately rather than reconnecting from scratch. The active-network selection explicitly gives Ethernet preference whenever Ethernet has a valid IP.

9. Currently proven hardware

The native ESP-IDF port has been physically tested through:

ESP32-S3 boot
   ↓
16 MB flash detection
   ↓
I2C relay controller
   ↓
8 onboard relays
   ↓
8 digital inputs
   ↓
input change monitoring
   ↓
W5500 SPI Ethernet
   ↓
DHCP
   ↓
Ethernet disconnect/reconnect
   ↓
persistent NVS configuration
   ↓
Wi-Fi fallback
   ↓
Wi-Fi → Ethernet recovery
   ↓
Ethernet → Wi-Fi failover

The network code considers either Ethernet or Wi-Fi IP connectivity sufficient for the higher FCN layers.

A couple of notes I'd add before publishing

I would explicitly label the next section “Not Yet Implemented in This Native Port” and mention micro-ROS and RS485 there. We have working implementations/reference code elsewhere, but a random GitHub user shouldn't interpret that as meaning they're already working in this particular native Waveshare build.

I'd also add a small warning that the current application partition still needs to be enlarged before micro-ROS is added. The physical flash is 16 MB, but merely setting the flash size to 16 MB doesn't automatically make the application partition large enough.

This is already getting close to something I'd be comfortable putting directly into the Waveshare project's readme.md. The one thing I'd want to do before calling it finished is compare these instructions against the current repository tree and dependency files, particularly idf_component.yml and the CMake files, so that a stranger cloning the repository gets an exact reproducible sequence rather than instructions based partly on the steps we took during development.
