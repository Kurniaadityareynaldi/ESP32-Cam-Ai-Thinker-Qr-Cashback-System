# ESP32-CAM QR Cashback System

An ESP32-CAM based QR code scanning and cashback system with Bluetooth thermal printer integration, product database lookup, PSRAM-based scan storage, door/solenoid control, limit switch monitoring, and buzzer feedback.

The system is designed to scan QR codes from bottles/products, identify them through an internal product database, calculate the total cashback, and print a cashback receipt automatically.

## Features

* QR code scanning using ESP32-CAM
* Product lookup from an internal database
* Support for multiple QR scans in one transaction
* Maximum 30 scanned products per transaction
* Product information stored in program memory (PROGMEM)
* Scan data stored in ESP32 PSRAM
* Bluetooth connection to a thermal printer
* Automatic cashback calculation
* QR code printing on thermal receipt
* Solenoid-controlled door
* Limit switch monitoring for door status
* Door close stability detection
* Buzzer notification system
* Bluetooth status indication using LED
* Heap and PSRAM memory monitoring
* Low-heap protection with automatic ESP32 restart
* State-machine based system operation

## System Flow

```text
                ┌─────────────────┐
                │    SYSTEM BOOT  │
                └────────┬────────┘
                         │
                         ▼
                ┌─────────────────┐
                │ Initialize PSRAM│
                │ Camera & System │
                └────────┬────────┘
                         │
                         ▼
                ┌─────────────────┐
                │    WAIT_QR      │
                │  Scan QR Code   │
                └────────┬────────┘
                         │
                    QR Valid?
                         │
                         ▼
                ┌─────────────────┐
                │ Product Lookup  │
                │ & Save QR Data  │
                └────────┬────────┘
                         │
                         ▼
                ┌─────────────────┐
                │ Unlock Solenoid │
                │    Door Opens   │
                └────────┬────────┘
                         │
                         ▼
                ┌─────────────────┐
                │    DOOR_OPEN    │
                │ Scan QR Again   │
                │ while open      │
                └────────┬────────┘
                         │
                    Door Closed?
                         │
                         ▼
                ┌─────────────────┐
                │ DOOR_CLOSE_WAIT │
                │ Wait lock delay │
                └────────┬────────┘
                         │
                         ▼
                ┌─────────────────┐
                │  Lock Solenoid  │
                │ Start Bluetooth │
                └────────┬────────┘
                         │
                         ▼
                ┌─────────────────┐
                │    PRINTING     │
                │ Print Receipt   │
                └────────┬────────┘
                         │
                         ▼
                ┌─────────────────┐
                │ Clear Transaction│
                │ Restart System  │
                └─────────────────┘
```

## Hardware

The current source code is configured for an **ESP32-CAM AI Thinker**.

### Main Components

* ESP32-CAM AI Thinker
* QR-compatible camera
* Solenoid lock
* Door limit switch
* Bluetooth thermal printer
* Buzzer
* Bluetooth status LED
* Power supply suitable for the ESP32-CAM and solenoid

## Pin Configuration

| Function          |    GPIO |
| ----------------- | ------: |
| Solenoid          | GPIO 14 |
| Buzzer            | GPIO 13 |
| Door Limit Switch | GPIO 15 |
| Bluetooth LED     |  GPIO 4 |

The pin configuration is defined directly in the source code:

```cpp
#define SOLENOID_PIN   14
#define BUZZER_PIN     13
#define LIMIT_PIN      15
#define LED_BT         4
```

## Software Requirements

Install the following Arduino libraries before compiling:

* `ESP32QRCodeReader`
* `BluetoothSerial`
* `Adafruit_Thermal`
* ESP32 Arduino Core

The main libraries used by the program are:

```cpp
#include <Arduino.h>
#include <ESP32QRCodeReader.h>
#include <BluetoothSerial.h>
#include "Adafruit_Thermal.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
```

## Configuration

Important system parameters are defined near the beginning of the program:

```cpp
#define MAX_SCAN        30
#define QR_LEN           32
#define LOCK_DELAY     2000
#define PRINT_DELAY   10000
#define LOW_HEAP_LIMIT 30000
#define SCAN_DELAY      300
```

### Parameter Description

| Parameter        |       Value | Description                            |
| ---------------- | ----------: | -------------------------------------- |
| `MAX_SCAN`       |          30 | Maximum QR codes in one transaction    |
| `QR_LEN`         |          32 | Maximum QR string buffer length        |
| `LOCK_DELAY`     |     2000 ms | Delay before locking after door closes |
| `PRINT_DELAY`    |    10000 ms | Delay before printing                  |
| `LOW_HEAP_LIMIT` | 30000 bytes | Minimum heap threshold                 |
| `SCAN_DELAY`     |      300 ms | Minimum interval between QR scans      |

## Product Database

Products are currently stored in a static database using `PROGMEM`.

Example:

```cpp
const Product productDB[DB_SIZE] PROGMEM = {
  {"PE500XYSH", "LIFEBUOY",  "PET",   500,  5000},
  {"HD500AB12", "SUNLIGHT",  "HDPE",  500,  5000},
  {"PE250K9QW", "AQUA",      "PET",   250,  2500},
  {"HD250LM88", "SOKLIN",    "HDPE",  250,  2500}
};
```

Each product contains:

```cpp
struct Product {
  char     qr[12];
  char     name[20];
  char     type[10];
  uint16_t size;
  uint32_t cashbackk;
};
```

### Product Data

| Field       | Description                 |
| ----------- | --------------------------- |
| `qr`        | QR code identifier          |
| `name`      | Product/brand name          |
| `type`      | Material/type               |
| `size`      | Product size in milliliters |
| `cashbackk` | Cashback value              |

If a scanned QR code is not found in the database, it is ignored and an error buzzer is activated.

## PSRAM Usage

The system uses PSRAM to store scanned QR codes and product information.

Two dynamically allocated data structures are used:

```cpp
scanList
scanProduct
```

The program checks whether PSRAM is available during startup:

```cpp
if (!psramFound()) {
  Serial.println("NO PSRAM");
  ESP.restart();
}
```

Memory information can also be monitored through the Serial Monitor:

```text
[MEM] SAVE | Heap:XX KB | PSRAM:XX KB
```

If available heap memory drops below the configured threshold, the ESP32 automatically restarts.

## QR Scanning

The QR scanner runs as a FreeRTOS task on **Core 1**.

```cpp
xTaskCreatePinnedToCore(
  qrTaskFunc,
  "QR_TASK",
  4096,
  NULL,
  1,
  NULL,
  1
);
```

A cooldown mechanism prevents QR scans from being accepted too quickly:

```cpp
if (now - lastScanTime < SCAN_DELAY) {
  continue;
}
```

The default scan interval is **300 ms**.

## Door Control

The solenoid uses active-low control:

```cpp
void unlockDoor() {
  digitalWrite(SOLENOID_PIN, LOW);
}

void lockDoor() {
  digitalWrite(SOLENOID_PIN, HIGH);
}
```

The system uses a limit switch to determine whether the door has opened and whether it has returned to the closed position.

A stable closed-door detection is implemented with a 300 ms stability period.

## State Machine

The main system uses four states:

```cpp
enum SystemState {
  WAIT_QR,
  DOOR_OPEN,
  DOOR_CLOSE_WAIT,
  PRINTING
};
```

### `WAIT_QR`

The system waits for a valid QR code.

When a valid QR is received:

1. QR is checked against the product database.
2. Product information is saved.
3. Solenoid is activated.
4. Door is allowed to open.
5. System changes to `DOOR_OPEN`.

### `DOOR_OPEN`

The system waits for the door to actually open.

Additional QR codes can be scanned while the door is open.

When the door is detected as closed, the camera is stopped and the system moves to `DOOR_CLOSE_WAIT`.

### `DOOR_CLOSE_WAIT`

The system waits for the configured:

```cpp
LOCK_DELAY
```

After the delay:

1. Door is locked.
2. Bluetooth is started.
3. System enters `PRINTING`.

### `PRINTING`

After:

```cpp
PRINT_DELAY
```

the system:

1. Checks Bluetooth printer connection.
2. Prints the transaction.
3. Prints the QR codes.
4. Prints product information.
5. Calculates total cashback.
6. Stops Bluetooth.
7. Clears transaction data.
8. Restarts the ESP32.

## Thermal Printer

The system communicates with the thermal printer using Bluetooth.

Printer configuration includes:

```cpp
#define QR_SIZE        12
#define QR_ECC         0x30
#define FEED_TOP        1
#define FEED_BOTTOM     4
```

The receipt contains:

```text
===== TOKO ABC =====
   STRUK CASHBACK

[QR CODE]

Product Name
Type     : PET
Size     : 500 ml
Cashback : Rp 5000

---------------------

TOTAL CASHBACK
Rp 5000

Terima Kasih
```

The QR code is printed using raw ESC/POS commands.

## Bluetooth Printer

The printer Bluetooth MAC address is configured in:

```cpp
uint8_t printerMAC[6] = {
  0x06, 0x09, 0x36, 0x63, 0xAC, 0x30
};
```

The Bluetooth PIN is:

```cpp
const char *btPin = "0000";
```

The Bluetooth device name is:

```cpp
SerialBT.begin("ESP32-CAM", true);
```

If the printer is not connected during printing, the system displays:

```text
BT DISCONNECTED
```

and activates the Bluetooth error buzzer pattern.

> **Important:** Change the Bluetooth MAC address and PIN according to your actual printer configuration before deployment.

## Buzzer

The buzzer uses non-blocking timing based on `millis()`.

Preset patterns are available:

| Function      | Purpose                  |
| ------------- | ------------------------ |
| `beepON()`    | System boot              |
| `beepOK()`    | QR successfully scanned  |
| `beepBT()`    | Bluetooth/printer error  |
| `beepError()` | QR not found in database |
| `beepDone()`  | Printing completed       |

This allows the buzzer to operate without using long blocking delays during normal operation.

## Serial Monitor

Set the Arduino Serial Monitor to:

```text
115200 baud
```

Example startup messages:

```text
SYSTEM BOOT
PSRAM OK
CAMERA ON
READY
```

During operation, messages such as the following can appear:

```text
QR SCANNED
QR SAVED
OPEN
DOOR REALLY OPENED
CLOSED
LOCKED
BT START
PRINT
DONE
```

Memory monitoring messages are also displayed during important operations.

## Installation

### 1. Clone the Repository

Open a terminal in VS Code:

```bash
git clone https://github.com/USERNAME/REPOSITORY.git
```

Enter the repository:

```bash
cd REPOSITORY
```

### 2. Open the Project

Open the project folder using VS Code or Arduino IDE.

### 3. Install ESP32 Board Support

Install the ESP32 Arduino Core through the Arduino IDE Board Manager.

Select the appropriate ESP32-CAM board, such as:

```text
AI Thinker ESP32-CAM
```

### 4. Install Required Libraries

Install:

```text
ESP32QRCodeReader
Adafruit Thermal Printer Library
BluetoothSerial
```

`BluetoothSerial` is provided through the ESP32 Arduino Core.

### 5. Configure the Hardware

Verify:

* GPIO assignments
* Solenoid driver circuit
* Limit switch wiring
* Buzzer wiring
* Bluetooth printer MAC address
* Bluetooth PIN
* Power supply

### 6. Upload

Connect the ESP32-CAM to your programmer/USB-to-TTL adapter and upload the `.cpp` source through the Arduino environment.

## Transaction Example

Suppose three bottles are scanned:

```text
PE500XYSH
PE250K9QW
PE100P0Z1
```

The system looks them up in the product database:

```text
500 ml  → Rp 5,000
250 ml  → Rp 2,500
100 ml  → Rp 1,000
```

The calculated total becomes:

```text
TOTAL CASHBACK
Rp 8500
```

The system then prints the transaction receipt.

## Project Structure

A simple repository structure can be:

```text
ESP32-CAM-QR-Cashback/
│
├── README.md
├── ESP32-CAM-QR-Cashback.ino
│
└── LICENSE
```

If the source remains as a `.cpp` file:

```text
ESP32-CAM-QR-Cashback/
│
├── README.md
├── Kode yang ditempelkan.cpp
└── LICENSE
```

For an Arduino project, however, renaming the main source file to match the project folder is generally easier to maintain, for example:

```text
ESP32-CAM-QR-Cashback/
│
├── README.md
├── ESP32-CAM-QR-Cashback.ino
└── LICENSE
```

## Important Notes

This repository contains the current implementation of the ESP32-CAM QR cashback system.

The product database is currently compiled directly into the firmware. Adding or changing products requires modifying the `productDB` array and uploading the firmware again.

The system also depends on PSRAM being available. If PSRAM is not detected, the firmware restarts automatically.

Before using the system in an actual machine, verify the electrical characteristics of the solenoid, printer, ESP32-CAM, power supply, and driver circuitry.

## License

This project is licensed under the **MIT License**.

You may use, modify, and distribute the software according to the terms of the MIT License.

---

## Author

**Kurnia Aditya Reynaldi**

Electrical Engineer

ESP32 / Embedded System / Electronic Control / IoT

---

## Project Status

**Development / Prototype**

The software is intended for development and prototyping of an automated QR-based cashback collection system.
