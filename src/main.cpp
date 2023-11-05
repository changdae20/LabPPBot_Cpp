#pragma warning( disable : 4251 )
#pragma comment( lib, "ws2_32.lib" )

#include <filesystem>
#include <fstream>
#include <future>
#include <google/protobuf/util/json_util.h>
#include <mutex>
#include <string>
#include <thread>

#include "HTTPRequest.hpp"
#include "checked.h"
#include "core.h"
#include "message.h"
#include "protobuf/config.pb.h"
#include "protobuf/member.pb.h"
#include "protobuf/message.pb.h"
#include "scheduler.h"
#include "utf8_core.h"
#include "util.h"

config::Config __config;

std::mutex mq_mutex; // Mutex for message_queue
std::vector<Message> message_queue;

// multi-threading으로 구현한 renewal
std::vector<std::future<std::pair<std::string, std::u16string>>> renewal_threads;

int wmain( int argc, wchar_t *argv[] ) {
    std::string json;
    std::getline( std::ifstream( "src/config.json" ), json, '\0' );
    google::protobuf::util::JsonStringToMessage( json, &__config );
    if ( argc > 1 && argv[ 1 ] == std::wstring( L"Up To Date" ) ) {
        Message( Util::FROMUTF8( __config.update_request_room() ), u"이미 최신 버전입니다." ).send();
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
        Message( Util::FROMUTF8( __config.update_request_room() ), log ).send();
    } else { // 업데이트 요청이 아닌 경우, 새로 봇 구동을 시작한 경우이므로 쌓여있던 명령어 데이터 삭제
        for ( const auto &entry : std::filesystem::directory_iterator( "message/data" ) ) {
            std::remove( entry.path().string().c_str() );
        }
    }
    std::thread scheduler( scheduler_boj, std::ref( message_queue ), std::ref( mq_mutex ), std::chrono::minutes( 5 ) ); // 백준 스케쥴러 등록
    while ( true ) {
        auto ret = loop( message_queue, mq_mutex );
        // 메세지 있으면 전송
        {
            mq_mutex.lock();
            if ( !message_queue.empty() ) {
                for ( auto &msg : message_queue ) {
                    msg.send();
                    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
                }
                message_queue.clear();
            }
            mq_mutex.unlock();
        }
        // 갱신 스레드 종료된거 있으면 join하고 삭제
        {
            for ( auto it = renewal_threads.begin(); it != renewal_threads.end(); ) {
                if ( auto status = it->wait_for( std::chrono::seconds( 0 ) ); status == std::future_status::ready ) {
                    auto message = it->get();
                    it = renewal_threads.erase( it );
                    Message( message.first, message.second ).send();
                    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
                } else {
                    ++it;
                }
            }
        }
        if ( ret.first == RETURN_CODE::UPDATE ) {
            __config.set_update_request_room( Util::TOUTF8( ret.second ) );
            std::string json;
            google::protobuf::util::MessageToJsonString( __config, &json );
            std::ofstream( "src/config.json" ) << json;
            scheduler.detach();
            return -12345;
        } else if ( ret.first == RETURN_CODE::SONGUPDATE ) {
            __config.set_update_request_room( Util::TOUTF8( ret.second ) );
            std::string json;
            google::protobuf::util::MessageToJsonString( __config, &json );
            std::ofstream( "src/config.json" ) << json;
            scheduler.detach();
            return -12346;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    }

    return 0;
}