#include <algorithm>
#include <array>
#include <filesystem>
#include <fmt/core.h>
#include <fmt/xchar.h>
#include <future>
#include <google/protobuf/util/json_util.h>
#include <iostream>
#include <numeric>
#include <regex>
#include <string>
#include <utility>
#include <windows.h>

#include "HTTPRequest.hpp"
#include "core.h"
#include "message.h"
#include "util.h"

extern config::Config __config;

// /갱신, >갱신에서 사용할 스레드
extern std::vector<std::future<std::pair<std::string, std::u16string>>> renewal_threads;

void achievement_count( std::vector<Message> &message_queue, std::mutex &mq_mutex, std::u16string name, int member_id, int counter_id, int val ) {
    http::Request request{ __config.api_endpoint() + "counter" };
    const std::string body = fmt::format( "member_id={}&counter_id={}&counter_value={}", member_id, counter_id, val );
    auto response = request.send( "POST", body, { { "Content-Type", "application/x-www-form-urlencoded" } } );
    db::AchievementList list;
    std::string res_text = std::string( response.body.begin(), response.body.end() );
    auto replaced = "{\"achievements\" : " + std::regex_replace( res_text, std::regex( "goal_counter" ), "goalCounter" ) + "}";
    google::protobuf::util::JsonStringToMessage( replaced.c_str(), &list );

    for ( const auto &achievement : list.achievements() ) {
        if ( achievement.type() == "normal" ) { // normal 업적의 경우 그냥 출력하면 됨
            mq_mutex.lock();
            message_queue.push_back( Message( __config.chatroom_name(), fmt::format( u"⭐{}님의 새로운 업적⭐\n[{}] {}\n***{}***", name, Util::UTF8toUTF16( achievement.tag() ), Util::UTF8toUTF16( achievement.name() ), Util::UTF8toUTF16( achievement.description() ) ) ) );
            mq_mutex.unlock();
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
                mq_mutex.lock();
                message_queue.push_back( Message( __config.chatroom_name(), fmt::format( u"⭐{}님의 새로운 업적⭐\n[{}] {}\n***{}***", name, Util::UTF8toUTF16( achievement.tag() ), Util::UTF8toUTF16( achievement.name() ), Util::UTF8toUTF16( achievement.description() ) ) ) );
                mq_mutex.unlock();
            } else {
                auto replaced_description = std::regex_replace( achievement.description(), std::regex( "[^\\s]" ), "?" );
                mq_mutex.lock();
                message_queue.push_back( Message( __config.chatroom_name(), fmt::format( u"⭐{}님의 새로운 업적⭐\n[{}] {}\n***{}***", name, Util::UTF8toUTF16( achievement.tag() ), Util::UTF8toUTF16( achievement.name() ), Util::UTF8toUTF16( replaced_description ) ) ) );
                mq_mutex.unlock();
            }
        }
    }
    return;
}

std::pair<RETURN_CODE, std::string> loop( std::vector<Message> &message_queue, std::mutex &mq_mutex ) {
    for ( const auto &entry : std::filesystem::directory_iterator( "message/data" ) ) {
        std::string s;
        message::Message m;
        std::getline( std::ifstream( entry.path().string() ), s, '\0' );
        google::protobuf::util::JsonStringToMessage( s, &m );
        auto res = execute_command( message_queue, mq_mutex, Util::FROMUTF8( m.room() ), Util::UTF8toUTF16( m.sender() ), Util::UTF8toUTF16( m.msg() ), m.isgroupchat() );
        if ( res != RETURN_CODE::OK ) {
            std::remove( entry.path().string().c_str() );
            return { res, Util::FROMUTF8( m.room() ) };
        } else {
            std::remove( entry.path().string().c_str() );
        }
    }
    return { RETURN_CODE::OK, "" };
}

std::pair<bool, member::Member> find_by_name( const std::u16string &name, const std::string &chatroom_name ) {
    member::Member m;
    http::Request req( fmt::format( "{}member/name?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( name ), Util::URLEncode( chatroom_name ) ) );
    auto response = req.send( "GET" );
    auto res_text = std::string( response.body.begin(), response.body.end() );
    if ( res_text == "{}" ) {
        return { false, m };
    }
    auto status = google::protobuf::util::JsonStringToMessage( res_text, &m );
    if ( status.ok() ) {
        return { true, m };
    } else {
        return { false, m };
    }
}

std::pair<bool, member::Member> find_by_hash( const std::u16string &name, const std::string &chatroom_name ) {
    member::Member m;
    http::Request req( fmt::format( "{}member?name={}&chatroom_name={}", __config.api_endpoint(), Util::URLEncode( name ), Util::URLEncode( chatroom_name ) ) );
    auto response = req.send( "GET" );
    auto res_text = std::string( response.body.begin(), response.body.end() );
    if ( res_text == "{}" ) {
        return { false, m };
    }
    auto status = google::protobuf::util::JsonStringToMessage( res_text, &m );
    if ( status.ok() ) {
        return { true, m };
    } else {
        return { false, m };
    }
}

RETURN_CODE execute_command( std::vector<Message> &message_queue, std::mutex &mq_mutex, const std::string &chatroom_name, const std::u16string &_name, const std::u16string &msg, bool is_groupchat ) {
    std::u16string name;
    member::Member m;

    // chatroom_name, name Resolving
    {
        auto [ found, _m ] = find_by_hash( _name, chatroom_name );
        if ( !found ) {
            return RETURN_CODE::OK;
        }
        m = std::move( _m );
        name = Util::UTF8toUTF16( m.name() );
    }

    // 단체방, 방 내부 공개 : /자라, /자라자라, /거북이, /인벤, /기린랭킹
    {
        if ( msg == u"/자라" ) {
            if ( Util::rand( 1, 100 ) == 100 ) { // 1%
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, std::u16string( u"거북이" ) ) );
                mq_mutex.unlock();
            } else {
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, std::u16string( u"자라" ) ) );
                mq_mutex.unlock();
            }
            return RETURN_CODE::OK;
        }
        if ( is_groupchat && m.permission().length() > 0 && m.permission()[ 0 ] == '1' ) {
            if ( msg == u"/자라자라" ) {
                if ( std::ifstream( "src/zara_data.json" ).fail() ) {
                    std::cout << "Fail!" << std::endl;
                    std::ofstream o( "src/zara_data.json" );
                    o << "{\"dict\":{}, \"age\":{}}";
                }
                std::string json;
                std::getline( std::ifstream( "src/zara_data.json" ), json, '\0' );
                turtle::ZaraData data;
                google::protobuf::util::JsonStringToMessage( json, &data );

                if ( ( data.dict().find( m.id() ) != data.dict().end() ) && ( *data.mutable_dict() )[ m.id() ] > std::time( NULL ) - 3600 * 5 ) { // 쿨이 안돈 경우
                    int sec = ( *data.mutable_dict() )[ m.id() ] + 3600 * 5 - std::time( NULL );
                    int hour = sec / 3600;
                    int min = ( sec % 3600 ) / 60;
                    sec %= 60;
                    mq_mutex.lock();
                    message_queue.push_back( Message( chatroom_name, fmt::format( u"아직 연속자라를 사용할 수 없습니다 : {}시간 {}분 {}초 남음", hour, min, sec ) ) );
                    mq_mutex.unlock();
                } else {                                                     // 쿨이 돈 경우
                    std::array<std::u16string, 5> arr;                       // 5번 가챠 결과 담는 컨테이너
                    std::vector<int> ages;                                   // 가챠 성공결과 담는 컨테이너
                    bool is_quiz = ( *data.mutable_dict() )[ m.id() ] == -1; // 퀴즈로 쿨초받은 경우 -1로 세팅되어있음.
                    for ( auto &el : arr ) {
                        if ( Util::rand( 1, 100 ) == 100 ) { // 1%
                            el = u"거북이";
                            ages.push_back( ( *data.mutable_age() )[ chatroom_name ] );
                            ( *data.mutable_age() )[ chatroom_name ] = 0;
                        } else {
                            ( *data.mutable_age() )[ chatroom_name ] += 1;
                            el = u"자라";
                        }
                    }

                    mq_mutex.lock();
                    message_queue.push_back( Message( chatroom_name, fmt::format( u"{}\n{}\n{}\n{}\n{}", arr[ 0 ], arr[ 1 ], arr[ 2 ], arr[ 3 ], arr[ 4 ] ) ) );
                    mq_mutex.unlock();

                    if ( ages.size() > 0 ) {                                              // 가챠로 먹은 경우
                        if ( std::find( ages.begin(), ages.end(), 100 ) != ages.end() ) { // 정확하게 100살짜리를 먹은 경우
                            achievement_count( message_queue, mq_mutex, Util::UTF8toUTF16( m.name() ), m.id(), 28, 1 );
                        }
                        auto [ min, max ] = std::minmax_element( ages.begin(), ages.end() );
                        achievement_count( message_queue, mq_mutex, Util::UTF8toUTF16( m.name() ), m.id(), 7, *max );
                        achievement_count( message_queue, mq_mutex, Util::UTF8toUTF16( m.name() ), m.id(), 8, *min );
                        if ( ages.size() >= 2 ) { // 쌍거북 이상의 경우
                            achievement_count( message_queue, mq_mutex, Util::UTF8toUTF16( m.name() ), m.id(), 7 + ages.size(), 1 );
                        }
                        if ( is_quiz ) { // 퀴즈 거북인 경우
                            achievement_count( message_queue, mq_mutex, Util::UTF8toUTF16( m.name() ), m.id(), 3, ages.size() );
                        } else { // normal case
                            achievement_count( message_queue, mq_mutex, Util::UTF8toUTF16( m.name() ), m.id(), 1, ages.size() );
                        }
                    }

                    achievement_count( message_queue, mq_mutex, Util::UTF8toUTF16( m.name() ), m.id(), 5, 1 );               // 쿨이 돈 연챠를 실행
                    achievement_count( message_queue, mq_mutex, Util::UTF8toUTF16( m.name() ), m.id(), 6, 5 - ages.size() ); // 거북이 먹은 개수 추가
                    ( *data.mutable_dict() )[ m.id() ] = std::time( NULL );
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

                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, fmt::format( u"현재 거북이 이후 {}연속 자라입니다.", ( *data.mutable_age() )[ chatroom_name ] ) ) );
                mq_mutex.unlock();
            }
            if ( msg == u"/인벤" || msg == u"/인벤토리" ) { // 자신의 인벤
                http::Request request{ fmt::format( "{}counter/inventory?member_id={}", __config.api_endpoint(), m.id() ) };
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

                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, fmt::format( u"<<{}님의 인벤토리>>\n\n거북이 : {}\n자라 : {}\n\n최고령 거북이 : {}\n최연소 거북이 : {}", name, tokens[ 0 ], tokens[ 3 ], tokens[ 4 ], tokens[ 5 ] ) ) );
                mq_mutex.unlock();
            } else if ( msg.rfind( u"/인벤 ", 0 ) == 0 || msg.rfind( u"/인벤토리 ", 0 ) == 0 ) { // 타인의 인벤
                auto u8msg = Util::UTF16toUTF8( msg );
                std::regex reg( Util::UTF16toUTF8( u"(/인벤|/인벤토리) ([\\s\\S]+)" ) );
                std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
                auto query_name = Util::UTF8toUTF16( *it );

                auto [ found, query_m ] = find_by_name( query_name, chatroom_name );
                if ( !found ) {
                    mq_mutex.lock();
                    message_queue.push_back( Message( chatroom_name, u"정보를 찾을 수 없습니다." ) );
                    mq_mutex.unlock();
                } else {
                    auto request = http::Request( fmt::format( "{}counter/inventory?member_id={}", __config.api_endpoint(), query_m.id() ) );
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

                    mq_mutex.lock();
                    message_queue.push_back( Message( chatroom_name, fmt::format( u"<<{}님의 인벤토리>>\n\n거북이 : {}\n자라 : {}\n\n최고령 거북이 : {}\n최연소 거북이 : {}", query_name, tokens[ 0 ], tokens[ 3 ], tokens[ 4 ], tokens[ 5 ] ) ) );
                    mq_mutex.unlock();
                }
            }
            if ( msg == u"/기린랭킹" ) {
                http::Request members_request{ fmt::format( "{}member/list?chatroom_name={}", __config.api_endpoint(), Util::URLEncode( chatroom_name ) ) };
                auto members_response = members_request.send( "GET" );
                auto res_text = std::string( members_response.body.begin(), members_response.body.end() );
                res_text = std::string( "{\"members\":" ) + res_text + "}";
                member::MemberList member_list;
                google::protobuf::util::JsonStringToMessage( res_text, &member_list );

                if ( member_list.members_size() == 0 ) {
                    mq_mutex.lock();
                    message_queue.push_back( Message( chatroom_name, u"정보를 불러올 수 없습니다." ) );
                    mq_mutex.unlock();
                    return RETURN_CODE::OK;
                }

                class Turtle {
                public:
                    Turtle( std::u16string name, int turtle, int zara ) : name( name ), zara( zara ), turtle( turtle ), score( ( turtle + zara ) == 0 ? 0.0 : ( static_cast<float>( turtle ) / ( turtle + zara ) ) ) {}
                    bool operator>( const Turtle &t ) const {
                        if ( score == t.score ) {
                            if ( turtle == t.turtle ) {
                                return zara < t.zara;
                            }
                            return turtle > t.turtle;
                        }
                        return score > t.score;
                    }

                    std::u16string name;
                    int zara;
                    int turtle;
                    float score;
                };

                std::vector<Turtle> turtle_data;

                auto insert_zero_width_space = []( std::u16string str ) {
                    return std::accumulate( str.begin(), str.end(), std::u16string(), []( std::u16string a, char16_t b ) {
                        return a + u"\u200B" + b;
                    } );
                };

                for ( auto &member : member_list.members() ) {
                    http::Request data_request{ fmt::format( "{}counter/inventory?member_id={}", __config.api_endpoint(), member.id() ) };
                    auto data_response = data_request.send( "GET" );
                    res_text = std::string( data_response.body.begin(), data_response.body.end() );
                    std::regex inven_pattern( "\\{\"1\":([0-9]+),\"2\":([0-9]+),\"3\":([0-9]+),\"6\":([0-9]+),\"7\":([0-9]+),\"8\":([-]*[0-9]+),\"29\":([0-9]+)\\}" );
                    std::vector<int> indices{ 1, 2, 3, 4, 5, 6, 7 };
                    std::sregex_token_iterator it( res_text.begin(), res_text.end(), inven_pattern, indices ), end;
                    std::vector<int> tokens;
                    for ( ; it != end; ++it )
                        tokens.push_back( std::stoi( *it ) );
                    turtle_data.push_back( Turtle( insert_zero_width_space( Util::UTF8toUTF16( member.name() ) ), tokens[ 0 ], tokens[ 3 ] ) );
                }
                turtle_data.push_back( Turtle( u"기댓값", 1, 99 ) );
                std::sort( turtle_data.begin(), turtle_data.end(), std::greater<Turtle>() );

                std::u16string result = u"🦒기린랭킹🦒\n";
                int rank = 0;
                float prev_score = -1;
                for ( auto &turtle : turtle_data ) {
                    if ( turtle.name == u"기댓값" ) {
                        result += fmt::format( u"<===== 기댓값 =====>\n" );
                    } else {
                        result += fmt::format( u"{}. {} : {}/{}({:.2f})%\n", prev_score == turtle.score ? rank : ++rank, turtle.name, turtle.turtle, turtle.turtle + turtle.zara, turtle.score * 100 );
                        prev_score = turtle.score;
                    }
                }
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, result.substr( 0, result.length() - 1 ) ) );
                mq_mutex.unlock();
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
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"곡정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            // TODO : 검색통해서 ~~~를 찾으시나요? 출력
        } else {
            std::string replaced = std::regex_replace( res_text, std::regex( "chain_vi" ), "chainVi" );
            replaced = std::regex_replace( res_text, std::regex( "chain_v" ), "chainV" );
            replaced = std::regex_replace( res_text, std::regex( "table_S" ), "tableS" );
            replaced = std::regex_replace( res_text, std::regex( "table_PUC" ), "tablePUC" );
            db::SdvxSong song;
            google::protobuf::util::JsonStringToMessage( replaced.c_str(), &song );
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"제목 : " + Util::UTF8toUTF16( song.title() ) +
                                                                 u"\n레벨 : " + Util::UTF8toUTF16( std::to_string( song.level() ) ) +
                                                                 u"\n작곡가 : " + Util::UTF8toUTF16( song.artist() ) +
                                                                 u"\n이펙터 : " + Util::UTF8toUTF16( song.effector() ) +
                                                                 u"\n일러스트레이터 : " + Util::UTF8toUTF16( song.illustrator() ) +
                                                                 u"\nBPM : " + Util::UTF8toUTF16( song.bpm() ) +
                                                                 u"\n체인수 : " + Util::UTF8toUTF16( std::to_string( song.chain_vi() ) ) +
                                                                 ( ( song.level() == 18 ) ? ( u"\nPUC 난이도 : " + Util::UTF8toUTF16( ( song.table_puc() == "undefined" ) ? Util::UTF16toUTF8( u"미정" ) : song.table_puc() ) ) : u"" ) ) );
            mq_mutex.unlock();
            std::string lower_code;
            std::transform( song.code().begin(), song.code().end(), back_inserter( lower_code ), ::tolower );

            try {
                auto frame = cv::imread( fmt::format( "songs/{}/jacket.png", lower_code ), cv::IMREAD_UNCHANGED );
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, frame ) );
                mq_mutex.unlock();
            } catch ( cv::Exception &e ) {
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, fmt::format( u"자켓을 찾을 수 없습니다.\nErr : {}", Util::UTF8toUTF16( e.what() ) ) ) );
                mq_mutex.unlock();
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
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"곡정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            // TODO : 검색통해서 ~~~를 찾으시나요? 출력
        } else {
            std::string replaced = std::regex_replace( res_text, std::regex( "chain_vi" ), "chainVi" );
            replaced = std::regex_replace( res_text, std::regex( "chain_v" ), "chainV" );
            replaced = std::regex_replace( res_text, std::regex( "table_S" ), "tableS" );
            replaced = std::regex_replace( res_text, std::regex( "table_PUC" ), "tablePUC" );
            db::SdvxSong song;
            google::protobuf::util::JsonStringToMessage( replaced.c_str(), &song );

            std::string lower_code;
            std::transform( song.code().begin(), song.code().end(), back_inserter( lower_code ), ::tolower );

            try {
                auto frame = cv::imread( fmt::format( "songs/{}/chart.png", lower_code ), cv::IMREAD_UNCHANGED );
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, frame ) );
                mq_mutex.unlock();
            } catch ( cv::Exception &e ) {
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, fmt::format( u"채보파일을 찾을 수 없습니다.\nErr : {}", Util::UTF8toUTF16( e.what() ) ) ) );
                mq_mutex.unlock();
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
            auto [ found, query_m ] = find_by_name( query_name, chatroom_name );
            if ( found ) {
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
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"곡정보를 찾지 못했습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
            // TODO : 검색으로 ~를 찾으시나요? 출력
        }
        std::string replaced = std::regex_replace( res_text, std::regex( "chain_vi" ), "chainVi" );
        replaced = std::regex_replace( res_text, std::regex( "chain_v" ), "chainV" );
        replaced = std::regex_replace( res_text, std::regex( "table_S" ), "tableS" );
        replaced = std::regex_replace( res_text, std::regex( "table_PUC" ), "tablePUC" );
        db::SdvxSong song;
        google::protobuf::util::JsonStringToMessage( replaced.c_str(), &song );
        if ( level == u"" ) {
            level = Util::UTF8toUTF16( std::to_string( song.level() ) );
        }

        auto [ found, query_m ] = find_by_name( query_name, chatroom_name );
        if ( !found || !query_m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }

        if ( query_m.info().permission() || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}info?id={}&pw={}&title={}&level={}", __config.api_endpoint(), Util::URLEncode( query_m.info().info_id() ), Util::URLEncode( query_m.info().info_pw() ), Util::URLEncode( song.title() ), Util::URLEncode( level ) ) };
            auto response = request.send( "GET" );
            res_text = std::string( response.body.begin(), response.body.end() );
            std::regex reg( "//" );
            std::sregex_token_iterator it( res_text.begin(), res_text.end(), reg, -1 );
            score = Util::UTF8toUTF16( *( it++ ) );
            clear_lamp = Util::UTF8toUTF16( *it );

            if ( score == u"-1" && clear_lamp == u"NP" ) { // Not Played
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, fmt::format( u"{}님의 점수 : ❌NP❌", query_name ) ) );
                mq_mutex.unlock();
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
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, fmt::format( u"{}님의 점수 : {} {}", query_name, score, clear_lamp ) ) );
                mq_mutex.unlock();
            }
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, fmt::format( u"해당 멤버에 대한 점수조회 권한이 없습니다." ) ) );
            mq_mutex.unlock();
        }
    }

    if ( msg == u"/갱신" || msg.rfind( u"/갱신 ", 0 ) == 0 ) { // 23.01.05 자신/타인 갱신 분기 하나로 합침
        if ( renewal_threads.size() >= 1 ) {                   // 동시에 진행되는 갱신은 1개로 제한, 2개 이상은 아직 테스트 안해봄, AWS인스턴스 비싼거 쓰면 충분히 가능할듯
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"갱신이 진행중입니다. 잠시 후 다시 시도해주세요." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        std::u16string query_name;
        if ( msg.rfind( u"/갱신 ", 0 ) == 0 ) {
            auto u8msg = Util::UTF16toUTF8( msg );
            std::regex reg( Util::UTF16toUTF8( u"(/갱신) ([\\S]+)" ) );
            if ( !std::regex_match( u8msg, reg ) ) {
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /갱신 [이름]" ) );
                mq_mutex.unlock();
                return RETURN_CODE::OK;
            } else {
                std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
                query_name = Util::UTF8toUTF16( *it );
            }
        } else if ( msg == u"/갱신" ) {
            query_name = name;
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /갱신 [이름]" ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        auto [ found, query_m ] = find_by_name( query_name, chatroom_name );
        if ( !found || !query_m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, u"갱신을 시작합니다." ) );
        mq_mutex.unlock();
        renewal_threads.push_back( std::async(
            std::launch::async, []( std::string api_endpoint, std::string info_svid, std::string info_id, std::string info_pw, std::string chatroom_name ) -> std::pair<std::string, std::u16string> {
                http::Request renewal_request{ fmt::format( "{}renewal?svid={}&id={}&pw={}", api_endpoint, Util::URLEncode( info_svid ), Util::URLEncode( info_id ), Util::URLEncode( info_pw ) ) };
                auto renewal_response = renewal_request.send( "GET" );
                auto res_text = std::string( renewal_response.body.begin(), renewal_response.body.end() );

                if ( res_text == "-1" ) {
                    return { chatroom_name, u"갱신 서버의 설정이 만료되었습니다. 관리자에게 문의해주세요." };
                } else {
                    return { chatroom_name, fmt::format( u"갱신이 완료되었습니다.\n소요시간 : {}ms", Util::UTF8toUTF16( res_text ) ) };
                }
            },
            __config.api_endpoint(), query_m.info().info_svid(), query_m.info().info_id(), query_m.info().info_pw(), chatroom_name ) );
    }

    if ( msg == u"/인포" ) { // 자신의 인포 조회
        if ( !m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        http::Request info_request{ fmt::format( "{}info/info?id={}&pw={}", __config.api_endpoint(), Util::URLEncode( m.info().info_id() ), Util::URLEncode( m.info().info_pw() ) ) };
        auto info_response = info_request.send( "GET" );
        auto res_text = std::string( info_response.body.begin(), info_response.body.end() );
        auto info_token = Util::split( Util::UTF8toUTF16( res_text ), "//" );

        if ( info_token.size() == 6 ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, fmt::format( u"<---{}님의 인포--->\n닉네임 : {}\n단 : {}단\n볼포스 : {}\n코인수 : {}\n최근 갱신 일자 : {}", name, info_token[ 1 ], info_token[ 2 ], info_token[ 3 ], info_token[ 4 ] == u"0" ? u"비공개" : info_token[ 4 ], info_token[ 5 ] ) ) );
            mq_mutex.unlock();
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 계정정보를 찾았지만 인포를 불러오지 못했습니다." ) );
            mq_mutex.unlock();
        }
    } else if ( msg.rfind( u"/인포 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(/인포) ([\\S]+)" ) );
        if ( !std::regex_match( u8msg, reg ) ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /인포 [이름]" ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );

        auto [ found, query_m ] = find_by_name( query_name, chatroom_name );

        if ( !found || !query_m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        if ( query_m.info().permission() || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request info_request{ fmt::format( "{}info/info?id={}&pw={}", __config.api_endpoint(), Util::URLEncode( query_m.info().info_id() ), Util::URLEncode( query_m.info().info_pw() ) ) };
            auto info_response = info_request.send( "GET" );
            auto res_text = std::string( info_response.body.begin(), info_response.body.end() );
            auto info_token = Util::split( Util::UTF8toUTF16( res_text ), "//" );

            if ( info_token.size() == 6 ) {
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, fmt::format( u"<---{}님의 인포--->\n닉네임 : {}\n단 : {}단\n볼포스 : {}\n코인수 : {}\n최근 갱신 일자 : {}", query_name, info_token[ 1 ], info_token[ 2 ], info_token[ 3 ], info_token[ 4 ] == u"0" ? u"비공개" : info_token[ 4 ], info_token[ 5 ] ) ) );
                mq_mutex.unlock();
            } else {
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, u"인포 계정정보를 찾았지만 인포를 불러오지 못했습니다." ) );
                mq_mutex.unlock();
            }
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"해당 멤버에 대한 인포 조회 권한이 없습니다." ) );
            mq_mutex.unlock();
        }
    }

    if ( msg == u"/볼포스목록" ) { // 자신의 볼포스목록 조회
        if ( !m.has_info() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        http::Request request{ fmt::format( "{}info/volforce_list?id={}&pw={}", __config.api_endpoint(), Util::URLEncode( m.info().info_id() ), Util::URLEncode( m.info().info_pw() ) ) };
        auto response = request.send( "GET" );
        auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, frame ) );
        mq_mutex.unlock();
    } else if ( msg.rfind( u"/볼포스목록 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(/볼포스목록) ([\\S]+)" ) );
        if ( !std::regex_match( u8msg, reg ) ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /볼포스목록 [이름]" ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );

        auto [ found, query_m ] = find_by_name( query_name, chatroom_name );
        if ( !found || !query_m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        if ( query_m.info().permission() || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}info/volforce_list?id={}&pw={}", __config.api_endpoint(), Util::URLEncode( query_m.info().info_id() ), Util::URLEncode( query_m.info().info_pw() ) ) };
            auto response = request.send( "GET" );
            auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, frame ) );
            mq_mutex.unlock();
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"해당 멤버에 대한 볼포스목록 조회 권한이 없습니다." ) );
            mq_mutex.unlock();
        }
    }

    if ( msg == u"/볼포스목록2" ) { // 자신의 볼포스목록 조회
        if ( !m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        http::Request request{ fmt::format( "{}info/new_volforce_list?id={}&pw={}", __config.api_endpoint(), Util::URLEncode( m.info().info_id() ), Util::URLEncode( m.info().info_pw() ) ) };
        auto response = request.send( "GET" );
        auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, frame ) );
        mq_mutex.unlock();
    } else if ( msg.rfind( u"/볼포스목록2 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(/볼포스목록2) ([\\S]+)" ) );
        if ( !std::regex_match( u8msg, reg ) ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /볼포스목록2 [이름]" ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );

        auto [ found, query_m ] = find_by_name( query_name, chatroom_name );
        if ( !found || !query_m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        if ( query_m.info().permission() || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}info/new_volforce_list?id={}&pw={}", __config.api_endpoint(), Util::URLEncode( query_m.info().info_id() ), Util::URLEncode( query_m.info().info_pw() ) ) };
            auto response = request.send( "GET" );
            auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, frame ) );
            mq_mutex.unlock();
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"해당 멤버에 대한 볼포스목록 조회 권한이 없습니다." ) );
            mq_mutex.unlock();
        }
    }

    if ( msg == u"/서열표 18PUC" ) { // 자신의 18PUC 목록 조회
        if ( !m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        http::Request request{ fmt::format( "{}table?level=18&query=PUC&id={}&pw={}", __config.api_endpoint(), Util::URLEncode( m.info().info_id() ), Util::URLEncode( m.info().info_pw() ) ) };
        auto response = request.send( "GET" );
        auto res_text = std::string( response.body.begin(), response.body.end() );
        std::vector<std::string> codes;
        if ( res_text != "[]" ) {
            std::regex re( "," );
            std::sregex_token_iterator it2( res_text.begin() + 1, res_text.end() - 1, re, -1 ), end;
            for ( ; it2 != end; ++it2 ) {
                codes.push_back( std::string( *it2 ).substr( 1, 6 ) );
            }
        }
        int my_size = codes.size();
        http::Request list_request{ fmt::format( "{}songs/list?level=18", __config.api_endpoint() ) };
        auto list_response = list_request.send( "GET" );
        auto list_res_text = std::string( list_response.body.begin(), list_response.body.end() );
        std::string list_replaced = std::regex_replace( list_res_text, std::regex( "chain_vi" ), "chainVi" );
        list_replaced = std::regex_replace( list_replaced, std::regex( "chain_v" ), "chainV" );
        list_replaced = std::regex_replace( list_replaced, std::regex( "table_S" ), "tableS" );
        list_replaced = std::regex_replace( list_replaced, std::regex( "table_PUC" ), "tablePUC" );
        list_replaced = "{\"sdvxsongs\":" + list_replaced + "}";
        db::SdvxList list;
        google::protobuf::util::JsonStringToMessage( list_replaced, &list );
        std::vector<std::vector<std::string>> table_data( 11, std::vector<std::string>() ); // [0] : 18.0, [1] : 18.1, ..., [9] : 18.9, [10] : undefined의 코드들 모아둠.
        for ( auto &song : list.sdvxsongs() ) {
            if ( song.table_puc().length() == 4 && '0' <= song.table_puc().at( 3 ) && song.table_puc().at( 3 ) <= '9' ) {
                table_data[ song.table_puc().at( 3 ) - '0' ].push_back( song.code() );
            } else {
                table_data[ 10 ].push_back( song.code() );
            }
        }

        // 각 레벨별 코드로 정렬 (최신곡이 우하단으로 가도록)
        for ( auto &line : table_data ) {
            std::sort( line.begin(), line.end(), []( std::string &a, std::string &b ) {
                return a.compare( b ) < 0;
            } );
        }
        // 데이터 로드
        cv::Mat header = cv::imread( "tables/18PUC/header.png" );
        cv::Mat body = cv::imread( "tables/18PUC/body.png" );
        cv::Mat footer = cv::imread( "tables/18PUC/footer.png" );
        cv::Mat table = cv::Mat( header.rows + body.rows * 100, header.cols, CV_8UC3 );
        header.copyTo( table( cv::Rect( 0, 0, header.cols, header.rows ) ) );
        for ( int i = 0; i < 100; ++i ) {
            body.copyTo( table( cv::Rect( 0, header.rows + body.rows * i, body.cols, body.rows ) ) );
        }
        int x_cursor = 105;
        int y_cursor = 705;
        // 각 레벨별로 마커 삽입
        for ( int level = 9; level >= 0; --level ) {
            auto marker = cv::imread( fmt::format( "tables/18PUC/18.{}.png", level ) );
            marker.copyTo( table( cv::Rect( x_cursor, y_cursor, marker.cols, marker.rows ) ) );
            y_cursor += table_data[ level ].size() == 0 ? 161 : 161 * ( table_data[ level ].size() / 20 + ( table_data[ level ].size() % 20 == 0 ? 0 : 1 ) );
        }
        // 미정 마커도 삽입
        {
            auto marker = cv::imread( "tables/18PUC/18.undefined.png" );
            marker.copyTo( table( cv::Rect( x_cursor, y_cursor, marker.cols, marker.rows ) ) );
        }

        // 자켓 그리면서 그 코드가 PUC 목록에 있으면 X 그림
        x_cursor = 362;
        y_cursor = 705;
        for ( int level = 9; level >= 0; --level ) {
            if ( table_data[ level ].size() == 0 ) {
                y_cursor += 161;
                continue;
            }
            for ( int song_idx = 0; song_idx < table_data[ level ].size(); ++song_idx ) {
                auto jacket = cv::imread( fmt::format( "songs/{}/jacket.png", table_data[ level ][ song_idx ] ) );
                cv::resize( jacket, jacket, cv::Size( 136, 136 ) );
                jacket.copyTo( table( cv::Rect( x_cursor, y_cursor, jacket.cols, jacket.rows ) ) );
                // PUC 목록에 있으면 X표시 하고 목록에서 삭제
                if ( auto it = std::find( codes.begin(), codes.end(), table_data[ level ][ song_idx ] ); it != codes.end() ) {
                    cv::line( table, cv::Point( x_cursor, y_cursor ), cv::Point( x_cursor + 135, y_cursor + 135 ), cv::Scalar( 0, 0, 255 ), 10 );
                    cv::line( table, cv::Point( x_cursor + 135, y_cursor ), cv::Point( x_cursor, y_cursor + 135 ), cv::Scalar( 0, 0, 255 ), 10 );
                    codes.erase( it );
                }
                if ( ( song_idx > 0 && song_idx % 20 == 19 ) || song_idx == table_data[ level ].size() - 1 ) {
                    x_cursor = 362;
                    y_cursor += 161;
                } else {
                    x_cursor += 142;
                }
            }
        }
        // 미정인 곡들도 추가
        if ( table_data[ 10 ].size() == 0 ) {
            y_cursor += 161;
        }
        for ( int song_idx = 0; song_idx < table_data[ 10 ].size(); ++song_idx ) {
            auto jacket = cv::imread( fmt::format( "songs/{}/jacket.png", table_data[ 10 ][ song_idx ] ) );
            cv::resize( jacket, jacket, cv::Size( 136, 136 ) );
            jacket.copyTo( table( cv::Rect( x_cursor, y_cursor, jacket.cols, jacket.rows ) ) );
            // PUC 목록에 있으면 X표시 하고 목록에서 삭제
            if ( auto it = std::find( codes.begin(), codes.end(), table_data[ 10 ][ song_idx ] ); it != codes.end() ) {
                cv::line( table, cv::Point( x_cursor, y_cursor ), cv::Point( x_cursor + 135, y_cursor + 135 ), cv::Scalar( 0, 0, 255 ), 10 );
                cv::line( table, cv::Point( x_cursor + 135, y_cursor ), cv::Point( x_cursor, y_cursor + 135 ), cv::Scalar( 0, 0, 255 ), 10 );
                codes.erase( it );
            }
            if ( ( song_idx > 0 && song_idx % 20 == 19 ) || song_idx == table_data[ 10 ].size() - 1 ) {
                x_cursor = 362;
                y_cursor += 161;
            } else {
                x_cursor += 142;
            }
        }
        y_cursor += body.rows - ( ( y_cursor - header.rows ) % body.rows );
        // 마무리로 footer 및 색칠한 곡의 개수 기록
        footer.copyTo( table( cv::Rect( 0, y_cursor, footer.cols, footer.rows ) ) );
        y_cursor += 0; // 80?
        x_cursor = 3137;

        cv::cvtColor( table, table, cv::COLOR_BGR2BGRA );

        // 전체 곡 개수
        int total = list.sdvxsongs_size();
        while ( total ) {
            int digit = total % 10;
            cv::Mat digit_im = cv::imread( fmt::format( "tables/nums/{}.png", digit ), cv::IMREAD_UNCHANGED );
            x_cursor -= ( digit_im.cols + 8 );
            cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
            cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
            total /= 10;
        }
        // '/'
        {
            cv::Mat digit_im = cv::imread( "tables/nums/slash.png", cv::IMREAD_UNCHANGED );
            x_cursor -= ( digit_im.cols + 8 );
            cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
            cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
        }
        // 달성한 곡
        if ( my_size == 0 ) {
            cv::Mat digit_im = cv::imread( "tables/nums/0.png", cv::IMREAD_UNCHANGED );
            x_cursor -= ( digit_im.cols + 8 );
            cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
            cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
        }
        while ( my_size ) {
            int digit = my_size % 10;
            cv::Mat digit_im = cv::imread( fmt::format( "tables/nums/{}.png", digit ), cv::IMREAD_UNCHANGED );
            x_cursor -= ( digit_im.cols + 8 );
            cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
            cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
            my_size /= 10;
        }

        // PUC : 글자 추가
        {
            cv::Mat digit_im = cv::imread( "tables/nums/PUC.png", cv::IMREAD_UNCHANGED );
            x_cursor -= ( digit_im.cols + 30 );
            cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
            cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
        }
        table = table( cv::Rect( 0, 0, table.cols, y_cursor + footer.rows ) );

        cv::Mat resized_table;
        cv::resize( table, resized_table, cv::Size(), 0.5, 0.5 );
        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, resized_table ) );
        mq_mutex.unlock();

    } else if ( auto u8msg = Util::UTF16toUTF8( msg ); std::regex_match( u8msg, std::regex( Util::UTF16toUTF8( u"(/서열표) ([\\S]+) (18PUC)" ) ) ) ) {
        std::regex reg( Util::UTF16toUTF8( u"(/서열표) ([\\S]+) (18PUC)" ) );
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );

        auto [ found, query_m ] = find_by_name( query_name, chatroom_name );
        if ( !found || !query_m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        if ( query_m.info().permission() || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}table?level=18&query=PUC&id={}&pw={}", __config.api_endpoint(), Util::URLEncode( query_m.info().info_id() ), Util::URLEncode( query_m.info().info_pw() ) ) };
            auto response = request.send( "GET" );
            auto res_text = std::string( response.body.begin(), response.body.end() );
            std::vector<std::string> codes;
            if ( res_text != "[]" ) {
                std::regex re( "," );
                std::sregex_token_iterator it2( res_text.begin() + 1, res_text.end() - 1, re, -1 ), end;
                for ( ; it2 != end; ++it2 ) {
                    codes.push_back( std::string( *it2 ).substr( 1, 6 ) );
                }
            }
            int my_size = codes.size();
            http::Request list_request{ fmt::format( "{}songs/list?level=18", __config.api_endpoint() ) };
            auto list_response = list_request.send( "GET" );
            auto list_res_text = std::string( list_response.body.begin(), list_response.body.end() );
            std::string list_replaced = std::regex_replace( list_res_text, std::regex( "chain_vi" ), "chainVi" );
            list_replaced = std::regex_replace( list_replaced, std::regex( "chain_v" ), "chainV" );
            list_replaced = std::regex_replace( list_replaced, std::regex( "table_S" ), "tableS" );
            list_replaced = std::regex_replace( list_replaced, std::regex( "table_PUC" ), "tablePUC" );
            list_replaced = "{\"sdvxsongs\":" + list_replaced + "}";
            db::SdvxList list;
            google::protobuf::util::JsonStringToMessage( list_replaced, &list );
            std::vector<std::vector<std::string>> table_data( 11, std::vector<std::string>() ); // [0] : 18.0, [1] : 18.1, ..., [9] : 18.9, [10] : undefined의 코드들 모아둠.
            for ( auto &song : list.sdvxsongs() ) {
                if ( song.table_puc().length() == 4 && '0' <= song.table_puc().at( 3 ) && song.table_puc().at( 3 ) <= '9' ) {
                    table_data[ song.table_puc().at( 3 ) - '0' ].push_back( song.code() );
                } else {
                    table_data[ 10 ].push_back( song.code() );
                }
            }

            // 각 레벨별 코드로 정렬 (최신곡이 우하단으로 가도록)
            for ( auto &line : table_data ) {
                std::sort( line.begin(), line.end(), []( std::string &a, std::string &b ) {
                    return a.compare( b ) < 0;
                } );
            }
            // 데이터 로드
            cv::Mat header = cv::imread( "tables/18PUC/header.png" );
            cv::Mat body = cv::imread( "tables/18PUC/body.png" );
            cv::Mat footer = cv::imread( "tables/18PUC/footer.png" );
            cv::Mat table = cv::Mat( header.rows + body.rows * 100, header.cols, CV_8UC3 );
            header.copyTo( table( cv::Rect( 0, 0, header.cols, header.rows ) ) );
            for ( int i = 0; i < 100; ++i ) {
                body.copyTo( table( cv::Rect( 0, header.rows + body.rows * i, body.cols, body.rows ) ) );
            }
            int x_cursor = 105;
            int y_cursor = 705;
            // 각 레벨별로 마커 삽입
            for ( int level = 9; level >= 0; --level ) {
                auto marker = cv::imread( fmt::format( "tables/18PUC/18.{}.png", level ) );
                marker.copyTo( table( cv::Rect( x_cursor, y_cursor, marker.cols, marker.rows ) ) );
                y_cursor += table_data[ level ].size() == 0 ? 161 : 161 * ( table_data[ level ].size() / 20 + ( table_data[ level ].size() % 20 == 0 ? 0 : 1 ) );
            }
            // 미정 마커도 삽입
            {
                auto marker = cv::imread( "tables/18PUC/18.undefined.png" );
                marker.copyTo( table( cv::Rect( x_cursor, y_cursor, marker.cols, marker.rows ) ) );
            }

            // 자켓 그리면서 그 코드가 PUC 목록에 있으면 X 그림
            x_cursor = 362;
            y_cursor = 705;
            for ( int level = 9; level >= 0; --level ) {
                if ( table_data[ level ].size() == 0 ) {
                    y_cursor += 161;
                    continue;
                }
                for ( int song_idx = 0; song_idx < table_data[ level ].size(); ++song_idx ) {
                    auto jacket = cv::imread( fmt::format( "songs/{}/jacket.png", table_data[ level ][ song_idx ] ) );
                    cv::resize( jacket, jacket, cv::Size( 136, 136 ) );
                    jacket.copyTo( table( cv::Rect( x_cursor, y_cursor, jacket.cols, jacket.rows ) ) );
                    // PUC 목록에 있으면 X표시 하고 목록에서 삭제
                    if ( auto it = std::find( codes.begin(), codes.end(), table_data[ level ][ song_idx ] ); it != codes.end() ) {
                        cv::line( table, cv::Point( x_cursor, y_cursor ), cv::Point( x_cursor + 135, y_cursor + 135 ), cv::Scalar( 0, 0, 255 ), 10 );
                        cv::line( table, cv::Point( x_cursor + 135, y_cursor ), cv::Point( x_cursor, y_cursor + 135 ), cv::Scalar( 0, 0, 255 ), 10 );
                        codes.erase( it );
                    }
                    if ( ( song_idx > 0 && song_idx % 20 == 19 ) || song_idx == table_data[ level ].size() - 1 ) {
                        x_cursor = 362;
                        y_cursor += 161;
                    } else {
                        x_cursor += 142;
                    }
                }
            }
            // 미정인 곡들도 추가
            if ( table_data[ 10 ].size() == 0 ) {
                y_cursor += 161;
            }
            for ( int song_idx = 0; song_idx < table_data[ 10 ].size(); ++song_idx ) {
                auto jacket = cv::imread( fmt::format( "songs/{}/jacket.png", table_data[ 10 ][ song_idx ] ) );
                cv::resize( jacket, jacket, cv::Size( 136, 136 ) );
                jacket.copyTo( table( cv::Rect( x_cursor, y_cursor, jacket.cols, jacket.rows ) ) );
                // PUC 목록에 있으면 X표시 하고 목록에서 삭제
                if ( auto it = std::find( codes.begin(), codes.end(), table_data[ 10 ][ song_idx ] ); it != codes.end() ) {
                    cv::line( table, cv::Point( x_cursor, y_cursor ), cv::Point( x_cursor + 135, y_cursor + 135 ), cv::Scalar( 0, 0, 255 ), 10 );
                    cv::line( table, cv::Point( x_cursor + 135, y_cursor ), cv::Point( x_cursor, y_cursor + 135 ), cv::Scalar( 0, 0, 255 ), 10 );
                    codes.erase( it );
                }
                if ( ( song_idx > 0 && song_idx % 20 == 19 ) || song_idx == table_data[ 10 ].size() - 1 ) {
                    x_cursor = 362;
                    y_cursor += 161;
                } else {
                    x_cursor += 142;
                }
            }
            y_cursor += body.rows - ( ( y_cursor - header.rows ) % body.rows );
            // 마무리로 footer 및 색칠한 곡의 개수 기록
            footer.copyTo( table( cv::Rect( 0, y_cursor, footer.cols, footer.rows ) ) );
            y_cursor += 0; // 80?
            x_cursor = 3137;

            cv::cvtColor( table, table, cv::COLOR_BGR2BGRA );

            // 전체 곡 개수
            int total = list.sdvxsongs_size();
            while ( total ) {
                int digit = total % 10;
                cv::Mat digit_im = cv::imread( fmt::format( "tables/nums/{}.png", digit ), cv::IMREAD_UNCHANGED );
                x_cursor -= ( digit_im.cols + 8 );
                cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
                cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
                total /= 10;
            }
            // '/'
            {
                cv::Mat digit_im = cv::imread( "tables/nums/slash.png", cv::IMREAD_UNCHANGED );
                x_cursor -= ( digit_im.cols + 8 );
                cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
                cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
            }
            // 달성한 곡
            if ( my_size == 0 ) {
                cv::Mat digit_im = cv::imread( "tables/nums/0.png", cv::IMREAD_UNCHANGED );
                x_cursor -= ( digit_im.cols + 8 );
                cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
                cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
            }
            while ( my_size ) {
                int digit = my_size % 10;
                cv::Mat digit_im = cv::imread( fmt::format( "tables/nums/{}.png", digit ), cv::IMREAD_UNCHANGED );
                x_cursor -= ( digit_im.cols + 8 );
                cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
                cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
                my_size /= 10;
            }

            // PUC : 글자 추가
            {
                cv::Mat digit_im = cv::imread( "tables/nums/PUC.png", cv::IMREAD_UNCHANGED );
                x_cursor -= ( digit_im.cols + 30 );
                cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
                cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
            }
            table = table( cv::Rect( 0, 0, table.cols, y_cursor + footer.rows ) );

            cv::Mat resized_table;
            cv::resize( table, resized_table, cv::Size(), 0.5, 0.5 );
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, resized_table ) );
            mq_mutex.unlock();
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"해당 멤버에 대한 서열표 조회 권한이 없습니다." ) );
            mq_mutex.unlock();
        }
    }

    if ( msg == u"/서열표 19S" ) { // 자신의 19S 목록 조회
        if ( !m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        http::Request request{ fmt::format( "{}table?level=19&query=S&id={}&pw={}", __config.api_endpoint(), Util::URLEncode( m.info().info_id() ), Util::URLEncode( m.info().info_pw() ) ) };
        auto response = request.send( "GET" );
        auto res_text = std::string( response.body.begin(), response.body.end() );
        std::vector<std::string> codes;
        if ( res_text != "[]" ) {
            std::regex re( "," );
            std::sregex_token_iterator it2( res_text.begin() + 1, res_text.end() - 1, re, -1 ), end;
            for ( ; it2 != end; ++it2 ) {
                codes.push_back( std::string( *it2 ).substr( 1, 6 ) );
            }
        }
        int my_size = codes.size();
        http::Request list_request{ fmt::format( "{}songs/list?level=19", __config.api_endpoint() ) };
        auto list_response = list_request.send( "GET" );
        auto list_res_text = std::string( list_response.body.begin(), list_response.body.end() );
        std::string list_replaced = std::regex_replace( list_res_text, std::regex( "chain_vi" ), "chainVi" );
        list_replaced = std::regex_replace( list_replaced, std::regex( "chain_v" ), "chainV" );
        list_replaced = std::regex_replace( list_replaced, std::regex( "table_S" ), "tableS" );
        list_replaced = std::regex_replace( list_replaced, std::regex( "table_PUC" ), "tablePUC" );
        list_replaced = "{\"sdvxsongs\":" + list_replaced + "}";
        db::SdvxList list;
        google::protobuf::util::JsonStringToMessage( list_replaced, &list );
        std::vector<std::vector<std::string>> table_data( 11, std::vector<std::string>() ); // [0] : 19.0, [1] : 19.1, ..., [9] : 19.9, [10] : undefined의 코드들 모아둠.
        for ( auto &song : list.sdvxsongs() ) {
            if ( song.table_s().length() == 4 && '0' <= song.table_s().at( 3 ) && song.table_s().at( 3 ) <= '9' ) {
                table_data[ song.table_s().at( 3 ) - '0' ].push_back( song.code() );
            } else {
                table_data[ 10 ].push_back( song.code() );
            }
        }
        // 각 레벨별 코드로 정렬 (최신곡이 우하단으로 가도록)
        for ( auto &line : table_data ) {
            std::sort( line.begin(), line.end(), []( std::string &a, std::string &b ) {
                return a.compare( b ) < 0;
            } );
        }
        // 데이터 로드
        cv::Mat header = cv::imread( "tables/19S/header.png" );
        cv::Mat body = cv::imread( "tables/19S/body.png" );
        cv::Mat footer = cv::imread( "tables/19S/footer.png" );
        cv::Mat table = cv::Mat( header.rows + body.rows * 100, header.cols, CV_8UC3 );
        header.copyTo( table( cv::Rect( 0, 0, header.cols, header.rows ) ) );
        for ( int i = 0; i < 100; ++i ) {
            body.copyTo( table( cv::Rect( 0, header.rows + body.rows * i, body.cols, body.rows ) ) );
        }
        int x_cursor = 105;
        int y_cursor = 705;
        // 각 레벨별로 마커 삽입
        for ( int level = 9; level >= 0; --level ) {
            auto marker = cv::imread( fmt::format( "tables/19S/19.{}.png", level ) );
            marker.copyTo( table( cv::Rect( x_cursor, y_cursor + 45, marker.cols, marker.rows ) ) );
            y_cursor += table_data[ level ].size() == 0 ? 272 : 272 * ( table_data[ level ].size() / 11 + ( table_data[ level ].size() % 11 == 0 ? 0 : 1 ) );
        }
        // 미정 마커도 삽입
        {
            auto marker = cv::imread( "tables/19S/19.undefined.png" );
            marker.copyTo( table( cv::Rect( x_cursor, y_cursor + 45, marker.cols, marker.rows ) ) );
        }

        // 자켓 그리면서 그 코드가 S 목록에 있으면 X 그림
        x_cursor = 362;
        y_cursor = 705;
        for ( int level = 9; level >= 0; --level ) {
            if ( table_data[ level ].size() == 0 ) {
                y_cursor += 161;
                continue;
            }
            for ( int song_idx = 0; song_idx < table_data[ level ].size(); ++song_idx ) {
                auto jacket = cv::imread( fmt::format( "songs/{}/jacket.png", table_data[ level ][ song_idx ] ) );
                cv::resize( jacket, jacket, cv::Size( 226, 226 ) );
                jacket.copyTo( table( cv::Rect( x_cursor, y_cursor, jacket.cols, jacket.rows ) ) );
                // PUC 목록에 있으면 X표시 하고 목록에서 삭제
                if ( auto it = std::find( codes.begin(), codes.end(), table_data[ level ][ song_idx ] ); it != codes.end() ) {
                    cv::line( table, cv::Point( x_cursor, y_cursor ), cv::Point( x_cursor + 225, y_cursor + 225 ), cv::Scalar( 0, 0, 255 ), 10 );
                    cv::line( table, cv::Point( x_cursor + 225, y_cursor ), cv::Point( x_cursor, y_cursor + 225 ), cv::Scalar( 0, 0, 255 ), 10 );
                    codes.erase( it );
                }
                if ( ( song_idx > 0 && song_idx % 11 == 10 ) || song_idx == table_data[ level ].size() - 1 ) {
                    x_cursor = 362;
                    y_cursor += 272;
                } else {
                    x_cursor += 246;
                }
            }
        }
        // 미정인 곡들도 추가
        if ( table_data[ 10 ].size() == 0 ) {
            y_cursor += 272;
        }
        for ( int song_idx = 0; song_idx < table_data[ 10 ].size(); ++song_idx ) {
            auto jacket = cv::imread( fmt::format( "songs/{}/jacket.png", table_data[ 10 ][ song_idx ] ) );
            cv::resize( jacket, jacket, cv::Size( 226, 226 ) );
            jacket.copyTo( table( cv::Rect( x_cursor, y_cursor, jacket.cols, jacket.rows ) ) );
            // PUC 목록에 있으면 X표시 하고 목록에서 삭제
            if ( auto it = std::find( codes.begin(), codes.end(), table_data[ 10 ][ song_idx ] ); it != codes.end() ) {
                cv::line( table, cv::Point( x_cursor, y_cursor ), cv::Point( x_cursor + 225, y_cursor + 225 ), cv::Scalar( 0, 0, 255 ), 10 );
                cv::line( table, cv::Point( x_cursor + 225, y_cursor ), cv::Point( x_cursor, y_cursor + 225 ), cv::Scalar( 0, 0, 255 ), 10 );
                codes.erase( it );
            }
            if ( ( song_idx > 0 && song_idx % 11 == 10 ) || song_idx == table_data[ 10 ].size() - 1 ) {
                x_cursor = 362;
                y_cursor += 272;
            } else {
                x_cursor += 246;
            }
        }
        y_cursor += body.rows - ( ( y_cursor - header.rows ) % body.rows );
        // 마무리로 footer 및 색칠한 곡의 개수 기록
        footer.copyTo( table( cv::Rect( 0, y_cursor, footer.cols, footer.rows ) ) );
        y_cursor += 0; // 80?
        x_cursor = 3137;

        cv::cvtColor( table, table, cv::COLOR_BGR2BGRA );

        // 전체 곡 개수
        int total = list.sdvxsongs_size();
        while ( total ) {
            int digit = total % 10;
            cv::Mat digit_im = cv::imread( fmt::format( "tables/nums/{}.png", digit ), cv::IMREAD_UNCHANGED );
            x_cursor -= ( digit_im.cols + 8 );
            cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
            cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
            total /= 10;
        }
        // '/'
        {
            cv::Mat digit_im = cv::imread( "tables/nums/slash.png", cv::IMREAD_UNCHANGED );
            x_cursor -= ( digit_im.cols + 8 );
            cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
            cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
        }
        // 달성한 곡
        if ( my_size == 0 ) {
            cv::Mat digit_im = cv::imread( "tables/nums/0.png", cv::IMREAD_UNCHANGED );
            x_cursor -= ( digit_im.cols + 8 );
            cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
            cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
        }
        while ( my_size ) {
            int digit = my_size % 10;
            cv::Mat digit_im = cv::imread( fmt::format( "tables/nums/{}.png", digit ), cv::IMREAD_UNCHANGED );
            x_cursor -= ( digit_im.cols + 8 );
            cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
            cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
            my_size /= 10;
        }

        table = table( cv::Rect( 0, 0, table.cols, y_cursor + footer.rows ) );

        cv::Mat resized_table;
        cv::resize( table, resized_table, cv::Size(), 0.5, 0.5 );
        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, resized_table ) );
        mq_mutex.unlock();

    } else if ( auto u8msg = Util::UTF16toUTF8( msg ); std::regex_match( u8msg, std::regex( Util::UTF16toUTF8( u"(/서열표) ([\\S]+) (19S)" ) ) ) ) {
        std::regex reg( Util::UTF16toUTF8( u"(/서열표) ([\\S]+) (19S)" ) );
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );

        auto [ found, query_m ] = find_by_name( query_name, chatroom_name );
        if ( !found || !query_m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        if ( query_m.info().permission() || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}table?level=19&query=S&id={}&pw={}", __config.api_endpoint(), Util::URLEncode( query_m.info().info_id() ), Util::URLEncode( query_m.info().info_pw() ) ) };
            auto response = request.send( "GET" );
            auto res_text = std::string( response.body.begin(), response.body.end() );
            std::vector<std::string> codes;
            if ( res_text != "[]" ) {
                std::regex re( "," );
                std::sregex_token_iterator it2( res_text.begin() + 1, res_text.end() - 1, re, -1 ), end;
                for ( ; it2 != end; ++it2 ) {
                    codes.push_back( std::string( *it2 ).substr( 1, 6 ) );
                }
            }
            int my_size = codes.size();
            http::Request list_request{ fmt::format( "{}songs/list?level=19", __config.api_endpoint() ) };
            auto list_response = list_request.send( "GET" );
            auto list_res_text = std::string( list_response.body.begin(), list_response.body.end() );
            std::string list_replaced = std::regex_replace( list_res_text, std::regex( "chain_vi" ), "chainVi" );
            list_replaced = std::regex_replace( list_replaced, std::regex( "chain_v" ), "chainV" );
            list_replaced = std::regex_replace( list_replaced, std::regex( "table_S" ), "tableS" );
            list_replaced = std::regex_replace( list_replaced, std::regex( "table_PUC" ), "tablePUC" );
            list_replaced = "{\"sdvxsongs\":" + list_replaced + "}";
            db::SdvxList list;
            google::protobuf::util::JsonStringToMessage( list_replaced, &list );
            std::vector<std::vector<std::string>> table_data( 11, std::vector<std::string>() ); // [0] : 19.0, [1] : 19.1, ..., [9] : 19.9, [10] : undefined의 코드들 모아둠.
            for ( auto &song : list.sdvxsongs() ) {
                if ( song.table_s().length() == 4 && '0' <= song.table_s().at( 3 ) && song.table_s().at( 3 ) <= '9' ) {
                    table_data[ song.table_s().at( 3 ) - '0' ].push_back( song.code() );
                } else {
                    table_data[ 10 ].push_back( song.code() );
                }
            }
            // 각 레벨별 코드로 정렬 (최신곡이 우하단으로 가도록)
            for ( auto &line : table_data ) {
                std::sort( line.begin(), line.end(), []( std::string &a, std::string &b ) {
                    return a.compare( b ) < 0;
                } );
            }
            // 데이터 로드
            cv::Mat header = cv::imread( "tables/19S/header.png" );
            cv::Mat body = cv::imread( "tables/19S/body.png" );
            cv::Mat footer = cv::imread( "tables/19S/footer.png" );
            cv::Mat table = cv::Mat( header.rows + body.rows * 100, header.cols, CV_8UC3 );
            header.copyTo( table( cv::Rect( 0, 0, header.cols, header.rows ) ) );
            for ( int i = 0; i < 100; ++i ) {
                body.copyTo( table( cv::Rect( 0, header.rows + body.rows * i, body.cols, body.rows ) ) );
            }
            int x_cursor = 105;
            int y_cursor = 705;
            // 각 레벨별로 마커 삽입
            for ( int level = 9; level >= 0; --level ) {
                auto marker = cv::imread( fmt::format( "tables/19S/19.{}.png", level ) );
                marker.copyTo( table( cv::Rect( x_cursor, y_cursor + 45, marker.cols, marker.rows ) ) );
                y_cursor += table_data[ level ].size() == 0 ? 272 : 272 * ( table_data[ level ].size() / 11 + ( table_data[ level ].size() % 11 == 0 ? 0 : 1 ) );
            }
            // 미정 마커도 삽입
            {
                auto marker = cv::imread( "tables/19S/19.undefined.png" );
                marker.copyTo( table( cv::Rect( x_cursor, y_cursor + 45, marker.cols, marker.rows ) ) );
            }

            // 자켓 그리면서 그 코드가 S 목록에 있으면 X 그림
            x_cursor = 362;
            y_cursor = 705;
            for ( int level = 9; level >= 0; --level ) {
                if ( table_data[ level ].size() == 0 ) {
                    y_cursor += 161;
                    continue;
                }
                for ( int song_idx = 0; song_idx < table_data[ level ].size(); ++song_idx ) {
                    auto jacket = cv::imread( fmt::format( "songs/{}/jacket.png", table_data[ level ][ song_idx ] ) );
                    cv::resize( jacket, jacket, cv::Size( 226, 226 ) );
                    jacket.copyTo( table( cv::Rect( x_cursor, y_cursor, jacket.cols, jacket.rows ) ) );
                    // PUC 목록에 있으면 X표시 하고 목록에서 삭제
                    if ( auto it = std::find( codes.begin(), codes.end(), table_data[ level ][ song_idx ] ); it != codes.end() ) {
                        cv::line( table, cv::Point( x_cursor, y_cursor ), cv::Point( x_cursor + 225, y_cursor + 225 ), cv::Scalar( 0, 0, 255 ), 10 );
                        cv::line( table, cv::Point( x_cursor + 225, y_cursor ), cv::Point( x_cursor, y_cursor + 225 ), cv::Scalar( 0, 0, 255 ), 10 );
                        codes.erase( it );
                    }
                    if ( ( song_idx > 0 && song_idx % 11 == 10 ) || song_idx == table_data[ level ].size() - 1 ) {
                        x_cursor = 362;
                        y_cursor += 272;
                    } else {
                        x_cursor += 246;
                    }
                }
            }
            // 미정인 곡들도 추가
            if ( table_data[ 10 ].size() == 0 ) {
                y_cursor += 272;
            }
            for ( int song_idx = 0; song_idx < table_data[ 10 ].size(); ++song_idx ) {
                auto jacket = cv::imread( fmt::format( "songs/{}/jacket.png", table_data[ 10 ][ song_idx ] ) );
                cv::resize( jacket, jacket, cv::Size( 226, 226 ) );
                jacket.copyTo( table( cv::Rect( x_cursor, y_cursor, jacket.cols, jacket.rows ) ) );
                // PUC 목록에 있으면 X표시 하고 목록에서 삭제
                if ( auto it = std::find( codes.begin(), codes.end(), table_data[ 10 ][ song_idx ] ); it != codes.end() ) {
                    cv::line( table, cv::Point( x_cursor, y_cursor ), cv::Point( x_cursor + 225, y_cursor + 225 ), cv::Scalar( 0, 0, 255 ), 10 );
                    cv::line( table, cv::Point( x_cursor + 225, y_cursor ), cv::Point( x_cursor, y_cursor + 225 ), cv::Scalar( 0, 0, 255 ), 10 );
                    codes.erase( it );
                }
                if ( ( song_idx > 0 && song_idx % 11 == 10 ) || song_idx == table_data[ 10 ].size() - 1 ) {
                    x_cursor = 362;
                    y_cursor += 272;
                } else {
                    x_cursor += 246;
                }
            }
            y_cursor += body.rows - ( ( y_cursor - header.rows ) % body.rows );
            // 마무리로 footer 및 색칠한 곡의 개수 기록
            footer.copyTo( table( cv::Rect( 0, y_cursor, footer.cols, footer.rows ) ) );
            y_cursor += 0; // 80?
            x_cursor = 3137;

            cv::cvtColor( table, table, cv::COLOR_BGR2BGRA );

            // 전체 곡 개수
            int total = list.sdvxsongs_size();
            while ( total ) {
                int digit = total % 10;
                cv::Mat digit_im = cv::imread( fmt::format( "tables/nums/{}.png", digit ), cv::IMREAD_UNCHANGED );
                x_cursor -= ( digit_im.cols + 8 );
                cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
                cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
                total /= 10;
            }
            // '/'
            {
                cv::Mat digit_im = cv::imread( "tables/nums/slash.png", cv::IMREAD_UNCHANGED );
                x_cursor -= ( digit_im.cols + 8 );
                cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
                cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
            }
            // 달성한 곡
            if ( my_size == 0 ) {
                cv::Mat digit_im = cv::imread( "tables/nums/0.png", cv::IMREAD_UNCHANGED );
                x_cursor -= ( digit_im.cols + 8 );
                cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
                cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
            }
            while ( my_size ) {
                int digit = my_size % 10;
                cv::Mat digit_im = cv::imread( fmt::format( "tables/nums/{}.png", digit ), cv::IMREAD_UNCHANGED );
                x_cursor -= ( digit_im.cols + 8 );
                cv::Mat roi = table( cv::Rect( x_cursor, y_cursor, digit_im.cols, digit_im.rows ) );
                cv::addWeighted( roi, 1.0, digit_im, 1.0, 0.0, roi );
                my_size /= 10;
            }

            table = table( cv::Rect( 0, 0, table.cols, y_cursor + footer.rows ) );

            cv::Mat resized_table;
            cv::resize( table, resized_table, cv::Size(), 0.5, 0.5 );
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, resized_table ) );
            mq_mutex.unlock();
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"해당 멤버에 대한 서열표 조회 권한이 없습니다." ) );
            mq_mutex.unlock();
        }
    }

    if ( msg == u"/평균" ) { // 자신의 평균목록 조회
        if ( m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        http::Request request{ fmt::format( "{}info/average?id={}&pw={}", __config.api_endpoint(), Util::URLEncode( m.info().info_id() ), Util::URLEncode( m.info().info_pw() ) ) };
        auto response = request.send( "GET" );
        auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, frame ) );
        mq_mutex.unlock();
    } else if ( msg.rfind( u"/평균 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(/평균) ([\\S]+)" ) );
        if ( !std::regex_match( u8msg, reg ) ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /평균 [이름]" ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );

        auto [ found, query_m ] = find_by_name( query_name, chatroom_name );
        if ( !found || !query_m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        if ( query_m.info().permission() || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}info/average?id={}&pw={}", __config.api_endpoint(), Util::URLEncode( query_m.info().info_id() ), Util::URLEncode( query_m.info().info_pw() ) ) };
            auto response = request.send( "GET" );
            auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, frame ) );
            mq_mutex.unlock();
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"해당 멤버에 대한 평균 조회 권한이 없습니다." ) );
            mq_mutex.unlock();
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
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /통계 {이름} [레벨]" ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }

        auto [ found, query_m ] = find_by_name( query_name, chatroom_name );
        if ( !found || !query_m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        if ( query_m.info().permission() || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}info/statistics?id={}&pw={}&level={}", __config.api_endpoint(), Util::URLEncode( query_m.info().info_id() ), Util::URLEncode( query_m.info().info_pw() ), Util::URLEncode( level ) ) };
            auto response = request.send( "GET" );
            auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, frame ) );
            mq_mutex.unlock();
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"해당 멤버에 대한 통계 조회 권한이 없습니다." ) );
            mq_mutex.unlock();
        }
    }

    if ( msg == u"/업데이트" && name == u"손창대" ) {
        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, u"업데이트를 진행합니다." ) );
        mq_mutex.unlock();
        return RETURN_CODE::UPDATE;
    } else if ( msg == u"/악곡업데이트" && name == u"손창대" ) {
        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, u"악곡업데이트를 진행합니다." ) );
        mq_mutex.unlock();
        return RETURN_CODE::SONGUPDATE;
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
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"라이브 스트리밍중이 아니거나 지원하는 스트리밍이 아닙니다.\n\n<<사용가능 목록>>\n\n관성(개인방송)\n릿샤(개인방송)\n싸이발키리\n싸이구기체\n싸이라이트닝\n싸이투덱\n량진발키리\n량진구기체" ) );
            mq_mutex.unlock();
        } else { // 해당 링크를 찾은 경우
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, Util::UTF8toUTF16( res_text ) ) );
            mq_mutex.unlock();
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
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"라이브 스트리밍중이 아니거나 지원하는 스트리밍이 아닙니다.\n\n<<사용가능 목록>>\n\n관성(개인방송)\n릿샤(개인방송)\n싸이발키리\n싸이구기체\n싸이라이트닝\n싸이투덱\n량진발키리\n량진구기체" ) );
            mq_mutex.unlock();
        } else { // 해당 링크를 찾은 경우
            request = http::Request( __config.api_endpoint() + "streaming/playback?url=" + Util::URLEncode( res_text ) );
            response = request.send( "GET" );
            res_text = std::string( response.body.begin(), response.body.end() );

            if ( res_text == "Error" ) {
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, u"라이브 스트리밍을 찾았지만, 플레이백 URL을 구하는 과정에서 에러가 발생했습니다." ) );
                mq_mutex.unlock();
            } else {
                auto capture = cv::VideoCapture( res_text );
                cv::Mat frame;
                auto grabbed = capture.read( frame );
                if ( grabbed ) { // 캡쳐에 성공한 경우
                    mq_mutex.lock();
                    message_queue.push_back( Message( chatroom_name, frame ) );
                    mq_mutex.unlock();
                } else { // playback은 있었지만 캡쳐에 실패한 경우
                    mq_mutex.lock();
                    message_queue.push_back( Message( chatroom_name, u"라이브 스트리밍을 찾았지만, 오류가 발생하여 썸네일을 생성하지 못했습니다." ) );
                    mq_mutex.unlock();
                }
            }
        }
    }

    if ( msg == u"/국내야구" ) {
        http::Request request{ __config.api_endpoint() + "etc/baseball" };
        auto response = request.send( "GET" );
        const std::string res_text = std::string( response.body.begin(), response.body.end() );
        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, Util::UTF8toUTF16( res_text ) ) );
        mq_mutex.unlock();
    }

    if ( msg == u"/국내야구랭킹" || msg == u"/국야랭" ) {
        http::Request request{ fmt::format( "{}etc/baseball_ranking", __config.api_endpoint() ) };
        auto response = request.send( "GET" );
        auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, frame ) );
        mq_mutex.unlock();
    }

    if ( msg.rfind( u"/", 0 ) == 0 && msg.find( u"vs" ) != std::u16string::npos ) {
        auto tokens = Util::split( msg.substr( 1 ), "vs" );
        auto selected = tokens.at( Util::rand( 0, tokens.size() - 1 ) );
        std::regex reg( "\\s" );
        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, Util::UTF8toUTF16( std::regex_replace( Util::UTF16toUTF8( selected ), reg, "" ) ) ) );
        mq_mutex.unlock();
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
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, u"장비를 조회하는 도중에 에러가 발생했습니다. 장비정보가 공개되어있는지 메이플스토리 공식홈페이지에서 한번 더 확인해주세요." ) );
                mq_mutex.unlock();
            } else {
                auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( image_response.body.data() ), static_cast<std::streamsize>( image_response.body.size() ) ), cv::IMREAD_UNCHANGED );
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, frame ) );
                mq_mutex.unlock();
            }
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /장비 [닉네임] [부위]\n조회 가능한 장비분류 : 반지1, 모자, 뚝, 뚝배기, 엠블렘, 엠블럼, 엠블, 반지2, 펜던트2, 펜던2, 얼굴장식, 얼장, 뱃지, 반지3, 펜던트1, 펜던트, 펜던, 눈장식, 눈장, 귀고리, 귀걸이, 이어링, 훈장, 메달, 반지4, 무기, 상의, 견장, 어깨장식, 보조, 보조무기, 포켓, 포켓아이템, 벨트, 하의, 장갑, 망토, 신발, 하트, 기계심장" ) );
            mq_mutex.unlock();
        }
    }

    if ( msg.rfind( u"/검색 ", 0 ) == 0 ) {
        auto search_text = msg.substr( 4 );

        if ( search_text.length() == 0 ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"검색어를 입력해주세요" ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }

        auto regex = std::regex( "^\\S$|^\\S.*\\S$" );
        auto u8str = Util::UTF16toUTF8( search_text );
        if ( !std::regex_match( u8str, regex ) ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /검색 [검색어]" ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        http::Request request{ __config.api_endpoint() + "songs/search?search_text=" + Util::URLEncode( search_text ) };
        auto response = request.send( "GET" );
        const std::string res_text = std::string( response.body.begin(), response.body.end() );
        std::string replaced = std::regex_replace( res_text, std::regex( "chain_vi" ), "chainVi" );
        replaced = std::regex_replace( res_text, std::regex( "chain_v" ), "chainV" );
        replaced = std::regex_replace( res_text, std::regex( "table_S" ), "tableS" );
        replaced = std::regex_replace( res_text, std::regex( "table_PUC" ), "tablePUC" );

        // protobuf로 만들기 위해 message formatting
        replaced = fmt::format( "{{\"result\":{}}}", replaced );

        db::SearchResult result;
        google::protobuf::util::JsonStringToMessage( replaced, &result );

        if ( result.result_size() == 0 ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, fmt::format( u"검색어 \"{}\"에 대한 결과를 찾을 수 없습니다.", search_text ) ) );
            mq_mutex.unlock();
        } else {
            std::u16string ret = fmt::format( u"\"{}\"에 대한 검색 결과입니다.", search_text );

            for ( size_t i = 0; i < result.result_size(); ++i ) {
                ret += fmt::format( u"\n{}. {} [Lv{}, 별명 : {}]", i + 1, Util::UTF8toUTF16( result.result( i ).song().title() ), result.result( i ).song().level(), result.result( i ).song().nick1() == "" ? u"없음" : Util::UTF8toUTF16( result.result( i ).song().nick1() ) );
            }

            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, ret ) );
            mq_mutex.unlock();
        }
    }

    if ( msg.rfind( u"/영상 ", 0 ) == 0 || msg.rfind( u"/퍼얼영상 ", 0 ) == 0 ) {
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
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"곡정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            // TODO : 검색통해서 ~~~를 찾으시나요? 출력
        } else {
            db::SdvxSong song;
            google::protobuf::util::JsonStringToMessage( res_text.c_str(), &song );
            if ( song.puc_video_url() == "" ) {
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, u"등록된 영상이 없습니다." ) );
                mq_mutex.unlock();
            } else {
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, Util::UTF8toUTF16( song.puc_video_url() ) ) );
                mq_mutex.unlock();
            }
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
            auto [ found, query_m ] = find_by_name( query_name, chatroom_name );
            if ( found ) {
                http::Request title_request{ __config.api_endpoint() + "popn_songs?title=" + Util::URLEncode( nick ) };
                title_response = title_request.send( "GET" );
            } else { // 멤버 없는 경우 /점수조회 곡명 명령어를 띄어쓰기 포함하여 사용한 경우.
                query_name = name;
                reg = std::regex( u8"(>점수조회) ([\\s\\S]+)" );
                std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
                auto nick = Util::UTF8toUTF16( *( it ) );
                http::Request title_request{ __config.api_endpoint() + "popn_songs?title=" + Util::URLEncode( nick ) };
                title_response = title_request.send( "GET" );
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
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"곡정보를 찾지 못했습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
            // TODO : 검색으로 ~를 찾으시나요? 출력
        }
        popndb::PopnSong song;
        google::protobuf::util::JsonStringToMessage( res_text.c_str(), &song );
        if ( level == u"" ) {
            level = Util::UTF8toUTF16( std::to_string( song.level() ) );
        }

        auto [ found, query_m ] = find_by_name( query_name, chatroom_name );
        if ( !found || !query_m.has_info() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }

        if ( query_m.info().permission() || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}popn_songs/score?info_id={}&song_id={}", __config.api_endpoint(), query_m.info_id(), song.id() ) };
            auto response = request.send( "GET" );
            res_text = std::string( response.body.begin(), response.body.end() );
            std::regex reg( "//" );
            std::sregex_token_iterator it( res_text.begin(), res_text.end(), reg, -1 );
            score = Util::UTF8toUTF16( *( it++ ) );
            grade = Util::UTF8toUTF16( *( it++ ) );
            medal = Util::UTF8toUTF16( *it );

            if ( score == u"-1" && grade == u"NP" && medal == u"NP" ) { // Not Played
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, fmt::format( u"{}님의 점수 : ❌NP❌", query_name ) ) );
                mq_mutex.unlock();
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
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, fmt::format( u"{}님의 점수 : {}{}", query_name, score, medal ) ) );
                mq_mutex.unlock();
            }
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, fmt::format( u"해당 멤버에 대한 점수조회 권한이 없습니다." ) ) );
            mq_mutex.unlock();
        }
    }

    // 팝픈뮤직 곡정보
    if ( msg.rfind( u">곡정보 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::u16string title, level, nick;                                                                          // 쿼리용 변수
        http::Response title_response;                                                                              // 결과
        if ( std::regex_match( u8msg, std::regex( u8"(>곡정보) ([\\s\\S]+) (41|42|43|44|45|46|47|48|49|50)" ) ) ) { // >곡정보 곡명 레벨
            std::regex reg( u8"(>점수조회) ([\\s\\S]+) (41|42|43|44|45|46|47|48|49|50)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2, 3 } ), end;
            auto nick = Util::UTF8toUTF16( *( it++ ) );
            level = Util::UTF8toUTF16( *it );
            http::Request title_request{ __config.api_endpoint() + "popn_songs?title=" + Util::URLEncode( nick ) + "&kind=" + Util::URLEncode( level ) };
            title_response = title_request.send( "GET" );
        } else if ( std::regex_match( u8msg, std::regex( u8"(>곡정보) ([\\s\\S]+)" ) ) ) { // >곡정보 곡명
            std::regex reg( u8"(>곡정보) ([\\s\\S]+)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } ), end;
            auto nick = Util::UTF8toUTF16( *( it ) );
            level = u"";
            http::Request title_request{ __config.api_endpoint() + "popn_songs?title=" + Util::URLEncode( nick ) };
            title_response = title_request.send( "GET" );
        }

        std::string res_text = std::string( title_response.body.begin(), title_response.body.end() );

        if ( res_text == "{}" ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"곡정보를 찾지 못했습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
            // TODO : 검색으로 ~를 찾으시나요? 출력
        }
        popndb::PopnSong song;
        google::protobuf::util::JsonStringToMessage( res_text.c_str(), &song );
        if ( level == u"" ) {
            level = Util::UTF8toUTF16( std::to_string( song.level() ) );
        }

        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, u"제목 : " + Util::UTF8toUTF16( song.title() ) +
                                                             u"\n장르 : " + Util::UTF8toUTF16( song.genre() ) +
                                                             u"\n레벨 : " + Util::UTF8toUTF16( std::to_string( song.level() ) ) +
                                                             u"\nBPM : " + Util::UTF8toUTF16( song.bpm() ) +
                                                             u"\n곡 길이 : " + ( ( song.duration() == "??:??" ) ? u"정보 없음" : Util::UTF8toUTF16( song.duration() ) ) +
                                                             u"\n노트수 : " + Util::UTF8toUTF16( std::to_string( song.notes() ) ) +
                                                             ( ( song.notes() >= 1537 ) ? u"(짠게)" : ( ( song.notes() <= 1024 ) ? u"(단게)" : u"" ) ) ) );
        mq_mutex.unlock();
    }

    // 팝픈뮤직 채보
    if ( msg.rfind( u">채보 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::u16string title, level, nick;                                                                        // 쿼리용 변수
        http::Response title_response;                                                                            // 결과
        if ( std::regex_match( u8msg, std::regex( u8"(>채보) ([\\s\\S]+) (41|42|43|44|45|46|47|48|49|50)" ) ) ) { // >채보 곡명 레벨
            std::regex reg( u8"(>점수조회) ([\\s\\S]+) (41|42|43|44|45|46|47|48|49|50)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2, 3 } ), end;
            auto nick = Util::UTF8toUTF16( *( it++ ) );
            level = Util::UTF8toUTF16( *it );
            http::Request title_request{ __config.api_endpoint() + "popn_songs?title=" + Util::URLEncode( nick ) + "&kind=" + Util::URLEncode( level ) };
            title_response = title_request.send( "GET" );
        } else if ( std::regex_match( u8msg, std::regex( u8"(>채보) ([\\s\\S]+)" ) ) ) { // >채보 곡명
            std::regex reg( u8"(>채보) ([\\s\\S]+)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } ), end;
            auto nick = Util::UTF8toUTF16( *( it ) );
            level = u"";
            http::Request title_request{ __config.api_endpoint() + "popn_songs?title=" + Util::URLEncode( nick ) };
            title_response = title_request.send( "GET" );
        }

        std::string res_text = std::string( title_response.body.begin(), title_response.body.end() );

        if ( res_text == "{}" ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"곡정보를 찾지 못했습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
            // TODO : 검색으로 ~를 찾으시나요? 출력
        }
        popndb::PopnSong song;
        google::protobuf::util::JsonStringToMessage( res_text.c_str(), &song );

        try {
            auto frame = cv::imread( fmt::format( "songs/popn_songs/{}/chart.png", song.id() ), cv::IMREAD_UNCHANGED );
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, frame ) );
            mq_mutex.unlock();
        } catch ( cv::Exception &e ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, fmt::format( u"이미지를 찾을 수 없습니다.\nErr : {}", Util::UTF8toUTF16( e.what() ) ) ) );
            mq_mutex.unlock();
        }
    }

    // 팝픈뮤직 갱신
    // 23.01.05 자신/타인 갱신 분기 하나로 합침
    if ( msg == u">갱신" || msg.rfind( u">갱신 ", 0 ) == 0 ) {
        std::u16string query_name;
        if ( msg.rfind( u">갱신 ", 0 ) == 0 ) {
            auto u8msg = Util::UTF16toUTF8( msg );
            std::regex reg( Util::UTF16toUTF8( u"(>갱신) ([\\S]+)" ) );
            if ( !std::regex_match( u8msg, reg ) ) {
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : >갱신 [이름]" ) );
                mq_mutex.unlock();
                return RETURN_CODE::OK;
            } else {
                std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
                query_name = Util::UTF8toUTF16( *it );
            }
        } else if ( msg == u">갱신" ) {
            query_name = name;
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : >갱신 [이름]" ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, u"갱신을 시작합니다." ) );
        mq_mutex.unlock();

        renewal_threads.push_back( std::async(
            std::launch::async, []( std::string api_endpoint, std::u16string query_name, std::string chatroom_name ) -> std::pair<std::string, std::u16string> {
                http::Request renewal_request{ fmt::format( "{}popn_songs/renewal?name={}", api_endpoint, Util::URLEncode( query_name ) ) };
                auto renewal_response = renewal_request.send( "GET" );
                auto res_text = std::string( renewal_response.body.begin(), renewal_response.body.end() );

                if ( res_text == "-2" ) {
                    return { chatroom_name, u"인포 정보를 찾을 수 없습니다." };
                } else if ( res_text == "-1" ) {
                    return { chatroom_name, u"갱신 서버의 설정이 만료되었습니다. 관리자에게 문의해주세요." };
                } else {
                    return { chatroom_name, fmt::format( u"갱신이 완료되었습니다.\n소요시간 : {}ms", Util::UTF8toUTF16( res_text ) ) };
                }
            },
            __config.api_endpoint(), query_name, chatroom_name ) );
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
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : >서든 [저속>0] [고속<1000]" ) );
                mq_mutex.unlock();
            } else {
                int sudden = 95 - ( 315.0f * std::min<>( bpm1, bpm2 ) / std::max<>( bpm1, bpm2 ) );
                mq_mutex.lock();
                message_queue.push_back( Message( chatroom_name, fmt::format( u"서든 : {}", sudden ) ) );
                mq_mutex.unlock();
            }
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : >서든 [저속] [고속]" ) );
            mq_mutex.unlock();
        }
    }

    // 팝픈뮤직 팝클래스 목록
    if ( msg == u">팝클목록" ) { // 자신의 팝클목록
        if ( !m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        http::Request request{ fmt::format( "{}popn_songs/popclass_list?name={}", __config.api_endpoint(), Util::URLEncode( name ) ) };
        auto response = request.send( "GET" );
        auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, frame ) );
        mq_mutex.unlock();
    } else if ( msg.rfind( u">팝클목록 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        std::regex reg( Util::UTF16toUTF8( u"(>팝클목록) ([\\S]+)" ) );
        if ( !std::regex_match( u8msg, reg ) ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : >팝클목록 [이름]" ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
        auto query_name = Util::UTF8toUTF16( *it );

        auto [ found, query_m ] = find_by_name( query_name, chatroom_name );
        if ( !found || !query_m.has_info_id() ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"인포 정보를 찾을 수 없습니다." ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }
        if ( query_m.info().permission() || query_name == name ) { // permission이 켜져있거나 본인이어야함
            http::Request request{ fmt::format( "{}popn_songs/popclass_list?name={}", __config.api_endpoint(), Util::URLEncode( query_name ) ) };
            auto response = request.send( "GET" );
            auto frame = cv::imdecode( cv::_InputArray( reinterpret_cast<const char *>( response.body.data() ), static_cast<std::streamsize>( response.body.size() ) ), cv::IMREAD_UNCHANGED );
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, frame ) );
            mq_mutex.unlock();
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"해당 멤버에 대한 팝클목록 조회 권한이 없습니다." ) );
            mq_mutex.unlock();
        }
    }

    if ( msg == u"/오늘의백준" || msg == u"/데일리백준" ) {
        http::Request request{ fmt::format( "{}boj/daily", __config.api_endpoint() ) };
        auto response = request.send( "GET" );
        auto res_text = std::string( response.body.begin(), response.body.end() );
        auto splitted = Util::split( Util::UTF8toUTF16( res_text ), "!@#" );
        if ( splitted.size() != 3 ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"오류가 발생했습니다." ) );
            mq_mutex.unlock();
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, fmt::format( u"📖오늘의 문제📖\n제목 : {}\n레벨 : {}\n\nhttps://www.acmicpc.net/problem/{}", splitted[ 1 ], splitted[ 2 ], splitted[ 0 ] ) ) );
            mq_mutex.unlock();
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
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : /곡추천 [레벨]" ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }

        http::Request request{ fmt::format( "{}songs/list?level={}", __config.api_endpoint(), Util::URLEncode( level ) ) };
        auto response = request.send( "GET" );
        auto res_text = std::string( response.body.begin(), response.body.end() );
        std::string replaced = std::regex_replace( res_text, std::regex( "chain_vi" ), "chainVi" );
        replaced = std::regex_replace( res_text, std::regex( "chain_v" ), "chainV" );
        replaced = std::regex_replace( res_text, std::regex( "table_S" ), "tableS" );
        replaced = std::regex_replace( res_text, std::regex( "table_PUC" ), "tablePUC" );
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

        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, fmt::format( u"🎵추천곡🎵\n{} {}{}", Util::UTF8toUTF16( song.title() ), diff, ( ( song.level() == 18 ) ? ( u"\nPUC 난이도 : " + Util::UTF8toUTF16( ( song.table_puc() == "undefined" ) ? Util::UTF16toUTF8( u"미정" ) : song.table_puc() ) ) : u"" ) ) ) );
        mq_mutex.unlock();
        try {
            std::string lower_code;
            std::transform( song.code().begin(), song.code().end(), back_inserter( lower_code ), ::tolower );
            auto frame = cv::imread( fmt::format( "songs/{}/jacket.png", lower_code ), cv::IMREAD_UNCHANGED );
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, frame ) );
            mq_mutex.unlock();
        } catch ( cv::Exception &e ) {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, fmt::format( u"자켓을 찾을 수 없습니다.\nErr : {}", Util::UTF8toUTF16( e.what() ) ) ) );
            mq_mutex.unlock();
        }
    }

    if ( msg.rfind( u">곡추천 ", 0 ) == 0 || msg.rfind( u">추천곡 ", 0 ) == 0 ) { // 랜덤 곡 추천 기능
        auto u8msg = Util::UTF16toUTF8( msg );
        std::u16string level = u""; // 쿼리용 변수
        if ( std::regex_match( u8msg, std::regex( u8"(>곡추천) (47|48|49|50)" ) ) ) {
            std::regex reg( u8"(>곡추천) (47|48|49|50)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
            level = Util::UTF8toUTF16( *it );
        } else if ( std::regex_match( u8msg, std::regex( u8"(>추천곡) (47|48|49|50)" ) ) ) {
            std::regex reg( u8"(>추천곡) (47|48|49|50)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 2 } );
            level = Util::UTF8toUTF16( *it );
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"잘못된 명령어입니다.\n사용법 : >곡추천 [레벨]" ) );
            mq_mutex.unlock();
            return RETURN_CODE::OK;
        }

        http::Request request{ fmt::format( "{}popn_songs/list?level={}", __config.api_endpoint(), Util::URLEncode( level ) ) };
        auto response = request.send( "GET" );
        auto res_text = std::string( response.body.begin(), response.body.end() );
        res_text = "{\"popnsongs\":" + res_text + "}";
        popndb::PopnList list;
        google::protobuf::util::JsonStringToMessage( res_text, &list );

        std::u16string diff = u"";
        popndb::PopnSong song = list.popnsongs( Util::rand( 0, list.popnsongs_size() - 1 ) );

        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, fmt::format( u"🎵추천곡🎵\n{}\n({})", Util::UTF8toUTF16( song.title() ), Util::UTF8toUTF16( song.nick1() ) ) ) );
        mq_mutex.unlock();
    }

    if ( msg == u"/오늘의운세" || msg == u"/오늘의 운세" ) {
        const std::array<std::u16string, 8> fortune = {
            u"와! 오늘은 교수님과 박사님이 안계셔요! 칼퇴할 수 있어요.",
            u"오늘의 대박! 오늘은 연차를 내서 집에서 쉴 수 있어요.",
            u"오늘은 느낌이 좋아요! 뭐든지 할 수 있을 것 같은날이에요!",
            u"저녁을 먹기 전에 퇴근을 할 수 있을 것 같아요! 리겜을 하러 가도 괜찮은 컨디션이에요!",
            u"오늘은 아무도 나를 건드릴 수 없어요. 하고 싶은걸 하는 날이에요!",
            u"날씨도 좋아서 느낌도 좋아요! 오늘은 산책을 가고 싶은 날이에요!",
            u"오늘은 뭔가 낌새가 이상해요. 뭘 하든 조심히 하는게 좋을 것 같아요.",
            u"슬프네요.. 오늘은 실험결과가 좋지않아 야근을 해야할 것 같아요.." };
        mq_mutex.lock();
        message_queue.push_back( Message( chatroom_name, fortune[ Util::rand( 0, fortune.size() - 1 ) ] ) );
        mq_mutex.unlock();
    }

    if ( msg.rfind( u"/시세 ", 0 ) == 0 ) {
        auto u8msg = Util::UTF16toUTF8( msg );
        if ( std::regex_match( u8msg, std::regex( u8"/시세 (앱솔|아케인)(무기|방어구)" ) ) ) {
            std::regex reg( u8"/시세 (앱솔|아케인)(무기|방어구)" );
            std::sregex_token_iterator it( u8msg.begin(), u8msg.end(), reg, std::vector<int>{ 1, 2 } );
            std::string kind = Util::UTF8toUTF16( *it++ ) == u"앱솔" ? "absol" : "arcane";
            std::string equipment = Util::UTF8toUTF16( *it ) == u"무기" ? "weapon" : "shield";

            http::Request request{ fmt::format( "{}price?kind={}&equipment={}", __config.api_endpoint(), kind, equipment ) };
            auto response = request.send( "GET" );
            auto res_text = std::string( response.body.begin(), response.body.end() );
            item::ItemList items;
            google::protobuf::util::JsonStringToMessage( res_text, &items );
            std::cout << items.items_size() << std::endl;
            std::u16string result = u"";
            for ( int i = 0; i < items.items_size(); ++i ) {
                auto item = items.items( i );
                auto comma_added_price = []( int64_t price ) {
                    std::string price_str = std::to_string( price );
                    std::string result = "";
                    int cnt = 0;
                    for ( int i = price_str.length() - 1; i >= 0; --i ) {
                        result = price_str[ i ] + result;
                        if ( ++cnt == 3 && i != 0 ) {
                            result = "," + result;
                            cnt = 0;
                        }
                    }
                    return result;
                }( item.price() );
                result += fmt::format( u"{}. {} : {}\n", i + 1, Util::UTF8toUTF16( item.name() ), Util::UTF8toUTF16( comma_added_price ) );
            }
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, result ) );
            mq_mutex.unlock();
        } else {
            mq_mutex.lock();
            message_queue.push_back( Message( chatroom_name, u"현재 지원하지 않는 아이템입니다." ) );
            mq_mutex.unlock();
        }
    }
    return RETURN_CODE::OK;
}
