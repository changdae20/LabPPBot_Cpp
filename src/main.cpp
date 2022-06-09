#pragma warning( disable : 4251 )
#pragma comment( lib, "ws2_32.lib" )

#include <fstream>
#include <google/protobuf/util/json_util.h>
#include <string>

#include "HTTPRequest.hpp"
#include "core.h"
#include "protobuf/config.pb.h"

#include "checked.h"
#include "utf8_core.h"

enum class STATUS { // Loop가 끝났을 때 반환하는 Status Code
    GOOD,
    UPDATE,
    ERR
};

config::Config __config;

int main() {
    std::string json;
    std::getline( std::ifstream( "src/config.json" ), json, '\0' );
    google::protobuf::util::JsonStringToMessage( json, &__config );
    auto &[ last_chat, last_idx ] = save_last_chat( __config.chatroom_name() );

    while ( true ) {
        auto ret = loop( __config.chatroom_name(), last_chat, last_idx );
        last_chat = ret.first;
        last_idx = ret.second;
        Sleep( 20 );
    }
    return 0;
}