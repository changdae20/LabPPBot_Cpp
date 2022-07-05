#ifndef CORE_H
#define CORE_H

#include "protobuf/achievement.pb.h"
#include "protobuf/config.pb.h"
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
    CLEAR,
    OK,
    ERROR
};

void SendReturn( HWND hwnd );
void kakao_sendtext( const std::string &chatroom_name, const std::u16string &text );
void kakao_sendimage( const std::string &chatroom_name );

void PostKeyEx( HWND &hwnd, UINT key, WPARAM shift, bool specialkey );

void achievement_count( const std::u16string &name, int counter_id, int val );

std::u16string GetClipboardText_Utf16();

std::u16string copy_chatroom( const std::string &chatroom_name );

std::pair<std::u16string, int> save_last_chat( const std::string &chatroom_name );

std::pair<std::u16string, int> loop( const std::string &chatroom_name, const std::u16string &last_chat, int last_idx );

RETURN_CODE execute_command( const std::string &chatroom_name, const std::u16string &name, const std::u16string &AMPM, const std::u16string &time, const std::u16string &msg );

#endif // CORE_H