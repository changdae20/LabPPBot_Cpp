#ifndef UTIL_H
#define UTIL_H

#include <regex>
#include <string>
#include <vector>

// UTF-8,16 LIB
#include "checked.h"
#include "utf8_core.h"
#include <Windows.h>

namespace Util {
std::string TOUTF8( std::string &multibyte_str );
std::string URLEncode( const std::string &str );
std::string URLEncode( const std::u16string &u16str );
std::string UTF16toUTF8( const std::u16string &u16str );
std::u16string UTF8toUTF16( const std::string &u8str );
std::vector<std::u16string> split( const std::u16string &str, const std::string &deli );
}; // namespace Util

#endif // UTIL_H