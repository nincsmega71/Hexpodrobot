# Mechanika – SolidWorks

Az alkatrészeket SolidWorks-makrók (`.swb`) építik fel a [makrok](makrok) mappában. Minden makró új alkatrészt nyit az alapértelmezett sablonnal, felépíti a modellt, és a végén szól, hogy mentsd el `.sldprt`-ként. A méretek a makrók elején, a `Const` sorokban vannak, ott módosíthatók.

Méretek és mechanizmusok: [docs/mechanika.md](../docs/mechanika.md).

## Futtatás

1. SolidWorks: **Eszközök → Makró → Futtatás** (Tools → Macro → Run).
2. Fájltípus: **SW Basic Macros (\*.swb)**.
3. Válaszd ki a makrót. Nem kell előtte dokumentumot nyitni.
4. A kész alkatrészt mentsd: **Fájl → Mentés másként → .sldprt**, és tedd ebbe a mappába.

Ha a makró azt írja, hogy nem tud új alkatrészt nyitni: **Beállítások → Rendszerbeállítások → Alapértelmezett sablonok**, ott legyen megadva alkatrész-sablon.

A makrók a síkokat a fa sorrendje alapján választják (Elölnézet, Felülnézet, Jobb oldali), így magyar és angol SolidWorksben is működnek.

## Makrók

| Makró | Alkatrész | Fő méretek |
|---|---|---|
| `hejszelet.swb` | Héjszelet, a gömb 1/12-e (12 egyforma darab) | R 160 mm, 2,5 mm fal, 60° széles, egyenlítőtől a pólusig |
| `szervo_burkolo.swb` | DS3218 / MG996R befoglaló test az összeállításhoz | 40 × 20 × 40,5 mm, rögzítőfülek 54,5 mm, tengely Ø6 |
| `lab_link.swb` | Lábszakasz-lemez (bekéri a tengelytávot) | 60 mm (comb) vagy 90 mm (lábszár), 20 × 3 mm, végeken kar-furatkép |
| `hejnyito_konzol.swb` | Felső héjszelet nyitókonzolja a két horonnyal | 12 mm csúszás, 17 mm csaptáv, 3,2 mm horony, 5 mm vastag |

## Tudnivalók

- A makrókat SolidWorks nélkül írtam, tehát még nem futottak élesben. Ha valamelyik hibát jelez, írd meg a hibaüzenetet és a sor számát, és javítom.
- A szervókar furatképét (`CSAVAR_KOR`, `CSAVAR_D` a `lab_link.swb`-ben) a saját karodról mérd le.
- A szervó burkolóteste egyszerűsített: a rögzítőfuratokat és a kábelkivezetést kézzel kell hozzáadni, ha kell.
- A héjszelet tartóinak furatai a tervezés következő lépésében kerülnek rá, amikor a láb és a konzol már megvan az összeállításban.
