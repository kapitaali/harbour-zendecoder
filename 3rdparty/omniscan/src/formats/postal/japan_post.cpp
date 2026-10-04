// Japan Post 4-state decoder (M4b). See japan_post.md and japan_post.h.
#include "japan_post.h"

namespace omniscan {
namespace postal {
namespace {

const char kKasutset[] = "1234567890-abcdefgh";  // 19 chars

// Bar triples per KASUTSET index (0=Full, 1=Asc, 2=Desc, 3=Track).
const int kJapanTable[19][3] = {
    {0, 0, 3}, {0, 2, 1}, {2, 0, 1}, {0, 1, 2}, {0, 3, 0}, {2, 1, 0},
    {1, 0, 2}, {1, 2, 0}, {3, 0, 0}, {0, 3, 3}, {3, 0, 3}, {2, 1, 3},
    {2, 3, 1}, {1, 2, 3}, {3, 2, 1}, {1, 3, 2}, {3, 1, 2}, {3, 3, 0},
    {0, 0, 0},
};

// Checksum positions ("0123456789-abcdefgh").
int chk_posn(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c == '-') return 10;
    if (c >= 'a' && c <= 'h') return 11 + (c - 'a');
    return -1;
}

}  // namespace

int japanpost_char_index(char c) {
    for (int i = 0; kKasutset[i]; ++i)
        if (kKasutset[i] == c) return i;
    return -1;
}

bool japanpost_expand_input(const std::string& input, std::string& inter) {
    inter.clear();
    if (input.empty()) return false;
    try {
        for (char raw : input) {
            char c = raw;
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
            if ((c >= '0' && c <= '9') || c == '-') {
                inter.push_back(c);
            } else if (c >= 'A' && c <= 'J') {
                inter.push_back('a');
                inter.push_back(static_cast<char>('0' + (c - 'A')));
            } else if (c >= 'K' && c <= 'T') {
                inter.push_back('b');
                inter.push_back(static_cast<char>('0' + (c - 'K')));
            } else if (c >= 'U' && c <= 'Z') {
                inter.push_back('c');
                inter.push_back(static_cast<char>('0' + (c - 'U')));
            } else {
                return false;
            }
            if (inter.size() > 20) return false;
        }
    } catch (...) {
        return false;
    }
    return !inter.empty();
}

char japanpost_check_for(const std::string& inter20) {
    if (inter20.size() != 20) return '?';
    int sum = 0;
    for (char c : inter20) {
        int p = chk_posn(c);
        if (p < 0) return '?';
        sum += p;
    }
    int check = 19 - (sum % 19);
    if (check == 19) check = 0;
    if (check <= 9) return static_cast<char>('0' + check);
    if (check == 10) return '-';
    return static_cast<char>('a' + (check - 11));
}

bool parse_japanpost(const std::vector<Bar4>& bars, std::string& text) {
    text.clear();
    int n = static_cast<int>(bars.size());
    // Fixed frame: [Full,Desc] + 21 groups of 3 + [Desc,Full].
    if (n != 67) return false;
    auto st = [&](int i) { return static_cast<int>(bars[i]); };
    if (st(0) != 0 || st(1) != 2 || st(n - 2) != 2 || st(n - 1) != 0)
        return false;
    std::string groups;
    try {
        for (int g = 0; g < 21; ++g) {
            int b0 = st(2 + 3 * g), b1 = st(2 + 3 * g + 1),
                b2 = st(2 + 3 * g + 2);
            int idx = -1;
            for (int k = 0; k < 19; ++k) {
                if (kJapanTable[k][0] == b0 && kJapanTable[k][1] == b1 &&
                    kJapanTable[k][2] == b2) {
                    idx = k;
                    break;
                }
            }
            if (idx < 0) return false;
            groups.push_back(kKasutset[idx]);
        }
    } catch (...) {
        return false;
    }
    std::string inter = groups.substr(0, 20);
    char want = japanpost_check_for(inter);
    if (want == '?' || groups[20] != want) return false;
    // Strip CC4 pads (trailing 'd's only; 'd' cannot come from input).
    size_t end = inter.size();
    while (end > 0 && inter[end - 1] == 'd') --end;
    if (end == 0) return false;
    // De-pair letters.
    std::string out;
    try {
        for (size_t i = 0; i < end;) {
            char c = inter[i];
            if ((c >= '0' && c <= '9') || c == '-') {
                out.push_back(c);
                ++i;
            } else if (c == 'a' || c == 'b' || c == 'c') {
                if (i + 1 >= end) return false;
                char d = inter[i + 1];
                if (d < '0' || d > '9') return false;
                if (c == 'a')
                    out.push_back(static_cast<char>('A' + (d - '0')));
                else if (c == 'b')
                    out.push_back(static_cast<char>('K' + (d - '0')));
                else {
                    if (d > '5') return false;  // U-Z only
                    out.push_back(static_cast<char>('U' + (d - '0')));
                }
                i += 2;
            } else {
                return false;  // d-h outside pad position
            }
        }
    } catch (...) {
        return false;
    }
    if (out.empty() || out.size() > 20) return false;
    text = out;
    return true;
}

}  // namespace postal
}  // namespace omniscan
