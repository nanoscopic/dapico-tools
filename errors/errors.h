#pragma once

#include <stdexcept>
#include <string>

enum error_code {
    ERROR_UNKNOWN = 0,
    ERROR_FORMAT = 1,
    ERROR_READ_FAILED = 2,
};

class failure_error : public std::runtime_error {
public:
    failure_error(error_code code, std::string message);

    error_code code() const noexcept;

private:
    error_code code_;
};
