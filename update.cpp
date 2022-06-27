#include <Windows.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <numeric>
#include <regex>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

std::string exec( const char *cmd ) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype( &pclose )> pipe( popen( cmd, "r" ), pclose );
    if ( !pipe ) {
        throw std::runtime_error( "popen() failed!" );
    }
    while ( fgets( buffer.data(), buffer.size(), pipe.get() ) != nullptr ) {
        result += buffer.data();
    }
    return result;
}

int main() {
    std::string status = "initial";
    while ( true ) {
        int status_code = system( std::string( ".\\bin\\Release\\main.exe \"" + status + "\"" ).c_str() );
        if ( status_code == 0 ) { // 일반적인 종료
            continue;
        } else if ( status_code == 1 ) { // 업데이트 요청
            auto old_log_raw = exec( "git log --format=%s" );
            std::regex re( "\n" );
            std::sregex_token_iterator it( old_log_raw.begin(), old_log_raw.end() - 1, re, -1 ), end;
            std::vector<std::string> old_log( it, end );

            auto update_result = exec( "git pull" );

            if ( update_result == "Already up to date.\n" ) { // 이미 최신버전인 경우
                status = "Up To Date";
            } else { // 업데이트가 있는 경우
                system( ".\\build.bat" ); // 새로 빌드
                auto new_log_raw = exec( "git log --format=%s" );
                std::regex re( "\n" );
                std::sregex_token_iterator it( new_log_raw.begin(), new_log_raw.end() - 1, re, -1 ), end;
                std::vector<std::string> new_log( it, end );
                new_log.erase( new_log.begin() + ( new_log.size() - old_log.size() ), new_log.end() );
                std::string logs = std::accumulate( new_log.begin(), new_log.end(), std::string( "" ), []( auto &a, auto &b ) { return a + b + "\n"; } ); // join with newline
                status = logs.substr( logs.length() - 1 );
            }
        }
    }
}