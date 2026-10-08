#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


enum class RezimTyp { ECO, PERFORMANCE, RACE };
enum class HWTyp { TELEFON_2D, PROJEKTOR_2D, LASER_AR };
enum class StavOdoslania { VYTVORENY, SIFROVANY, ODOSLANY };
enum class StavTiku { OK, POZASTAVENE, PRERUSENE };

static std::string nazov(RezimTyp r) {
    switch (r) {
        case RezimTyp::ECO: return "ECO";
        case RezimTyp::PERFORMANCE: return "PERFORMANCE";
        case RezimTyp::RACE: return "RACE";
    }
    return "?";
}

static std::string kmh(double v) { return std::to_string(static_cast<int>(std::lround(v))) + " km/h"; }

struct SignalLostException : std::runtime_error { using std::runtime_error::runtime_error; };

struct HudPoruchaException : std::runtime_error { using std::runtime_error::runtime_error; };


class Vodic {
public:
    Vodic(std::string id, std::string meno, RezimTyp preferovany = RezimTyp::ECO)
        : id_(std::move(id)), meno_(std::move(meno)), preferovanyRezim_(preferovany) {
        if (meno_.empty()) throw std::invalid_argument("Meno vodiča nesmie byť prázdne.");
    }
    void zvolRezim(RezimTyp rezim) { preferovanyRezim_ = rezim; }
    RezimTyp preferovanyRezim() const { return preferovanyRezim_; }
    const std::string& meno() const { return meno_; }

private:
    std::string id_, meno_;
    RezimTyp preferovanyRezim_;
};


class DopravnaSituacia {
public:
    DopravnaSituacia(std::string idUdalosti = "-", std::string stavPremavky = "voľná",
                     bool kolona = false, int meskanie = 0)
        : idUdalosti_(std::move(idUdalosti)), stavPremavky_(std::move(stavPremavky)),
          kolona_(kolona), meskanie_(meskanie) {
        if (meskanie_ < 0) throw std::invalid_argument("Meškanie nemôže byť záporné.");
    }
    bool kolona() const { return kolona_; }
    const std::string& stavPremavky() const { return stavPremavky_; }
    int meskanie() const { return meskanie_; }

private:
    std::string idUdalosti_, stavPremavky_;
    bool kolona_;
    int meskanie_;
};

class UsekCesty {
public:
    UsekCesty(std::string id, double rychlostnyLimit, std::string suradnice)
        : id_(std::move(id)), rychlostnyLimit_(rychlostnyLimit), suradnice_(std::move(suradnice)) {
        if (rychlostnyLimit_ <= 0 || rychlostnyLimit_ > 200)
            throw std::invalid_argument("Rýchlostný limit musí byť v intervale (0, 200] km/h.");
    }
    double getLimit() const { return rychlostnyLimit_; }

private:
    std::string id_;
    double rychlostnyLimit_;
    std::string suradnice_;
};


class OdporucaciaZona {
public:
    void vypocitaj(double aktualna, double limit, double odporucana) {
        aktualna_ = aktualna;
        odporucana_ = odporucana;
        if (aktualna > limit) { typ_ = "BRZDI"; farba_ = "ČERVENÁ"; }
        else if (aktualna > odporucana) { typ_ = "SPOMAĽ"; farba_ = "ORANŽOVÁ"; }
        else { typ_ = "UDRŽUJ"; farba_ = "ZELENÁ"; }
    }
    const std::string& typOdporucania() const { return typ_; }
    const std::string& farbaGradientu() const { return farba_; }
    double odporucana() const { return odporucana_; }
    double aktualna() const { return aktualna_; }

private:
    double aktualna_ = 0, odporucana_ = 0;
    std::string typ_ = "UDRŽUJ", farba_ = "ZELENÁ";
};


class RezimPolitika {
public:
    virtual ~RezimPolitika() = default;
    virtual RezimTyp typ() const = 0;
    virtual double odporucanaRychlost(double limit, const DopravnaSituacia& s) const = 0;
    virtual bool zaznamenavaPorusenia() const = 0;
    static std::unique_ptr<RezimPolitika> vytvor(RezimTyp typ);
};

class EcoPolitika : public RezimPolitika {
public:
    RezimTyp typ() const override { return RezimTyp::ECO; }
    double odporucanaRychlost(double limit, const DopravnaSituacia& s) const override {
        if (s.kolona()) return std::min(limit, KOLONA_RYCHLOST);
        if (s.stavPremavky() == "hustá") return limit * 0.8;
        return limit;
    }
    bool zaznamenavaPorusenia() const override { return true; }

private:
    static constexpr double KOLONA_RYCHLOST = 30.0;
};

constexpr double EcoPolitika::KOLONA_RYCHLOST;


class SportPolitika : public RezimPolitika {
public:
    explicit SportPolitika(RezimTyp typ) : typ_(typ) {}
    RezimTyp typ() const override { return typ_; }
    double odporucanaRychlost(double limit, const DopravnaSituacia&) const override { return limit; }
    bool zaznamenavaPorusenia() const override { return false; }

private:
    RezimTyp typ_;
};

std::unique_ptr<RezimPolitika> RezimPolitika::vytvor(RezimTyp typ) {
    if (typ == RezimTyp::ECO) return std::make_unique<EcoPolitika>();
    return std::make_unique<SportPolitika>(typ);
}


struct Telemetria {
    double rychlost = 0;  // km/h
    double odstup = 0;    // m
    bool stavACC = false;
};

class ADASRozhranie {
public:
    explicit ADASRozhranie(std::string idVozidla) : idVozidla_(std::move(idVozidla)) {}

    void prijmiData(double rychlost, double odstup, bool stavACC) {
        if (rychlost < 0 || rychlost > 300) throw std::invalid_argument("Neplatná rýchlosť z CAN: " + std::to_string(rychlost));
        if (odstup < 0) throw std::invalid_argument("Odstup nemôže byť záporný.");
        tel_ = {rychlost, odstup, stavACC};
    }
    void nastavSignal(bool ok) { signal_ = ok; }

    const Telemetria& nacitaj() const {
        if (!signal_) throw SignalLostException("Strata signálu OBD-II / CAN");
        return tel_;
    }

private:
    std::string idVozidla_;
    Telemetria tel_;
    bool signal_ = true;
};

class GpsModul {
public:
    explicit GpsModul(UsekCesty usek) : usek_(std::move(usek)) {}
    void nastavUsek(UsekCesty usek) { usek_ = std::move(usek); }
    void nastavSignal(bool ok) { signal_ = ok; }
    const UsekCesty& usek() const {
        if (!signal_) throw SignalLostException("Strata GPS signálu");
        return usek_;
    }

private:
    UsekCesty usek_;
    bool signal_ = true;
};

class DopravnyServer {
public:
    void nastav(DopravnaSituacia s) { situacia_ = std::move(s); }
    const DopravnaSituacia& aktualna() const { return situacia_; }

private:
    DopravnaSituacia situacia_;
};


class HUDRenderer {
public:
    HUDRenderer(HWTyp typ, std::string idZariadenia, std::string hwProfil, std::string geometria, int jas)
        : typZariadenia_(typ), idZariadenia_(std::move(idZariadenia)),
          hwProfil_(std::move(hwProfil)), geometria_(std::move(geometria)) {
        nastavJas(jas);
    }
    virtual ~HUDRenderer() = default;

    void nastavJas(int jas) {
        if (jas < 0 || jas > 100) throw std::invalid_argument("Jas musí byť 0–100 %.");
        jas_ = jas;
    }
    void nastavFunkcny(bool f) { funkcny_ = f; } 
    void inicializuj() const {
        overFunkcnost();
        std::cout << "   [HUD] Inicializované: " << hwProfil_ << " (" << geometria_ << ", jas " << jas_ << " %)\n";
    }
    void renderuj(const OdporucaciaZona& zona) const {
        overFunkcnost();
        vykresli(zona);
    }
    void varuj(const std::string& text) const {
        std::cout << "   [" << hwProfil_ << "] ⚠ " << text << "\n";
    }
    const std::string& hwProfil() const { return hwProfil_; }

protected:
    virtual void vykresli(const OdporucaciaZona& zona) const = 0;
    std::string popis(const OdporucaciaZona& z) const {
        return "odporúčané " + kmh(z.odporucana()) + " (aktuálne " + kmh(z.aktualna()) + ") – " + z.typOdporucania();
    }

private:
    void overFunkcnost() const {
        if (!funkcny_) throw HudPoruchaException("Zlyhanie zariadenia: " + hwProfil_);
    }
    HWTyp typZariadenia_;
    std::string idZariadenia_, hwProfil_, geometria_;
    int jas_ = 0;
    bool funkcny_ = true;
};

class Telefon2D : public HUDRenderer {
public:
    explicit Telefon2D(std::string id, int jas = 80)
        : HUDRenderer(HWTyp::TELEFON_2D, std::move(id), "Smartfón + 2D podložka", "2D", jas) {}

protected:
    void vykresli(const OdporucaciaZona& z) const override {
        std::cout << "   [" << hwProfil() << "] gradient " << z.farbaGradientu() << " | " << popis(z) << "\n";
    }
};

class Projektor2D : public HUDRenderer {
public:
    explicit Projektor2D(std::string id, int jas = 90)
        : HUDRenderer(HWTyp::PROJEKTOR_2D, std::move(id), "Samostatný 2D HUD projektor", "2D", jas) {}

protected:
    void vykresli(const OdporucaciaZona& z) const override {
        std::cout << "   [" << hwProfil() << "] gradient " << z.farbaGradientu() << " + piktogram | " << popis(z) << "\n";
    }
};

class LaserARHUD : public HUDRenderer {
public:
    explicit LaserARHUD(std::string id, int jas = 100)
        : HUDRenderer(HWTyp::LASER_AR, std::move(id), "Laser AR-HUD", "3D", jas) {}

protected:
    void vykresli(const OdporucaciaZona& z) const override {
        int pocet = z.typOdporucania() == "BRZDI" ? 5 : (z.typOdporucania() == "SPOMAĽ" ? 3 : 1);
        std::cout << "   [" << hwProfil() << "] 3D šípky " << std::string(pocet, '>') << " " << z.farbaGradientu()
                  << " | " << popis(z) << "\n";
    }
};


class ZaznamJazdy;

class TretiaStrana {
public:
    TretiaStrana(std::string nazov, std::string kontakt) : nazov_(std::move(nazov)), kontakt_(std::move(kontakt)) {}
    void prijmiZaznam(const ZaznamJazdy& zaznam);
    size_t pocetPrijatych() const { return prijate_; }

private:
    std::string nazov_, kontakt_;
    size_t prijate_ = 0;
};

class ZaznamJazdy {
public:
    ZaznamJazdy(std::string id, double limit, double nameranaRychlost, int trvanie)
        : id_(std::move(id)), limit_(limit), nameranaRychlost_(nameranaRychlost), trvaniePrekrocenia_(trvanie) {}

    void aktualizuj(double rychlost, int trvanie) {
        if (stav_ != StavOdoslania::VYTVORENY) return; 
        nameranaRychlost_ = std::max(nameranaRychlost_, rychlost);
        trvaniePrekrocenia_ = trvanie;
    }

    void sifruj() {
        if (stav_ != StavOdoslania::VYTVORENY) return;
        std::ostringstream plain, out;
        plain << id_ << ";limit=" << limit_ << ";max=" << nameranaRychlost_ << ";trvanie=" << trvaniePrekrocenia_ << "s";
        for (unsigned char c : plain.str()) out << std::hex << std::setw(2) << std::setfill('0') << (c ^ 0x5A);
        sifrovane_ = out.str();
        stav_ = StavOdoslania::SIFROVANY;
    }
    void odosli(TretiaStrana& tretiaStrana) {
        if (stav_ == StavOdoslania::ODOSLANY) return;
        sifruj();
        tretiaStrana.prijmiZaznam(*this);
        stav_ = StavOdoslania::ODOSLANY;
    }
    const std::string& id() const { return id_; }
    const std::string& sifrovanyObsah() const { return sifrovane_; }
    int trvanie() const { return trvaniePrekrocenia_; }
    double nameranaRychlost() const { return nameranaRychlost_; }
    StavOdoslania stav() const { return stav_; }

private:
    std::string id_;
    double limit_, nameranaRychlost_;
    int trvaniePrekrocenia_;
    StavOdoslania stav_ = StavOdoslania::VYTVORENY;
    std::string sifrovane_;
};

void TretiaStrana::prijmiZaznam(const ZaznamJazdy& z) {
    ++prijate_;
    std::cout << "   [" << nazov_ << "] Prijatý šifrovaný záznam " << z.id() << " (" << z.sifrovanyObsah().substr(0, 24)
              << "…)\n";
}


class Jazda {
public:
    static constexpr int PRAH_DLHODOBEHO_PREKROCENIA_S = 5;  

    Jazda(std::string id, const Vodic& vodic, HUDRenderer& hud, HUDRenderer& zaloha,
          GpsModul& gps, DopravnyServer& server, ADASRozhranie& adas)
        : id_(std::move(id)), rezim_(vodic.preferovanyRezim()), politika_(RezimPolitika::vytvor(rezim_)),
          hud_(&hud), zaloha_(&zaloha), gps_(gps), server_(server), adas_(adas) {}


    bool inicializuj() {
        casStart_ = std::time(nullptr);
        bezi_ = true;
        std::cout << "[Jazda " << id_ << "] Režim: " << nazov(rezim_) << "\n";
        try {
            try {
                hud_->inicializuj();                       
            } catch (const HudPoruchaException& e) {
                prepniNaZalohu(e.what());
                hud_->inicializuj();
            }
            if (!nacitajVstupy()) hud_->varuj("Chýba časť vstupných dát, čakám na signál.");  
        } catch (const std::runtime_error& e) {
            prerus(e.what());
            return false;
        }
        return true;
    }


    StavTiku tik() {
        if (!bezi_) {
            std::cout << "   [SYSTÉM] Jazda už nebeží – tik ignorovaný.\n";
            return StavTiku::PRERUSENE;
        }
        try {
            if (!nacitajVstupy()) {
                hud_->varuj("Chýba signál – projekcia pozastavená.");
                trvaniePrekrocenia_ = 0;
                return StavTiku::POZASTAVENE;
            }
            OdporucaciaZona zona = vypocitajOdporucanie();  
            zobraz(zona);                                    
            zaznamenajPorusenie();                         
            return StavTiku::OK;
        } catch (const std::runtime_error& e) {
            prerus(e.what());
            return StavTiku::PRERUSENE;
        }
    }

    OdporucaciaZona vypocitajOdporucanie() const {
        OdporucaciaZona zona;
        zona.vypocitaj(tel_.rychlost, limit_, politika_->odporucanaRychlost(limit_, situacia_));
        return zona;
    }


    ZaznamJazdy* zaznamenajPorusenie() {
        if (tel_.rychlost <= limit_) {
            trvaniePrekrocenia_ = 0;
            aktualnyZaznam_ = -1;
            return nullptr;
        }
        ++trvaniePrekrocenia_;
        if (!politika_->zaznamenavaPorusenia() || trvaniePrekrocenia_ < PRAH_DLHODOBEHO_PREKROCENIA_S) return nullptr;

        if (aktualnyZaznam_ < 0) {
            zaznamy_.emplace_back(id_ + "-Z" + std::to_string(zaznamy_.size() + 1), limit_, tel_.rychlost, trvaniePrekrocenia_);
            aktualnyZaznam_ = static_cast<int>(zaznamy_.size()) - 1;
            hud_->varuj("Spomaľte – prekračujete limit! Dlhodobé prekročenie sa zaznamenáva.");
        } else {
            zaznamy_[aktualnyZaznam_].aktualizuj(tel_.rychlost, trvaniePrekrocenia_);
        }
        return &zaznamy_[aktualnyZaznam_];
    }


    void ukonci(TretiaStrana& tretiaStrana) {
        if (bezi_) {
            bezi_ = false;
            casKoniec_ = std::time(nullptr);
        }
        for (auto& z : zaznamy_) z.odosli(tretiaStrana);
        std::cout << "[Jazda " << id_ << "] Ukončená. Zaznamenaných porušení: " << zaznamy_.size()
                  << ", odoslaných: " << tretiaStrana.pocetPrijatych() << ".\n";
    }

private:
    bool nacitajVstupy() {
        bool gpsOk = true, canOk = true;
        try { limit_ = gps_.usek().getLimit(); } catch (const SignalLostException&) { gpsOk = false; }
        try { tel_ = adas_.nacitaj(); } catch (const SignalLostException&) { canOk = false; }
        if (!gpsOk && !canOk) throw SignalLostException("Strata GPS aj OBD-II signálu (napr. dlhý tunel)");
        if (!gpsOk || !canOk) return false;
        situacia_ = server_.aktualna();
        return true;
    }

    void zobraz(const OdporucaciaZona& zona) {
        try {
            hud_->renderuj(zona);
        } catch (const HudPoruchaException& e) {
            prepniNaZalohu(e.what());
            hud_->renderuj(zona);
        }
    }
    void prepniNaZalohu(const std::string& dovod) {
        if (hud_ == zaloha_) throw HudPoruchaException("Zlyhal aj záložný výstup (" + dovod + ")");
        std::cout << "   [SYSTÉM] " << dovod << " → prepínam na záložný výstup: " << zaloha_->hwProfil() << "\n";
        hud_ = zaloha_;
    }
    void prerus(const std::string& dovod) {
        if (!bezi_) return;
        bezi_ = false;
        casKoniec_ = std::time(nullptr);
        for (auto& z : zaznamy_) z.sifruj();  
        std::cout << "   [SYSTÉM] ✖ Projekcia bezpečne prerušená: " << dovod
                  << ". Záznamy (" << zaznamy_.size() << ") ostávajú šifrované v lokálnom sklade.\n";
    }

    std::string id_;
    RezimTyp rezim_;
    std::unique_ptr<RezimPolitika> politika_;
    HUDRenderer* hud_;
    HUDRenderer* zaloha_;
    GpsModul& gps_;
    DopravnyServer& server_;
    ADASRozhranie& adas_;

    std::time_t casStart_ = 0, casKoniec_ = 0;
    bool bezi_ = false;
    double limit_ = 0;
    Telemetria tel_;
    DopravnaSituacia situacia_;
    int trvaniePrekrocenia_ = 0;
    int aktualnyZaznam_ = -1;
    std::vector<ZaznamJazdy> zaznamy_;
};


static void nadpis(const std::string& t) { std::cout << "\n=== " << t << " ===\n"; }

static StavTiku jazdi(Jazda& jazda, ADASRozhranie& adas, double rychlost, int pocetTikov = 1) {
    StavTiku st = StavTiku::OK;
    for (int i = 0; i < pocetTikov; ++i) {
        adas.prijmiData(rychlost, 40, true);
        std::cout << " > vozidlo: " << kmh(rychlost) << "\n";
        st = jazda.tik();
    }
    return st;
}


static void scenarioIdealny() {
    nadpis("1) IDEÁLNY SCENÁR – hlavný scenár Use Case (ECO)");
    Vodic vodic("V1", "Matúš");
    vodic.zvolRezim(RezimTyp::ECO);  

    LaserARHUD laser("L1");
    Projektor2D zaloha("P1");
    GpsModul gps(UsekCesty("U1", 50, "48.73N,18.92E"));
    DopravnyServer doprava;
    ADASRozhranie adas("SK123AB");
    TretiaStrana policia("Polícia SR", "kontakt@minv.sk");

    Jazda jazda("J1", vodic, laser, zaloha, gps, doprava, adas);
    adas.prijmiData(0, 40, true);
    if (!jazda.inicializuj()) return;

    jazdi(jazda, adas, 45, 2);                                     
    doprava.nastav(DopravnaSituacia("E1", "hustá", true, 4));      
    jazdi(jazda, adas, 45, 1);
    doprava.nastav(DopravnaSituacia());                           
    jazdi(jazda, adas, 62, 7);                                   
    jazdi(jazda, adas, 48, 1);                                   
    jazda.ukonci(policia);                                       
}


static void scenarioHranicny() {
    nadpis("2) HRANIČNE RIEŠITEĽNÝ SCENÁR – porucha laseru, fail-over na 2D");
    Vodic vodic("V1", "Matúš");
    LaserARHUD laser("L1");
    Projektor2D zaloha("P1");
    GpsModul gps(UsekCesty("U1", 50, "48.73N,18.92E"));
    DopravnyServer doprava;
    ADASRozhranie adas("SK123AB");
    TretiaStrana policia("Polícia SR", "kontakt@minv.sk");

    Jazda jazda("J2", vodic, laser, zaloha, gps, doprava, adas);
    adas.prijmiData(0, 40, true);
    jazda.inicializuj();
    jazdi(jazda, adas, 45, 2);
    std::cout << " ! simulácia: laserový projektor prestal fungovať\n";
    laser.nastavFunkcny(false);
    jazdi(jazda, adas, 47, 2); 
    jazda.ukonci(policia);
}


static void scenarioMimoHranic() {
    nadpis("3) SITUÁCIA, KTORÚ SYSTÉM NEZVLÁDNE – strata GPS aj OBD-II (tunel)");
    Vodic vodic("V1", "Matúš");
    LaserARHUD laser("L1");
    Projektor2D zaloha("P1");
    GpsModul gps(UsekCesty("U1", 50, "48.73N,18.92E"));
    DopravnyServer doprava;
    ADASRozhranie adas("SK123AB");

    Jazda jazda("J3", vodic, laser, zaloha, gps, doprava, adas);
    adas.prijmiData(0, 40, true);
    jazda.inicializuj();
    jazdi(jazda, adas, 45, 1);
    std::cout << " ! simulácia: vjazd do tunela, vypadol GPS signál\n";
    gps.nastavSignal(false);
    jazdi(jazda, adas, 45, 1);  
    std::cout << " ! simulácia: vypadol aj OBD-II / CAN\n";
    adas.nastavSignal(false);
    jazdi(jazda, adas, 45, 1);  
    jazdi(jazda, adas, 45, 1);  
}


static void ukazkaTestera() {
    nadpis("TESTER – neplatné vstupy");
    auto skus = [](const std::string& popis, auto f) {
        try { f(); std::cout << " ? " << popis << ": prešlo (neočakávané)\n"; }
        catch (const std::invalid_argument& e) { std::cout << " ✔ " << popis << " → odmietnuté: " << e.what() << "\n"; }
    };
    skus("prázdne meno vodiča", [] { Vodic("V9", ""); });
    skus("záporný limit úseku", [] { UsekCesty("U9", -50, "0,0"); });
    skus("limit 900 km/h", [] { UsekCesty("U9", 900, "0,0"); });
    skus("záporná rýchlosť z CAN", [] { ADASRozhranie("X").prijmiData(-10, 40, true); });
    skus("jas 150 %", [] { LaserARHUD("L9", 150); });
    skus("záporné meškanie", [] { DopravnaSituacia("E9", "voľná", false, -3); });
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    scenarioIdealny();
    scenarioHranicny();
    scenarioMimoHranic();
    ukazkaTestera();
    return 0;
}
