#include "HexBus.h"
#include "driver/uart.h"

static uint8_t txbuf[260];
static uint16_t txlen;
static uint8_t pre[260];       // adás után beolvasott bájtok (visszhang vagy már válasz)
static uint16_t preLen, prePos;

void HexBus::begin(HardwareSerial &ser, int rxPin, int txPin, int rtsPin, uint32_t baud) {
  s = &ser;
  byteUs = 10000000UL / baud;                     // 10 bit / bájt
  s->setRxBufferSize(1024);
  s->begin(baud, SERIAL_8N1, rxPin, txPin);
  s->setPins(rxPin, txPin, -1, rtsPin);           // RTS -> puffer OE
  s->setMode(UART_MODE_RS485_HALF_DUPLEX);        // RTS adás alatt aktív
}

void HexBus::send(uint8_t id, uint8_t ins, const uint8_t *p, uint16_t np) {
  while (s->available()) s->read();               // régi szemét ki
  uint8_t len = uint8_t(np + 2);
  uint8_t sum = id + len + ins;
  txlen = 0;
  txbuf[txlen++] = 0xFF; txbuf[txlen++] = 0xFF;
  txbuf[txlen++] = id;   txbuf[txlen++] = len; txbuf[txlen++] = ins;
  for (uint16_t i = 0; i < np; i++) { txbuf[txlen++] = p[i]; sum += p[i]; }
  txbuf[txlen++] = uint8_t(~sum);
  s->write(txbuf, txlen);
  s->flush();                                     // megvárja az adás végét

  // Visszhang: az RX a buszon ül, így a saját csomagunk is visszajön.
  preLen = prePos = 0;
  uint32_t t0 = micros(), lim = 300 + byteUs * 2;
  while (preLen < txlen && micros() - t0 < lim) {
    if (s->available()) pre[preLen++] = s->read();
  }
  if (preLen == txlen && memcmp(pre, txbuf, txlen) == 0) preLen = 0;  // visszhang eldobva
}

// Egy válaszcsomag beolvasása. expectId = 0xFF: bármelyik ID elfogadva (az ID-t idOut adja vissza).
bool HexBus::receive(uint8_t expectId, uint8_t *data, uint8_t len, uint8_t *err, uint32_t timeoutUs, uint8_t *idOut) {
  uint8_t st = 0, rlen = 0, rerr = 0, n = 0, sum = 0, rid = 0;
  uint32_t t0 = micros();
  while (micros() - t0 < timeoutUs) {
    int c;
    if (prePos < preLen) c = pre[prePos++];
    else if (s->available()) c = s->read();
    else continue;
    uint8_t b = uint8_t(c);
    switch (st) {
      case 0: st = (b == 0xFF) ? 1 : 0; break;
      case 1: st = (b == 0xFF) ? 2 : 0; break;
      case 2: if (b == 0xFF) break;
              if (expectId != 0xFF && b != expectId) { st = 0; break; }
              rid = b; sum = b; st = 3; break;
      case 3: rlen = b; sum += b;
              if (rlen != uint8_t(len + 2)) { st = 0; break; }
              st = 4; break;
      case 4: rerr = b; sum += b; n = 0; st = (len == 0) ? 6 : 5; break;
      case 5: data[n++] = b; sum += b; if (n == len) st = 6; break;
      case 6:
        if (uint8_t(~sum) != b) { st = 0; break; }
        if (err) *err = rerr;
        if (idOut) *idOut = rid;
        return true;
    }
  }
  return false;
}

bool HexBus::ping(uint8_t id, uint8_t *err) {
  send(id, Ins::PING, nullptr, 0);
  return receive(id, nullptr, 0, err, 2000);
}

bool HexBus::read(uint8_t id, uint8_t addr, uint8_t len, uint8_t *out, uint8_t *err) {
  uint8_t p[2] = { addr, len };
  send(id, Ins::READ, p, 2);
  return receive(id, out, len, err, 2000 + byteUs * (len + 6));
}

bool HexBus::read16(uint8_t id, uint8_t addr, int16_t &v) {
  uint8_t d[2];
  if (!read(id, addr, 2, d)) return false;
  v = int16_t(d[0] | (d[1] << 8));
  return true;
}

bool HexBus::write(uint8_t id, uint8_t addr, const uint8_t *data, uint8_t len, uint8_t *err) {
  uint8_t p[130];
  p[0] = addr;
  memcpy(p + 1, data, len);
  send(id, Ins::WRITE, p, len + 1);
  if (id == BROADCAST_ID) return true;
  return receive(id, nullptr, 0, err, 2000);
}

bool HexBus::factoryReset(uint8_t id) {
  send(id, Ins::FACTORY_RESET, nullptr, 0);
  return receive(id, nullptr, 0, nullptr, 2000);
}

void HexBus::syncWrite16(uint8_t addr, const uint8_t *ids, const int16_t *vals, uint8_t n) {
  uint8_t p[2 + 3 * 40];
  uint16_t k = 0;
  p[k++] = addr; p[k++] = 2;
  for (uint8_t i = 0; i < n && i < 40; i++) {
    p[k++] = ids[i];
    p[k++] = uint8_t(vals[i]);
    p[k++] = uint8_t(uint16_t(vals[i]) >> 8);
  }
  send(BROADCAST_ID, Ins::SYNC_WRITE, p, k);
}

void HexBus::syncMove(const uint8_t *ids, const int16_t *pos_x10, uint16_t time_ms, uint8_t n) {
  uint8_t p[2 + 5 * 24];
  uint16_t k = 0;
  p[k++] = Reg::GOAL_POS; p[k++] = 4;
  for (uint8_t i = 0; i < n && i < 24; i++) {
    p[k++] = ids[i];
    p[k++] = uint8_t(pos_x10[i]);
    p[k++] = uint8_t(uint16_t(pos_x10[i]) >> 8);
    p[k++] = uint8_t(time_ms);
    p[k++] = uint8_t(time_ms >> 8);
  }
  send(BROADCAST_ID, Ins::SYNC_WRITE, p, k);
}

uint8_t HexBus::syncReadStatus(const uint8_t *ids, uint8_t n, ServoStatus *out, bool *ok) {
  uint8_t p[2 + 40];
  uint8_t k = 0;
  p[k++] = Reg::PRESENT_POS; p[k++] = STATUS_LEN;
  for (uint8_t i = 0; i < n && i < 40; i++) p[k++] = ids[i];
  send(BROADCAST_ID, Ins::SYNC_READ, p, k);

  // Minden szervó a saját időrésében válaszol; a válaszokat ID alapján rendeljük a listához,
  // így egy hiányzó szervó nem csúsztatja el a többit.
  uint32_t slot = byteUs * (6 + STATUS_LEN) + 20;
  uint32_t t0 = micros(), total = slot * n + 1000;
  uint8_t good = 0;
  for (uint8_t i = 0; i < n; i++) ok[i] = false;
  while (good < n) {
    uint32_t used = micros() - t0;
    if (used >= total) break;
    uint8_t d[STATUS_LEN], e = 0, rid = 0;
    if (!receive(0xFF, d, STATUS_LEN, &e, total - used, &rid)) break;
    for (uint8_t i = 0; i < n; i++) {
      if (ids[i] != rid || ok[i]) continue;
      ok[i] = true;
      out[i].pos_x10     = int16_t(d[0] | (d[1] << 8));
      out[i].speed       = int16_t(d[2] | (d[3] << 8));
      out[i].current_ma  = int16_t(d[4] | (d[5] << 8));
      out[i].voltage_x10 = d[6];
      out[i].temp_c      = d[7];
      out[i].error       = d[8];
      out[i].moving      = d[9];
      good++;
      break;
    }
  }
  return good;
}

uint8_t HexBus::scan(uint8_t *found, uint8_t maxFound, uint8_t firstId, uint8_t lastId) {
  uint8_t n = 0;
  for (uint16_t id = firstId; id <= lastId && n < maxFound; id++) {
    if (ping(uint8_t(id))) found[n++] = uint8_t(id);
  }
  return n;
}
