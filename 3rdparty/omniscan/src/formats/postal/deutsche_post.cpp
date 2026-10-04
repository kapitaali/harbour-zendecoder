// Deutsche Post Identcode / Leitcode wrapper over native ITF (M4a).
// Check rule (both): weights 4,9 alternating from the LEFT over the data
// digits (odd positions x4), C = (10 - sum%10) % 10. Refs: German Wikipedia
// Leitcode + idealsoftware Identcode doc (worked example 56310243031 -> 3).
#include "fourstate.h"
#include "../linear/codecs.h"

namespace omniscan {
namespace postal {

bool dp_check_valid(const std::string& full) {
    int n = static_cast<int>(full.size());
    if (n != 12 && n != 14) return false;
    int sum = 0;
    for (int i = 0; i < n - 1; ++i) {
        if (full[i] < '0' || full[i] > '9') return false;
        sum += (full[i] - '0') * ((i % 2 == 0) ? 4 : 9);
    }
    if (full[n - 1] < '0' || full[n - 1] > '9') return false;
    return (10 - (sum % 10)) % 10 == (full[n - 1] - '0');
}

bool parse_deutsche_post(const std::vector<int>& pixel_runs, std::string& text,
                         std::string& kind) {
    text.clear();
    kind.clear();
    std::string digits;
    if (!linear::parse_itf(pixel_runs, digits)) return false;
    if (digits.size() != 12 && digits.size() != 14) return false;
    if (!dp_check_valid(digits)) return false;
    text = digits;  // check digit included: it is part of the tracking number
    kind = (digits.size() == 12) ? "identcode" : "leitcode";
    return true;
}

}  // namespace postal
}  // namespace omniscan
