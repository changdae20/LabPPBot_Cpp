#include "util.h"

namespace Util {
void my_draw_bitmap( cv::Mat &img, FT_Bitmap *bitmap, int x, int y, cv::Scalar color ) {
    cv::Scalar src_col, dst_col;
    for ( int i = 0; i < bitmap->rows; i++ ) {
        for ( int j = 0; j < bitmap->width; j++ ) {
            unsigned char val = bitmap->buffer[ j + i * bitmap->pitch ];
            float mix = ( float )val / 255.0;
            if ( val != 0 ) {
                src_col = cv::Scalar( img.at<cv::Vec3b>( i + y, j + x ) );
                dst_col = mix * color + ( 1.0 - mix ) * src_col;
                img.at<cv::Vec3b>( i + y, j + x ) = cv::Vec3b( dst_col[ 0 ], dst_col[ 1 ], dst_col[ 2 ] );
            }
        }
    }
}
float PrintString( cv::Mat &img, std::u32string str, int x, int y, cv::Scalar color, FT_Face &face ) {
    FT_Bool use_kerning = 0;
    FT_UInt previous = 0;
    use_kerning = FT_HAS_KERNING( face );
    float prev_yadv = 0;
    float posx = 0;
    float posy = 0;
    float dx = 0;
    for ( int k = 0; k < str.length(); k++ ) {
        int glyph_index = FT_Get_Char_Index( face, str.c_str()[ k ] );
        FT_GlyphSlot slot = face->glyph; // a small shortcut
        if ( k > 0 ) {
            dx = slot->advance.x / 64;
        }
        FT_Load_Glyph( face, glyph_index, FT_LOAD_DEFAULT | FT_LOAD_COLOR );
        // FT_Render_Glyph (slot,FT_RENDER_MODE_NORMAL);
        prev_yadv = slot->metrics.vertAdvance / 64;
        if ( use_kerning && previous && glyph_index ) {
            FT_Vector delta;
            FT_Get_Kerning( face, previous, glyph_index, FT_KERNING_DEFAULT, &delta );
            posx += ( delta.x / 64 );
        }
        posx += ( dx );
        my_draw_bitmap( img, &slot->bitmap, posx + x + slot->bitmap_left, y - slot->bitmap_top + posy, color );
        previous = glyph_index;
    }
    return prev_yadv;
}
void PrintText( cv::Mat &img, std::u32string str, int x, int y, cv::Scalar color, FT_Face &face ) {
    float posy = 0;
    for ( int pos = str.find_first_of( u'\n' ); pos != std::u16string::npos; pos = str.find_first_of( u'\n' ) ) {
        std::u32string substr = str.substr( 0, pos );
        str.erase( 0, pos + 1 );
        posy += PrintString( img, substr, x, y + posy, color, face );
    }
    PrintString( img, str, x, y + posy, color, face );
}
int time_distance( std::u16string AMPM, std::u16string stime ) {
    std::time_t t = time( NULL );
    struct tm *tm = localtime( &t );
    std::array<char, 10> buffer;
    int len = strftime( &buffer[ 0 ], buffer.size(), "%H", tm );
    int hour = std::stoi( std::string( buffer.begin(), buffer.begin() + len ) );

    len = strftime( &buffer[ 0 ], buffer.size(), "%M", tm );
    int min = std::stoi( std::string( buffer.begin(), buffer.begin() + len ) );

    auto input = split( stime, ":" );
    int input_hour = std::stoi( UTF16toUTF8( input[ 0 ] ) ) + ( AMPM == u"오전" ? 0 : 12 ) + ( input[ 0 ] == u"12" ? -12 : 0 );
    int input_min = std::stoi( UTF16toUTF8( input[ 1 ] ) );

    return std::abs( hour * 60 + min - input_hour * 60 - input_min );
}

template <typename T, typename UnaryOperation>
std::enable_if_t<is_specialization<T, std::vector>::value, T> parse( std::string a, UnaryOperation unary_op ) {
    T ans;
    if ( a.length() < 2 || a[ 0 ] != '[' || a[ a.length() - 1 ] != ']' )
        return ans; // Error Case
    if ( a == "[]" )
        return ans; // Base Case

    if constexpr ( is_specialization<T::value_type, std::vector>::value ) { // 2D이상의 vector로 parse하는 경우 recursive하게 구현
        int count = 0;                                                      // opening bracket '['과 closing bracket ']'의 개수 차이를 count함.
        std::string::iterator start;
        for ( auto it = a.begin() + 1; it < a.end() - 1; ++it ) {
            if ( ( *it ) == '[' ) {
                if ( count == 0 ) {
                    start = it;
                }
                count++;
            } else if ( ( *it ) == ']' ) {
                count--;
                if ( count == 0 ) {
                    ans.push_back( parse<T::value_type>( std::string( start, ++it ), unary_op ) );
                }
            }
        }
        return ans;
    } else { // base case
        std::regex re( "," );
        std::sregex_token_iterator it( a.begin() + 1, a.end() - 1, re, -1 ), end;
        std::transform( it, end, std::back_inserter( ans ), unary_op );
        return ans;
    }
}

int rand( int start, int end ) {
    std::random_device rd;
    std::mt19937 gen( rd() );
    std::uniform_int_distribution<> dist( start, end );
    return dist( gen );
}

HBITMAP ConvertCVMatToBMP( cv::Mat &frame, bool padding ) {
    auto [ width, height ] = frame.size();
    if ( !padding ) {                                                                                                   // resize mode
        cv::resize( frame, frame, cv::Size( ( width + 2 ) - ( width + 2 ) % 4, ( height + 2 ) - ( height + 2 ) % 4 ) ); // HBITMAP으로 변환하려면 4의 배수가 되어야 예외 없다고 함. 출처 : https://stackoverflow.com/questions/43656578/convert-mat-to-bitmap-in-windows-application
    } else {                                                                                                            // padding mode
        cv::copyMakeBorder( frame, frame, 0, ( 4 - height % 4 ) % 4, 0, ( 4 - width % 4 ) % 4, cv::BORDER_REPLICATE );
    }
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