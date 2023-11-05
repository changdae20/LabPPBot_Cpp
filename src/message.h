#ifndef MESSAGE_H
#define MESSAGE_H

#include <chrono>
#include <fmt/core.h>
#include <fmt/xchar.h>
#include <string>
#include <thread>
#include <variant>
#include <windows.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "util.h"
void SendReturn( HWND hwnd );

void PostKeyEx( HWND &hwnd, UINT key, WPARAM shift, bool specialkey );

void open_chatroom( const std::string &chatroom_name );

class Message {
public:
    Message( std::string chatroom_name, std::variant<std::u16string, cv::Mat> message ) : chatroom_name( chatroom_name ), message( message ) {}

    void send() {
        if ( std::holds_alternative<std::u16string>( message ) ) {
            HWND hwnd = ::FindWindowA( NULL, reinterpret_cast<LPCSTR>( chatroom_name.c_str() ) );
            if ( hwnd == nullptr ) {
                open_chatroom( chatroom_name );
            }
            hwnd = ::FindWindowA( NULL, reinterpret_cast<LPCSTR>( chatroom_name.c_str() ) );
            if ( hwnd ) {
                auto child_wnd = ::FindWindowExA( hwnd, NULL, reinterpret_cast<LPCSTR>( "RICHEDIT50W" ), NULL );
                ::SendMessageW( child_wnd, WM_SETTEXT, 0, reinterpret_cast<LPARAM>( std::get<std::u16string>( message ).c_str() ) );
                SendReturn( child_wnd );
            }
        } else if ( std::holds_alternative<cv::Mat>( message ) ) {
            auto bmp = Util::ConvertCVMatToBMP( std::get<cv::Mat>( message ) );
            if ( Util::PasteBMPToClipboard( bmp ) ) {
                HWND hwnd = ::FindWindowA( NULL, reinterpret_cast<LPCSTR>( chatroom_name.c_str() ) );
                if ( hwnd == nullptr ) {
                    open_chatroom( chatroom_name );
                }
                hwnd = ::FindWindowA( NULL, reinterpret_cast<LPCSTR>( chatroom_name.c_str() ) );
                if ( hwnd ) {
                    auto child_wnd = ::FindWindowExA( hwnd, NULL, reinterpret_cast<LPCSTR>( "EVA_VH_ListControl_Dblclk" ), NULL );
                    SetForegroundWindow( child_wnd );
                    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
                    PostKeyEx( child_wnd, static_cast<UINT>( 'V' ), VK_CONTROL, false );
                    if ( GetForegroundWindow() == child_wnd ) {
                        PostKeyEx( child_wnd, static_cast<UINT>( 'V' ), VK_CONTROL, false );
                    }
                    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
                    SendReturn( GetForegroundWindow() );
                    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
                }
            }
        }
    }

private:
    std::string chatroom_name;
    std::variant<std::u16string, cv::Mat> message;
};

#endif // MESSAGE_H