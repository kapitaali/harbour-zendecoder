// RM4SCC + KIX codecs (M4a). Alphabet and check math cross-checked against
// Wikipedia RM4SCC (table + worked example) and zint backend/postal.c
// (RM4KIX table, start = Ascender, stop = Full). Independent implementation.
#include "fourstate.h"

namespace omniscan {
namespace postal {
namespace {

// Bar states: 0 = Full, 1 = Ascender, 2 = Descender, 3 = Tracker.
const int kRM4KIX[36][4] = {
    {3, 3, 0, 0}, {3, 2, 1, 0}, {3, 2, 0, 1}, {2, 3, 1, 0}, {2, 3, 0, 1},
    {2, 2, 1, 1}, {3, 1, 2, 0}, {3, 0, 3, 0}, {3, 0, 2, 1}, {2, 1, 3, 0},
    {2, 1, 2, 1}, {2, 0, 3, 1}, {3, 1, 0, 2}, {3, 0, 1, 2}, {3, 0, 0, 3},
    {2, 1, 1, 2}, {2, 1, 0, 3}, {2, 0, 1, 3}, {1, 3, 2, 0}, {1, 2, 3, 0},
    {1, 2, 2, 1}, {0, 3, 3, 0}, {0, 3, 2, 1}, {0, 2, 3, 1}, {1, 3, 0, 2},
    {1, 2, 1, 2}, {1, 2, 0, 3}, {0, 3, 1, 2}, {0, 3, 0, 3}, {0, 2, 1, 3},
    {1, 1, 2, 2}, {1, 0, 3, 2}, {1, 0, 2, 3}, {0, 1, 3, 2}, {0, 1, 2, 3},
    {0, 0, 3, 3},
};

// Per-char (top, bottom) values 1..6 with 0 meaning 6 (weights 4,2,1,0).
const int kTopBottom[36][2] = {
    {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {1, 0}, {2, 1}, {2, 2}, {2, 3},
    {2, 4}, {2, 5}, {2, 0}, {3, 1}, {3, 2}, {3, 3}, {3, 4}, {3, 5}, {3, 0},
    {4, 1}, {4, 2}, {4, 3}, {4, 4}, {4, 5}, {4, 0}, {5, 1}, {5, 2}, {5, 3},
    {5, 4}, {5, 5}, {5, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 0},
};

const char kKRSET[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

}  // namespace

int rm4kix_lookup(Bar4 b0, Bar4 b1, Bar4 b2, Bar4 b3) {
    int q[4] = {(int)b0, (int)b1, (int)b2, (int)b3};
    for (int i = 0; i < 36; ++i) {
        if (kRM4KIX[i][0] == q[0] && kRM4KIX[i][1] == q[1] &&
            kRM4KIX[i][2] == q[2] && kRM4KIX[i][3] == q[3])
            return i;
    }
    return -1;
}

char rm4kix_char(int idx) {
    if (idx < 0 || idx > 35) return '?';
    return kKRSET[idx];
}

int rm4scc_check_idx(const std::string& data) {
    if (data.empty() || data.size() > 50) return -1;
    int top = 0, bottom = 0;
    for (char c : data) {
        int idx = -1;
        if (c >= '0' && c <= '9')
            idx = c - '0';
        else if (c >= 'A' && c <= 'Z')
            idx = c - 'A' + 10;
        else if (c >= 'a' && c <= 'z')
            idx = c - 'a' + 10;
        else
            return -1;
        top += kTopBottom[idx][0];
        bottom += kTopBottom[idx][1];
    }
    int row = (top % 6) - 1;
    int col = (bottom % 6) - 1;
    if (row < 0) row = 5;
    if (col < 0) col = 5;
    return 6 * row + col;
}

char rm4scc_check_char(const std::string& data) {
    int idx = rm4scc_check_idx(data);
    if (idx < 0) return '?';
    return kKRSET[idx];
}

bool parse_rm4scc(const std::vector<Bar4>& bars, std::string& text,
                  char& check) {
    text.clear();
    check = '?';
    int n = static_cast<int>(bars.size());
    // Guards + 4-bar groups, data >= 4 chars + check, cap like zint (50).
    if (n < 2 + 4 * 5 || (n - 2) % 4 != 0) return false;
    int groups = (n - 2) / 4;
    if (groups - 1 < 4 || groups - 1 > 50) return false;
    if (bars[0] != Bar4::Asc || bars[n - 1] != Bar4::Full) return false;
    std::string all;
    try {
        for (int g = 0; g < groups; ++g) {
            int idx = rm4kix_lookup(bars[1 + 4 * g], bars[1 + 4 * g + 1],
                                    bars[1 + 4 * g + 2], bars[1 + 4 * g + 3]);
            if (idx < 0) return false;
            all.push_back(kKRSET[idx]);
        }
    } catch (...) {
        return false;
    }
    std::string data = all.substr(0, all.size() - 1);
    char got = all.back();
    if (rm4scc_check_char(data) != got) return false;
    text = data;
    check = got;
    return true;
}

bool kix_grammar_ok(const std::string& text) {
    // PostNL: 4 digits + 2 letters + 1..5 digits + optional X + 1..6 alnum.
    int n = static_cast<int>(text.size());
    if (n < 7 || n > 18) return false;
    for (int i = 0; i < 4; ++i)
        if (text[i] < '0' || text[i] > '9') return false;
    for (int i = 4; i < 6; ++i)
        if (text[i] < 'A' || text[i] > 'Z') return false;
    int i = 6;
    int nd = 0;
    while (i < n && text[i] >= '0' && text[i] <= '9') {
        ++i;
        ++nd;
    }
    if (nd < 1 || nd > 5) return false;
    if (i == n) return true;
    if (text[i] != 'X') return false;
    ++i;
    int ns = 0;
    while (i < n) {
        char c = text[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
        if (!ok) return false;
        ++i;
        ++ns;
    }
    return ns >= 1 && ns <= 6;
}

bool parse_kix(const std::vector<Bar4>& bars, std::string& text) {
    text.clear();
    int n = static_cast<int>(bars.size());
    if (n % 4 != 0) return false;
    int chars = n / 4;
    if (chars < 7 || chars > 18) return false;
    try {
        for (int g = 0; g < chars; ++g) {
            int idx = rm4kix_lookup(bars[4 * g], bars[4 * g + 1],
                                    bars[4 * g + 2], bars[4 * g + 3]);
            if (idx < 0) return false;
            text.push_back(kKRSET[idx]);
        }
    } catch (...) {
        return false;
    }
    return kix_grammar_ok(text);
}

}  // namespace postal
}  // namespace omniscan
