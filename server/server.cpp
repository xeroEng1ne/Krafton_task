#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <cmath>
#include <random>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../common/utils.hpp"
#include "../common/protocol.hpp"

using std::cout;
using std::cerr;
using std::endl;

#define nl "\n"

//----- networking helpers -----

bool send_all(int sock, const std::string& data){
    const char* buf=data.c_str();
    size_t total=0;
    size_t len=data.size();
    while(total<len){
        ssize_t n=::send(sock,buf+total,len-total,0);
        if(n<=0){
            return false;
        }
        total+=static_cast<size_t>(n);
    }
    return true;
}

// same but automatically append '\n'
bool send_line(int sock, const std::string& line){
    std::string data=line;
    data.push_back('\n');
    return send_all(sock,data);
}


// --- Game structures ---

struct Player{
    int id=0;
    float x=0.0f;
    float y=0.0f;
    float vx=0.0f;
    float vy=0.0f;
    int score=0;
};

struct Coin{
    float x=0.0f;
    float y=0.0f;
    bool active=false;
};

struct InputEvent{
    int player_id=0; // 0 or 1
    int seq=0;
    int dx=0; // -1, 0, 1
    int dy=0; // -1, 0, 1
    double ready_time=0.0; // when to apply
};

Player g_players[2];
Coin g_coin;

int g_client_socks[2]={-1,-1};
std::atomic<bool> g_client_connected[2]={false,false};

std::mutex g_input_mutex;
std::queue<InputEvent> g_input_queue;

std::atomic<bool> g_running{true};

// random generator
std::mt19937 g_rng{std::random_device{}()};

// game logic helpers

void spawn_coin(){
    std::uniform_real_distribution<float> dist_x(proto::COIN_RADIUS,
                                                 proto::WORLD_WIDTH-proto::COIN_RADIUS);
    std::uniform_real_distribution<float> dist_y(proto::COIN_RADIUS,
                                                 proto::WORLD_HEIGHT-proto::COIN_RADIUS);

    g_coin.x=dist_x(g_rng);
    g_coin.y=dist_y(g_rng);
    g_coin.active=true;
    cout<<"Spawned coint at ("<<g_coin.x<<", "<<g_coin.y<<")\n";
}

void init_players() {
    // Two starting positions: left and right side of the world
    g_players[0].id=1;
    g_players[0].x=proto::WORLD_WIDTH * 0.25f;
    g_players[0].y=proto::WORLD_HEIGHT * 0.5f;
    g_players[0].vx=0.0f;
    g_players[0].vy=0.0f;
    g_players[0].score = 0;

    g_players[1].id=2;
    g_players[1].x=proto::WORLD_WIDTH * 0.75f;
    g_players[1].y=proto::WORLD_HEIGHT * 0.5f;
    g_players[1].vx=0.0f;
    g_players[1].vy=0.0f;
    g_players[1].score=0;
}

void apply_input_event(const InputEvent& ev){
    Player& p=g_players[ev.player_id];
    p.vx=static_cast<float>(ev.dx)*proto::PLAYER_SPEED;
    p.vy=static_cast<float>(ev.dy)*proto::PLAYER_SPEED;
}

void update_world(double dt){
    for(int i=0;i<2;i++){
        Player& p=g_players[i];
        p.x+=p.vx*static_cast<float>(dt);
        p.y+=p.vy*static_cast<float>(dt);

        if(p.x<proto::PLAYER_RADIUS) p.x=proto::PLAYER_RADIUS;
        if(p.x>proto::WORLD_WIDTH-proto::PLAYER_RADIUS)
            p.x=proto::WORLD_WIDTH-proto::PLAYER_RADIUS;
        if(p.y<proto::PLAYER_RADIUS) p.y=proto::PLAYER_RADIUS;
        if(p.y>proto::WORLD_HEIGHT-proto::PLAYER_RADIUS)
            p.y=proto::WORLD_HEIGHT-proto::PLAYER_RADIUS;
    }

    // player collision
    float dx=g_players[0].x-g_players[1].x;
    float dy=g_players[0].y-g_players[1].y;
    float dist=std::sqrt(dx*dx+dy*dy);

    float minDist=proto::PLAYER_RADIUS*2.0f;
    if (dist<minDist && dist > 0.0f){
        float push=(minDist - dist)*0.5f;
        float nx=dx/dist;
        float ny=dy/dist;

        g_players[0].x+=nx*push;
        g_players[0].y+=ny*push;
        g_players[1].x-=nx*push;
        g_players[1].y-=ny*push;
    }

    // coin spawning
    if(!g_coin.active){
        spawn_coin();
    }

    // coin pickup checks
    const float pickup_dist=proto::PLAYER_RADIUS+proto::COIN_RADIUS;
    const float pickup_dist_sq=pickup_dist*pickup_dist;

    for(int i=0;i<2;i++){
        if(!g_coin.active) break;

        Player& p=g_players[i];
        float dx=p.x-g_coin.x;
        float dy=p.y-g_coin.y;
        float dist_sq=dx*dx+dy*dy;

        if(dist_sq<=pickup_dist_sq){
            p.score+=1;
            g_coin.active=false;
            cout<<"Player "<<p.id<<" picked up coin! Score is : "<<p.score<<nl;
        }
    }
}


proto::worldSnapshot build_snapshot(int tick){
    proto::worldSnapshot s;
    s.tick=tick;
    s.server_time=now_seconds();

    for(int i=0;i<2;i++){
        s.players[i].id=g_players[i].id;
        s.players[i].x=g_players[i].x;
        s.players[i].y=g_players[i].y;
        s.players[i].score=g_players[i].score;
    }

    s.coin_x=g_coin.x;
    s.coin_y=g_coin.y;
    s.coin_active=g_coin.active;

    return s;
}

void broadcast_state(int tick) {
    proto::worldSnapshot s=build_snapshot(tick);
    std::string line=proto::encode_state(s);

    for (int i=0;i<2;++i){
        if (!g_client_connected[i]) continue;
        int sock=g_client_socks[i];
        if(!send_line(sock, line)){
            cerr<<"Failed to send STATE to player "<<(i+1)<<"\n";
        }
    }
}

// network listener and client threads

void handle_client(int player_id, int sock){
    cout<<"Client thread started for player "<<(player_id+1)<<nl;
    {
        std::string welcome="WELCOME "+std::to_string(player_id+1);
        send_line(sock,welcome);
    }

    std::string buffer;

    char recv_buf[1024];
    while(g_running && g_client_connected[player_id]){
        ssize_t n=::recv(sock,recv_buf,sizeof(recv_buf),0);
        if(n<=0){
            cerr<<"Player "<<(player_id+1)<<" disconnected or recv error. \n";
            g_client_connected[player_id]=false;
            ::close(sock);
            g_client_socks[player_id]=-1;
            break;
        }

        buffer.append(recv_buf,recv_buf+n);

        size_t pos;
        while((pos=buffer.find('\n'))!=std::string::npos){
            std::string line=buffer.substr(0,pos);
            buffer.erase(0,pos+1);

            if(!line.empty() && line.back()=='\r'){
                line.pop_back();
            }

            if(line.empty()) continue;

            // parse input
            if(line.rfind("INPUT ", 0)==0){
                std::istringstream iss(line);
                std::string tag;
                int seq,dx,dy;
                if(iss>>tag>>seq>>dx>>dy){
                    InputEvent ev;
                    ev.player_id=player_id;
                    ev.seq=seq;
                    ev.dx=dx;
                    ev.dy=dy;
                    ev.ready_time=now_seconds()+proto::SIMULATED_LATENCY; // simulate latency

                    std::lock_guard<std::mutex> lock(g_input_mutex);
                    g_input_queue.push(ev);
                }
            }
            else if(line.rfind("JOIN", 0)==0){
                // Not strictly needed if we auto-assign player index,
                // but you could parse player name here if you want.
                cout<<"Player "<<(player_id+1)<<" sent JOIN.\n";
            }
            else{
                // Unknown message type; ignore for now.
                cout<<"Unknown message from player "<<(player_id+1)<<": "<<line<<nl;
            }
        }
    }

    cout<<"Client thread exiting for player "<<(player_id+1)<<nl;
}

// 2 client for now
void accept_clients(int server_sock){
    for(int i=0;i<2;i++){
        cout<<"Waiting for player "<<(i+1)<<" to connect..."<<endl;

        sockaddr_in client_addr;
        socklen_t client_len=sizeof(client_addr);
        int client_sock=::accept(server_sock,(sockaddr*)&client_addr, &client_len);
        if(client_sock<0){
            cerr<<"accept() failed: "<<std::strerror(errno)<<endl;
            --i; // retry
            continue;
        }

        cout<<"Player "<<(i+1)<<" connected from "
            <<inet_ntoa(client_addr.sin_addr)<<":"<<ntohs(client_addr.sin_port)<<endl;

        g_client_socks[i]=client_sock;
        g_client_connected[i]=true;

        std::thread t(handle_client,i,client_sock);
        t.detach();
    }

    cout<<"Both player connected. Starting game loop. \n";
}

// =============== GAME LOOP ====================

void game_loop(){
    init_players();
    spawn_coin();

    const double dt=1.0/static_cast<double>(proto::TICK_RATE);
    double next_tick_time=now_seconds();
    int tick=0;

    while(g_running){
        double now=now_seconds();
        if(now<next_tick_time){
            sleep_for_seconds(next_tick_time-now);
            continue;
        }
        double frame_start=now_seconds();

        {
            std::lock_guard<std::mutex> lock(g_input_mutex);
            while(!g_input_queue.empty()){
                InputEvent& ev=g_input_queue.front();
                if(ev.ready_time<=frame_start){
                    apply_input_event(ev);
                    g_input_queue.pop();
                }
                else break;
            }
        }

        update_world(dt);
        broadcast_state(tick);

        tick++;
        next_tick_time+=dt;
    }
}

// server setup

int main(){
    cout<<"Starting server on port "<<proto::SERVER_PORT<<"...\n";
    int server_sock=::socket(AF_INET,SOCK_STREAM,0);
    if(server_sock<0){
        cerr<<"socket() failed: "<<std::strerror(errno)<<endl;
        return 1;
    }

    int opt=1;
    if(setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))<0){
        cerr<<"setsockopt(SO_REUSEADDR) failed: "<<std::strerror(errno)<<endl;
    }

    sockaddr_in addr;
    std::memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(INADDR_ANY);
    addr.sin_port=htons(proto::SERVER_PORT);

    if(bind(server_sock, (sockaddr*)&addr, sizeof(addr))<0){
        cerr<<"bind() failed: "<<std::strerror(errno)<<endl;
        ::close(server_sock);
        return 1;
    }

    if(listen(server_sock,2)<0){
        cerr<<"listen() failed: "<<std::strerror(errno)<<endl;
        ::close(server_sock);
        return 1;
    }

    cout<<"Server listening.\n";

    accept_clients(server_sock);

    game_loop();

    cout<<"Shutting down server.\n";
    for(int i=0;i<2;i++){
        if(g_client_connected[i]){
            ::close(g_client_socks[i]);
        }
    }
    ::close(server_sock);
    return 0;
}
