# SmartHUD – mapovacia poznámka (SA ↔ kód)

Súbor: `SmartHUD.cpp` (preklad: `g++ -std=c++17 -Wall -Wextra SmartHUD.cpp -o smarthud`)

## 1. Triedy v kóde ↔ SA

| SA (modul / aktér / trieda) | Kód |
|---|---|
| Aktér **Vodič** | `Vodic` |
| Lajna **Aplikácia SmartHUD** / trieda `Jazda` | `Jazda` (riadi celý scenár) |
| Modul **HUD projekcie** / `HUDRenderer` | abstraktná `HUDRenderer` + `Telefon2D`, `Projektor2D`, `LaserARHUD` |
| Modul **ADAS / OBD-II** / `ADASRozhranie`, aktér Palubný ADAS | `ADASRozhranie` |
| Modul **GPS a mapové dáta** / `UsekCesty` | `GpsModul` (zdroj) + `UsekCesty` (dáta) |
| Modul **dopravných informácií** / `DopravnaSituacia` | `DopravnyServer` (zdroj) + `DopravnaSituacia` (dáta) |
| `OdporucaciaZona` | `OdporucaciaZona` |
| Modul **záznamu porušení** / `ZaznamJazdy` | `ZaznamJazdy` |
| Aktér **Tretia strana (Polícia SR)** | `TretiaStrana` |
| Režimy ECO / PERFORMANCE / RACE (Mode Selector) | `RezimTyp` + `RezimPolitika` (`EcoPolitika`, `SportPolitika`) |

## 2. Kde je hlavný scenár

Scenár 1 „Zobraziť HUD odporúčanie (ECO)“ = `scenarioIdealny()` v `main()`, ktorý volá:

| Sekvenčný diagram | Kód |
|---|---|
| 1. Spustiť aplikáciu, zvoliť režim a HW | `Vodic::zvolRezim()`, výber HUD v konštruktore `Jazda` |
| 2. Inicializovať HW výstup | `Jazda::inicializuj()` → `HUDRenderer::inicializuj()` |
| 3–8. GPS, doprava, ADAS | `Jazda::nacitajVstupy()` → `GpsModul::usek()`, `DopravnyServer::aktualna()`, `ADASRozhranie::nacitaj()` |
| 9. Vypočítať odporúčanú rýchlosť | `Jazda::vypocitajOdporucanie()` |
| 10–12. Vykresliť a zobraziť | `Jazda::zobraz()` → `HUDRenderer::renderuj()` |
| 13–14. Označiť porušenie, informovať vodiča | `Jazda::zaznamenajPorusenie()` |
| 15–16. Odoslať šifrovaný záznam | `Jazda::ukonci()` → `ZaznamJazdy::odosli()` → `TretiaStrana::prijmiZaznam()` |

Slučka „počas jazdy“ = `Jazda::tik()` (1 tik = 1 s).

## 3. Tri scenáre z kapitoly Hranice systému

1. **Ideálny** – `scenarioIdealny()`: Laser AR-HUD, GPS, doprava, CAN/ACC, ECO; dlhodobé prekročenie → šifrovaný záznam → Polícia SR.
2. **Hranične riešiteľný** – `scenarioHranicny()`: laser počas jazdy zlyhá, systém prepne na samostatný 2D projektor a jazda pokračuje.
3. **Nezvládne** – `scenarioMimoHranic()`: vypadne GPS (pozastavenie s varovaním), potom aj OBD-II/CAN → projekcia sa bezpečne preruší s nahlásením dôvodu, ďalšie tiky nespadnú.

**Upresnenie k SA (kap. 7):** „hranične riešiteľný“ = porucha zobrazovača alebo výpadok jedného zdroja signálu (GPS alebo CAN), ktorý systém zvládne záložným výstupom/pozastavením. „Mimo hraníc“ = výpadok GPS aj CAN naraz, bez ktorého sa nedá určiť ani rýchlosť, ani limit. „Dlhodobé prekročenie“ = limit prekročený 5 tikov (s) po sebe.

## 4. OOP a konštrukcia

- **Dedičnosť + polymorfizmus:** `HUDRenderer` (tri typy HW, `vykresli()` je virtuálna) a `RezimPolitika` (správanie režimov).
- **Abstrakcia:** `Jazda` pracuje len s `HUDRenderer*` a `RezimPolitika`, nevie, aký konkrétny typ to je.
- **Skladanie (MÁ, nie JE):** `Jazda` má `RezimPolitika`, záznamy, HUD, zdroje dát. `Vodic` ani `Jazda` od ničoho nededia.
- **Enkapsulácia:** všetky atribúty sú privátne, `ZaznamJazdy` sa po zašifrovaní nedá meniť.
- **Jedno miesto validácie:** meno vodiča → konštruktor `Vodic`; limit → konštruktor `UsekCesty`; telemetria → `ADASRozhranie::prijmiData`; jas → `HUDRenderer::nastavJas`; meškanie → konštruktor `DopravnaSituacia`. Ostatné triedy sa na to spoliehajú.
- **Bez duplicity:** kontrola poruchy HW je len v `HUDRenderer::overFunkcnost()` (šablónová metóda `renderuj`), PERFORMANCE a RACE zdieľajú jednu triedu `SportPolitika`.

## 5. Zmeny oproti pôvodným diagramom a prečo

- **Pridané `GpsModul`, `DopravnyServer`:** v sekvenčnom diagrame sú ako lajny, v triednom chýbali.
- **Pridané `RezimPolitika` (+2 potomkovia):** triedny diagram mal len atribút `rezim`; správanie režimov (ECO zaznamenáva, ostatné nie) patrí do polymorfných tried, nie do `if` v `Jazda`.
- **`ADASRozhranie::synchronizuj(Jazda)` → `nacitaj()`:** v sekvencii aplikácia požiada ADAS a ten odpovie, preto `Jazda` údaje číta sama (bez cyklickej závislosti tried).
- **`HUDRenderer::renderuj` je neabstraktná šablónová metóda**, ktorá volá abstraktnú `vykresli()`; kontrola poruchy a fail-over (kap. 6) tak nie sú v každom potomkovi.
- **`UsekCesty` už neobsahuje `OdporucaciaZona` ani `DopravnaSituacia`:** zóna sa počíta pre každý tik ako hodnota, situáciu dodáva `DopravnyServer`.
- **`OdporucaciaZona`:** `id` vynechané (hodnotový objekt); `vypocitaj()` dostáva vstupy ako parametre.
- **`ZaznamJazdy`:** pridané `aktualizuj()` (aktivita „Vytvoriť/aktualizovať záznam“); `stavOdoslania` je enum. Šifrovanie je len demonštračné XOR (v produkcii AES-GCM).
- **Nerealizované v tejto úlohe:** brzdné body a ideálna stopa RACE (Scenár 3 v SA), tepelná ochrana smartfónu, detekcia RACE na verejnej ceste.
