/*************************************************
 * ESP32-CAM QR + Bluetooth Printer
 * PRODUCT DATABASE + PSRAM
 * STABLE + LOW HEAP
 *************************************************/

#include <Arduino.h>
#include <ESP32QRCodeReader.h>
#include <BluetoothSerial.h>
#include "Adafruit_Thermal.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

/* ================= PIN ================= */
#define SOLENOID_PIN   14
#define BUZZER_PIN     13
#define LIMIT_PIN      15
#define LED_BT         4

/* ================= CONFIG ================= */
#define MAX_SCAN        30
#define QR_LEN           32
#define LOCK_DELAY     2000
#define PRINT_DELAY   10000
#define LOW_HEAP_LIMIT 30000
#define SCAN_DELAY      300   // jeda antar scan (ms)

/* ================= PRINTER CONFIG ================= */
#define QR_SIZE        12
#define QR_ECC         0x30
#define FEED_TOP        1
#define FEED_BOTTOM     4
#define ALIGN_HEADER  'C'
#define ALIGN_BODY    'L'
#define ALIGN_QR      'C'

/* ================= PRODUCT DB ================= */
#define DB_SIZE 10

struct Product {
  char     qr[12];
  char     name[20];
  char     type[10];
  uint16_t size;
  uint32_t cashbackk;
};

const Product productDB[DB_SIZE] PROGMEM = {
  {"PE500XYSH", "LIFEBUOY",  "PET",   500,  5000},
  {"HD500AB12", "SUNLIGHT",  "HDPE",  500,  5000},
  {"PE250K9QW", "AQUA",      "PET",   250,  2500},
  {"HD250LM88", "SOKLIN",    "HDPE",  250,  2500},
  {"PE100P0Z1", "NUTRISARI", "PET",   100,  1000},
  {"HD100TT77", "RINSO",     "HDPE",  100,  1000},
  {"PE1L0QQQ9", "TEH BOTOL", "PET",  1000, 10000},
  {"HD1L0W2E3", "MIZONE",    "HDPE", 1000, 10000},
  {"PE2L5MNBV", "AIR MINUM", "PET",  2500, 25000},
  {"HD2L5R4T5", "SABUN",     "HDPE", 2500, 25000},
};

/* ================= CAMERA ================= */
ESP32QRCodeReader reader(CAMERA_MODEL_AI_THINKER);

/* ================= BLUETOOTH ================= */
BluetoothSerial   SerialBT;
Adafruit_Thermal   printer(&SerialBT);
uint8_t printerMAC[6] = {0x06, 0x09, 0x36, 0x63, 0xAC, 0x30};
const char *btPin = "0000";

/* ================= PSRAM ================= */
char    **scanList;
Product  *scanProduct;
uint16_t  scanCount = 0;

/* ================= VAR ================= */
volatile bool qrEnable      = false;
volatile bool qrReady       = false;
char          lastQR[QR_LEN];
unsigned long closeTime     = 0;
unsigned long lockTime      = 0;
bool          doorHasOpened = false;
unsigned long lastScanTime  = 0;

/* ================= STATE ================= */
enum SystemState {
  WAIT_QR,
  DOOR_OPEN,
  DOOR_CLOSE_WAIT,
  PRINTING
};
SystemState state = WAIT_QR;

/* ================= MEMORY ================= */
void printMemory(const char *tag) {
  Serial.printf("[MEM] %s | Heap:%d KB | PSRAM:%d KB\n",
                tag, ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
}

/* =========================================================
 * BUZZER SYSTEM
 * ========================================================= */
bool          buzzerActive  = false;
bool          buzzerState   = false;
unsigned long buzzerStart   = 0;
unsigned long buzzerOnTime  = 0;
unsigned long buzzerOffTime = 0;
int           buzzerRepeat  = 0;
int           buzzerCount   = 0;

/* Start pattern buzzer */
void buzzerStartPattern(int onTime, int offTime, int repeat) {
  buzzerOnTime  = onTime;
  buzzerOffTime = offTime;
  buzzerRepeat  = repeat;
  buzzerCount   = 0;
  buzzerState   = true;
  buzzerActive  = true;
  buzzerStart   = millis();
  digitalWrite(BUZZER_PIN, LOW); // ON (aktif LOW)
}

/* Update buzzer (WAJIB dipanggil di loop) */
void buzzerUpdate() {
  if (!buzzerActive) return;

  unsigned long now = millis();

  if (buzzerState) {
    // sedang ON
    if (now - buzzerStart >= buzzerOnTime) {
      digitalWrite(BUZZER_PIN, HIGH); // OFF
      buzzerState = false;
      buzzerStart = now;
    }
  } else {
    // sedang OFF
    if (now - buzzerStart >= buzzerOffTime) {
      buzzerCount++;
      if (buzzerRepeat > 0 && buzzerCount >= buzzerRepeat) {
        buzzerActive = false;
        digitalWrite(BUZZER_PIN, HIGH);
        return;
      }
      digitalWrite(BUZZER_PIN, LOW); // ON
      buzzerState = true;
      buzzerStart = now;
    }
  }
}

/* ================= PRESET BEEP ================= */
void beepON()    { buzzerStartPattern(2000,    0, 1); } // Boot
void beepOK()    { buzzerStartPattern(1000,    0, 1); } // QR OK
void beepBT()    { buzzerStartPattern(1000, 1000, 2); } // BT Error
void beepError() { buzzerStartPattern( 300,  300, 5); } // QR Error
void beepDone()  { buzzerStartPattern(2000,    0, 1); } // Print Done

/* ================= DOOR & QR UTILS ================= */
void unlockDoor() { digitalWrite(SOLENOID_PIN, LOW); }
void lockDoor()   { digitalWrite(SOLENOID_PIN, HIGH); }

void resetQR() {
  qrReady = false;
  memset(lastQR, 0, QR_LEN);
}

/* ================= LIMIT SWITCH ================= */
bool doorClosedStable() {
  static unsigned long t = 0;
  if (digitalRead(LIMIT_PIN) == LOW) {
    if (millis() - t > 300) return true;
  } else {
    t = millis();
  }
  return false;
}

/* ================= PRODUCT LOOKUP ================= */
bool findProduct(const char *qr, Product *out) {
  Product temp;
  for (int i = 0; i < DB_SIZE; i++) {
    memcpy_P(&temp, &productDB[i], sizeof(Product));
    if (strcmp(temp.qr, qr) == 0) {
      memcpy(out, &temp, sizeof(Product));
      return true;
    }
  }
  return false;
}

/* ================= SAVE ================= */
bool saveQR(const char *qr) {
  if (scanCount >= MAX_SCAN) return false;

  Product temp;
  if (!findProduct(qr, &temp)) {
    Serial.println("QR NOT IN DB - IGNORED");
    beepError();
    return false;
  }

  strncpy(scanList[scanCount], qr, QR_LEN - 1);
  scanList[scanCount][QR_LEN - 1] = 0;
  memcpy(&scanProduct[scanCount], &temp, sizeof(Product));
  scanCount++;

  Serial.println("QR SAVED");
  printMemory("SAVE");
  return true;
}

void clearAll() {
  for (int i = 0; i < MAX_SCAN; i++) {
    memset(scanList[i], 0, QR_LEN);
  }
  scanCount     = 0;
  doorHasOpened = false;
  lastScanTime  = 0;
  resetQR();
  printMemory("CLEAR");
}

/* ================= CAMERA ================= */
void startCamera() {
  Serial.println("CAMERA ON");
  reader.setup();
  reader.beginOnCore(1);
  qrEnable = true;
  delay(300);
  printMemory("CAM ON");
}

void stopCamera() {
  Serial.println("CAMERA OFF");
  qrEnable = false;
  reader.end();
  delay(300);
  printMemory("CAM OFF");
}

/* ================= PRINTER ================= */
void printerInit() {
  printer.begin();
  printer.setDefault();
  printer.justify(ALIGN_HEADER);
  printer.feed(FEED_TOP);
}

/* ================= QR PRINT (raw ESC/POS) ================= */
void printQR_Config(const char *data) {
  printer.justify(ALIGN_QR);

  // Set QR module size
  printer.write(0x1D); printer.write(0x28); printer.write(0x6B);
  printer.write(0x03); printer.write(0x00);
  printer.write(0x31); printer.write(0x43); printer.write(QR_SIZE);

  // Set QR error correction level
  printer.write(0x1D); printer.write(0x28); printer.write(0x6B);
  printer.write(0x03); printer.write(0x00);
  printer.write(0x31); printer.write(0x45); printer.write(QR_ECC);

  // Store QR data
  int len = strlen(data) + 3;
  printer.write(0x1D); printer.write(0x28); printer.write(0x6B);
  printer.write(len & 0xFF); printer.write((len >> 8) & 0xFF);
  printer.write(0x31); printer.write(0x50); printer.write(0x30);
  printer.print(data);

  // Print QR
  printer.write(0x1D); printer.write(0x28); printer.write(0x6B);
  printer.write(0x03); printer.write(0x00);
  printer.write(0x31); printer.write(0x51); printer.write(0x30);

  printer.feed(1);
}

/* ================= PRINT RECEIPT ================= */
void printAll() {
  printMemory("PRINT");

  if (!SerialBT.connected()) {
    Serial.println("BT DISCONNECTED");
    beepBT();
    return;
  }

  printerInit();
  printer.println("===== TOKO ABC =====");
  printer.println("   STRUK CASHBACK   ");
  printer.feed(1);
  printer.justify(ALIGN_BODY);

  uint32_t totalCashback = 0;

  for (int i = 0; i < scanCount; i++) {
    Product *p = &scanProduct[i];

    printQR_Config(scanList[i]);
    printer.println(p->name);
    printer.printf("Type     : %s\n", p->type);
    printer.printf("Size     : %d ml\n", p->size);
    printer.printf("Cashback : Rp %lu\n", p->cashbackk);
    printer.println("---------------------");

    totalCashback += p->cashbackk;
    printer.feed(1);
  }

  printer.justify(ALIGN_HEADER);
  printer.println("TOTAL CASHBACK");
  printer.printf("Rp %lu\n", totalCashback);
  printer.feed(1);
  printer.println("Terima Kasih");
  printer.feed(FEED_BOTTOM);

  printMemory("DONE");
}

/* ================= QR SCAN TASK (Core 1) ================= */
void qrTaskFunc(void *pv) {
  struct QRCodeData data;

  while (true) {
    if (!qrEnable) {
      vTaskDelay(500 / portTICK_PERIOD_MS);
      continue;
    }

    if (reader.receiveQrCode(&data, 200)) {
      if (data.valid) {
        unsigned long now = millis();

        // cooldown filter antar scan
        if (now - lastScanTime < SCAN_DELAY) {
          continue;
        }
        lastScanTime = now;

        strncpy(lastQR, (char *)data.payload, QR_LEN - 1);
        lastQR[QR_LEN - 1] = 0;
        qrReady = true;

        Serial.println("QR SCANNED");
        beepOK();
        printMemory("QR");
      }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/* ================= BLUETOOTH ================= */
void startBT() {
  Serial.println("BT START");
  digitalWrite(LED_BT, HIGH);

  SerialBT.setPin(btPin, strlen(btPin));
  SerialBT.begin("ESP32-CAM", true);
  delay(500);

  SerialBT.connect(printerMAC);
  delay(1000);

  printer.begin();
  printMemory("BT ON");
}

void stopBT() {
  Serial.println("BT STOP");
  digitalWrite(LED_BT, LOW);

  SerialBT.disconnect();
  SerialBT.end();
  delay(500);

  printMemory("BT OFF");
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("SYSTEM BOOT");

  if (!psramFound()) {
    Serial.println("NO PSRAM");
    ESP.restart();
  }

  scanList    = (char **)heap_caps_malloc(MAX_SCAN * sizeof(char *), MALLOC_CAP_SPIRAM);
  scanProduct = (Product *)heap_caps_malloc(MAX_SCAN * sizeof(Product), MALLOC_CAP_SPIRAM);

  for (int i = 0; i < MAX_SCAN; i++) {
    scanList[i] = (char *)heap_caps_malloc(QR_LEN, MALLOC_CAP_SPIRAM);
  }

  printMemory("PSRAM OK");

  /* QR TASK on core 1 */
  xTaskCreatePinnedToCore(
    qrTaskFunc, "QR_TASK", 4096, NULL, 1, NULL, 1
  );

  clearAll();
  startCamera();

  /* SAFE BOOT & RESET STATE */
  gpio_reset_pin((gpio_num_t)13);
  gpio_reset_pin((gpio_num_t)14);
  gpio_reset_pin((gpio_num_t)15);

  pinMode(SOLENOID_PIN, OUTPUT);
  pinMode(BUZZER_PIN,   OUTPUT);
  pinMode(LIMIT_PIN,    INPUT_PULLUP);
  pinMode(LED_BT,       OUTPUT);

  beepON();
  lockDoor();
  delay(500);

  Serial.println("READY");
}

/* ================= LOOP ================= */
void loop() {
  buzzerUpdate(); // WAJIB dipanggil setiap loop

  if (ESP.getFreeHeap() < LOW_HEAP_LIMIT) {
    Serial.println("LOW HEAP WARNING");
    printMemory("LOW");
    ESP.restart();
  }

  switch (state) {

    case WAIT_QR:
      if (qrReady) {
        saveQR(lastQR);
        qrReady = false;
        unlockDoor();
        Serial.println("OPEN");
        doorHasOpened = false;
        state = DOOR_OPEN;
      }
      break;

    case DOOR_OPEN:
      if (!doorHasOpened) {
        if (digitalRead(LIMIT_PIN) == HIGH) {
          doorHasOpened = true;
          Serial.println("DOOR REALLY OPENED");
        }
        break;
      }

      if (qrReady) {
        qrReady = false;
        saveQR(lastQR);
      }

      if (doorClosedStable()) {
        closeTime = millis();
        Serial.println("CLOSED");
        stopCamera();
        state = DOOR_CLOSE_WAIT;
      }
      break;

    case DOOR_CLOSE_WAIT:
      if (millis() - closeTime >= LOCK_DELAY) {
        lockDoor();
        lockTime = millis();
        Serial.println("LOCKED");
        startBT();
        state = PRINTING;
      }
      break;

    case PRINTING:
      if (millis() - lockTime >= PRINT_DELAY) {
        printAll();
        beepDone();
        stopBT();
        clearAll();
        Serial.println("RESTART SYSTEM");
        delay(1000);
        ESP.restart();
      }
      break;
  }

  delay(5); // vTaskDelay(5 / portTICK_PERIOD_MS);
}
