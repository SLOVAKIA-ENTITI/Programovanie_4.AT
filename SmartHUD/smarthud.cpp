// =============================================================================
// SmartHUD – Od analýzy k programu (OOP)
// Hlavný scenár: Scenár 1 – "Zobraziť HUD odporúčanie (ECO mód)"
//
// Mapovanie na SA a odchýlky od diagramov: pozri MAPOVANIE.md
// Preklad / spustenie:  g++ -std=c++17 -Wall -Wextra -o smarthud smarthud.cpp
// =============================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

// ============================ Chyby systému ==================================
// Chyby, ktoré systém nevie sám vyriešiť (kritické situácie z kapitoly 6/7 SA).
class ChybaSystemu : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class StratSignalu : public ChybaSystemu {
public:
    StratSignalu() : ChybaSystemu("Strata GPS aj OBD-II signalu - rychlost vozidla nie je znama.") {}
};

// ============================ Základné typy ==================================
enum class Farba { ZELENA, ORANZOVA, CERVENA };

static std::string farbaText(Farba f) {
    switch (f) {
        case Farba::ZELENA:   return "ZELENA";
        case Farba::ORANZOVA: return "ORANZOVA";
        default:              return "CERVENA";
    }
}

// ============================ ÚsekCesty ======================================
// Validácia limitu žije LEN tu (jediné miesto) – ostatné triedy sa naň spoliehajú.
class UsekCesty {
public:
    UsekCesty(int id, double limitKmh, bool verejna)
        : id_(id), limit_(limitKmh), verejna_(verejna) {
        if (!(limitKmh >= 1 && limitKmh <= 200))
            throw std::invalid_argument("Limit useku musi byt v rozsahu 1-200 km/h.");
    }
    int id() const { return id_; }
    double limit() const { return limit_; }
    bool jeVerejna() const { return verejna_; }

private:
    int id_;
    double limit_;
    bool verejna_;
};

// Zjednodušenie "Modul GPS a mapové dáta": register známych úsekov.
class MapaCiest {
public:
    void pridaj(const UsekCesty& u) { useky_.emplace(u.id(), u); }
    const UsekCesty* najdi(int id) const {
        auto it = useky_.find(id);
        return it == useky_.end() ? nullptr : &it->second;
    }

private:
    std::map<int, UsekCesty> useky_;
};

// ============================ OdporúčaciaZóna ================================
// Nemenný výsledok výpočtu – to, čo uvidí vodič.
struct OdporucaciaZona {
    double odporucanaRychlost;
    Farba farba;
    std::string sprava;
};

// ============================ Režimy jazdy (polymorfizmus) ===================
// Template method: vypocitaj() je spoločné, režimy menia len pravidlá.
class RezimJazdy {
public:
    virtual ~RezimJazdy() = default;
    virtual std::string nazov() const = 0;
    // Má sa na tomto úseku sledovať prekročenie rýchlosti (záznam pre políciu)?
    virtual bool monitorujeLimity(const UsekCesty& u) const = 0;

    OdporucaciaZona vypocitaj(double rychlost, const UsekCesty& u) const {
        return monitorujeLimity(u) ? podlaLimitu(rychlost, u) : bezLimitu(rychlost, u);
    }

protected:
    static OdporucaciaZona podlaLimitu(double rychlost, const UsekCesty& u) {
        const double prekrocenie = rychlost - u.limit();
        if (prekrocenie <= 0)  return {u.limit(), Farba::ZELENA,   "Rychlost v poriadku"};
        if (prekrocenie <= 10) return {u.limit(), Farba::ORANZOVA, "Zvolnite - nad limitom"};
        return {u.limit(), Farba::CERVENA, "Vyrazne nad limitom - spomalte!"};
    }
    // Predvolene sa správa ako podlaLimitu (ECO ho nikdy nepoužije).
    virtual OdporucaciaZona bezLimitu(double rychlost, const UsekCesty& u) const {
        return podlaLimitu(rychlost, u);
    }
};

class ECORezim : public RezimJazdy {
public:
    std::string nazov() const override { return "ECO"; }
    bool monitorujeLimity(const UsekCesty&) const override { return true; }
};

class PerformanceRezim : public RezimJazdy {
public:
    explicit PerformanceRezim(bool autorizovany) : autorizovany_(autorizovany) {}
    std::string nazov() const override { return "PERFORMANCE"; }
    bool monitorujeLimity(const UsekCesty& u) const override {
        return u.jeVerejna() && !autorizovany_;
    }

protected:
    OdporucaciaZona bezLimitu(double, const UsekCesty&) const override {
        return {0, Farba::ZELENA, "PERFORMANCE: bez obmedzenia (autorizovana jazda)"};
    }

private:
    bool autorizovany_;
};

class RaceRezim : public RezimJazdy {
public:
    std::string nazov() const override { return "RACE"; }
    // SA kap. 6: RaceHUD na verejnej ceste -> automaticky štandardné limity a monitoring.
    bool monitorujeLimity(const UsekCesty& u) const override { return u.jeVerejna(); }

protected:
    OdporucaciaZona bezLimitu(double, const UsekCesty&) const override {
        return {0, Farba::ZELENA, "RACE: navadzanie na brzdne body (okruh)"};
    }
};

// ============================ Kryptografia (simulácia) =======================
// POZOR: XOR + FNV-1a je len ukážka toku "šifrovaný záznam + kontrola integrity".
// V reálnom systéme: AES-GCM / podpis (HMAC), správa kľúčov.
namespace kripto {
const std::string KLUC = "smarthud-demo-kluc";

inline uint32_t fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}
inline std::string xorText(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<char>(out[i] ^ KLUC[i % KLUC.size()]);
    return out;
}
inline std::string naHex(const std::string& s) {
    static const char* h = "0123456789abcdef";
    std::string out;
    for (unsigned char c : s) { out += h[c >> 4]; out += h[c & 15]; }
    return out;
}
inline std::string zHex(const std::string& hex) {
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
    return out;
}
inline std::string zabal(const std::string& data) {
    return naHex(xorText(data + "#" + std::to_string(fnv1a(data))));
}
// Vráti pôvodné dáta, alebo nullopt ak bola porušená integrita / formát.
inline std::optional<std::string> rozbal(const std::string& hex) {
    try {
        const std::string plain = xorText(zHex(hex));
        const auto pos = plain.rfind('#');
        if (pos == std::string::npos) return std::nullopt;
        const std::string data = plain.substr(0, pos);
        if (std::to_string(fnv1a(data)) != plain.substr(pos + 1)) return std::nullopt;
        return data;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}
}  // namespace kripto

// ============================ ZáznamJazdy ====================================
class ZaznamJazdy {
public:
    ZaznamJazdy(int id, int usekId, double maxRychlost, double limit, int trvanieS)
        : id_(id), usekId_(usekId), max_(maxRychlost), limit_(limit), trvanieS_(trvanieS) {
        if (maxRychlost <= limit || trvanieS <= 0)
            throw std::invalid_argument("Zaznam porusenia musi mat rychlost nad limitom a kladne trvanie.");
    }
    std::string zasifrovany() const {
        std::ostringstream os;
        os << "id=" << id_ << ";usek=" << usekId_ << ";max=" << max_
           << ";limit=" << limit_ << ";trvanie=" << trvanieS_ << "s";
        return kripto::zabal(os.str());
    }
    int id() const { return id_; }
    int trvanieS() const { return trvanieS_; }

private:
    int id_, usekId_;
    double max_, limit_;
    int trvanieS_;
};

// Sleduje trvanie prekročenia; vytvorí JEDEN záznam na jednu epizódu porušenia.
class MonitorPorusenia {
public:
    MonitorPorusenia(double prahKmh = 10.0, int minTikov = 5) : prah_(prahKmh), minTikov_(minTikov) {}

    std::optional<ZaznamJazdy> vyhodnot(double rychlost, const UsekCesty& u) {
        if (rychlost <= u.limit() + prah_) { reset(); return std::nullopt; }
        ++tikov_;
        maxRychlost_ = std::max(maxRychlost_, rychlost);
        if (tikov_ >= minTikov_ && !nahlasene_) {
            nahlasene_ = true;
            return ZaznamJazdy(dalsiId_++, u.id(), maxRychlost_, u.limit(), tikov_ /* 1 tik = 1 s */);
        }
        return std::nullopt;
    }
    void reset() { tikov_ = 0; maxRychlost_ = 0; nahlasene_ = false; }

private:
    double prah_;
    int minTikov_;
    int tikov_ = 0;
    double maxRychlost_ = 0;
    bool nahlasene_ = false;
    int dalsiId_ = 1;
};

// ============================ TretiaStrana (Polícia SR) ======================
class TretiaStrana {
public:
    explicit TretiaStrana(std::string nazov) : nazov_(std::move(nazov)) {}
    void prijmi(const std::string& sifrovane) {
        const auto data = kripto::rozbal(sifrovane);
        if (!data) {
            std::cout << "   [" << nazov_ << "] Zaznam ODMIETNUTY - poskodena integrita.\n";
            return;
        }
        ++prijatych_;
        std::cout << "   [" << nazov_ << "] Zaznam prijaty, integrita OK: " << *data << "\n";
    }
    int prijatych() const { return prijatych_; }

private:
    std::string nazov_;
    int prijatych_ = 0;
};

// ============================ HUD zobrazovače (polymorfizmus) ================
class HUDRenderer {
public:
    virtual ~HUDRenderer() = default;
    virtual std::string nazov() const = 0;
    virtual bool jeDostupny() const { return !porucha_; }
    virtual void vykresli(const OdporucaciaZona& z) const = 0;
    void nastavPoruchu(bool p) { porucha_ = p; }

private:
    bool porucha_ = false;
};

// Spoločná logika oboch 2D variantov (telefón aj samostatný projektor) – bez duplicity.
class HUD2D : public HUDRenderer {
public:
    void vykresli(const OdporucaciaZona& z) const override {
        std::cout << "   [" << nazov() << "] gradient " << farbaText(z.farba);
        if (z.odporucanaRychlost > 0) std::cout << " | odporucana " << z.odporucanaRychlost << " km/h";
        std::cout << " | " << z.sprava << "\n";
    }
};

class SmartfonHUD : public HUD2D {
public:
    std::string nazov() const override { return "2D Smartfon"; }
    bool jeDostupny() const override { return HUDRenderer::jeDostupny() && teplotaC_ < 45.0; }
    void nastavTeplotu(double c) { teplotaC_ = c; }

private:
    double teplotaC_ = 25.0;
};

class Projektor2DHUD : public HUD2D {
public:
    std::string nazov() const override { return "2D HUD projektor"; }
};

class LaserARHUD : public HUDRenderer {
public:
    std::string nazov() const override { return "Laser AR-HUD"; }
    void vykresli(const OdporucaciaZona& z) const override {
        const int sipok = z.farba == Farba::ZELENA ? 3 : (z.farba == Farba::ORANZOVA ? 2 : 1);
        std::cout << "   [" << nazov() << "] 3D sipky " << farbaText(z.farba) << " x" << sipok;
        if (z.odporucanaRychlost > 0) std::cout << " | odporucana " << z.odporucanaRychlost << " km/h";
        std::cout << " | " << z.sprava << "\n";
    }
};

// Vyberá aktívny zobrazovač podľa priority a rieši fail-over (SA kap. 5 – Spoľahlivosť).
class HUDManazer {
public:
    void pridaj(std::shared_ptr<HUDRenderer> r) { zariadenia_.push_back(std::move(r)); }  // poradie = priorita

    void vykresli(const OdporucaciaZona& z) {
        HUDRenderer* r = vyberDostupny();
        if (!r) throw ChybaSystemu("Ziadne HUD zariadenie nie je dostupne.");
        if (posledny_ && posledny_ != r)
            std::cout << "   [HUD] FAIL-OVER: " << posledny_->nazov() << " -> " << r->nazov() << "\n";
        posledny_ = r;
        r->vykresli(z);
    }
    void varuj(const std::string& text) {
        HUDRenderer* r = vyberDostupny();
        std::cout << "   [" << (r ? r->nazov() : "SYSTEM") << "] VAROVANIE: " << text << "\n";
    }

private:
    HUDRenderer* vyberDostupny() const {
        for (const auto& r : zariadenia_)
            if (r->jeDostupny()) return r.get();
        return nullptr;
    }
    std::vector<std::shared_ptr<HUDRenderer>> zariadenia_;
    HUDRenderer* posledny_ = nullptr;
};

// ============================ ADASRozhranie ==================================
// Vstup zo simulovaného zdroja dát (GPS + OBD-II/CAN). Validácia hodnôt je LEN tu.
struct SurovaVzorka {
    std::optional<double> obd;   // rýchlosť z OBD-II (nullopt = nedostupné)
    std::optional<double> gps;   // rýchlosť z GPS   (nullopt = nedostupné)
    int usekId;
};
struct Vzorka {
    std::optional<double> rychlost;  // OBD, inak GPS, inak nullopt
    bool gpsOk;                      // vieme určiť polohu/úsek?
    int usekId;
};

class ADASRozhranie {
public:
    void nacitajData(std::vector<SurovaVzorka> data) { data_ = std::move(data); index_ = 0; }

    std::optional<Vzorka> dalsiaVzorka() {
        if (index_ >= data_.size()) return std::nullopt;
        const SurovaVzorka& s = data_[index_++];
        Vzorka v;
        v.gpsOk = platna(s.gps);
        v.usekId = s.usekId;
        if (platna(s.obd))      v.rychlost = s.obd;
        else if (platna(s.gps)) v.rychlost = s.gps;
        return v;
    }

private:
    static bool platna(const std::optional<double>& r) {
        return r && std::isfinite(*r) && *r >= 0 && *r <= 400;
    }
    std::vector<SurovaVzorka> data_;
    size_t index_ = 0;
};

// ============================ Jazda (orchestrácia scenára) ===================
enum class VysledokJazdy { DOKONCENA, PRERUSENA };

class Jazda {
public:
    Jazda(ADASRozhranie& adas, const MapaCiest& mapa, HUDManazer& hud, TretiaStrana& policia)
        : adas_(adas), mapa_(mapa), hud_(hud), policia_(policia) {}

    void zvolRezim(std::unique_ptr<RezimJazdy> r) {
        if (!r) throw std::invalid_argument("Rezim nesmie byt prazdny.");
        rezim_ = std::move(r);
        monitor_.reset();
        std::cout << "[Vodic] zvolil rezim " << rezim_->nazov() << "\n";
    }

    // Periodická slučka zo sekvenčného diagramu: zber dát -> výpočet -> projekcia -> záznam.
    VysledokJazdy spusti() {
        if (!rezim_) throw std::logic_error("Pred jazdou treba zvolit rezim.");
        try {
            while (auto v = adas_.dalsiaVzorka()) krok(*v);
            return VysledokJazdy::DOKONCENA;
        } catch (const ChybaSystemu& e) {
            hud_.varuj(e.what());
            std::cout << "   [Jazda] Projekcia bezpecne ukoncena. Dovod: " << e.what() << "\n";
            return VysledokJazdy::PRERUSENA;
        }
    }

private:
    void krok(const Vzorka& v) {
        ++cas_;
        if (!v.rychlost) throw StratSignalu();
        std::cout << "t=" << cas_ << "s  rychlost " << *v.rychlost << " km/h\n";
        if (!v.gpsOk) return pozastav("GPS nedostupne - projekcia pozastavena");
        const UsekCesty* usek = mapa_.najdi(v.usekId);
        if (!usek) return pozastav("Neznamy usek cesty - projekcia pozastavena");

        hud_.vykresli(rezim_->vypocitaj(*v.rychlost, *usek));

        if (rezim_->monitorujeLimity(*usek)) {
            if (auto zaznam = monitor_.vyhodnot(*v.rychlost, *usek)) {
                std::cout << "   [Jazda] Dlhodobe prekrocenie (" << zaznam->trvanieS()
                          << " s) -> zaznam #" << zaznam->id() << " pre Policiu SR\n";
                policia_.prijmi(zaznam->zasifrovany());
            }
        } else {
            monitor_.reset();
        }
    }
    void pozastav(const std::string& dovod) {
        hud_.varuj(dovod);
        monitor_.reset();
    }

    ADASRozhranie& adas_;
    const MapaCiest& mapa_;
    HUDManazer& hud_;
    TretiaStrana& policia_;
    std::unique_ptr<RezimJazdy> rezim_;
    MonitorPorusenia monitor_;
    int cas_ = 0;
};

// ============================ Demonštrácia ===================================
static MapaCiest vytvorMapu() {
    MapaCiest m;
    m.pridaj(UsekCesty(1, 50, true));    // mesto
    m.pridaj(UsekCesty(2, 90, true));    // cesta mimo mesta
    m.pridaj(UsekCesty(3, 200, false));  // uzavretý okruh
    return m;
}
static void nadpis(const std::string& t) { std::cout << "\n==== " << t << " ====\n"; }
static SurovaVzorka s(double kmh, int usek) { return {kmh, kmh, usek}; }  // OBD aj GPS OK

// 1) Ideálny scenár (Scenár 1 z SA): ECO, Laser AR-HUD, všetky dáta k dispozícii.
static void scenarIdealny() {
    nadpis("1) IDEALNY SCENAR - ECO, Laser AR-HUD, plne funkcne data");
    MapaCiest mapa = vytvorMapu();
    ADASRozhranie adas;
    TretiaStrana policia("Policia SR");
    HUDManazer hud;
    hud.pridaj(std::make_shared<LaserARHUD>());
    hud.pridaj(std::make_shared<Projektor2DHUD>());
    hud.pridaj(std::make_shared<SmartfonHUD>());

    Jazda jazda(adas, mapa, hud, policia);
    jazda.zvolRezim(std::make_unique<ECORezim>());
    adas.nacitajData({s(45, 1), s(48, 1), s(55, 1), s(63, 1), s(66, 1), s(68, 1),
                      s(67, 1), s(65, 1), s(62, 1), s(50, 1), s(70, 2)});
    auto v = jazda.spusti();
    std::cout << "Vysledok: " << (v == VysledokJazdy::DOKONCENA ? "jazda dokoncena" : "jazda prerusena")
              << ", zaznamov odoslanych: " << policia.prijatych() << "\n";
}

// 2) Hranične riešiteľný: laser zlyhá (fail-over) a krátko vypadne GPS (pozastavenie).
static void scenarHranicny() {
    nadpis("2) HRANICNE RIESITELNY - vypadok laseru a GPS");
    MapaCiest mapa = vytvorMapu();
    ADASRozhranie adas;
    TretiaStrana policia("Policia SR");
    auto laser = std::make_shared<LaserARHUD>();
    HUDManazer hud;
    hud.pridaj(laser);
    hud.pridaj(std::make_shared<Projektor2DHUD>());
    hud.pridaj(std::make_shared<SmartfonHUD>());

    Jazda jazda(adas, mapa, hud, policia);
    jazda.zvolRezim(std::make_unique<ECORezim>());
    adas.nacitajData({s(46, 1), s(49, 1)});
    jazda.spusti();

    std::cout << "--- (simulacia) Laser AR projektor zlyhal ---\n";
    laser->nastavPoruchu(true);
    adas.nacitajData({s(51, 1), SurovaVzorka{52.0, std::nullopt, 1} /* GPS vypadok */, s(47, 1)});
    auto v = jazda.spusti();
    std::cout << "Vysledok: " << (v == VysledokJazdy::DOKONCENA ? "jazda pokracuje/dokoncena v zalozom rezime"
                                                                : "jazda prerusena") << "\n";
}

// 3) Systém nezvládne: úplná strata GPS aj OBD-II -> korektné nahlásenie.
static void scenarNezvladne() {
    nadpis("3) SYSTEM NEZVLADNE - uplna strata GPS aj OBD-II (napr. tunel)");
    MapaCiest mapa = vytvorMapu();
    ADASRozhranie adas;
    TretiaStrana policia("Policia SR");
    HUDManazer hud;
    hud.pridaj(std::make_shared<LaserARHUD>());
    hud.pridaj(std::make_shared<Projektor2DHUD>());

    Jazda jazda(adas, mapa, hud, policia);
    jazda.zvolRezim(std::make_unique<ECORezim>());
    adas.nacitajData({s(60, 2), s(62, 2), SurovaVzorka{std::nullopt, std::nullopt, 2}, s(61, 2)});
    auto v = jazda.spusti();
    std::cout << "Vysledok: " << (v == VysledokJazdy::DOKONCENA ? "jazda dokoncena" : "jazda PRERUSENA (nespracovane vzorky ostali)")
              << "\n";
}

// Bonus – "hráme sa na testera": zlé vstupy nesmú spôsobiť pád.
static void demonstraciaTestera() {
    nadpis("BONUS) TESTER - zle vstupy");
    try { UsekCesty zly(9, -5, true); }
    catch (const std::invalid_argument& e) { std::cout << "Neplatny usek odmietnuty: " << e.what() << "\n"; }

    MapaCiest mapa = vytvorMapu();
    ADASRozhranie adas;
    TretiaStrana policia("Policia SR");
    HUDManazer hud;
    hud.pridaj(std::make_shared<Projektor2DHUD>());
    Jazda jazda(adas, mapa, hud, policia);

    try { jazda.spusti(); }
    catch (const std::logic_error& e) { std::cout << "Jazda bez rezimu: " << e.what() << "\n"; }

    jazda.zvolRezim(std::make_unique<RaceRezim>());
    std::cout << "RACE na verejnej ceste -> limity sa aj tak uplatnia; OBD zaporne (-20) -> pouzije sa GPS; "
                 "prazdny vstup -> nic sa nestane:\n";
    adas.nacitajData({SurovaVzorka{-20.0, 58.0, 1}});
    jazda.spusti();
    adas.nacitajData({});
    jazda.spusti();
    std::cout << "Obe hodnoty nezmyselne (9999, NaN) -> bezpecne ukoncenie:\n";
    adas.nacitajData({SurovaVzorka{9999.0, std::nan(""), 1}});
    jazda.spusti();
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif
    std::cout << "SmartHUD - demonstracia hlavneho scenara\n";
    scenarIdealny();
    scenarHranicny();
    scenarNezvladne();
    demonstraciaTestera();
    return 0;
}
