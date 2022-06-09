#include <algorithm>
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
        PostKeyEx( child_wnd, static_cast<UINT>( 'V' ), VK_CONTROL, false );
        Sleep( 50 );
        SendReturn( GetForegroundWindow() );
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
        }
        return std::pair( splitted.at( splitted.size() - 1 ), last_idx + splitted.size() );
    }
}

RETURN_CODE execute_command( const std::string &chatroom_name, const std::u16string &name, const std::u16string &AMPM, const std::u16string &time, const std::u16string &msg ) {
    if ( name == u"EndTime" ) {
        return RETURN_CODE::OK;
    }

    if ( msg == u"/자라" ) {
        kakao_sendtext( chatroom_name, std::u16string( u"자라" ) );
        return RETURN_CODE::OK;
    }
    if ( msg.rfind( u"/곡정보", 0 ) == 0 ) {
        auto args = Util::split( msg, " " );
        if ( args.size() == 2 && args[ 1 ] != u"" ) { // /곡정보 별명
            http::Request request{ __config.api_endpoint() + "songs?title=" + Util::URLEncode( ( args[ 1 ] ) ) };
            const auto response = request.send( "GET" );
            const std::string res_text = std::string( response.body.begin(), response.body.end() );
            if ( res_text == "{}" ) { // 검색 결과가 없는 경우
                kakao_sendtext( chatroom_name, u"곡정보를 찾을 수 없습니다." );
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
    }
    return RETURN_CODE::OK;
}