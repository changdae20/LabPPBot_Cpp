#include "util.h"

namespace Util {
std::string TOUTF8( std::string &multibyte_str ) {
    char *pszIn = new char[ multibyte_str.length() + 1 ];
    strncpy_s( pszIn, multibyte_str.length() + 1, multibyte_str.c_str(), multibyte_str.length() );

    std::string resultString;

    int nLenOfUni = 0, nLenOfUTF = 0;
    wchar_t *uni_wchar = NULL;
    char *pszOut = NULL;

    // 1. ANSI(multibyte) Length
    if ( ( nLenOfUni = MultiByteToWideChar( 0, 0, pszIn, ( int )strlen( pszIn ), NULL, 0 ) ) <= 0 )
        return 0;

    uni_wchar = new wchar_t[ nLenOfUni + 1 ];
    memset( uni_wchar, 0x00, sizeof( wchar_t ) * ( nLenOfUni + 1 ) );

    // 2. ANSI(multibyte) ---> unicode
    nLenOfUni = MultiByteToWideChar( 0, 0, pszIn, ( int )strlen( pszIn ), uni_wchar, nLenOfUni );

    // 3. utf8 Length
    if ( ( nLenOfUTF = WideCharToMultiByte( 65001, 0, uni_wchar, nLenOfUni, NULL, 0, NULL, NULL ) ) <= 0 ) {
        delete[] uni_wchar;
        return 0;
    }

    pszOut = new char[ nLenOfUTF + 1 ];
    memset( pszOut, 0, sizeof( char ) * ( nLenOfUTF + 1 ) );

    // 4. unicode ---> utf8
    nLenOfUTF = WideCharToMultiByte( 65001, 0, uni_wchar, nLenOfUni, pszOut, nLenOfUTF, NULL, NULL );
    pszOut[ nLenOfUTF ] = 0;
    resultString = pszOut;

    delete[] pszIn;
    delete[] uni_wchar;
    delete[] pszOut;

    return resultString;
}

std::string URLEncode( const std::string &str ) {
    std::string result{};
    char buff[ 4 ];
    for ( const auto &ch : str ) {
        if ( ch == ' ' )
            result.push_back( L'+' );
        else if ( ( ch >= 'A' && ch <= 'Z' ) || ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' ) )
            result.push_back( static_cast<char>( ch ) );
        else if ( ch == '-' || ch == '_' || ch == '.' || ch == '!' || ch == '~' || ch == '*' || ch == '(' || ch == ')' || ch == ':' || ch == '/' || ch == '?' || ch == '=' )
            result.push_back( static_cast<char>( ch ) );
        else {
            sprintf_s( buff, "%%%X", static_cast<unsigned char>( ch ) );
            result += buff;
        }
    }
    return result;
}

std::string URLEncode( const std::u16string &u16str ) {
    std::string str_utf8;
    utf8::utf16to8( u16str.begin(), u16str.end(), back_inserter( str_utf8 ) );
    std::string result{};
    char buff[ 4 ];
    for ( const auto &ch : str_utf8 ) {
        if ( ch == ' ' )
            result.push_back( L'+' );
        else if ( ( ch >= 'A' && ch <= 'Z' ) || ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' ) )
            result.push_back( static_cast<char>( ch ) );
        else if ( ch == '-' || ch == '_' || ch == '.' || ch == '!' || ch == '~' || ch == '*' || ch == '(' || ch == ')' || ch == ':' || ch == '/' || ch == '?' || ch == '=' )
            result.push_back( static_cast<char>( ch ) );
        else {
            sprintf_s( buff, "%%%X", static_cast<unsigned char>( ch ) );
            result += buff;
        }
    }
    return result;
}

std::string UTF16toUTF8( const std::u16string &u16str ) {
    std::string s;
    utf8::utf16to8( u16str.begin(), u16str.end(), back_inserter( s ) );
    return s;
}

std::u16string UTF8toUTF16( const std::string &u8str ) {
    std::u16string s;
    utf8::utf8to16( u8str.begin(), utf8::find_invalid( u8str.begin(), u8str.end() ), back_inserter( s ) );
    return s;
}

std::vector<std::u16string> split( const std::u16string &str, const std::string &deli ) {
    std::regex re( deli );
    std::string temp = Util::UTF16toUTF8( str );
    std::sregex_token_iterator it( temp.begin(), temp.end(), re, -1 ), end;
    std::vector<std::u16string> splitted;
    for ( ; it != end; ++it )
        splitted.push_back( Util::UTF8toUTF16( *it ) );

    return splitted;
}
}; // namespace Util