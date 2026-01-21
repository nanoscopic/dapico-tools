#include "errors.h"

failure_error::failure_error(error_code code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

error_code failure_error::code() const noexcept {
    return code_;
}
