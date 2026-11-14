/*
 * HexBus – a busz-szervók kezelése ESP32-S3-ról
 * Half-duplex UART: TX -> 74LVC1G126 (OE = RTS) -> DATA, RX <- DATA (1 kOhm)
 * Protokoll: docs/busz-protokoll.md
 */
#pragma once
#include <Arduino.h>

namespace Reg {
  constexpr uint8_t MODEL = 0x00, FW_VERSION = 0x02, ID = 0x05, BAUD = 0x06,
                    RETURN_DELAY = 0x07, MIN_ANGLE = 0x08, MAX_ANGLE = 0x0A,
                    POT_ZERO = 0x0C, DIRECTION = 0x0E, KP = 0x10, KI = 0x12, KD = 0x14,
                    MAX_PWM = 0x16, CURRENT_LIMIT = 0x18, MAX_TEMP = 0x1A,
                    WATCHDOG = 0x1B, POT_SCALE = 0x1C,
                    TORQUE = 0x28, LED = 0x29, GOAL_POS = 0x2A, MOVE_TIME = 0x2C,
                    MAX_SPEED = 0x2E, PRESENT_POS = 0x38, PRESENT_SPEED = 0x3A,
                    PRESENT_CURRENT = 0x3C, VOLTAGE = 0x3E, TEMPERATURE = 0x3F,
                    ERROR = 0x40, MOVING = 0x41;
}

namespace Ins {
  constexpr uint8_t PING = 0x01, READ = 0x02, WRITE = 0x03, REG_WRITE = 0x04,
                    ACTION = 0x05, FACTORY_RESET = 0x06,
                    SYNC_READ = 0x82, SYNC_WRITE = 0x83;
}

constexpr uint8_t BROADCAST_ID = 0xFE;
constexpr uint8_t STATUS_LEN   = 10;     // 0x38..0x41

struct ServoStatus {
  int16_t pos_x10;      // 0,1 fok
  int16_t speed;        // fok/s
  int16_t current_ma;
  uint8_t voltage_x10;
  uint8_t temp_c;
  uint8_t error;
  uint8_t moving;
};

class HexBus {
public:
  void begin(HardwareSerial &ser, int rxPin, int txPin, int rtsPin, uint32_t baud = 1000000);

  bool ping(uint8_t id, uint8_t *err = nullptr);
  bool read(uint8_t id, uint8_t addr, uint8_t len, uint8_t *out, uint8_t *err = nullptr);
  bool write(uint8_t id, uint8_t addr, const uint8_t *data, uint8_t len, uint8_t *err = nullptr);
  bool write8(uint8_t id, uint8_t addr, uint8_t v)  { return write(id, addr, &v, 1); }
  bool write16(uint8_t id, uint8_t addr, int16_t v) {
    uint8_t d[2] = { uint8_t(v), uint8_t(uint16_t(v) >> 8) }; return write(id, addr, d, 2);
  }
  bool read16(uint8_t id, uint8_t addr, int16_t &v);
  bool factoryReset(uint8_t id);

  // Egy csomagban több szervó 16 bites regisztere (pl. célpozíció)
  void syncWrite16(uint8_t addr, const uint8_t *ids, const int16_t *vals, uint8_t n);
  // Pozíció + mozgásidő együtt (0x2A..0x2D)
  void syncMove(const uint8_t *ids, const int16_t *pos_x10, uint16_t time_ms, uint8_t n);
  // Állapotblokk (0x38..0x41) minden megadott szervótól; visszaadja a sikeres válaszok számát
  uint8_t syncReadStatus(const uint8_t *ids, uint8_t n, ServoStatus *out, bool *ok);

  uint8_t scan(uint8_t *found, uint8_t maxFound, uint8_t firstId = 1, uint8_t lastId = 253);

private:
  HardwareSerial *s = nullptr;
  uint32_t byteUs = 10;
  void send(uint8_t id, uint8_t ins, const uint8_t *p, uint16_t np);
  bool receive(uint8_t expectId, uint8_t *data, uint8_t len, uint8_t *err, uint32_t timeoutUs, uint8_t *idOut = nullptr);
};
