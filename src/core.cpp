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
        auto child_wnd = ::FindWindowExA( hwnd, NULL, reinterpret_cast<LPCSTR>( "RICHEDIT50W" ), NULL );
        SetForegroundWindow( child_wnd );
        PostKeyEx( child_wnd, static_cast<UINT>( 'V' ), VK_CONTROL, false );
        Sleep( 100 );
        SendReturn( GetForegroundWindow() );
        Sleep( 50 );
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
            Sleep( 25 );
            PostMessage( hwnd, WM_KEYDOWN, key, lparam );
            Sleep( 25 );
            PostMessage( hwnd, WM_KEYUP, key, lparam | 0xC0000000 );
            Sleep( 25 );
            SetKeyboardState( pKeyBuffers_old );
            Sleep( 25 );
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
                return std::pair( u"Update", -1 );
        }
        return std::pair( splitted.at( splitted.size() - 1 ), last_idx + splitted.size() );
    }
}

RETURN_CODE execute_command( const std::string &chatroom_name, const std::u16string &name, const std::u16string &AMPM, const std::u16string &time, const std::u16string &msg ) {
    if ( name == u"EndTime" ) {
        return RETURN_CODE::OK;
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
                auto [ min, max ] = std::minmax( ages.begin(), ages.end() );
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
        std::regex inven_pattern( "\\{\"1\":([0-9]+),\"2\":([0-9]+),\"3\":([0-9]+),\"6\":([0-9]+),\"7\":([0-9]+),\"8\":(-[0-9]+),\"29\":([0-9]+)\\}" );
        std::vector<int> indices{ 1, 2, 3, 4, 5, 6, 7 };
        std::sregex_token_iterator it( res_text.begin(), res_text.end(), inven_pattern, indices ), end;
        std::vector<std::u16string> tokens;
        for ( ; it != end; ++it )
            tokens.push_back( Util::UTF8toUTF16( *it ) );

        tokens[ 4 ] = tokens[ 4 ] != u"0" ? tokens[ 4 ] : u"데이터 없음";
        tokens[ 5 ] = tokens[ 5 ] != u"-10000" ? Util::UTF8toUTF16( std::to_string( -std::stoi( Util::UTF16toUTF8( tokens[ 5 ] ) ) ) ) : u"데이터 없음";

        kakao_sendtext( chatroom_name, fmt::format( u"연챠 거북이 : {}\n단챠 거북이 : {}\n자연산 거북이 : {}\n퀴즈 거북이 : {}\n\n자라 : {}\n\n최고령 거북이 : {}\n최연소 거북이 : {}", tokens[ 0 ], tokens[ 6 ], tokens[ 1 ], tokens[ 2 ], tokens[ 3 ], tokens[ 4 ], tokens[ 5 ] ) );
    } else if ( msg.rfind( u"/인벤 ", 0 ) == 0 || msg.rfind( u"/인벤토리 ", 0 ) == 0 ) { // 타인의 인벤
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(/인벤|/인벤토리) ([\\s\\S]+)" ) );
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );

        http::Request request{ __config.api_endpoint() + "member?chatroom_name=" + Util::URLEncode( __config.chatroom_name() ) };
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
                std::regex inven_pattern( "\\{\"1\":([0-9]+),\"2\":([0-9]+),\"3\":([0-9]+),\"6\":([0-9]+),\"7\":([0-9]+),\"8\":(-[0-9]+),\"29\":([0-9]+)\\}" );
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

            const auto jacket_response = jacket_request.send( "GET" );
            auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( jacket_response.body.data() ), static_cast<std::streamsize>( jacket_response.body.size() ) ), cv::IMREAD_UNCHANGED );
            auto bmp = Util::ConvertCVMatToBMP( frame );
            if ( Util::PasteBMPToClipboard( bmp ) ) {
                kakao_sendimage( chatroom_name );
            }
        }
    }

    if ( msg == u"/업데이트" && name == u"손창대" ) {
        kakao_sendtext( chatroom_name, u"업데이트를 진행합니다." );
        return RETURN_CODE::UPDATE;
    }
    return RETURN_CODE::OK;
}