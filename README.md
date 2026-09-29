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

Electrical Engineer | Embedded Systems | Control Systems | Electronics R&D

Contributions, issues, and pull requests are welcome.

---

## Project Status

**Development / Prototype**

The software is intended for development and prototyping of an automated QR-based cashback collection system.
