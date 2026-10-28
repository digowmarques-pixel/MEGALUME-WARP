// MD5 (RFC 1321), for PJLink authentication. Not for security.
#pragma once

#include <string>

namespace lumen {

std::string md5Hex(const std::string &data);

} // namespace lumen
