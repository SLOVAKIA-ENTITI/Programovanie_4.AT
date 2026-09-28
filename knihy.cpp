#include <iostream>
#include <string>
#include <ctime> 

using namespace std;

class Kniha {
private:
    string nazov;
    string autor;
    int rokVydania;
    int pocetStran;

public:
    Kniha(string n, string a, int rok, int strany) {
        nazov = n;
        autor = a;
        rokVydania = rok;
        pocetStran = strany;
    }

    void vypisInfo() {
        cout << "Názov: " << nazov << endl;
        cout << "Autor: " << autor << endl;
        cout << "Rok vydania: " << rokVydania << endl;
        cout << "Počet strán: " << pocetStran << endl;
    }

    bool jeStara() {
        time_t cas = time(nullptr);                         
        int aktualnyRok = localtime(&cas)->tm_year + 1900;  
        return (aktualnyRok - rokVydania) > 50;
    }
};

int main() {
    Kniha k1("1984", "George Orwell", 1949, 328);
    Kniha k2("Krstný otec", "Mario Puzo",  1969, 472);
    Kniha k3("Killing Floor", "Lee Child", 1997, 560);


    k1.vypisInfo();
    if (k1.jeStara()) {
        cout << "Táto kniha je stará!" << endl;
    } else {
        cout << "Táto kniha nie je stará!" << endl;
    }
    cout << endl;

    k2.vypisInfo();
    if (k2.jeStara()) {
        cout << "Táto kniha je stará!" << endl;
    } else {
        cout << "Táto kniha nie je stará!" << endl;
    }
    cout << endl;

    k3.vypisInfo();
    if (k3.jeStara()) {
        cout << "Táto kniha je stará!" << endl;
    } else {
        cout << "Táto kniha nie je stará!" << endl;
    }

    return 0;
}