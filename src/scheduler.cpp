#include "scheduler.h"

extern config::Config __config;

void scheduler_boj( std::vector<std::u16string> &scheduler_message, std::mutex &m, std::chrono::minutes &interval ) {
    while ( true ) {
        {
            std::unique_lock<std::mutex> lock( m );
            http::Request request( fmt::format( "{}boj", __config.api_endpoint() ) );
            auto response = request.send( "GET" );
            auto res_text = std::string( response.body.begin(), response.body.end() );
            auto u16_text = Util::UTF8toUTF16( res_text );
            if ( u16_text.length() != 0 ) {
                // u16_text parsing

                auto res = Util::split( u16_text, "," );

                for ( auto it = res.begin(); it != res.end(); ++it ) {
                    std::u16string tmp = *it;

                    auto name_problem_id = Util::split( tmp, "!@#" );

                    if ( name_problem_id.size() != 2 ) {
                        std::cout << "name_problem_id.size() != 2" << std::endl;
                        for ( auto it = name_problem_id.begin(); it != name_problem_id.end(); ++it ) {
                            std::cout << Util::UTF16toUTF8( *it ) << std::endl;
                        }
                        continue;
                    }

                    std::u16string name = name_problem_id[ 0 ];
                    std::u16string problem_id = name_problem_id[ 1 ];

                    // 해결한 문제의 이름과 레벨을 !@#로 구분하여 가져옴
                    http::Request request2( fmt::format( "{}boj/problem?id={}", __config.api_endpoint(), Util::URLEncode( problem_id ) ) );
                    auto response2 = request2.send( "GET" );
                    auto res_text2 = Util::UTF8toUTF16( std::string( response2.body.begin(), response2.body.end() ) );

                    auto title_level = Util::split( res_text2, "!@#" );

                    if ( title_level.size() != 2 ) {
                        continue;
                    }

                    std::u16string title = title_level[ 0 ];
                    std::u16string level = title_level[ 1 ];

                    // 해결한 사람의 레이팅과 티어를 ,로 구분하여 가져옴
                    http::Request request3( fmt::format( "{}boj/info?name={}", __config.api_endpoint(), Util::URLEncode( name ) ) );
                    auto response3 = request3.send( "GET" );
                    auto res_text3 = Util::UTF8toUTF16( std::string( response3.body.begin(), response3.body.end() ) );

                    auto rating_tier = Util::split( res_text3, "," );

                    if ( rating_tier.size() != 2 ) {
                        continue;
                    }

                    std::u16string rating = rating_tier[ 0 ];
                    std::u16string tier = rating_tier[ 1 ];

                    std::u16string tier_emoji;

                    if ( tier.rfind( u"Unranked", 0 ) == 0 ) {
                        tier_emoji = u"📌";
                    } else if ( tier.rfind( u"Bronze", 0 ) == 0 ) {
                        tier_emoji = u"🥉";
                    } else if ( tier.rfind( u"Silver", 0 ) == 0 ) {
                        tier_emoji = u"💿";
                    } else if ( tier.rfind( u"Gold", 0 ) == 0 ) {
                        tier_emoji = u"📀";
                    } else if ( tier.rfind( u"Platinum", 0 ) == 0 ) {
                        tier_emoji = u"💠";
                    } else if ( tier.rfind( u"Diamond", 0 ) == 0 ) {
                        tier_emoji = u"💎";
                    } else if ( tier.rfind( u"Ruby", 0 ) == 0 ) {
                        tier_emoji = u"💘";
                    } else if ( tier.rfind( u"Master", 0 ) == 0 ) {
                        tier_emoji = u"🏆";
                    }
                    scheduler_message.push_back( fmt::format( u"** 백준 알리미 **\n{}님이 [{}] {} 문제를 해결했습니다!\nRating : {}{} {}", name, level, title, tier_emoji, tier, rating ) );
                }
            }
        }

        std::this_thread::sleep_for( interval );
    }

    return;
}