/*
 * HexMaster – Hexpodrobot központi vezérlő (ESP32-S3)
 *
 * Első verzió: busz-szervó beállító és tesztprogram az Arduino IDE soros monitorához
 * (115200 baud, sorvége: "Új sor"). A járásvezérlés erre épül majd.
 *
 * Arduino IDE: alaplap "ESP32S3 Dev Module", arduino-esp32 3.x
 * Bekötés:
 *   GPIO17 (TX)  -> 74LVC1G126 A bemenet, kimenete a busz DATA vonalára
 *   GPIO8  (RTS) -> 74LVC1G126 OE
 *   GPIO18 (RX)  <- busz DATA, 1 kOhm soros ellenállással
 *   DATA vonalon 4,7 kOhm felhúzó 3,3 V-ra
 *
 * Parancsok (ID = szervó azonosító, szög fokban, tizedes ponttal):
 *   sugo                     – parancslista
 *   scan                     – szervók keresése a buszon (1..99)
 *   ping <ID>
 *   id <régi> <új>           – ID átírása (nyomaték legyen kikapcsolva)
 *   nyomatek <ID> <0|1>
 *   mozgat <ID> <szög> [ms]  – pl. mozgat 12 45.5 500
 *   allapot <ID>             – pozíció, áram, feszültség, hőm., hibák
 *   olvas <ID> <cím> <hossz> – nyers regiszterolvasás (cím lehet 0x..)
 *   ir <ID> <cím> <érték> [1|2] – regiszter írása 1 vagy 2 bájton
 *   nulla <ID>               – a mostani állás legyen a 0 fok (nyomaték ki)
 *   irany <ID> <0|1>         – forgásirány megfordítása
 *   reset <ID>               – gyári beállítások, ID = 1
 *   lab <láb> <x> <y> <z> [ms] – inverz kinematika egy lábra (mm), pl. lab 1 91 0 -70 800
 *   figyel <láb|0>           – a láb 3 szervójának állapota folyamatosan (0 = ki)
 */
#include "HexBus.h"
#include "LegIK.h"

constexpr int BUS_RX  = 18;
constexpr int BUS_TX  = 17;
constexpr int BUS_RTS = 8;
constexpr int32_t ADC_FULL_SUM = 16368;   // a szervó firmware-rel egyezik

HexBus bus;
LegGeom geom;
uint8_t watchLeg = 0;
uint32_t lastWatch = 0;

static long toInt(const String &s) {
  return strtol(s.c_str(), nullptr, 0);   // 10-es vagy 0x.. alak
}

static int split(const String &line, String *tok, int maxTok) {
  int n = 0, i = 0;
  while (i < (int)line.length() && n < maxTok) {
    while (i < (int)line.length() && line[i] == ' ') i++;
    int j = i;
    while (j < (int)line.length() && line[j] != ' ') j++;
    if (j > i) tok[n++] = line.substring(i, j);
    i = j;
  }
  return n;
}

static void printErrors(uint8_t e) {
  if (!e) { Serial.print("nincs"); return; }
  const char *names[8] = { "feszültség", "szöghatár", "túlmelegedés", "tartomány",
                           "ellenőrzőösszeg", "túlterhelés", "utasítás", "meghajtó/watchdog" };
  bool first = true;
  for (int b = 0; b < 8; b++) if (e & (1 << b)) {
    if (!first) Serial.print(", ");
    Serial.print(names[b]); first = false;
  }
}

static void printStatus(uint8_t id, const ServoStatus &st) {
  Serial.printf("ID %3u: %7.1f fok  %5d fok/s  %5d mA  %4.1f V  %3u C  mozog:%u  hiba: ",
                id, st.pos_x10 / 10.0, st.speed, st.current_ma,
                st.voltage_x10 / 10.0, st.temp_c, st.moving);
  printErrors(st.error);
  Serial.println();
}

static bool readStatus(uint8_t id, ServoStatus &st) {
  uint8_t d[STATUS_LEN];
  if (!bus.read(id, Reg::PRESENT_POS, STATUS_LEN, d)) return false;
  st.pos_x10 = int16_t(d[0] | (d[1] << 8));
  st.speed = int16_t(d[2] | (d[3] << 8));
  st.current_ma = int16_t(d[4] | (d[5] << 8));
  st.voltage_x10 = d[6]; st.temp_c = d[7]; st.error = d[8]; st.moving = d[9];
  return true;
}

static void help() {
  Serial.println(F("Parancsok: sugo, scan, ping, id, nyomatek, mozgat, allapot, olvas, ir,"));
  Serial.println(F("           nulla, irany, reset, lab, figyel  (részletek a forrás elején)"));
}

static void handle(const String &line) {
  String t[8];
  int n = split(line, t, 8);
  if (n == 0) return;
  String c = t[0];
  c.toLowerCase();

  if (c == "sugo" || c == "help") { help(); return; }

  if (c == "scan") {
    uint8_t f[100];
    uint8_t k = bus.scan(f, 100, 1, 99);
    Serial.printf("%u szervó található:", k);
    for (uint8_t i = 0; i < k; i++) Serial.printf(" %u", f[i]);
    Serial.println();
    return;
  }

  if (n < 2) { Serial.println("Hiányzó ID."); return; }
  uint8_t id = uint8_t(toInt(t[1]));

  if (c == "ping") {
    uint8_t e = 0;
    if (bus.ping(id, &e)) { Serial.printf("ID %u válaszol, hiba: ", id); printErrors(e); Serial.println(); }
    else Serial.printf("ID %u nem válaszol.\n", id);
  }
  else if (c == "id" && n >= 3) {
    uint8_t nid = uint8_t(toInt(t[2]));
    bus.write8(id, Reg::TORQUE, 0);
    Serial.println(bus.write8(id, Reg::ID, nid) ? "ID átírva (flash-be mentés kb. 0,1 s múlva)." : "Nincs válasz.");
  }
  else if (c == "nyomatek" && n >= 3) {
    Serial.println(bus.write8(id, Reg::TORQUE, toInt(t[2]) ? 1 : 0) ? "OK" : "Nincs válasz.");
  }
  else if (c == "mozgat" && n >= 3) {
    int16_t pos = int16_t(lroundf(t[2].toFloat() * 10));
    uint16_t ms = n >= 4 ? uint16_t(toInt(t[3])) : 0;
    uint8_t d[4] = { uint8_t(pos), uint8_t(uint16_t(pos) >> 8), uint8_t(ms), uint8_t(ms >> 8) };
    bus.write8(id, Reg::TORQUE, 1);
    uint8_t e = 0;
    if (bus.write(id, Reg::GOAL_POS, d, 4, &e)) { Serial.print("OK, hiba: "); printErrors(e); Serial.println(); }
    else Serial.println("Nincs válasz.");
  }
  else if (c == "allapot") {
    ServoStatus st;
    if (readStatus(id, st)) printStatus(id, st); else Serial.println("Nincs válasz.");
  }
  else if (c == "olvas" && n >= 4) {
    uint8_t a = uint8_t(toInt(t[2])), len = uint8_t(toInt(t[3])), d[70];
    if (len > 66) len = 66;
    if (bus.read(id, a, len, d)) {
      for (uint8_t i = 0; i < len; i++) Serial.printf("0x%02X: %3u (0x%02X)\n", a + i, d[i], d[i]);
    } else Serial.println("Nincs válasz.");
  }
  else if (c == "ir" && n >= 4) {
    uint8_t a = uint8_t(toInt(t[2]));
    long v = toInt(t[3]);
    bool two = n >= 5 && toInt(t[4]) == 2;
    uint8_t e = 0;
    uint8_t d[2] = { uint8_t(v), uint8_t(v >> 8) };
    if (bus.write(id, a, d, two ? 2 : 1, &e)) { Serial.print("OK, hiba: "); printErrors(e); Serial.println(); }
    else Serial.println("Nincs válasz.");
  }
  else if (c == "nulla") {
    int16_t pos, zero, scale; uint8_t dir;
    bus.write8(id, Reg::TORQUE, 0);
    delay(5);
    if (!bus.read16(id, Reg::PRESENT_POS, pos) || !bus.read16(id, Reg::POT_ZERO, zero) ||
        !bus.read16(id, Reg::POT_SCALE, scale) || !bus.read(id, Reg::DIRECTION, 1, &dir) || scale == 0) {
      Serial.println("Nincs válasz."); return;
    }
    int32_t rawPos = dir ? -pos : pos;
    int32_t nz = int32_t(uint16_t(zero)) + rawPos * ADC_FULL_SUM / scale;
    bus.write16(id, Reg::POT_ZERO, int16_t(nz));
    Serial.printf("Új nullpont: %ld (előtte %u). A mostani állás = 0 fok.\n", (long)nz, uint16_t(zero));
  }
  else if (c == "irany" && n >= 3) {
    bus.write8(id, Reg::TORQUE, 0);
    Serial.println(bus.write8(id, Reg::DIRECTION, toInt(t[2]) ? 1 : 0) ? "OK" : "Nincs válasz.");
  }
  else if (c == "reset") {
    bus.write8(id, Reg::TORQUE, 0);
    Serial.println(bus.factoryReset(id) ? "Gyári beállítások visszaállítva, ID = 1." : "Nincs válasz.");
  }
  else if (c == "lab" && n >= 5) {
    uint8_t leg = id;   // itt az első szám a láb sorszáma
    LegAngles a;
    if (!legIK(geom, t[2].toFloat(), t[3].toFloat(), t[4].toFloat(), a)) {
      Serial.println("A pont nem érhető el."); return;
    }
    uint16_t ms = n >= 6 ? uint16_t(toInt(t[5])) : 500;
    uint8_t ids[3] = { uint8_t(leg * 10 + 1), uint8_t(leg * 10 + 2), uint8_t(leg * 10 + 3) };
    int16_t pos[3] = { a.coxa_x10, a.femur_x10, a.tibia_x10 };
    for (uint8_t i = 0; i < 3; i++) bus.write8(ids[i], Reg::TORQUE, 1);
    bus.syncMove(ids, pos, ms, 3);
    Serial.printf("Láb %u: csípő %.1f, comb %.1f, lábszár %.1f fok, %u ms\n",
                  leg, a.coxa_x10 / 10.0, a.femur_x10 / 10.0, a.tibia_x10 / 10.0, ms);
  }
  else if (c == "figyel") {
    watchLeg = id;
    Serial.println(watchLeg ? "Figyelés be (\"figyel 0\" = ki)." : "Figyelés ki.");
  }
  else {
    Serial.println("Ismeretlen parancs vagy hiányzó paraméter. \"sugo\" = lista.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  bus.begin(Serial1, BUS_RX, BUS_TX, BUS_RTS, 1000000);
  Serial.println("HexMaster busz-teszt. Írd be: sugo");
}

void loop() {
  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') { handle(line); line = ""; }
    else if (line.length() < 120) line += ch;
  }

  if (watchLeg && millis() - lastWatch >= 200) {
    lastWatch = millis();
    uint8_t ids[3] = { uint8_t(watchLeg * 10 + 1), uint8_t(watchLeg * 10 + 2), uint8_t(watchLeg * 10 + 3) };
    ServoStatus st[3]; bool ok[3];
    bus.syncReadStatus(ids, 3, st, ok);
    for (uint8_t i = 0; i < 3; i++) {
      if (ok[i]) printStatus(ids[i], st[i]);
      else Serial.printf("ID %3u: nincs válasz\n", ids[i]);
    }
    Serial.println();
  }
}
