#include <iostream>
using namespace std;

int main() {
    char mode;
    cin >> mode;

    if (mode != 'h' && mode != 'v') {
        cout << "Neplatný mód vykreslenia" << endl;
        return 1;
    }

    int n;
    int m;

    cin >> n;
    cin >> m;

    int hist[9];
    for (int i = 0; i < 9; i++) {
        hist[i] = 0;
    }

    int invalidCount = 0;

    for (int i = 0; i < n; i++) {
        int x;
        cin >> x;
        if (x >= m && x <= m + 8) {
            int index = x - m;
            hist[index] = hist[index] + 1;
        } else {
            invalidCount = invalidCount + 1;
        }
    }

    if (mode == 'h') {
        int maxNum = m + 8;
        int width = 0;
        int temp = maxNum;
        if (temp == 0) {
            width = 1;
        } else {
            while (temp > 0) {
                width = width + 1;
                temp = temp / 10;
            }
        }

        for (int i = 0; i < 9; i++) {
            int value = m + i;
            int digits = 0;
            int t = value;
            if (t == 0) {
                digits = 1;
            } else {
                while (t > 0) {
                    digits = digits + 1;
                    t = t / 10;
                }
            }
            int spaces = width - digits;
            for (int s = 0; s < spaces; s++) {
                cout << " ";
            }
            cout << value << ": ";
            for (int k = 0; k < hist[i]; k++) {
                cout << "#";
            }
            cout << endl;
        }

        cout << "Počet neplatných čísel: " << invalidCount << endl;

    } else {
        int maxHeight = invalidCount;
        for (int i = 0; i < 9; i++) {
            if (hist[i] > maxHeight) {
                maxHeight = hist[i];
            }
        }

        for (int row = maxHeight; row >= 1; row--) {

            if (invalidCount >= row) {
                cout << "#";
            } else {
                cout << " ";
            }
            cout << " ";

            for (int i = 0; i < 9; i++) {
                if (hist[i] >= row) {
                    cout << "#";
                } else {
                    cout << " ";
                }
                cout << " ";
            }

            cout << endl;
        }

        cout << "i ";
        for (int i = 1; i <= 9; i++) {
            cout << i << " ";
        }
        cout << endl;
    }

    return 0;
}
