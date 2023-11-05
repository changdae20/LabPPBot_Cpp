#include "message.h"

void SendReturn( HWND hwnd ) {
    PostMessage( hwnd, WM_KEYDOWN, VK_RETURN, 0 );
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    PostMessage( hwnd, WM_KEYUP, VK_RETURN, 0 );
    return;
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
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            PostMessage( hwnd, WM_KEYDOWN, key, lparam );
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            PostMessage( hwnd, WM_KEYUP, key, lparam | 0xC0000000 );
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            SetKeyboardState( pKeyBuffers_old );
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            AttachThreadInput( GetCurrentThreadId(), ThreadId, false );
        }
    } else {
        SendMessage( hwnd, WM_KEYDOWN, key, lparam );
        SendMessage( hwnd, WM_KEYUP, key, lparam | 0xC0000000 );
    }
    return;
}

void open_chatroom( const std::string &chatroom_name ) {
    HWND hwnd = ::FindWindowA( NULL, reinterpret_cast<LPCSTR>( "카카오톡" ) );
    HWND child_wnd = ::FindWindowExA( hwnd, NULL, reinterpret_cast<LPCSTR>( "EVA_ChildWindow" ), NULL );
    HWND temp_wnd = ::FindWindowExA( child_wnd, NULL, reinterpret_cast<LPCSTR>( "EVA_Window" ), NULL );
    HWND list_wnd = ::FindWindowExA( child_wnd, temp_wnd, reinterpret_cast<LPCSTR>( "EVA_Window" ), NULL );
    HWND edit_wnd = ::FindWindowExA( list_wnd, NULL, reinterpret_cast<LPCSTR>( "Edit" ), NULL );

    SendMessageA( edit_wnd, WM_SETTEXT, NULL, reinterpret_cast<LPARAM>( chatroom_name.c_str() ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    int retry_count = 3;
    do {
        SendReturn( edit_wnd );
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    } while ( --retry_count > 0 && !::FindWindowA( NULL, reinterpret_cast<LPCSTR>( chatroom_name.c_str() ) ) );

    if ( retry_count == 0 ) {
        SetForegroundWindow( hwnd );
        // down arrow
        PostKeyEx( edit_wnd, VK_DOWN, NULL, false );
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        // enter
        PostKeyEx( edit_wnd, VK_RETURN, NULL, false );
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        return;
    }
    return;
}