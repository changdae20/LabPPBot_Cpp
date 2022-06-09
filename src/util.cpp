#include "util.h"

namespace Util {
int rand(int start, int end){
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(start,end);
    return dist(gen);
}
HBITMAP ConvertCVMatToBMP( cv::Mat &frame ) {
    auto [ width, height ] = frame.size();
    cv::resize( frame, frame, cv::Size( ( width + 2 ) - ( width + 2 ) % 4, ( height + 2 ) - ( height + 2 ) % 4 ) ); // HBITMAP으로 변환하려면 4의 배수가 되어야 예외 없다고 함. 출처 : https://stackoverflow.com/questions/43656578/convert-mat-to-bitmap-in-windows-application
    auto convertOpenCVBitDepthToBits = []( const int32_t value ) {
        auto regular = 0u;

        switch ( value ) {
        case CV_8U:
        case CV_8S:
            regular = 8u;
            break;

        case CV_16U:
        case CV_16S:
            regular = 16u;
            break;

        case CV_32S:
        case CV_32F:
            regular = 32u;
            break;

        case CV_64F:
            regular = 64u;
            break;

        default:
            regular = 0u;
            break;
        }
        return regular;
    };

    auto imageSize = frame.size();
    assert( imageSize.width && "invalid size provided by frame" );
    assert( imageSize.height && "invalid size provided by frame" );

    if ( imageSize.width && imageSize.height ) {
        auto headerInfo = BITMAPINFOHEADER{};
        ZeroMemory( &headerInfo, sizeof( headerInfo ) );

        headerInfo.biSize = sizeof( headerInfo );
        headerInfo.biWidth = imageSize.width;
        headerInfo.biHeight = -( imageSize.height ); // negative otherwise it will be upsidedown
        headerInfo.biPlanes = 1;                     // must be set to 1 as per documentation frame.channels();

        const auto bits = convertOpenCVBitDepthToBits( frame.depth() );
        headerInfo.biBitCount = frame.channels() * bits;

        auto bitmapInfo = BITMAPINFO{};
        ZeroMemory( &bitmapInfo, sizeof( bitmapInfo ) );

        bitmapInfo.bmiHeader = headerInfo;
        bitmapInfo.bmiColors->rgbBlue = 0;
        bitmapInfo.bmiColors->rgbGreen = 0;
        bitmapInfo.bmiColors->rgbRed = 0;
        bitmapInfo.bmiColors->rgbReserved = 0;

        auto dc = GetDC( nullptr );
        assert( dc != nullptr && "Failure to get DC" );
        auto bmp = CreateDIBitmap( dc,
                                   &headerInfo,
                                   CBM_INIT,
                                   frame.data,
                                   &bitmapInfo,
                                   DIB_RGB_COLORS );
        assert( bmp != nullptr && "Failure creating bitmap from captured frame" );
        return bmp;
    } else {
        return nullptr;
    }
}

bool PasteBMPToClipboard( void *bmp ) {
    assert( bmp != nullptr && "You need a bmp for this function to work" );

    if ( OpenClipboard( 0 ) && bmp != nullptr ) {
        EmptyClipboard();
        SetClipboardData( CF_BITMAP, bmp );
        CloseClipboard();
        return true;
    } else {
        return false;
    }
}

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