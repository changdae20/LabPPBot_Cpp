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
#include "message.h"
#include "protobuf/config.pb.h"
#include "util.h"

void scheduler_boj( std::vector<Message> &message_queue, std::mutex &mq_mutex, std::chrono::minutes &interval = std::chrono::minutes( 10 ) );

#endif // SCHEUDLER_H