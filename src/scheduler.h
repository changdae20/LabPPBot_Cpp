#ifndef SCHEUDLER_H
#define SCHEUDLER_H

#include <chrono>
#include <fmt/core.h>
#include <fmt/xchar.h>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "HTTPRequest.hpp"
#include "protobuf/config.pb.h"
#include "util.h"

void scheduler_boj( std::vector<std::u16string> &scheduler_message, std::mutex &m, std::chrono::minutes &interval = std::chrono::minutes( 10 ) );

#endif // SCHEUDLER_H