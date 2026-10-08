# SmartHUD – mapovacia poznámka (SA → kód)

Súbor: `smarthud.cpp` · preklad: `g++ -std=c++17 -Wall -Wextra -o smarthud smarthud.cpp`

## 1. Triedy v kóde ↔ moduly/aktéri z SA

| SA (modul / aktér / trieda)            | Kód                                                                 |
|----------------------------------------|---------------------------------------------------------------------|
| Vodič (aktér)                          | `main()` / `scenar*()` – volí režim a HW (`zvolRezim`, poradie `HUDManazer::pridaj`) |
| Jazda (atribút `rezim`)                | `Jazda` + hierarchia `RezimJazdy` (`ECORezim`, `PerformanceRezim`, `RaceRezim`) |
| Modul správcu režimov                  | `RezimJazdy` (polymorfizmus: pravidlá výpočtu a monitoringu)        |
| HUDRenderer (2D Smartfón / 2D Projektor / Laser AR) | `HUDRenderer` → `HUD2D` → `SmartfonHUD`, `Projektor2DHUD`; `LaserARHUD` |
| ADASRozhranie / OBD-II + GPS           | `ADASRozhranie` (simulovaný zdroj dát, jediné miesto validácie hodnôt) |
| ÚsekCesty / modul GPS a mapové dáta    | `UsekCesty` (validácia limitu) + `MapaCiest`                         |
| OdporúčaciaZóna                        | `OdporucaciaZona` (nemenný výsledok výpočtu)                         |
| ZáznamJazdy / modul záznamu porušení   | `ZaznamJazdy` + `MonitorPorusenia`                                   |
| TretiaStrana (Polícia SR)              | `TretiaStrana`                                                       |
| Hardware fail-over (FURPS – Spoľahlivosť) | `HUDManazer`                                                      |

## 2. Kde je hlavný scenár (Scenár 1 – ECO)

- Tok v `main()` → `scenarIdealny()`.
- Slučka zo sekvenčného diagramu: `Jazda::spusti()` → `Jazda::krok()`:
  zber dát (`ADASRozhranie::dalsiaVzorka`) → výpočet (`RezimJazdy::vypocitaj`) →
  projekcia (`HUDManazer::vykresli`) → vyhodnotenie prekročenia (`MonitorPorusenia::vyhodnot`) →
  záznam a odoslanie (`ZaznamJazdy::zasifrovany` → `TretiaStrana::prijmi`).

## 3. Tri situácie z kapitoly „Hranice systému“ (v behu programu)

1. **Ideálny** – `scenarIdealny()`: laser AR-HUD, GPS aj OBD OK, dlhodobé prekročenie → záznam pre políciu.
2. **Hranične riešiteľný** – `scenarHranicny()`: zlyhá laser → fail-over na 2D projektor; krátky výpadok GPS → projekcia sa pozastaví s varovaním, jazda pokračuje.
3. **Nezvládne** – `scenarNezvladne()`: úplná strata GPS aj OBD-II → `StratSignalu`, projekcia sa bezpečne ukončí a systém to nahlási.
- Bonus `demonstraciaTestera()`: záporný limit, jazda bez režimu, záporná/nezmyselná rýchlosť, prázdny vstup, RACE na verejnej ceste.

**Upresnenie k SA (kap. 7):** *Hranične riešiteľný stav* = zlyhanie laseru (fail-over na 2D) alebo prechodný výpadok GPS pri funkčnom OBD-II. *Situácia, ktorú systém nezvládne* = súčasná strata GPS aj OBD-II (rýchlosť nie je známa) → projekcia sa preruší.

## 4. Zmeny oproti diagramom a prečo

- **`HUDManazer`** (nová trieda): fail-over a výber zariadenia by inak zaťažili `Jazda` (zodpovednosť triedy).
- **`RezimJazdy` je hierarchia** namiesto atribútu `rezim` v `Jazda`: každý režim má iné pravidlá (monitoring, výpočet) – inak by tam boli `if/switch` na viacerých miestach.
- **`HUD2D`** (medzitrieda): telefón aj 2D projektor kreslia rovnako, líšia sa len dostupnosťou (prehriatie) – vyhnutie sa duplicite.
- **`MonitorPorusenia`** (nová trieda): sledovanie trvania prekročenia nepatrí do `Jazda` ani do `ZaznamJazdy`.
- **`MapaCiest` a `ADASRozhranie`** zjednodušujú moduly GPS/mapy/dopravné info: GPS a OBD sú v jednom rozhraní, dopravné informácie (kolóny) v tejto ukážke nie sú implementované.
- **Šifrovanie záznamu** je len simulácia (XOR + kontrolný súčet FNV-1a) na ukážku toku a kontroly integrity; v reálnom systéme AES-GCM/HMAC.
- Validácia: limit úseku len v `UsekCesty`, hodnoty senzorov len v `ADASRozhranie`, záznam len v `ZaznamJazdy`; ostatné triedy sa na ne spoliehajú.
