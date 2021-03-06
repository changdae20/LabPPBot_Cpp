#pragma warning( disable : 4251 )
#pragma comment( lib, "ws2_32.lib" )

#include <fstream>
#include <google/protobuf/util/json_util.h>
#include <string>

#include "HTTPRequest.hpp"
#include "core.h"
#include "protobuf/config.pb.h"
#include "util.h"

#include "checked.h"
#include "utf8_core.h"

enum class STATUS { // Loop가 끝났을 때 반환하는 Status Code
    GOOD,
    UPDATE,
    ERR
};

config::Config __config;

int wmain( int argc, wchar_t *argv[] ) {
    std::string json;
    std::getline( std::ifstream( "src/config.json" ), json, '\0' );
    google::protobuf::util::JsonStringToMessage( json, &__config );
    auto &[ last_chat, last_idx ] = save_last_chat( __config.chatroom_name() );

    if ( argc > 1 && argv[ 1 ] == std::wstring( L"Up To Date" ) ) {
        kakao_sendtext( __config.chatroom_name(), u"이미 최신 버전입니다." );
    } else if ( argc > 1 && ( argv[ 1 ] != std::wstring( L"Up To Date" ) && argv[ 1 ] != std::wstring( L"initial" ) ) ) {
        std::string logs_raw;
        std::wstring t( argv[ 1 ] );

        logs_raw.assign( t.begin(), t.end() );
        std::regex re( "!@#" );
        std::sregex_token_iterator token_it( logs_raw.begin(), logs_raw.end(), re, -1 ), end;
        std::vector<std::string> logs( token_it, end );
        std::u16string log = u"업데이트가 완료되었습니다.\n\n[업데이트 목록]\n";
        for ( auto it = logs.rbegin(); it != logs.rend(); ++it ) {
            log += Util::UTF8toUTF16( std::to_string( std::distance( logs.rbegin(), it ) + 1 ) ) + std::u16string( u". " ) + Util::UTF8toUTF16( *it ) + std::u16string( u"\n" );
        }
        log.erase( log.end() - 1 );
        kakao_sendtext( __config.chatroom_name(), log );
    }
    while ( true ) {
        auto ret = loop( __config.chatroom_name(), last_chat, last_idx );
        last_chat = ret.first;
        last_idx = ret.second;
        if ( last_chat == u"Update" && last_idx == -12345 ) {
            return -12345;
        } else if ( last_chat == u"Error" && last_idx == -24680 ) {
            auto reload = save_last_chat( __config.chatroom_name() );
            last_chat = reload.first;
            last_idx = reload.second;
        }
        Sleep( 20 );
    }
    return 0;
}