﻿#include "scheduler.h"

extern config::Config __config;

void scheduler_boj( std::vector<Message> &message_queue, std::mutex &mq_mutex, std::chrono::minutes &interval ) {
    while ( true ) {
        {
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

                    // 해결한 문제가 실버 난이도 이상일 때만 메세지 전송
                    if ( level.rfind( u"Unranked", 0 ) == 0 || level.rfind( u"Bronze", 0 ) == 0 ) {
                        continue;
                    }

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

                    // 맞춘 문제에 대한 정보를 메모리,시간,언어,코드길이,맞춘 시각의 순서로 가져옴.
                    http::Request solved_info_req{ fmt::format( "{}boj/solved_info?name={}&problem_id={}", __config.api_endpoint(), Util::URLEncode( name ), Util::URLEncode( problem_id ) ) };
                    auto solved_info_res = solved_info_req.send( "GET" );
                    auto solved_info = Util::split( Util::UTF8toUTF16( std::string( solved_info_res.body.begin(), solved_info_res.body.end() ) ), "," );
                    if ( solved_info.size() != 5 )
                        continue;
                    mq_mutex.lock();
                    message_queue.push_back( Message( __config.chatroom_name(), fmt::format( u"** 백준 알리미 **\n{}님이 [{}] {} 문제를 해결했습니다!\n언어 : {}\n시간 : {}ms\n메모리 : {}KB\nRating : {}{} {}", name, level, title, solved_info[ 2 ], solved_info[ 1 ], solved_info[ 0 ], tier_emoji, tier, rating ) ) );
                    mq_mutex.unlock();
                }
            }
        }

        std::this_thread::sleep_for( interval );
    }

    return;
}