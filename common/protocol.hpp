#pragma once

#include <string>
#include <sstream>
#include <iomanip>

namespace proto{

// ---- general game network settings ---- 

// port server listens on and clients connect to
constexpr int SERVER_PORT=40000;

// num of simulation steps per second the server runs
constexpr int TICK_RATE=30;

// simulated one way network latency
constexpr double SIMULATED_LATENCY=0.2;

// world size
constexpr float WORLD_WIDTH=800.0f;
constexpr float WORLD_HEIGHT=600.0f;

constexpr float PLAYER_SPEED=200.0f;

constexpr float PLAYER_RADIUS=15.0f;
constexpr float COIN_RADIUS=10.0f;

// structs

struct playerState{
    int id=0;
    float x=0.0f;
    float y=0.0f;
    int score=0;
};

struct worldSnapshot{
    int tick=0;
    double server_time=0.0;

    playerState players[2]; // supporting 2 players only for now

    float coin_x=0.0f;
    float coin_y=0.0f;
    bool coin_active=false;
};

// encoder and decoder for state space

// Text format (single line, space-separated):
// STATE <tick>
//       <p1x> <p1y> <p1score>
//       <p2x> <p2y> <p2score>
//       <coinx> <coiny> <coin_active>
//       <server_time>
//
// Example:
// STATE 42 100 200 1 300 200 0 150 150 1 12345.678900

inline std::string encode_state(const worldSnapshot& s){
    std::ostringstream oss;
    oss<<"STATE "
        <<s.tick<<' '
        <<s.players[0].x<<' '<<s.players[0].y<<' '<<s.players[0].score<<' '
        <<s.players[1].x<<' '<<s.players[1].y<<' '<<s.players[1].score<<' '
        <<s.coin_x<<' '<<s.coin_y<<' '<<(s.coin_active ? 1 : 0)<<' '
        <<std::fixed<<std::setprecision(6)<<s.server_time;
    return oss.str();
}

// parser for state
inline bool decode_state(const std::string& line, worldSnapshot& out){
    std::istringstream iss(line);
    std::string tag;
    int coin_active=0;

    if(!(iss>>tag)) return false;
    if(tag!="STATE") return false;

    if(!(iss>>out.tick
            >>out.players[0].x>>out.players[0].y>>out.players[0].score
            >>out.players[1].x>>out.players[1].y>>out.players[1].score
            >>out.coin_x>>out.coin_y>>coin_active
            >>out.server_time)){
        return false;
    }

    out.coin_active=(coin_active!=0);
    out.players[0].id=1;
    out.players[1].id=2;

    return true;
}
}

