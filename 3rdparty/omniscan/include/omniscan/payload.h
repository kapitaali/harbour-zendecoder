#pragma once
// Payload sniffing umbrella (M7). Tries each parser in an order that keeps
// ambiguous inputs honest (explicit markers, then VIN-by-check, then raw
// GS1). Merges into parsed WITHOUT overwriting existing keys.
// Umbrella for all payload parsers (include this for everything).
#include <map>
#include <string>
#include "omniscan/payload/gs1_ai.h"
#include "omniscan/payload/mecard.h"
#include "omniscan/payload/otpauth.h"
#include "omniscan/payload/swissqr.h"
#include "omniscan/payload/vcard.h"
#include "omniscan/payload/vin.h"
#include "omniscan/payload/wifi.h"
#include "omniscan/export.h"

namespace omniscan {
namespace payload {

// Returns true when some parser claimed the text (parsed merged).
OMNISCAN_API bool sniff_into(const std::string& text,
                       std::map<std::string, std::string>& parsed);

}  // namespace payload
}  // namespace omniscan
