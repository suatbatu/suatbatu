#include "BleTimer.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <algorithm>

#include "Settings.h"
#include "StringRun.h"
#include "TimerApp.h"
#include "config.h"

BleTimer ble;

namespace {

// Nordic UART Service. See docs/BLE_PROTOCOL.md for where these came from.
constexpr char NUS_SERVICE[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char NUS_RX[] = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";  // client writes
constexpr char NUS_TX[] = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";  // we notify

constexpr size_t FRAME_LEN = 14;
// Values start at offset 2 and each is two bytes, so a frame holds six.
constexpr uint8_t VALUES_PER_FRAME = 6;
// The reference client only accepts frame counters 10..26.
constexpr uint8_t FIRST_FRAME_ID = 10;
constexpr uint8_t LAST_FRAME_ID = 26;
constexpr uint8_t MAX_BLE_SHOTS = (LAST_FRAME_ID - FIRST_FRAME_ID + 1) * VALUES_PER_FRAME;

BLEServer* server = nullptr;
BLECharacteristic* txChar = nullptr;
volatile bool clientConnected = false;

// Inbound commands cross a task boundary: BLE writes arrive on the host task,
// and the state machine belongs to the main loop.
struct BleCommand {
  char text[28];
};
QueueHandle_t cmdQueue = nullptr;

void putBE(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v & 0xFF);
}

// Every time on the wire is big-endian centiseconds. Saturating rather than
// wrapping: a 700-second string should read as "very long", not as 1.4 s.
uint16_t centiseconds(uint32_t ms) {
  const uint32_t cs = (ms + 5) / 10;
  return static_cast<uint16_t>(std::min<uint32_t>(cs, 65535));
}

void notifyFrame(const uint8_t* frame) {
  if (!txChar || !clientConnected) return;
  txChar->setValue(const_cast<uint8_t*>(frame), FRAME_LEN);
  txChar->notify();
}

void notifyState(uint8_t code) {
  uint8_t f[FRAME_LEN] = {0};
  f[0] = 0x01;
  f[1] = code;
  notifyFrame(f);
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    clientConnected = true;
    (void)s;
  }
  void onDisconnect(BLEServer* s) override {
    clientConnected = false;
    // Without this the timer is invisible after the first client leaves.
    s->startAdvertising();
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    if (!cmdQueue) return;
    const String value = c->getValue();
    BleCommand cmd{};
    const size_t n = std::min(value.length(), sizeof(cmd.text) - 1);
    memcpy(cmd.text, value.c_str(), n);
    cmd.text[n] = '\0';
    // Never dispatch here — this is the BLE host task.
    xQueueSend(cmdQueue, &cmd, 0);
  }
};

}  // namespace

bool BleTimer::begin() {
  if (!settings.bleEnabled) {
    // Off by default: Wi-Fi and BLE share one radio, and the timer should not
    // pay a coexistence cost nobody has measured yet.
    log_i("BLE disabled in settings");
    return false;
  }

  cmdQueue = xQueueCreate(4, sizeof(BleCommand));
  if (!cmdQueue) return false;

  BLEDevice::init(settings.bleName);
  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service = server->createService(NUS_SERVICE);

  txChar = service->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());

  BLECharacteristic* rxChar = service->createCharacteristic(
      NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxChar->setCallbacks(new RxCallbacks());

  service->start();

  // The 128-bit service UUID and a 20-character name do not both fit in the
  // 31-byte advertising packet, so the name goes in the scan response. Clients
  // filter on the name, so it has to be somewhere a scan will see it.
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  BLEDevice::startAdvertising();

  active_ = true;
  log_i("BLE advertising as \"%s\"", settings.bleName);
  return true;
}

bool BleTimer::connected() const { return clientConnected; }

void BleTimer::onTimerEvent(const JsonDocument& doc) {
  if (!active_) return;
  const char* type = doc["type"] | "";

  if (strcmp(type, "beep") == 0) {
    notifyState(0x05);
    return;
  }

  if (strcmp(type, "end") == 0) {
    notifyState(0x08);
    return;
  }

  if (strcmp(type, "shot") == 0) {
    const uint16_t index = doc["index"] | 0;  // 0-based here, 1-based on the wire
    uint8_t f[FRAME_LEN] = {0};
    f[0] = 0x01;
    f[1] = 0x03;
    putBE(f + 2, index + 1);
    putBE(f + 4, centiseconds(doc["atMs"] | 0));
    putBE(f + 6, centiseconds(doc["splitMs"] | 0));
    putBE(f + 8, centiseconds(app.run().firstShotMs()));
    // f[10..11] is the field the reference implementation marks unknown.
    putBE(f + 12, static_cast<uint16_t>(app.run().id() & 0xFFFF));
    notifyFrame(f);
  }
}

void BleTimer::sendStringFrames(const StringRun& run) {
  const uint8_t total = std::min<uint8_t>(run.count(), MAX_BLE_SHOTS);
  if (total == 0) {
    // Still send frame 10 with a zero count, so a client that asked gets an
    // answer meaning "nothing here" rather than silence it has to time out on.
    uint8_t f[FRAME_LEN] = {0};
    f[0] = FIRST_FRAME_ID;
    notifyFrame(f);
    return;
  }

  uint8_t sent = 0;
  uint8_t frameId = FIRST_FRAME_ID;
  while (sent < total && frameId <= LAST_FRAME_ID) {
    const uint8_t n = std::min<uint8_t>(VALUES_PER_FRAME, total - sent);
    uint8_t f[FRAME_LEN] = {0};
    f[0] = frameId++;
    f[1] = n;
    for (uint8_t i = 0; i < n; i++) {
      putBE(f + 2 + i * 2, centiseconds(run.shotMs(sent + i)));
    }
    notifyFrame(f);
    sent += n;
  }
}

void BleTimer::handleCommand(const char* cmd) {
  if (strncmp(cmd, "COM START", 9) == 0) {
    app.postStart();
    return;
  }
  if (strncmp(cmd, "REQ STRING HEX", 14) == 0) {
    sendStringFrames(app.run());
    return;
  }
  if (strncmp(cmd, "SET SENSITIVITY", 15) == 0) {
    const int value = atoi(cmd + 15);
    if (value >= 1 && value <= 10) {
      settings.profile().sensitivity = static_cast<uint8_t>(value);
      settings.save();
    }
    return;
  }
  // REQ SCREEN HEX and anything else: the reply format is not established from
  // public sources, so we stay quiet rather than invent one.
  log_i("BLE: ignoring unknown command \"%s\"", cmd);
}

void BleTimer::loop() {
  if (!active_ || !cmdQueue) return;
  BleCommand cmd;
  while (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE) {
    handleCommand(cmd.text);
  }
}
