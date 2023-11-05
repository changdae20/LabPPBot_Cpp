#ifndef CORE_H
#define CORE_H

#include "message.h"
#include "protobuf/achievement.pb.h"
#include "protobuf/config.pb.h"
#include "protobuf/member.pb.h"
#include "protobuf/message.pb.h"
#include "protobuf/popn_songs.pb.h"
#include "protobuf/price.pb.h"
#include "protobuf/sdvx_songs.pb.h"
#include "protobuf/turtle.pb.h"
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <windows.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

enum class RETURN_CODE {
    UPDATE,
    SONGUPDATE,
    CLEAR,
    OK,
    ERR
};

void achievement_count( std::vector<Message> &message_queue, std::mutex &mq_mutex, std::u16string name, int member_id, int counter_id, int val );

std::pair<RETURN_CODE, std::string> loop( std::vector<Message> &message_queue, std::mutex &mq_mutex );

std::pair<bool, member::Member> find_by_name( const std::u16string &name, const std::string &chatroom_name );
std::pair<bool, member::Member> find_by_hash( const std::u16string &name, const std::string &chatroom_name );

RETURN_CODE execute_command( std::vector<Message> &message_queue, std::mutex &mq_mutex, const std::string &chatroom_name, const std::u16string &_name, const std::u16string &msg, bool is_groupchat );

#endif // CORE_H