#include <algorithm>
#include <array>
#include <fmt/core.h>
#include <fmt/xchar.h>
#include <google/protobuf/util/json_util.h>
#include <iostream>
#include <regex>
#include <string>
#include <utility>
#include <windows.h>

#include "HTTPRequest.hpp"
#include "core.h"
#include "util.h"

extern config::Config __config;

std::u16string quiz_answer;
std::vector<std::u16string> hint_request;

void SendReturn( HWND hwnd ) {
    PostMessage( hwnd, WM_KEYDOWN, VK_RETURN, 0 );
    Sleep( 10 );
    PostMessage( hwnd, WM_KEYUP, VK_RETURN, 0 );
    return;
}

void kakao_sendtext( const std::string &chatroom_name, const std::u16string &text ) {
    HWND hwnd = ::FindWindowA( NULL, reinterpret_cast<LPCSTR>( chatroom_name.c_str() ) );
    if ( hwnd == nullptr ) {
        std::cout << "Chatroom Not Opened!\n";
    } else {
        // std::cout << "Chatroom is Opened, hwnd : " << hwnd << "\n";
        auto child_wnd = ::FindWindowExA( hwnd, NULL, reinterpret_cast<LPCSTR>( "RICHEDIT50W" ), NULL );
        // std::cout << child_wnd << std::endl;
        ::SendMessageW( child_wnd, WM_SETTEXT, 0, reinterpret_cast<LPARAM>( text.c_str() ) );
        SendReturn( child_wnd );
    }
}

void kakao_sendimage( const std::string &chatroom_name ) {
    HWND hwnd = ::FindWindowA( NULL, reinterpret_cast<LPCSTR>( chatroom_name.c_str() ) );
    if ( hwnd == nullptr ) {
        std::cout << "Chatroom Not Opened!\n";
    } else {
        auto child_wnd = ::FindWindowExA( hwnd, NULL, reinterpret_cast<LPCSTR>( "EVA_VH_ListControl_Dblclk" ), NULL );
        SetForegroundWindow( child_wnd );
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        PostKeyEx( child_wnd, static_cast<UINT>( 'V' ), VK_CONTROL, false );
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        SendReturn( GetForegroundWindow() );
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    }
}

void PostKeyEx( HWND &hwnd, UINT key, WPARAM shift, bool specialkey ) {
    auto ThreadId = GetWindowThreadProcessId( hwnd, NULL );

    LPARAM lparam = MAKELONG( 0, MapVirtualKeyA( key, 0 ) );
    if ( specialkey ) {
        lparam |= 0x1000000;
    }

    if ( shift != NULL ) {
        BYTE pKeyBuffers[ 256 ];
        BYTE pKeyBuffers_old[ 256 ];

        SendMessage( hwnd, WM_ACTIVATE, WA_ACTIVE, 0 );
        AttachThreadInput( GetCurrentThreadId(), ThreadId, true );
        GetKeyboardState( pKeyBuffers_old );

        if ( shift == VK_MENU ) {
            lparam = lparam | 0x20000000;
            pKeyBuffers[ shift ] |= 128;
            SetKeyboardState( pKeyBuffers );
            PostMessage( hwnd, WM_SYSKEYDOWN, key, lparam );
            PostMessage( hwnd, WM_SYSKEYUP, key, lparam | 0xC0000000 );
            SetKeyboardState( pKeyBuffers_old );
            AttachThreadInput( GetCurrentThreadId(), ThreadId, false );
        } else {
            pKeyBuffers[ shift ] |= 128;
            SetKeyboardState( pKeyBuffers );
            Sleep( 50 );
            PostMessage( hwnd, WM_KEYDOWN, key, lparam );
            Sleep( 50 );
            PostMessage( hwnd, WM_KEYUP, key, lparam | 0xC0000000 );
            Sleep( 50 );
            SetKeyboardState( pKeyBuffers_old );
            Sleep( 50 );
            AttachThreadInput( GetCurrentThreadId(), ThreadId, false );
        }
    } else {
        SendMessage( hwnd, WM_KEYDOWN, key, lparam );
        SendMessage( hwnd, WM_KEYUP, key, lparam | 0xC0000000 );
    }
    return;
}

void achievement_count( const std::u16string &name, int counter_id, int val ) {
    http::Request request{ __config.api_endpoint() + "counter" };
    const std::string body = fmt::format( "name={}&counter_id={}&counter_value={}", Util::URLEncode( name ), counter_id, val );
    auto response = request.send( "POST", body, { { "Content-Type", "application/x-www-form-urlencoded" } } );
    db::AchievementList list;
    std::string res_text = std::string( response.body.begin(), response.body.end() );
    auto replaced = "{\"achievements\" : " + std::regex_replace( res_text, std::regex( "goal_counter" ), "goalCounter" ) + "}";
    google::protobuf::util::JsonStringToMessage( replaced.c_str(), &list );

    for ( const auto &achievement : list.achievements() ) {
        if ( achievement.type() == "normal" ) { // normal 업적의 경우 그냥 출력하면 됨
            kakao_sendtext( __config.chatroom_name(), fmt::format( u"⭐{}님의 새로운 업적⭐\n[{}] {}\n***{}***", name, Util::UTF8toUTF16( achievement.tag() ), Util::UTF8toUTF16( achievement.name() ), Util::UTF8toUTF16( achievement.description() ) ) );
        }

        if ( achievement.type() == "hidden" ) { // hidden 업적의 경우 달성 유저가 3명이상인 경우에만 설명 출력
            request = http::Request{ fmt::format( "{}achievements/achievement_info?achievements_id={}", __config.api_endpoint(), achievement.id() ) };
            response = request.send( "GET" );
            res_text = std::string( response.body.begin(), response.body.end() );
            auto replaced = std::regex_replace( res_text, std::regex( "counter_id" ), "counterId" );
            replaced = std::regex_replace( res_text, std::regex( "goal_counter" ), "goalCounter" );
            replaced = std::regex_replace( res_text, std::regex( "achieved_user_list" ), "achievedUserList" );
            replaced = std::regex_replace( res_text, std::regex( "achievements_id" ), "achievementsId" );

            db::AchievementInfo info;
            google::protobuf::util::JsonStringToMessage( replaced.c_str(), &info );

            if ( info.achievement_user_list_size() >= 3 ) {
                kakao_sendtext( __config.chatroom_name(), fmt::format( u"⭐{}님의 새로운 업적⭐\n[{}] {}\n***{}***", name, Util::UTF8toUTF16( achievement.tag() ), Util::UTF8toUTF16( achievement.name() ), Util::UTF8toUTF16( achievement.description() ) ) );
            } else {
                auto replaced_description = std::regex_replace( achievement.description(), std::regex( "[^\\s]" ), "?" );
                kakao_sendtext( __config.chatroom_name(), fmt::format( u"⭐{}님의 새로운 업적⭐\n[{}] {}\n***{}***", name, Util::UTF8toUTF16( achievement.tag() ), Util::UTF8toUTF16( achievement.name() ), Util::UTF8toUTF16( replaced_description ) ) );
            }
        }
    }
    return;
}
std::u16string GetClipboardText_Utf16() {
    std::u16string strData;

    if ( !OpenClipboard( NULL ) )
        return std::u16string();

    HANDLE hData = GetClipboardData( CF_UNICODETEXT );
    if ( hData == nullptr )
        return std::u16string();

    char16_t *pszText = static_cast<char16_t *>( GlobalLock( hData ) );
    if ( pszText == nullptr )
        return std::u16string();

    std::u16string text( pszText );
    GlobalUnlock( hData );
    CloseClipboard();

    return text;
}

std::u16string copy_chatroom( const std::string &chatroom_name ) {
    HWND hwnd = ::FindWindowA( NULL, reinterpret_cast<LPCSTR>( chatroom_name.c_str() ) );
    auto child_wnd = ::FindWindowExA( hwnd, NULL, reinterpret_cast<LPCSTR>( "EVA_VH_ListControl_Dblclk" ), NULL );
    if ( child_wnd == nullptr )
        return std::u16string();
    PostKeyEx( child_wnd, static_cast<UINT>( 'A' ), VK_CONTROL, false );
    Sleep( 10 );
    PostKeyEx( child_wnd, static_cast<UINT>( 'C' ), VK_CONTROL, false );
    return GetClipboardText_Utf16();
}

std::pair<std::u16string, int> save_last_chat( const std::string &chatroom_name ) {
    std::u16string chat_rawdata = copy_chatroom( chatroom_name );
    auto splitted = Util::split( chat_rawdata, "\r\n" );

    return std::pair( splitted.at( splitted.size() - 1 ), splitted.size() );
}

std::pair<std::u16string, int> loop( const std::string &chatroom_name, const std::u16string &last_chat, int last_idx ) {
    std::cout << __LINE__ << " | " << ( *last_chat.c_str() ) << ", " << last_idx << std::endl;
    std::u16string chat_rawdata = copy_chatroom( chatroom_name );
    auto splitted = Util::split( chat_rawdata, "\r\n" );

    std::regex chat_pattern( u8"\\[([\\S\\s]+)\\] \\[(오전|오후) ([0-9:\\s]+)\\] ([\\S\\s]+)" );
    std::regex date_pattern( u8"[0-9]+년 [0-9]+월 [0-9]+일 (월|화|수|목|금|토|일)요일" );

    if ( last_idx == splitted.size() ) { // 채팅이 없는 경우
        std::cout << "채팅 없음...\n";
        return std::pair( last_chat, last_idx );
    } else if ( last_idx > splitted.size() ) { // 더 작아지면 잘못 동작한 경우로, 새로 로딩해야함
        return std::pair( splitted.at( splitted.size() - 1 ), splitted.size() );
    } else { // 채팅이 새로 있는 경우
        splitted.erase( splitted.begin(), splitted.begin() + last_idx );
        std::vector<int> indices{ 1, 2, 3, 4 };
        for ( const auto &__line : splitted ) { // 새로운 채팅에 대해서 loop
            auto line = Util::UTF16toUTF8( __line );
            if ( !std::regex_match( line, chat_pattern ) )
                continue;
            std::sregex_token_iterator it( line.begin(), line.end(), chat_pattern, indices ), end;
            std::vector<std::u16string> tokens;
            for ( ; it != end; ++it )
                tokens.push_back( Util::UTF8toUTF16( *it ) );
            auto ret = execute_command( chatroom_name, tokens[ 0 ], tokens[ 1 ], tokens[ 2 ], tokens[ 3 ] );
            if ( ret == RETURN_CODE::UPDATE )
                return std::pair( u"Update", -12345 );
            else if ( ret == RETURN_CODE::ERR ) {
                return std::pair( u"Error", -24680 );
            }
        }
        return std::pair( splitted.at( splitted.size() - 1 ), last_idx + splitted.size() );
    }
}

RETURN_CODE execute_command( const std::string &chatroom_name, const std::u16string &name, const std::u16string &AMPM, const std::u16string &time, const std::u16string &msg ) {
    if ( name == u"EndTime" ) {
        return RETURN_CODE::OK;
    }

    if ( Util::time_distance( AMPM, time ) >= 3 ) {
        return RETURN_CODE::ERR;
    }

    if ( msg == u"/자라" ) {
        if ( Util::rand( 1, 100 ) == 100 ) { // 1%
            kakao_sendtext( chatroom_name, std::u16string( u"거북이" ) );
        } else {
            kakao_sendtext( chatroom_name, std::u16string( u"자라" ) );
        }
        return RETURN_CODE::OK;
    }

    if ( msg == u"/자라자라" ) {
        if ( std::ifstream( "src/zara_data.json" ).fail() ) {
            std::cout << "Fail!" << std::endl;
            std::ofstream o( "src/zara_data.json" );
            o << "{\"dict\":{}}";
        }
        std::string json;
        std::getline( std::ifstream( "src/zara_data.json" ), json, '\0' );
        turtle::ZaraData data;
        google::protobuf::util::JsonStringToMessage( json, &data );

        if ( ( data.dict().find( Util::UTF16toUTF8( name ) ) != data.dict().end() ) && ( *data.mutable_dict() )[ Util::UTF16toUTF8( name ) ] > std::time( NULL ) - 3600 * 5 ) { // 쿨이 안돈 경우
            int sec = ( *data.mutable_dict() )[ Util::UTF16toUTF8( name ) ] + 3600 * 5 - std::time( NULL );
            int hour = sec / 3600;
            int min = ( sec % 3600 ) / 60;
            sec %= 60;
            kakao_sendtext( chatroom_name, fmt::format( u"아직 연속자라를 사용할 수 없습니다 : {}시간 {}분 {}초 남음", hour, min, sec ) );
        } else {                                                                        // 쿨이 돈 경우
            std::array<std::u16string, 5> arr;                                          // 5번 가챠 결과 담는 컨테이너
            std::vector<int> ages;                                                      // 가챠 성공결과 담는 컨테이너
            bool is_quiz = ( *data.mutable_dict() )[ Util::UTF16toUTF8( name ) ] == -1; // 퀴즈로 쿨초받은 경우 -1로 세팅되어있음.
            for ( auto &el : arr ) {
                if ( Util::rand( 1, 100 ) == 100 ) { // 1%
                    el = u"거북이";
                    ages.push_back( data.age() );
                    data.set_age( 0 );
                } else {
                    data.set_age( data.age() + 1 );
                    el = u"자라";
                }
            }

            kakao_sendtext( chatroom_name, fmt::format( u"{}\n{}\n{}\n{}\n{}", arr[ 0 ], arr[ 1 ], arr[ 2 ], arr[ 3 ], arr[ 4 ] ) );

            if ( ages.size() > 0 ) {                                              // 가챠로 먹은 경우
                if ( std::find( ages.begin(), ages.end(), 100 ) != ages.end() ) { // 정확하게 100살짜리를 먹은 경우
                    achievement_count( name, 28, 1 );
                }
                auto [ min, max ] = std::minmax_element( ages.begin(), ages.end() );
                achievement_count( name, 7, *max );
                achievement_count( name, 8, *min );
                if ( ages.size() >= 2 ) { // 쌍거북 이상의 경우
                    achievement_count( name, 7 + ages.size(), 1 );
                }
                if ( is_quiz ) { // 퀴즈 거북인 경우
                    achievement_count( name, 3, ages.size() );
                } else { // normal case
                    achievement_count( name, 1, ages.size() );
                }
            }

            achievement_count( name, 5, 1 );               // 쿨이 돈 연챠를 실행
            achievement_count( name, 6, 5 - ages.size() ); // 거북이 먹은 개수 추가
            ( *data.mutable_dict() )[ Util::UTF16toUTF8( name ) ] = std::time( NULL );
            json.clear();
            google::protobuf::util::MessageToJsonString( data, &json );
            std::ofstream o( "src/zara_data.json" );
            o << json;
        }

        return RETURN_CODE::OK;
    }

    if ( msg == u"/거북이" ) {
        if ( std::ifstream( "src/zara_data.json" ).fail() ) { // 저장 파일 못찾은 경우
            int zara_count = 0;
        }
        std::string json;
        std::getline( std::ifstream( "src/zara_data.json" ), json, '\0' );
        turtle::ZaraData data;
        google::protobuf::util::JsonStringToMessage( json, &data );

        kakao_sendtext( chatroom_name, fmt::format( u"현재 거북이 이후 {}연속 자라입니다.", data.age() ) );
    }
    if ( msg == u"/인벤" || msg == u"/인벤토리" ) { // 자신의 인벤
        http::Request request{ fmt::format( "{}counter/inventory?name={}", __config.api_endpoint(), Util::URLEncode( name ) ) };
        auto response = request.send( "GET" );
        auto res_text = std::string( response.body.begin(), response.body.end() );
        std::regex inven_pattern( "\\{\"1\":([0-9]+),\"2\":([0-9]+),\"3\":([0-9]+),\"6\":([0-9]+),\"7\":([0-9]+),\"8\":([-]*[0-9]+),\"29\":([0-9]+)\\}" );
        std::vector<int> indices{ 1, 2, 3, 4, 5, 6, 7 };
        std::sregex_token_iterator it( res_text.begin(), res_text.end(), inven_pattern, indices ), end;
        std::vector<std::u16string> tokens;
        for ( ; it != end; ++it )
            tokens.push_back( Util::UTF8toUTF16( *it ) );

        tokens[ 4 ] = tokens[ 4 ] != u"0" ? tokens[ 4 ] : u"데이터 없음";
        tokens[ 5 ] = tokens[ 5 ] != u"-10000" ? Util::UTF8toUTF16( std::to_string( -std::stoi( Util::UTF16toUTF8( tokens[ 5 ] ) ) ) ) : u"데이터 없음";

        kakao_sendtext( chatroom_name, fmt::format( u"<<{}님의 인벤토리>>\n\n연챠 거북이 : {}\n단챠 거북이 : {}\n자연산 거북이 : {}\n퀴즈 거북이 : {}\n\n자라 : {}\n\n최고령 거북이 : {}\n최연소 거북이 : {}", name, tokens[ 0 ], tokens[ 6 ], tokens[ 1 ], tokens[ 2 ], tokens[ 3 ], tokens[ 4 ], tokens[ 5 ] ) );
    } else if ( msg.rfind( u"/인벤 ", 0 ) == 0 || msg.rfind( u"/인벤토리 ", 0 ) == 0 ) { // 타인의 인벤
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(/인벤|/인벤토리) ([\\s\\S]+)" ) );
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );

        http::Request request{ __config.api_endpoint() + "member?chatroom_name=" + Util::URLEncode( chatroom_name ) };
        auto response = request.send( "GET" );
        std::string res_text = std::string( response.body.begin(), response.body.end() );
        if ( res_text == "[]" ) { // DB에 해당 단체방에 대한 정보가 없음
            kakao_sendtext( chatroom_name, u"지원하지 않는 단체방입니다." );
        } else {
            auto splitted = Util::split( Util::UTF8toUTF16( std::string( res_text.begin() + 1, res_text.end() - 1 ) ), "," );
            if ( std::find( splitted.begin(), splitted.end(), fmt::format( u"\"{}\"", query_name ) ) != splitted.end() ) { // 멤버를 찾음
                request = http::Request( fmt::format( "{}counter/inventory?name={}", __config.api_endpoint(), Util::URLEncode( query_name ) ) );
                response = request.send( "GET" );
                res_text = std::string( response.body.begin(), response.body.end() );
                std::regex inven_pattern( "\\{\"1\":([0-9]+),\"2\":([0-9]+),\"3\":([0-9]+),\"6\":([0-9]+),\"7\":([0-9]+),\"8\":([-]*[0-9]+),\"29\":([0-9]+)\\}" );
                std::vector<int> indices{ 1, 2, 3, 4, 5, 6, 7 };
                std::sregex_token_iterator it( res_text.begin(), res_text.end(), inven_pattern, indices ), end;
                std::vector<std::u16string> tokens;
                for ( ; it != end; ++it )
                    tokens.push_back( Util::UTF8toUTF16( *it ) );

                tokens[ 4 ] = tokens[ 4 ] != u"0" ? tokens[ 4 ] : u"데이터 없음";
                tokens[ 5 ] = tokens[ 5 ] != u"-10000" ? Util::UTF8toUTF16( std::to_string( -std::stoi( Util::UTF16toUTF8( tokens[ 5 ] ) ) ) ) : u"데이터 없음";

                kakao_sendtext( chatroom_name, fmt::format( u"<<{}님의 인벤토리>>\n\n연챠 거북이 : {}\n단챠 거북이 : {}\n자연산 거북이 : {}\n퀴즈 거북이 : {}\n\n자라 : {}\n\n최고령 거북이 : {}\n최연소 거북이 : {}", query_name, tokens[ 0 ], tokens[ 6 ], tokens[ 1 ], tokens[ 2 ], tokens[ 3 ], tokens[ 4 ], tokens[ 5 ] ) );
            } else {
                kakao_sendtext( chatroom_name, u"단체방 멤버를 찾을 수 없습니다." );
            }
        }
    }

    if ( msg.rfind( u"/곡정보 ", 0 ) == 0 ) {
        auto args = Util::split( msg, " " );
        http::Response response;
        if ( args.size() == 2 && args[ 1 ] != u"" ) { // /곡정보 별명
            http::Request request{ __config.api_endpoint() + "songs?title=" + Util::URLEncode( ( args[ 1 ] ) ) };
            response = request.send( "GET" );
        } else if ( args.size() == 3 && args[ 1 ] != u"" && args[ 2 ] != u"" ) { // /곡정보 별명 레벨
            http::Request request{ __config.api_endpoint() + "songs?title=" + Util::URLEncode( ( args[ 1 ] ) ) + "&kind=" + Util::URLEncode( ( args[ 2 ] ) ) };
            response = request.send( "GET" );
        }
        const std::string res_text = std::string( response.body.begin(), response.body.end() );
        if ( res_text == "{}" ) { // 검색 결과가 없는 경우
            kakao_sendtext( chatroom_name, u"곡정보를 찾을 수 없습니다." );
            // TODO : 검색통해서 ~~~를 찾으시나요? 출력
        } else {
            std::string replaced = std::regex_replace( res_text, std::regex( "chain_vi" ), "chainVi" );
            replaced = std::regex_replace( res_text, std::regex( "chain_v" ), "chainV" );
            db::SdvxSong song;
            google::protobuf::util::JsonStringToMessage( replaced.c_str(), &song );
            kakao_sendtext( chatroom_name, u"제목 : " + Util::UTF8toUTF16( song.title() ) +
                                               u"\n레벨 : " + Util::UTF8toUTF16( std::to_string( song.level() ) ) +
                                               u"\n작곡가 : " + Util::UTF8toUTF16( song.artist() ) +
                                               u"\n이펙터 : " + Util::UTF8toUTF16( song.effector() ) +
                                               u"\n일러스트레이터 : " + Util::UTF8toUTF16( song.illustrator() ) +
                                               u"\nBPM : " + Util::UTF8toUTF16( song.bpm() ) +
                                               u"\n체인수 : " + Util::UTF8toUTF16( std::to_string( song.chain_vi() ) ) );

            std::string lower_code;
            std::transform( song.code().begin(), song.code().end(), back_inserter( lower_code ), ::tolower );
            http::Request jacket_request{ __config.storage_server() + "songs/" + lower_code + "/jacket.png" };

            try {
                const auto jacket_response = jacket_request.send( "GET", "", {}, std::chrono::seconds( 4 ) );
                auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( jacket_response.body.data() ), static_cast<std::streamsize>( jacket_response.body.size() ) ), cv::IMREAD_UNCHANGED );
                auto bmp = Util::ConvertCVMatToBMP( frame );
                if ( Util::PasteBMPToClipboard( bmp ) ) {
                    kakao_sendimage( chatroom_name );
                }
            } catch ( const http::ResponseError &e ) {
                try {
                    auto frame = cv::imread( fmt::format( "songs/{}/jacket.png", lower_code ), cv::IMREAD_UNCHANGED );
                    auto bmp = Util::ConvertCVMatToBMP( frame );
                    if ( Util::PasteBMPToClipboard( bmp ) ) {
                        kakao_sendimage( chatroom_name );
                    }
                } catch ( cv::Exception &e ) {
                    kakao_sendtext( chatroom_name, fmt::format( u"자켓을 찾을 수 없습니다.\nErr : {}", Util::UTF8toUTF16( e.what() ) ) );
                }
            }
        }
    }

    if ( msg.rfind( u"/채보 ", 0 ) == 0 ) {
        auto args = Util::split( msg, " " );
        http::Response response;
        if ( args.size() == 2 && args[ 1 ] != u"" ) { // /채보 별명
            http::Request request{ __config.api_endpoint() + "songs?title=" + Util::URLEncode( ( args[ 1 ] ) ) };
            response = request.send( "GET" );
        } else if ( args.size() == 3 && args[ 1 ] != u"" && args[ 2 ] != u"" ) { // /채보 별명 레벨
            http::Request request{ __config.api_endpoint() + "songs?title=" + Util::URLEncode( ( args[ 1 ] ) ) + "&kind=" + Util::URLEncode( ( args[ 2 ] ) ) };
            response = request.send( "GET" );
        }
        const std::string res_text = std::string( response.body.begin(), response.body.end() );
        if ( res_text == "{}" ) { // 검색 결과가 없는 경우
            kakao_sendtext( chatroom_name, u"곡정보를 찾을 수 없습니다." );
            // TODO : 검색통해서 ~~~를 찾으시나요? 출력
        } else {
            std::string replaced = std::regex_replace( res_text, std::regex( "chain_vi" ), "chainVi" );
            replaced = std::regex_replace( res_text, std::regex( "chain_v" ), "chainV" );
            db::SdvxSong song;
            google::protobuf::util::JsonStringToMessage( replaced.c_str(), &song );

            std::string lower_code;
            std::transform( song.code().begin(), song.code().end(), back_inserter( lower_code ), ::tolower );
            http::Request chart_request{ __config.storage_server() + "songs/" + lower_code + "/chart.png" };

            try {
                const auto chart_response = chart_request.send( "GET", "", {}, std::chrono::seconds( 10 ) );
                auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( chart_response.body.data() ), static_cast<std::streamsize>( chart_response.body.size() ) ), cv::IMREAD_UNCHANGED );
                auto bmp = Util::ConvertCVMatToBMP( frame );
                if ( Util::PasteBMPToClipboard( bmp ) ) {
                    kakao_sendimage( chatroom_name );
                }
            } catch ( const http::ResponseError &e ) {
                try {
                    auto frame = cv::imread( fmt::format( "songs/{}/chart.png", lower_code ), cv::IMREAD_UNCHANGED );
                    auto bmp = Util::ConvertCVMatToBMP( frame );
                    if ( Util::PasteBMPToClipboard( bmp ) ) {
                        kakao_sendimage( chatroom_name );
                    }
                } catch ( cv::Exception &e ) {
                    kakao_sendtext( chatroom_name, fmt::format( u"자켓을 찾을 수 없습니다.\nErr : {}", Util::UTF8toUTF16( e.what() ) ) );
                }
            }
        }
    }

    if ( msg.rfind( u"/점수조회 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::u16string query_name, title, level, nick; // 쿼리용 변수
        http::Response title_response;
        std::u16string score, clear_lamp;                                                                 // 결과
        if ( std::regex_match( u8msg, std::regex( u8"(/점수조회) ([\\S]+) ([\\s\\S]+) (18|19|20)" ) ) ) { // /점수조회 사람 곡명 레벨
            std::regex reg( u8"(/점수조회) ([\\S]+) ([\\s\\S]+) (18|19|20)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2, 3, 4 } ), end;
            query_name = Util::UTF8toUTF16( *( it++ ) );
            auto nick = Util::UTF8toUTF16( *( it++ ) );
            level = Util::UTF8toUTF16( *it );
            http::Request title_request{ __config.api_endpoint() + "songs?title=" + Util::URLEncode( nick ) + "&kind=" + Util::URLEncode( level ) };
            title_response = title_request.send( "GET" );
        } else if ( std::regex_match( u8msg, std::regex( u8"(/점수조회) ([\\s\\S]+) (18|19|20)" ) ) ) { // /점수조회 곡명 레벨
            std::regex reg( u8"(/점수조회) ([\\s\\S]+) (18|19|20)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2, 3 } ), end;
            query_name = name;
            auto nick = Util::UTF8toUTF16( *( it++ ) );
            level = Util::UTF8toUTF16( *it );
            http::Request title_request{ __config.api_endpoint() + "songs?title=" + Util::URLEncode( nick ) + "&kind=" + Util::URLEncode( level ) };
            title_response = title_request.send( "GET" );
        } else if ( std::regex_match( u8msg, std::regex( u8"(/점수조회) ([\\S]+) ([\\s\\S]+)" ) ) ) { // /점수조회 사람 곡명
            std::regex reg( u8"(/점수조회) ([\\S]+) ([\\s\\S]+)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2, 3 } ), end;
            query_name = Util::UTF8toUTF16( *( it++ ) );
            auto nick = Util::UTF8toUTF16( *it );
            level = u"";

            // 혹시 (/점수조회 곡명)인지 확인하기 위해 query_name이 진짜 DB에 있는지 확인
            http::Request request{ __config.api_endpoint() + "member?chatroom_name=" + Util::URLEncode( chatroom_name ) };
            auto response = request.send( "GET" );
            std::string res_text = std::string( response.body.begin(), response.body.end() );
            if ( res_text == "[]" ) { // DB에 해당 단체방에 대한 정보가 없음
                kakao_sendtext( chatroom_name, u"지원하지 않는 단체방입니다." );
            } else {
                auto splitted = Util::split( Util::UTF8toUTF16( std::string( res_text.begin() + 1, res_text.end() - 1 ) ), "," );
                if ( std::find( splitted.begin(), splitted.end(), fmt::format( u"\"{}\"", query_name ) ) != splitted.end() ) { // 멤버를 찾음
                    http::Request title_request{ __config.api_endpoint() + "songs?title=" + Util::URLEncode( nick ) };
                    title_response = title_request.send( "GET" );
                } else { // 멤버 없는 경우 /점수조회 곡명 명령어를 띄어쓰기 포함하여 사용한 경우.
                    query_name = name;
                    reg = std::regex( u8"(/점수조회) ([\\s\\S]+)" );
                    std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
                    auto nick = Util::UTF8toUTF16( *( it ) );
                    http::Request title_request{ __config.api_endpoint() + "songs?title=" + Util::URLEncode( nick ) };
                    title_response = title_request.send( "GET" );
                }
            }
        } else if ( std::regex_match( u8msg, std::regex( u8"(/점수조회) ([\\s\\S]+)" ) ) ) { // /점수조회 곡명
            std::regex reg( u8"(/점수조회) ([\\s\\S]+)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } ), end;
            query_name = name;
            auto nick = Util::UTF8toUTF16( *( it ) );
            level = u"";
            http::Request title_request{ __config.api_endpoint() + "songs?title=" + Util::URLEncode( nick ) };
            title_response = title_request.send( "GET" );
        }

        std::string res_text = std::string( title_response.body.begin(), title_response.body.end() );

        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"곡정보를 찾지 못했습니다." );
            return RETURN_CODE::OK;
            // TODO : 검색으로 ~를 찾으시나요? 출력
        }
        std::string replaced = std::regex_replace( res_text, std::regex( "chain_vi" ), "chainVi" );
        replaced = std::regex_replace( res_text, std::regex( "chain_v" ), "chainV" );
        db::SdvxSong song;
        google::protobuf::util::JsonStringToMessage( replaced.c_str(), &song );
        if ( level == u"" ) {
            level = Util::UTF8toUTF16( std::to_string( song.level() ) );
        }

        http::Request account_request{ fmt::format( "{}member/account?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( query_name ), Util::URLEncode( chatroom_name ) ) };
        auto account_response = account_request.send( "GET" );
        res_text = std::string( account_response.body.begin(), account_response.body.end() );
        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
            return RETURN_CODE::OK;
        }
        std::regex reg( "//" );
        std::sregex_token_iterator it( res_text.begin(), res_text.end(), reg, -1 );
        auto [ info_id, info_pw, info_svid, permission ] = std::tuple( *it, *( std::next( it, 1 ) ), *( std::next( it, 2 ) ), *( std::next( it, 3 ) ) );

        if ( permission == "1" || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}info?id={}&pw={}&title={}&level={}", __config.api_endpoint(), Util::URLEncode( info_id ), Util::URLEncode( info_pw ), Util::URLEncode( song.title() ), Util::URLEncode( level ) ) };
            auto response = request.send( "GET" );
            res_text = std::string( response.body.begin(), response.body.end() );
            std::sregex_token_iterator it( res_text.begin(), res_text.end(), reg, -1 );
            score = Util::UTF8toUTF16( *( it++ ) );
            clear_lamp = Util::UTF8toUTF16( *it );

            if ( score == u"-1" && clear_lamp == u"NP" ) { // Not Played
                kakao_sendtext( chatroom_name, fmt::format( u"{}님의 점수 : ❌NP❌", query_name ) );
                return RETURN_CODE::OK;
            } else {
                if ( clear_lamp == u"play" ) {
                    clear_lamp = u"<Played>";
                } else if ( clear_lamp == u"comp" ) {
                    clear_lamp = u"<Comp>";
                } else if ( clear_lamp == u"comp_ex" ) {
                    clear_lamp = u"<EX_Comp>";
                } else if ( clear_lamp == u"uc" ) {
                    clear_lamp = u"💮UC💮";
                } else if ( clear_lamp == u"puc" ) {
                    clear_lamp = u"💯PUC💯";
                }
                kakao_sendtext( chatroom_name, fmt::format( u"{}님의 점수 : {} {}", query_name, score, clear_lamp ) );
            }
        } else {
            kakao_sendtext( chatroom_name, fmt::format( u"해당 멤버에 대한 점수조회 권한이 없습니다." ) );
        }
    }

    if ( msg == u"/갱신" ) { // 자신을 갱신
        http::Request account_request{ fmt::format( "{}member/account?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( name ), Util::URLEncode( chatroom_name ) ) };
        auto account_response = account_request.send( "GET" );
        auto res_text = std::string( account_response.body.begin(), account_response.body.end() );
        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
            return RETURN_CODE::OK;
        }
        kakao_sendtext( chatroom_name, u"갱신을 시작합니다." );
        std::regex reg( "//" );
        std::sregex_token_iterator it( res_text.begin(), res_text.end(), reg, -1 );
        auto [ info_id, info_pw, info_svid, permission ] = std::tuple( *it, *( std::next( it, 1 ) ), *( std::next( it, 2 ) ), *( std::next( it, 3 ) ) );
        http::Request renewal_request{ fmt::format( "{}renewal?svid={}&id={}&pw={}", __config.api_endpoint(), Util::URLEncode( info_svid ), Util::URLEncode( info_id ), Util::URLEncode( info_pw ) ) };
        auto renewal_response = renewal_request.send( "GET" );
        res_text = std::string( renewal_response.body.begin(), renewal_response.body.end() );
        if ( res_text == "-1" ) {
            kakao_sendtext( chatroom_name, u"갱신 서버의 설정이 만료되었습니다. 관리자에게 문의해주세요." );
        } else {
            kakao_sendtext( chatroom_name, fmt::format( u"갱신이 완료되었습니다.\n소요시간 : {}ms", Util::UTF8toUTF16( res_text ) ) );
        }
    } else if ( msg.rfind( u"/갱신 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(/갱신) ([\\S]+)" ) );
        if ( !std::regex_match( u8msg, reg ) ) {
            kakao_sendtext( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /갱신 [이름]" );
            return RETURN_CODE::OK;
        }
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );
        http::Request account_request{ fmt::format( "{}member/account?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( query_name ), Util::URLEncode( chatroom_name ) ) };
        auto account_response = account_request.send( "GET" );
        auto res_text = std::string( account_response.body.begin(), account_response.body.end() );
        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
            return RETURN_CODE::OK;
        }
        kakao_sendtext( chatroom_name, u"갱신을 시작합니다." );
        reg = std::regex( "//" );
        it = std::sregex_token_iterator( res_text.begin(), res_text.end(), reg, -1 );
        auto [ info_id, info_pw, info_svid, permission ] = std::tuple( *it, *( std::next( it, 1 ) ), *( std::next( it, 2 ) ), *( std::next( it, 3 ) ) );
        http::Request renewal_request{ fmt::format( "{}renewal?svid={}&id={}&pw={}", __config.api_endpoint(), Util::URLEncode( info_svid ), Util::URLEncode( info_id ), Util::URLEncode( info_pw ) ) };
        auto renewal_response = renewal_request.send( "GET" );
        res_text = std::string( renewal_response.body.begin(), renewal_response.body.end() );
        if ( res_text == "-1" ) {
            kakao_sendtext( chatroom_name, u"갱신 서버의 설정이 만료되었습니다. 관리자에게 문의해주세요." );
        } else {
            kakao_sendtext( chatroom_name, fmt::format( u"갱신이 완료되었습니다.\n소요시간 : {}ms", Util::UTF8toUTF16( res_text ) ) );
        }
    }

    if ( msg == u"/인포" ) { // 자신의 인포 조회
        http::Request account_request{ fmt::format( "{}member/account?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( name ), Util::URLEncode( chatroom_name ) ) };
        auto account_response = account_request.send( "GET" );
        auto res_text = std::string( account_response.body.begin(), account_response.body.end() );
        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
            return RETURN_CODE::OK;
        }
        std::regex reg( "//" );
        std::sregex_token_iterator it( res_text.begin(), res_text.end(), reg, -1 );
        auto [ info_id, info_pw, info_svid, permission ] = std::tuple( *it, *( std::next( it, 1 ) ), *( std::next( it, 2 ) ), *( std::next( it, 3 ) ) );
        http::Request info_request{ fmt::format( "{}info/info?id={}&pw={}", __config.api_endpoint(), Util::URLEncode( info_id ), Util::URLEncode( info_pw ) ) };
        auto info_response = info_request.send( "GET" );
        res_text = std::string( info_response.body.begin(), info_response.body.end() );
        auto info_token = Util::split( Util::UTF8toUTF16( res_text ), "//" );

        if ( info_token.size() == 6 ) {
            kakao_sendtext( chatroom_name, fmt::format( u"<---{}님의 인포--->\n닉네임 : {}\n단 : {}단\n볼포스 : {}\n코인수 : {}\n최근 갱신 일자 : {}", name, info_token[ 1 ], info_token[ 2 ], info_token[ 3 ], info_token[ 4 ] == u"0" ? u"비공개" : info_token[ 4 ], info_token[ 5 ] ) );
        } else {
            kakao_sendtext( chatroom_name, u"인포 계정정보를 찾았지만 인포를 불러오지 못했습니다." );
        }
    } else if ( msg.rfind( u"/인포 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(/인포) ([\\S]+)" ) );
        if ( !std::regex_match( u8msg, reg ) ) {
            kakao_sendtext( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /인포 [이름]" );
            return RETURN_CODE::OK;
        }
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );

        http::Request account_request{ fmt::format( "{}member/account?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( query_name ), Util::URLEncode( chatroom_name ) ) };
        auto account_response = account_request.send( "GET" );
        auto res_text = std::string( account_response.body.begin(), account_response.body.end() );
        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
            return RETURN_CODE::OK;
        }
        reg = std::regex( "//" );
        it = std::sregex_token_iterator( res_text.begin(), res_text.end(), reg, -1 );
        auto [ info_id, info_pw, info_svid, permission ] = std::tuple( *it, *( std::next( it, 1 ) ), *( std::next( it, 2 ) ), *( std::next( it, 3 ) ) );

        if ( permission == "1" || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request info_request{ fmt::format( "{}info/info?id={}&pw={}", __config.api_endpoint(), Util::URLEncode( info_id ), Util::URLEncode( info_pw ) ) };
            auto info_response = info_request.send( "GET" );
            res_text = std::string( info_response.body.begin(), info_response.body.end() );
            auto info_token = Util::split( Util::UTF8toUTF16( res_text ), "//" );

            if ( info_token.size() == 6 ) {
                kakao_sendtext( chatroom_name, fmt::format( u"<---{}님의 인포--->\n닉네임 : {}\n단 : {}단\n볼포스 : {}\n코인수 : {}\n최근 갱신 일자 : {}", query_name, info_token[ 1 ], info_token[ 2 ], info_token[ 3 ], info_token[ 4 ] == u"0" ? u"비공개" : info_token[ 4 ], info_token[ 5 ] ) );
            } else {
                kakao_sendtext( chatroom_name, u"인포 계정정보를 찾았지만 인포를 불러오지 못했습니다." );
            }
        } else {
            kakao_sendtext( chatroom_name, u"해당 멤버에 대한 인포 조회 권한이 없습니다." );
        }
    }

    if ( msg == u"/볼포스목록" ) { // 자신의 볼포스목록 조회
        http::Request account_request{ fmt::format( "{}member/account?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( name ), Util::URLEncode( chatroom_name ) ) };
        auto account_response = account_request.send( "GET" );
        auto res_text = std::string( account_response.body.begin(), account_response.body.end() );
        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
            return RETURN_CODE::OK;
        }
        std::regex reg( "//" );
        std::sregex_token_iterator it( res_text.begin(), res_text.end(), reg, -1 );
        auto [ info_id, info_pw, info_svid, permission ] = std::tuple( *it, *( std::next( it, 1 ) ), *( std::next( it, 2 ) ), *( std::next( it, 3 ) ) );
        http::Request request{ fmt::format( "{}info/volforce_list?id={}&pw={}", __config.api_endpoint(), Util::URLEncode( info_id ), Util::URLEncode( info_pw ) ) };
        auto response = request.send( "GET" );
        auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
        auto bmp = Util::ConvertCVMatToBMP( frame );
        if ( Util::PasteBMPToClipboard( bmp ) ) {
            kakao_sendimage( chatroom_name );
        }
    } else if ( msg.rfind( u"/볼포스목록 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(/볼포스목록) ([\\S]+)" ) );
        if ( !std::regex_match( u8msg, reg ) ) {
            kakao_sendtext( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /볼포스목록 [이름]" );
            return RETURN_CODE::OK;
        }
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );

        http::Request account_request{ fmt::format( "{}member/account?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( query_name ), Util::URLEncode( chatroom_name ) ) };
        auto account_response = account_request.send( "GET" );
        auto res_text = std::string( account_response.body.begin(), account_response.body.end() );
        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
            return RETURN_CODE::OK;
        }
        reg = std::regex( "//" );
        it = std::sregex_token_iterator( res_text.begin(), res_text.end(), reg, -1 );
        auto [ info_id, info_pw, info_svid, permission ] = std::tuple( *it, *( std::next( it, 1 ) ), *( std::next( it, 2 ) ), *( std::next( it, 3 ) ) );

        if ( permission == "1" || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}info/volforce_list?id={}&pw={}", __config.api_endpoint(), Util::URLEncode( info_id ), Util::URLEncode( info_pw ) ) };
            auto response = request.send( "GET" );
            auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
            auto bmp = Util::ConvertCVMatToBMP( frame );
            if ( Util::PasteBMPToClipboard( bmp ) ) {
                kakao_sendimage( chatroom_name );
            }
        } else {
            kakao_sendtext( chatroom_name, u"해당 멤버에 대한 볼포스목록 조회 권한이 없습니다." );
        }
    }

    if ( msg == u"/서열표 18PUC" ) { // 자신의 18PUC 목록 조회
        http::Request account_request{ fmt::format( "{}member/account?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( name ), Util::URLEncode( chatroom_name ) ) };
        auto account_response = account_request.send( "GET" );
        auto res_text = std::string( account_response.body.begin(), account_response.body.end() );
        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
            return RETURN_CODE::OK;
        }
        std::regex reg( "//" );
        std::sregex_token_iterator it( res_text.begin(), res_text.end(), reg, -1 );
        auto [ info_id, info_pw, info_svid, permission ] = std::tuple( *it, *( std::next( it, 1 ) ), *( std::next( it, 2 ) ), *( std::next( it, 3 ) ) );
        http::Request request{ fmt::format( "{}table?level=18&query=PUC&id={}&pw={}", __config.api_endpoint(), Util::URLEncode( info_id ), Util::URLEncode( info_pw ) ) };
        auto response = request.send( "GET" );
        res_text = std::string( response.body.begin(), response.body.end() );
        std::vector<std::string> codes;
        if ( res_text != "[]" ) {
            std::regex re( "," );
            std::sregex_token_iterator it2( res_text.begin() + 1, res_text.end() - 1, re, -1 ), end;
            for ( ; it2 != end; ++it2 ) {
                codes.push_back( std::string( *it2 ).substr( 1, 6 ) );
            }
        }
        http::Request table_request{ fmt::format( "{}table/18PUC_{}.png", __config.storage_server(), codes.size() ) };
        cv::Mat table;
        try {
            const auto table_response = table_request.send( "GET", "", {}, std::chrono::seconds( 15 ) );
            table = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( table_response.body.data() ), static_cast<std::streamsize>( table_response.body.size() ) ), cv::IMREAD_UNCHANGED );
        } catch ( const http::ResponseError &e ) {
            if ( std::strcmp( e.what(), "Request timed out" ) == 0 ) { // Request Timeout Error
                kakao_sendtext( chatroom_name, u"스토리지 서버에서 서열표파일을 가져오는데 실패했습니다." );
            } else {
                kakao_sendtext( chatroom_name, std::u16string( u"스토리지 서버에서 예상치 못한 에러가 발생했습니다." ) + Util::UTF8toUTF16( std::string( e.what() ) ) );
            }
            return RETURN_CODE::OK;
        }

        http::Request json_request{ fmt::format( "{}table/18PUC ", __config.api_endpoint() ) };
        response = json_request.send( "GET" );
        res_text = std::string( response.body.begin(), response.body.end() );

        db::TableList list;
        google::protobuf::util::JsonStringToMessage( res_text, &list );

        for ( auto &code : codes ) {
            if ( list.dict().find( code ) != list.dict().end() ) {
                std::cout << __LINE__ << " | " << code << ", " << list.dict().at( code ).x() << ", " << list.dict().at( code ).y() << std::endl;
                cv::line( table, cv::Point( list.dict().at( code ).x(), list.dict().at( code ).y() ), cv::Point( list.dict().at( code ).x() + 135, list.dict().at( code ).y() + 135 ), cv::Scalar( 0, 0, 255 ), 10 );
                cv::line( table, cv::Point( list.dict().at( code ).x() + 135, list.dict().at( code ).y() ), cv::Point( list.dict().at( code ).x(), list.dict().at( code ).y() + 135 ), cv::Scalar( 0, 0, 255 ), 10 );
            }
        }

        cv::Mat resized_table;
        cv::resize( table, resized_table, cv::Size(), 0.5, 0.5 );
        auto bmp = Util::ConvertCVMatToBMP( resized_table );
        if ( Util::PasteBMPToClipboard( bmp ) ) {
            kakao_sendimage( chatroom_name );
        }

    } else if ( msg.rfind( u"/서열표 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(/서열표) ([\\S]+) (18PUC)" ) );
        if ( !std::regex_match( u8msg, reg ) ) {
            kakao_sendtext( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /볼포스목록 {이름} [18PUC]" );
            return RETURN_CODE::OK;
        }
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );

        http::Request account_request{ fmt::format( "{}member/account?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( query_name ), Util::URLEncode( chatroom_name ) ) };
        auto account_response = account_request.send( "GET" );
        auto res_text = std::string( account_response.body.begin(), account_response.body.end() );
        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
            return RETURN_CODE::OK;
        }
        reg = std::regex( "//" );
        it = std::sregex_token_iterator( res_text.begin(), res_text.end(), reg, -1 );
        auto [ info_id, info_pw, info_svid, permission ] = std::tuple( *it, *( std::next( it, 1 ) ), *( std::next( it, 2 ) ), *( std::next( it, 3 ) ) );

        if ( permission == "1" || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}table?level=18&query=PUC&id={}&pw={}", __config.api_endpoint(), Util::URLEncode( info_id ), Util::URLEncode( info_pw ) ) };
            auto response = request.send( "GET" );
            res_text = std::string( response.body.begin(), response.body.end() );
            std::vector<std::string> codes;
            if ( res_text != "[]" ) {
                std::regex re( "," );
                std::sregex_token_iterator it2( res_text.begin() + 1, res_text.end() - 1, re, -1 ), end;
                for ( ; it2 != end; ++it2 ) {
                    codes.push_back( std::string( *it2 ).substr( 1, 6 ) );
                }
            }
            http::Request table_request{ fmt::format( "{}table/18PUC_{}.png", __config.storage_server(), codes.size() ) };
            cv::Mat table;
            try {
                const auto table_response = table_request.send( "GET", "", {}, std::chrono::seconds( 15 ) );
                table = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( table_response.body.data() ), static_cast<std::streamsize>( table_response.body.size() ) ), cv::IMREAD_UNCHANGED );
            } catch ( const http::ResponseError &e ) {
                if ( std::strcmp( e.what(), "Request timed out" ) == 0 ) { // Request Timeout Error
                    kakao_sendtext( chatroom_name, u"스토리지 서버에서 서열표파일을 가져오는데 실패했습니다." );
                } else {
                    kakao_sendtext( chatroom_name, std::u16string( u"스토리지 서버에서 예상치 못한 에러가 발생했습니다." ) + Util::UTF8toUTF16( std::string( e.what() ) ) );
                }
                return RETURN_CODE::OK;
            }

            http::Request json_request{ fmt::format( "{}table/18PUC ", __config.api_endpoint() ) };
            response = json_request.send( "GET" );
            res_text = std::string( response.body.begin(), response.body.end() );

            db::TableList list;
            google::protobuf::util::JsonStringToMessage( res_text, &list );

            for ( auto &code : codes ) {
                if ( list.dict().find( code ) != list.dict().end() ) {
                    std::cout << __LINE__ << " | " << code << ", " << list.dict().at( code ).x() << ", " << list.dict().at( code ).y() << std::endl;
                    cv::line( table, cv::Point( list.dict().at( code ).x(), list.dict().at( code ).y() ), cv::Point( list.dict().at( code ).x() + 135, list.dict().at( code ).y() + 135 ), cv::Scalar( 0, 0, 255 ), 10 );
                    cv::line( table, cv::Point( list.dict().at( code ).x() + 135, list.dict().at( code ).y() ), cv::Point( list.dict().at( code ).x(), list.dict().at( code ).y() + 135 ), cv::Scalar( 0, 0, 255 ), 10 );
                }
            }

            cv::Mat resized_table;
            cv::resize( table, resized_table, cv::Size(), 0.5, 0.5 );
            auto bmp = Util::ConvertCVMatToBMP( resized_table );
            if ( Util::PasteBMPToClipboard( bmp ) ) {
                kakao_sendimage( chatroom_name );
            }
        } else {
            kakao_sendtext( chatroom_name, u"해당 멤버에 대한 서열표 조회 권한이 없습니다." );
        }
    }

    if ( msg == u"/평균" ) { // 자신의 평균목록 조회
        http::Request account_request{ fmt::format( "{}member/account?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( name ), Util::URLEncode( chatroom_name ) ) };
        auto account_response = account_request.send( "GET" );
        auto res_text = std::string( account_response.body.begin(), account_response.body.end() );
        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
            return RETURN_CODE::OK;
        }
        std::regex reg( "//" );
        std::sregex_token_iterator it( res_text.begin(), res_text.end(), reg, -1 );
        auto [ info_id, info_pw, info_svid, permission ] = std::tuple( *it, *( std::next( it, 1 ) ), *( std::next( it, 2 ) ), *( std::next( it, 3 ) ) );
        http::Request request{ fmt::format( "{}info/average?id={}&pw={}", __config.api_endpoint(), Util::URLEncode( info_id ), Util::URLEncode( info_pw ) ) };
        auto response = request.send( "GET" );
        auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
        auto bmp = Util::ConvertCVMatToBMP( frame );
        if ( Util::PasteBMPToClipboard( bmp ) ) {
            kakao_sendimage( chatroom_name );
        }
    } else if ( msg.rfind( u"/평균 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(/평균) ([\\S]+)" ) );
        if ( !std::regex_match( u8msg, reg ) ) {
            kakao_sendtext( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /평균 [이름]" );
            return RETURN_CODE::OK;
        }
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );

        http::Request account_request{ fmt::format( "{}member/account?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( query_name ), Util::URLEncode( chatroom_name ) ) };
        auto account_response = account_request.send( "GET" );
        auto res_text = std::string( account_response.body.begin(), account_response.body.end() );
        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
            return RETURN_CODE::OK;
        }
        reg = std::regex( "//" );
        it = std::sregex_token_iterator( res_text.begin(), res_text.end(), reg, -1 );
        auto [ info_id, info_pw, info_svid, permission ] = std::tuple( *it, *( std::next( it, 1 ) ), *( std::next( it, 2 ) ), *( std::next( it, 3 ) ) );

        if ( permission == "1" || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}info/average?id={}&pw={}", __config.api_endpoint(), Util::URLEncode( info_id ), Util::URLEncode( info_pw ) ) };
            auto response = request.send( "GET" );
            auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
            auto bmp = Util::ConvertCVMatToBMP( frame );
            if ( Util::PasteBMPToClipboard( bmp ) ) {
                kakao_sendimage( chatroom_name );
            }
        } else {
            kakao_sendtext( chatroom_name, u"해당 멤버에 대한 평균 조회 권한이 없습니다." );
        }
    }

    if ( msg.rfind( u"/통계 ", 0 ) == 0 ) {
        std::u16string query_name, level;
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg1( Util::UTF16toUTF8( u"(/통계) ([\\S]+) ([1-9]|1\\d|20)$" ) );
        std::regex reg2( Util::UTF16toUTF8( u"(/통계) ([1-9]|1\\d|20)$" ) );

        if ( std::regex_match( u8msg, reg1 ) ) {
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg1, std::vector<int>{ 2, 3 } );
            query_name = Util::UTF8toUTF16( *it );
            level = Util::UTF8toUTF16( *( std::next( it ) ) );
        } else if ( std::regex_match( u8msg, reg2 ) ) {
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg2, std::vector<int>{ 2 } );
            query_name = name;
            level = Util::UTF8toUTF16( *it );
            std::cout << "LEVEL : " << ( *it ) << std::endl;
        } else {
            kakao_sendtext( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /통계 {이름} [레벨]" );
            return RETURN_CODE::OK;
        }

        http::Request account_request{ fmt::format( "{}member/account?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( query_name ), Util::URLEncode( chatroom_name ) ) };
        auto account_response = account_request.send( "GET" );
        auto res_text = std::string( account_response.body.begin(), account_response.body.end() );
        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
            return RETURN_CODE::OK;
        }
        std::regex reg( "//" );
        auto it = std::sregex_token_iterator( res_text.begin(), res_text.end(), reg, -1 );
        auto [ info_id, info_pw, info_svid, permission ] = std::tuple( *it, *( std::next( it, 1 ) ), *( std::next( it, 2 ) ), *( std::next( it, 3 ) ) );

        if ( permission == "1" || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}info/statistics?id={}&pw={}&level={}", __config.api_endpoint(), Util::URLEncode( info_id ), Util::URLEncode( info_pw ), Util::URLEncode( level ) ) };
            auto response = request.send( "GET" );
            auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
            auto bmp = Util::ConvertCVMatToBMP( frame );
            if ( Util::PasteBMPToClipboard( bmp ) ) {
                kakao_sendimage( chatroom_name );
            }
        } else {
            kakao_sendtext( chatroom_name, u"해당 멤버에 대한 통계 조회 권한이 없습니다." );
        }
    }

    if ( msg == u"/업데이트" && name == u"손창대" ) {
        kakao_sendtext( chatroom_name, u"업데이트를 진행합니다." );
        return RETURN_CODE::UPDATE;
    }

    if ( msg.rfind( u"/링크 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(/링크) ([\\s\\S]+)" ) );
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query = Util::UTF8toUTF16( *it );

        http::Request request{ __config.api_endpoint() + "streaming?kind=" + Util::URLEncode( query ) };
        auto response = request.send( "GET" );
        const std::string res_text = std::string( response.body.begin(), response.body.end() );

        if ( res_text == "Error" ) { // 해당하는 링크를 찾지 못한 경우
            kakao_sendtext( chatroom_name, u"라이브 스트리밍중이 아니거나 지원하는 스트리밍이 아닙니다.\n\n<<사용가능 목록>>\n\n관성(개인방송)\n릿샤(개인방송)\n싸이발키리\n싸이구기체\n싸이라이트닝\n싸이투덱\n량진발키리\n량진구기체" );
        } else { // 해당 링크를 찾은 경우
            kakao_sendtext( chatroom_name, Util::UTF8toUTF16( res_text ) );
        }
    }

    if ( msg.rfind( u"/대기 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(/대기) ([\\s\\S]+)" ) );
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query = Util::UTF8toUTF16( *it );

        http::Request request{ __config.api_endpoint() + "streaming?kind=" + Util::URLEncode( query ) };
        auto response = request.send( "GET" );
        std::string res_text = std::string( response.body.begin(), response.body.end() );

        if ( res_text == "Error" ) { // 해당하는 링크를 찾지 못한 경우
            kakao_sendtext( chatroom_name, u"라이브 스트리밍중이 아니거나 지원하는 스트리밍이 아닙니다.\n\n<<사용가능 목록>>\n\n관성(개인방송)\n릿샤(개인방송)\n싸이발키리\n싸이구기체\n싸이라이트닝\n싸이투덱\n량진발키리\n량진구기체" );
        } else { // 해당 링크를 찾은 경우
            request = http::Request( __config.api_endpoint() + "streaming/playback?url=" + Util::URLEncode( res_text ) );
            response = request.send( "GET" );
            res_text = std::string( response.body.begin(), response.body.end() );

            if ( res_text == "Error" ) {
                kakao_sendtext( chatroom_name, u"라이브 스트리밍을 찾았지만, 플레이백 URL을 구하는 과정에서 에러가 발생했습니다." );
            } else {
                auto capture = cv::VideoCapture( res_text );
                cv::Mat frame;
                auto grabbed = capture.read( frame );
                if ( grabbed ) { // 캡쳐에 성공한 경우
                    auto bmp = Util::ConvertCVMatToBMP( frame );
                    if ( Util::PasteBMPToClipboard( bmp ) ) {
                        kakao_sendimage( chatroom_name );
                    }
                } else { // playback은 있었지만 캡쳐에 실패한 경우
                    kakao_sendtext( chatroom_name, u"라이브 스트리밍을 찾았지만, 오류가 발생하여 썸네일을 생성하지 못했습니다." );
                }
            }
        }
    }

    if ( msg == u"/국내야구" ) {
        http::Request request{ __config.api_endpoint() + "etc/baseball" };
        auto response = request.send( "GET" );
        const std::string res_text = std::string( response.body.begin(), response.body.end() );
        kakao_sendtext( chatroom_name, Util::UTF8toUTF16( res_text ) );
    }

    if ( msg == u"/국내야구랭킹" || msg == u"/국야랭" ) {
        http::Request request{ fmt::format( "{}etc/baseball_ranking", __config.api_endpoint() ) };
        auto response = request.send( "GET" );
        auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
        auto bmp = Util::ConvertCVMatToBMP( frame );
        if ( Util::PasteBMPToClipboard( bmp ) ) {
            kakao_sendimage( chatroom_name );
        }
    }

    if ( msg.rfind( u"/", 0 ) == 0 && msg.find( u"vs" ) != std::u16string::npos ) {
        auto tokens = Util::split( msg.substr( 1 ), "vs" );
        auto selected = tokens.at( Util::rand( 0, tokens.size() - 1 ) );
        std::regex reg( "\\s" );
        kakao_sendtext( chatroom_name, Util::UTF8toUTF16( std::regex_replace( Util::UTF16toUTF8( selected ), reg, "" ) ) );
    }

    if ( msg.rfind( u"/장비 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::u16string nick, kind; // 쿼리용 변수
        http::Response image_response;
        if ( std::regex_match( u8msg, std::regex( u8"(/장비) ([\\S]+) (반지1|모자|뚝|뚝배기|엠블렘|엠블럼|엠블|반지2|펜던트2|펜던2|얼굴장식|얼장|뱃지|반지3|펜던트1|펜던트|펜던|눈장식|눈장|귀고리|귀걸이|이어링|훈장|메달|반지4|무기|상의|견장|어깨장식|보조|보조무기|포켓|포켓아이템|벨트|하의|장갑|망토|신발|하트|기계심장)" ) ) ) { // /장비 닉네임 부위
            std::regex reg( u8"(/장비) ([\\S]+) (반지1|모자|뚝|뚝배기|엠블렘|엠블럼|엠블|반지2|펜던트2|펜던2|얼굴장식|얼장|뱃지|반지3|펜던트1|펜던트|펜던|눈장식|눈장|귀고리|귀걸이|이어링|훈장|메달|반지4|무기|상의|견장|어깨장식|보조|보조무기|포켓|포켓아이템|벨트|하의|장갑|망토|신발|하트|기계심장)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2, 3 } ), end;
            nick = Util::UTF8toUTF16( *( it++ ) );
            kind = Util::UTF8toUTF16( *it );
            http::Request title_request{ __config.api_endpoint() + "maple?nick=" + Util::URLEncode( nick ) + "&kind=" + Util::URLEncode( kind ) };
            image_response = title_request.send( "GET" );
            if ( std::string( image_response.body.begin(), image_response.body.end() ) == "ERROR" ) {
                kakao_sendtext( chatroom_name, u"장비를 조회하는 도중에 에러가 발생했습니다. 장비정보가 공개되어있는지 메이플스토리 공식홈페이지에서 한번 더 확인해주세요." );
            } else {
                auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( image_response.body.data() ), static_cast<std::streamsize>( image_response.body.size() ) ), cv::IMREAD_UNCHANGED );
                auto bmp = Util::ConvertCVMatToBMP( frame );
                if ( Util::PasteBMPToClipboard( bmp ) ) {
                    kakao_sendimage( chatroom_name );
                }
            }
        } else {
            kakao_sendtext( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /장비 [닉네임] [부위]\n조회 가능한 장비분류 : 반지1, 모자, 뚝, 뚝배기, 엠블렘, 엠블럼, 엠블, 반지2, 펜던트2, 펜던2, 얼굴장식, 얼장, 뱃지, 반지3, 펜던트1, 펜던트, 펜던, 눈장식, 눈장, 귀고리, 귀걸이, 이어링, 훈장, 메달, 반지4, 무기, 상의, 견장, 어깨장식, 보조, 보조무기, 포켓, 포켓아이템, 벨트, 하의, 장갑, 망토, 신발, 하트, 기계심장" );
        }
    }

    if ( msg.rfind( u"/검색 ", 0 ) == 0 ) {
        auto search_text = msg.substr( 4 );

        if ( search_text.length() == 0 ) {
            kakao_sendtext( chatroom_name, u"검색어를 입력해주세요" );
            return RETURN_CODE::OK;
        }

        auto regex = std::regex( "^\\S$|^\\S.*\\S$" );
        auto u8str = Util::UTF16toUTF8( search_text );
        if ( !std::regex_match( u8str, regex ) ) {
            kakao_sendtext( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /검색 [검색어]" );
            return RETURN_CODE::OK;
        }
        http::Request request{ __config.api_endpoint() + "songs/search?search_text=" + Util::URLEncode( search_text ) };
        auto response = request.send( "GET" );
        const std::string res_text = std::string( response.body.begin(), response.body.end() );
        std::string replaced = std::regex_replace( res_text, std::regex( "chain_vi" ), "chainVi" );
        replaced = std::regex_replace( res_text, std::regex( "chain_v" ), "chainV" );

        // protobuf로 만들기 위해 message formatting
        replaced = fmt::format( "{{\"result\":{}}}", replaced );

        db::SearchResult result;
        google::protobuf::util::JsonStringToMessage( replaced, &result );

        if ( result.result_size() == 0 ) {
            kakao_sendtext( chatroom_name, fmt::format( u"검색어 \"{}\"에 대한 결과를 찾을 수 없습니다.", search_text ) );
        } else {
            std::u16string ret = fmt::format( u"\"{}\"에 대한 검색 결과입니다.", search_text );

            for ( size_t i = 0; i < result.result_size(); ++i ) {
                ret += fmt::format( u"\n{}. {} [Lv{}, 별명 : {}]", i + 1, Util::UTF8toUTF16( result.result( i ).song().title() ), result.result( i ).song().level(), result.result( i ).song().nick1() == "" ? u"없음" : Util::UTF8toUTF16( result.result( i ).song().nick1() ) );
            }

            kakao_sendtext( chatroom_name, ret );
        }
    }

    // 팝픈뮤직 점수조회
    if ( msg.rfind( u">점수조회 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::u16string query_name, title, level, nick; // 쿼리용 변수
        http::Response title_response;
        std::u16string score, grade, medal;                                                                                    // 결과
        if ( std::regex_match( u8msg, std::regex( u8"(>점수조회) ([\\S]+) ([\\s\\S]+) (41|42|43|44|45|46|47|48|49|50)" ) ) ) { // >점수조회 사람 곡명 레벨
            std::regex reg( u8"(>점수조회) ([\\S]+) ([\\s\\S]+) (41|42|43|44|45|46|47|48|49|50)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2, 3, 4 } ), end;
            query_name = Util::UTF8toUTF16( *( it++ ) );
            auto nick = Util::UTF8toUTF16( *( it++ ) );
            level = Util::UTF8toUTF16( *it );
            http::Request title_request{ __config.api_endpoint() + "popn_songs?title=" + Util::URLEncode( nick ) + "&kind=" + Util::URLEncode( level ) };
            title_response = title_request.send( "GET" );
        } else if ( std::regex_match( u8msg, std::regex( u8"(>점수조회) ([\\s\\S]+) (41|42|43|44|45|46|47|48|49|50)" ) ) ) { // >점수조회 곡명 레벨
            std::regex reg( u8"(>점수조회) ([\\s\\S]+) (41|42|43|44|45|46|47|48|49|50)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2, 3 } ), end;
            query_name = name;
            auto nick = Util::UTF8toUTF16( *( it++ ) );
            level = Util::UTF8toUTF16( *it );
            http::Request title_request{ __config.api_endpoint() + "popn_songs?title=" + Util::URLEncode( nick ) + "&kind=" + Util::URLEncode( level ) };
            title_response = title_request.send( "GET" );
        } else if ( std::regex_match( u8msg, std::regex( u8"(>점수조회) ([\\S]+) ([\\s\\S]+)" ) ) ) { // >점수조회 사람 곡명
            std::regex reg( u8"(>점수조회) ([\\S]+) ([\\s\\S]+)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2, 3 } ), end;
            query_name = Util::UTF8toUTF16( *( it++ ) );
            auto nick = Util::UTF8toUTF16( *it );
            level = u"";

            // 혹시 (>점수조회 곡명)인지 확인하기 위해 query_name이 진짜 DB에 있는지 확인
            http::Request request{ __config.api_endpoint() + "member?chatroom_name=" + Util::URLEncode( chatroom_name ) };
            auto response = request.send( "GET" );
            std::string res_text = std::string( response.body.begin(), response.body.end() );
            if ( res_text == "[]" ) { // DB에 해당 단체방에 대한 정보가 없음
                kakao_sendtext( chatroom_name, u"지원하지 않는 단체방입니다." );
            } else {
                auto splitted = Util::split( Util::UTF8toUTF16( std::string( res_text.begin() + 1, res_text.end() - 1 ) ), "," );
                if ( std::find( splitted.begin(), splitted.end(), fmt::format( u"\"{}\"", query_name ) ) != splitted.end() ) { // 멤버를 찾음
                    http::Request title_request{ __config.api_endpoint() + "popn_songs?title=" + Util::URLEncode( nick ) };
                    title_response = title_request.send( "GET" );
                } else { // 멤버 없는 경우 >점수조회 곡명 명령어를 띄어쓰기 포함하여 사용한 경우.
                    query_name = name;
                    reg = std::regex( u8"(>점수조회) ([\\s\\S]+)" );
                    std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
                    auto nick = Util::UTF8toUTF16( *( it ) );
                    http::Request title_request{ __config.api_endpoint() + "popn_songs?title=" + Util::URLEncode( nick ) };
                    title_response = title_request.send( "GET" );
                }
            }
        } else if ( std::regex_match( u8msg, std::regex( u8"(>점수조회) ([\\s\\S]+)" ) ) ) { // >점수조회 곡명
            std::regex reg( u8"(>점수조회) ([\\s\\S]+)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } ), end;
            query_name = name;
            auto nick = Util::UTF8toUTF16( *( it ) );
            level = u"";
            http::Request title_request{ __config.api_endpoint() + "popn_songs?title=" + Util::URLEncode( nick ) };
            title_response = title_request.send( "GET" );
        }

        std::string res_text = std::string( title_response.body.begin(), title_response.body.end() );

        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"곡정보를 찾지 못했습니다." );
            return RETURN_CODE::OK;
            // TODO : 검색으로 ~를 찾으시나요? 출력
        }
        popndb::PopnSong song;
        google::protobuf::util::JsonStringToMessage( res_text.c_str(), &song );
        if ( level == u"" ) {
            level = Util::UTF8toUTF16( std::to_string( song.level() ) );
        }

        http::Request account_request{ fmt::format( "{}member/account?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( query_name ), Util::URLEncode( chatroom_name ) ) };
        auto account_response = account_request.send( "GET" );
        res_text = std::string( account_response.body.begin(), account_response.body.end() );
        if ( res_text == "{}" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
            return RETURN_CODE::OK;
        }
        std::regex reg( "//" );
        std::sregex_token_iterator it( res_text.begin(), res_text.end(), reg, -1 );
        auto [ info_id, info_pw, info_svid, permission ] = std::tuple( *it, *( std::next( it, 1 ) ), *( std::next( it, 2 ) ), *( std::next( it, 3 ) ) );

        if ( permission == "1" || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}popn_songs/score?name={}&song_id={}", __config.api_endpoint(), Util::URLEncode( query_name ), song.id() ) };
            auto response = request.send( "GET" );
            res_text = std::string( response.body.begin(), response.body.end() );
            std::sregex_token_iterator it( res_text.begin(), res_text.end(), reg, -1 );
            score = Util::UTF8toUTF16( *( it++ ) );
            grade = Util::UTF8toUTF16( *( it++ ) );
            medal = Util::UTF8toUTF16( *it );

            if ( score == u"-1" && grade == u"NP" && medal == u"NP" ) { // Not Played
                kakao_sendtext( chatroom_name, fmt::format( u"{}님의 점수 : ❌NP❌", query_name ) );
                return RETURN_CODE::OK;
            } else {
                if ( grade == u"s" ) {
                    grade = u"S";
                } else if ( grade == u"a3" ) {
                    grade = u"AAA";
                } else if ( grade == u"a2" ) {
                    grade = u"AA";
                } else if ( grade == u"a1" ) {
                    grade = u"A";
                } else if ( grade == u"b" ) {
                    grade = u"B";
                } else if ( grade == u"c" ) {
                    grade = u"C";
                } else if ( grade == u"d" ) {
                    grade = u"D";
                } else if ( grade == u"e" ) {
                    grade = u"E";
                }

                if ( medal == u"a" ) {
                    medal = u"퍼펙";
                } else if ( medal == u"b" ) {
                    medal = u"은별";
                } else if ( medal == u"c" ) {
                    medal = u"은다이아";
                } else if ( medal == u"d" ) {
                    medal = u"은쟁반";
                } else if ( medal == u"e" ) {
                    medal = u"별";
                } else if ( medal == u"f" ) {
                    medal = u"다이아";
                } else if ( medal == u"g" ) {
                    medal = u"클리어";
                } else if ( medal == u"h" ) {
                    medal = u"흑별";
                } else if ( medal == u"i" ) {
                    medal = u"흑다이아";
                } else if ( medal == u"j" ) {
                    medal = u"불클";
                } else if ( medal == u"k" ) {
                    medal = u"새싹";
                }
                kakao_sendtext( chatroom_name, fmt::format( u"{}님의 점수 : {}{}", query_name, score, medal ) );
            }
        } else {
            kakao_sendtext( chatroom_name, fmt::format( u"해당 멤버에 대한 점수조회 권한이 없습니다." ) );
        }
    }

    // 팝픈뮤직 갱신
    if ( msg == u">갱신" ) { // 자신을 갱신
        kakao_sendtext( chatroom_name, u"갱신을 시작합니다." );
        http::Request renewal_request{ fmt::format( "{}popn_songs/renewal?name={}", __config.api_endpoint(), Util::URLEncode( name ) ) };
        auto renewal_response = renewal_request.send( "GET" );
        auto res_text = std::string( renewal_response.body.begin(), renewal_response.body.end() );
        if ( res_text == "-2" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
        } else if ( res_text == "-1" ) {
            kakao_sendtext( chatroom_name, u"갱신 서버의 설정이 만료되었습니다. 관리자에게 문의해주세요." );
        } else {
            kakao_sendtext( chatroom_name, fmt::format( u"갱신이 완료되었습니다.\n소요시간 : {}ms", Util::UTF8toUTF16( res_text ) ) );
        }
    } else if ( msg.rfind( u">갱신 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(>갱신) ([\\S]+)" ) );
        if ( !std::regex_match( u8msg, reg ) ) {
            kakao_sendtext( chatroom_name, u"잘못된 명령어입니다.\n사용법 : >갱신 [이름]" );
            return RETURN_CODE::OK;
        }
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );

        kakao_sendtext( chatroom_name, u"갱신을 시작합니다." );

        http::Request renewal_request{ fmt::format( "{}popn_songs/renewal?name={}", __config.api_endpoint(), Util::URLEncode( query_name ) ) };
        auto renewal_response = renewal_request.send( "GET" );
        auto res_text = std::string( renewal_response.body.begin(), renewal_response.body.end() );
        if ( res_text == "-2" ) {
            kakao_sendtext( chatroom_name, u"인포 정보를 찾을 수 없습니다." );
        } else if ( res_text == "-1" ) {
            kakao_sendtext( chatroom_name, u"갱신 서버의 설정이 만료되었습니다. 관리자에게 문의해주세요." );
        } else {
            kakao_sendtext( chatroom_name, fmt::format( u"갱신이 완료되었습니다.\n소요시간 : {}ms", Util::UTF8toUTF16( res_text ) ) );
        }
    }

    if ( msg.rfind( u">서든 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        int bpm1, bpm2;
        if ( std::regex_match( u8msg, std::regex( u8"(>서든) ([0-9]+) ([0-9]+)" ) ) ) {
            std::regex reg( u8"(>서든) ([0-9]+) ([0-9]+)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2, 3 } ), end;
            bpm1 = std::stoi( *( it++ ) );
            bpm2 = std::stoi( *it );

            if ( std::min<>( bpm1, bpm2 ) <= 0 || std::max<>( bpm1, bpm2 ) >= 1000 ) {
                kakao_sendtext( chatroom_name, u"잘못된 명령어입니다.\n사용법 : >서든 [저속>0] [고속<1000]" );
            } else {
                int sudden = 95 - ( 315.0f * std::min<>( bpm1, bpm2 ) / std::max<>( bpm1, bpm2 ) );
                kakao_sendtext( chatroom_name, fmt::format( u"서든 : {}", sudden ) );
            }
        } else {
            kakao_sendtext( chatroom_name, u"잘못된 명령어입니다.\n사용법 : >서든 [저속] [고속]" );
        }
    }

    if ( msg == u"/오늘의백준" || msg == u"/데일리백준" ) {
        http::Request request{ fmt::format( "{}boj/daily", __config.api_endpoint() ) };
        auto response = request.send( "GET" );
        auto res_text = std::string( response.body.begin(), response.body.end() );
        auto splitted = Util::split( Util::UTF8toUTF16( res_text ), "!@#" );
        if ( splitted.size() != 3 ) {
            kakao_sendtext( chatroom_name, u"오류가 발생했습니다." );
        } else {
            kakao_sendtext( chatroom_name, fmt::format( u"📖오늘의 문제📖\n제목 : {}\n레벨 : {}\n\nhttps://www.acmicpc.net/problem/{}", splitted[ 1 ], splitted[ 2 ], splitted[ 0 ] ) );
        }
    }

    if ( msg.rfind( u"/곡추천 ", 0 ) == 0 || msg.rfind( u"/추천곡 ", 0 ) == 0 ) { // 랜덤 곡 추천 기능
        auto u8msg = Util::UTF16toUTF8( msg );
        std::u16string level = u""; // 쿼리용 변수
        if ( std::regex_match( u8msg, std::regex( u8"(/곡추천) (18|19|20)" ) ) ) {
            std::regex reg( u8"(/곡추천) (18|19|20)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
            level = Util::UTF8toUTF16( *it );
        } else if ( std::regex_match( u8msg, std::regex( u8"(/추천곡) (18|19|20)" ) ) ) {
            std::regex reg( u8"(/추천곡) (18|19|20)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
            level = Util::UTF8toUTF16( *it );
        } else {
            kakao_sendtext( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /곡추천 [레벨]" );
            return RETURN_CODE::OK;
        }

        http::Request request{ fmt::format( "{}songs/list?level={}", __config.api_endpoint(), Util::URLEncode( level ) ) };
        auto response = request.send( "GET" );
        auto res_text = std::string( response.body.begin(), response.body.end() );
        std::string replaced = std::regex_replace( res_text, std::regex( "chain_vi" ), "chainVi" );
        replaced = std::regex_replace( res_text, std::regex( "chain_v" ), "chainV" );
        replaced = "{\"sdvxsongs\":" + replaced + "}";
        db::SdvxList list;
        google::protobuf::util::JsonStringToMessage( replaced, &list );

        std::u16string diff = u"";
        db::SdvxSong song = list.sdvxsongs( Util::rand( 0, list.sdvxsongs_size() - 1 ) );
        if ( song.code().at( 5 ) == 'N' ) {
            diff = u"[NOV]";
        } else if ( song.code().at( 5 ) == 'A' ) {
            diff = u"[ADV]";
        } else if ( song.code().at( 5 ) == 'E' ) {
            diff = u"[EXH]";
        } else if ( song.code().at( 5 ) == 'I' ) {
            diff = u"[INF]";
        } else if ( song.code().at( 5 ) == 'G' ) {
            diff = u"[GRV]";
        } else if ( song.code().at( 5 ) == 'H' ) {
            diff = u"[HVN]";
        } else if ( song.code().at( 5 ) == 'V' ) {
            diff = u"[VVD]";
        } else if ( song.code().at( 5 ) == 'M' ) {
            diff = u"[MXM]";
        } else if ( song.code().at( 5 ) == 'X' ) {
            diff = u"[XCD]";
        }

        kakao_sendtext( chatroom_name, fmt::format( u"🎵추천곡🎵\n{} {}", Util::UTF8toUTF16( song.title() ), diff ) );
        try {
            std::string lower_code;
            std::transform( song.code().begin(), song.code().end(), back_inserter( lower_code ), ::tolower );
            auto frame = cv::imread( fmt::format( "songs/{}/jacket.png", lower_code ), cv::IMREAD_UNCHANGED );
            auto bmp = Util::ConvertCVMatToBMP( frame );
            if ( Util::PasteBMPToClipboard( bmp ) ) {
                kakao_sendimage( chatroom_name );
            }
        } catch ( cv::Exception &e ) {
            kakao_sendtext( chatroom_name, fmt::format( u"자켓을 찾을 수 없습니다.\nErr : {}", Util::UTF8toUTF16( e.what() ) ) );
        }
    }
    return RETURN_CODE::OK;
}
