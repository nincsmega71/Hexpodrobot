# Szervó firmware – CH32V003F4U6

Busz-szervó firmware a DS3218 / MG996R szervókba épített saját panelhez. Protokoll: [docs/busz-protokoll.md](../../docs/busz-protokoll.md).

## Fordítás és feltöltés

1. VS Code + PlatformIO bővítmény.
2. A `firmware/servo` mappa megnyitása (első alkalommal a PlatformIO letölti a CH32V platformot és a WCH NoneOS SDK-t).
3. Környezet kiválasztása: `ds3218` (comb, lábszár) vagy `mg996r` (csípő, héj). A különbség csak a 0x00 modellszám-regiszter.
4. WCH-LinkE programozó: SWIO, 3V3, GND a panel tesztpadjeire → **Upload**.

Méret: kb. 7,8 KB flash (16 KB-ból), 0,9 KB RAM (2 KB-ból). A beállítások az utolsó 1 KB-os flash-lapon vannak (0x08003C00).

## Fájlok

| Fájl | Tartalom |
|---|---|
| `src/config.h` | Lábkiosztás, PWM, ADC, védelmi határok – **élesztés előtt ellenőrizni** |
| `src/regs.c/h` | Regisztertábla, alapértékek, flash-mentés |
| `src/hw.c/h` | GPIO, TIM1 PWM (20 kHz), ADC + DMA, TIM2 (1 kHz), USART1 half-duplex |
| `src/control.c/h` | 1 kHz szabályzás: mérés, pálya, PID, áramkorlát, védelmek |
| `src/bus.c/h` | Csomagvétel (megszakítás), utasítások feldolgozása, válasz |
| `src/main.c` | Főciklus: busz, flash-mentés, LED |

## Élesztési sorrend

1. Táp nélkül a motor: ellenőrizd a soros kommunikációt (`ping`, `allapot`) – a feszültség, potméter és áram értékeinek értelmesnek kell lenniük.
2. `nulla <ID>` – a mostani állás legyen 0 fok.
3. Kis `Kp`-vel (`ir <ID> 0x10 512 2`) és alacsony max. kitöltéssel (`ir <ID> 0x16 300 2`) kapcsold be a nyomatékot.
4. Ha a szervó rossz irányba indul el és nekimegy a végállásnak, kapcsold ki, és fordítsd meg a motor két vezetékét, **vagy** `irany <ID> 1` (ez a potméter- és a motorirányt együtt fordítja – ha csak az egyik fordított, a motorvezetékeket kell cserélni).
5. Növeld a `Kp`-t és `Kd`-t, amíg a mozgás gyors, de nem leng.
