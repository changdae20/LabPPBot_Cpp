#ifndef UTIL_H
#define UTIL_H

#include <ctime>
#include <iomanip>
#include <iostream>
#include <random>
#include <regex>
#include <string>
#include <type_traits>
#include <vector>

// UTF-8,16 LIB
#include "checked.h"
#include "utf8_core.h"
#include <Windows.h>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

template <typename Test, template <typename...> class Ref>
struct is_specialization : std::false_type {};

template <template <typename...> class Ref, typename... Args>
struct is_specialization<Ref<Args...>, Ref> : std::true_type {};

namespace Util {
int time_distance( std::u16string AMPM, std::u16string time );
template <typename T, typename UnaryOperation>
std::enable_if_t<is_specialization<T, std::vector>::value, T> ParseToVec( std::string a, UnaryOperation unary_op );
int rand( int start, int end );
HBITMAP ConvertCVMatToBMP( cv::Mat &frame, bool padding = false );
bool PasteBMPToClipboard( void *bmp );
std::string TOUTF8( std::string &multibyte_str );
std::string URLEncode( const std::string &str );
std::string URLEncode( const std::u16string &u16str );
std::string UTF16toUTF8( const std::u16string &u16str );
std::u16string UTF8toUTF16( const std::string &u8str );
std::vector<std::u16string> split( const std::u16string &str, const std::string &deli );
}; // namespace Util

#endif // UTIL_H