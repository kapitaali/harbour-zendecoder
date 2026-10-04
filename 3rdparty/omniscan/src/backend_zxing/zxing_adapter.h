// zxing backend adapter.
// With OMNISCAN_WITH_ZXING=ON this wraps the real zxing-cpp decoder.
// Otherwise it is an honest stub reporting BackendNotAvailable for Grade-1.
#pragma once
#include "omniscan/image.h"
#include "omniscan/options.h"
#include "omniscan/result.h"
#include "omniscan/symbology.h"
#include <vector>

namespace omniscan {
namespace backend_zxing {

struct Backend {
    virtual ~Backend() = default;
    virtual const char* name() const noexcept = 0;
    virtual const char* version() const noexcept = 0;
    virtual bool available() const noexcept = 0;
    virtual SymMask handled() const noexcept = 0;
    virtual DecodeStatus decode(const ImageView&, const Options&,
                                std::vector<Result>&) const noexcept = 0;
};

const Backend& instance() noexcept;

} // namespace backend_zxing
} // namespace omniscan
