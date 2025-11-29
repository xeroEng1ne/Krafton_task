#include <SDL2/SDL.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm> // std::clamp
#include <cstring>   // memset, strerror
#include <unistd.h>  // close
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

// Networking helpers
bool send_all(int sock,const std::string& data){
    const char* buf = data.c_str();
    size_t total=0;
    size_t len=data.size();
    while (total<len) {
        ssize_t n=::send(sock, buf + total, len - total, 0);
        if (n<=0) {
            return false;
        }
        total+=static_cast<size_t>(n);
    }
    return true;
}

bool send_line(int sock,const std::string& line){
    std::string data=line;
    data.push_back('\n');
    return send_all(sock, data);
}

// client state structures

struct TimedSnapshot{
proto::worldSnapshot snap;
    double recv_time=0.0;
};

// Local predicted state for player
struct PredictedState {
    float x=0.0f;
    float y=0.0f;
    float vx=0.0f;
    float vy=0.0f;
    bool initialized=false;
};

std::atomic<bool> g_running{true};
int g_sock=-1;

int g_player_id=0; // player id
int g_player_idx=0; // player index

std::mutex g_snap_mutex;
std::deque<TimedSnapshot> g_snapshots;
bool g_has_snapshot=false;
proto::worldSnapshot g_latest_snapshot;

PredictedState g_predicted;

// input
std::atomic<int> g_input_dx{0};
std::atomic<int> g_input_dy{0};
std::atomic<int> g_input_seq{0};

// network receive thread

void network_thread_func(){
    std::string buffer;
    char recv_buf[1024];

    while(g_running){
        ssize_t n=::recv(g_sock,recv_buf,sizeof(recv_buf),0);
        if(n<=0){
            cerr<<"Diconnected from server or recv error.\n";
            g_running=false;
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

            // handle welcome
            if(line.rfind("WELCOME",0)==0){
                std::istringstream iss(line);
                std::string tag;
                int id;
                if(iss>>tag>>id){
                    g_player_id=id;
                    g_player_idx=id-1;
                    cout<<"Got WELCOME, I am player "<<g_player_id<<nl;
                }
            }
            // handle state
            else if(line.rfind("STATE",0)==0){
                proto::worldSnapshot s;
                if(proto::decode_state(line,s)){
                    TimedSnapshot ts;
                    ts.snap=s;
                    ts.recv_time=now_seconds();

                    std::lock_guard<std::mutex> lock(g_snap_mutex);
                    g_latest_snapshot=s;
                    g_has_snapshot=true;
                    g_snapshots.push_back(ts);
                    while(g_snapshots.size()>120){
                        g_snapshots.pop_front();
                    }
                }
            }
            else{
                cout<<"Unknown line from server: "<<line<<nl;
            }
        }
    }

    cout<<"Network thread exiting.\n";
}

// utils : interpolated positions

struct RenderState {
    float local_x=0.0f;
    float local_y=0.0f;
    float remote_x=0.0f;
    float remote_y=0.0f;
    float coin_x=0.0f;
    float coin_y=0.0f;
    bool coin_active=false;
    int local_score=0;
    int remote_score=0;
    bool ready=false;
};

RenderState compute_render_state(){
    RenderState rs;

    if(!g_has_snapshot || g_player_id==0){
        return rs;
    }

    std::deque<TimedSnapshot> snaps_copy;
    proto::worldSnapshot latest;
    {
        std::lock_guard<std::mutex> lock(g_snap_mutex);
        if(g_snapshots.empty()){
            return rs;
        }
        snaps_copy=g_snapshots;
        latest=g_latest_snapshot;
    }

    rs.local_x=g_predicted.x;
    rs.local_y=g_predicted.y;

    // const double interp_delay=proto::SIMULATED_LATENCY; // 0.2
    const double interp_delay=0.1;
    double t_render=now_seconds()-interp_delay;
    
    TimedSnapshot A=snaps_copy.front();
    TimedSnapshot B=snaps_copy.back();

    if(snaps_copy.size()>=2){
        for(size_t i=2;i<snaps_copy.size();i++){
            if(snaps_copy[i].recv_time>=t_render){

                A=snaps_copy[i-1];
                B=snaps_copy[i];
                break;
            }
        }
    }
    else{
        A=B=snaps_copy.front();
    }

    double t;
    if(B.recv_time==A.recv_time){
        t=0.0;
    }
    else{
        t=(t_render-A.recv_time)/(B.recv_time-A.recv_time);
        t=std::clamp(t,0.0,1.0);
    }

    auto lerp=[](float a, float b, double tt){
        return a+(b-a)*static_cast<float>(tt);
    };

    int local_idx=g_player_idx;
    int remote_idx=1-g_player_idx;

    // using linear interpolation
    rs.remote_x=lerp(A.snap.players[remote_idx].x,B.snap.players[remote_idx].x,t);
    rs.remote_y=lerp(A.snap.players[remote_idx].y,B.snap.players[remote_idx].y,t);

    // coin position
    rs.coin_x=latest.coin_x;
    rs.coin_y=latest.coin_y;
    rs.coin_active=latest.coin_active;

    rs.local_score=latest.players[local_idx].score;
    rs.remote_score=latest.players[remote_idx].score;

    rs.ready=true;
    return rs;
}

// main

int main(int argc, char** argv){

    // connect to server
    std::string server_ip="127.0.0.1";
    if(argc>=2){
        server_ip=argv[1];
    }

    g_sock=::socket(AF_INET,SOCK_STREAM,0);
    if(g_sock<0){
        cerr<<"socket() failed: "<<std::strerror(errno)<<endl;
        return 1;
    }

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_port=htons(proto::SERVER_PORT);
    if(::inet_pton(AF_INET,server_ip.c_str(),&addr.sin_addr)<=0){
        cerr<<"inet_pton failed for IP "<<server_ip<<endl;
        ::close(g_sock);
        return 1;
    }

    cout<<"Connecting to "<<server_ip<<":"<<proto::SERVER_PORT<<"...\n";
    if(::connect(g_sock,(sockaddr*)&addr,sizeof(addr))<0){
        cerr<<"connect() failed: "<<std::strerror(errno)<<endl;
        ::close(g_sock);
        return 1;
    }
    cout<<"Connected to server. \n";

    send_line(g_sock,"JOIN client");
    std::thread net_thread(network_thread_func);

    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        cerr << "SDL_Init failed: " << SDL_GetError() << endl;
        g_running = false;
        net_thread.join();
        ::close(g_sock);
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Multiplayer Client",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        (int)proto::WORLD_WIDTH, (int)proto::WORLD_HEIGHT,
        SDL_WINDOW_SHOWN
    );
    if (!window) {
        cerr << "SDL_CreateWindow failed: " << SDL_GetError() << endl;
        SDL_Quit();
        g_running = false;
        net_thread.join();
        ::close(g_sock);
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        g_running = false;
        net_thread.join();
        ::close(g_sock);
        return 1;
    }

    // Main loop
    double last_time = now_seconds();

    while (g_running) {
        // Handle events
        SDL_Event e;
        bool input_changed = false;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                g_running = false;
            } else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                bool down = (e.type == SDL_KEYDOWN);
                int sym = e.key.keysym.sym;
                int dx = g_input_dx.load();
                int dy = g_input_dy.load();

                if (sym == SDLK_w || sym == SDLK_UP) {
                    dy = down ? -1 : (dy == -1 ? 0 : dy);
                    input_changed = true;
                } else if (sym == SDLK_s || sym == SDLK_DOWN) {
                    dy = down ? 1 : (dy == 1 ? 0 : dy);
                    input_changed = true;
                } else if (sym == SDLK_a || sym == SDLK_LEFT) {
                    dx = down ? -1 : (dx == -1 ? 0 : dx);
                    input_changed = true;
                } else if (sym == SDLK_d || sym == SDLK_RIGHT) {
                    dx = down ? 1 : (dx == 1 ? 0 : dx);
                    input_changed = true;
                }

                if (input_changed) {
                    g_input_dx = dx;
                    g_input_dy = dy;

                    int seq = g_input_seq.fetch_add(1);
                    std::ostringstream oss;
                    oss << "INPUT " << seq << " " << dx << " " << dy;
                    if (!send_line(g_sock, oss.str())) {
                        cerr << "Failed to send INPUT.\n";
                    }

                    // Update local predicted velocity
                    g_predicted.vx = (float)dx * proto::PLAYER_SPEED;
                    g_predicted.vy = (float)dy * proto::PLAYER_SPEED;
                }
            }
        }

        double now = now_seconds();
        double dt = now - last_time;
        last_time = now;
        if (dt < 0.0) dt = 0.0;
        if (dt > 0.1) dt = 0.1; // clamp huge dt

        // Initialize prediction when we get the first snapshot
        if (g_has_snapshot && !g_predicted.initialized) {
            std::lock_guard<std::mutex> lock(g_snap_mutex);
            proto::worldSnapshot s = g_latest_snapshot;
            g_predicted.x = s.players[g_player_idx].x;
            g_predicted.y = s.players[g_player_idx].y;
            g_predicted.vx = 0.0f;
            g_predicted.vy = 0.0f;
            g_predicted.initialized = true;
        }

        // Update predicted position
        if (g_predicted.initialized) {
            g_predicted.x += g_predicted.vx * (float)dt;
            g_predicted.y += g_predicted.vy * (float)dt;

            // Clamp to world bounds
            if (g_predicted.x < proto::PLAYER_RADIUS) 
                g_predicted.x = proto::PLAYER_RADIUS;
            if (g_predicted.x > proto::WORLD_WIDTH - proto::PLAYER_RADIUS)
                g_predicted.x = proto::WORLD_WIDTH - proto::PLAYER_RADIUS;

            if (g_predicted.y < proto::PLAYER_RADIUS) 
                g_predicted.y = proto::PLAYER_RADIUS;
            if (g_predicted.y > proto::WORLD_HEIGHT - proto::PLAYER_RADIUS)
                g_predicted.y = proto::WORLD_HEIGHT - proto::PLAYER_RADIUS;

            // Reconciliation: gently nudge towards authoritative position
            //INFO: removed reconcilation for snappy movement for now

            // if (g_has_snapshot) {
            //     proto::worldSnapshot s;
            //     {
            //         std::lock_guard<std::mutex> lock(g_snap_mutex);
            //         s = g_latest_snapshot;
            //     }
            //     float auth_x = s.players[g_player_idx].x;
            //     float auth_y = s.players[g_player_idx].y;
            //     float dx = auth_x - g_predicted.x;
            //     float dy = auth_y - g_predicted.y;
            //     // If difference is large, snap more strongly
            //     const float snap_threshold = 20.0f;
            //     if (dx*dx + dy*dy > snap_threshold * snap_threshold) {
            //         g_predicted.x = auth_x;
            //         g_predicted.y = auth_y;
            //     } else {
            //         // Smooth correction
            //         const float alpha = 0.2f;
            //         g_predicted.x += dx * alpha;
            //         g_predicted.y += dy * alpha;
            //     }
            // }
        }

        // Compute render state (interpolation for remote player)
        RenderState rs = compute_render_state();

        // Render
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
        SDL_RenderClear(renderer);

        if (rs.ready && g_predicted.initialized) {
            // Draw local player (blue)
            SDL_Rect local_rect;
            local_rect.w = (int)(proto::PLAYER_RADIUS * 2.0f);
            local_rect.h = (int)(proto::PLAYER_RADIUS * 2.0f);
            local_rect.x = (int)(g_predicted.x - proto::PLAYER_RADIUS);
            local_rect.y = (int)(g_predicted.y - proto::PLAYER_RADIUS);
            SDL_SetRenderDrawColor(renderer, 50, 150, 255, 255);
            SDL_RenderFillRect(renderer, &local_rect);

            // Draw remote player (red)
            SDL_Rect remote_rect;
            remote_rect.w = local_rect.w;
            remote_rect.h = local_rect.h;
            remote_rect.x = (int)(rs.remote_x - proto::PLAYER_RADIUS);
            remote_rect.y = (int)(rs.remote_y - proto::PLAYER_RADIUS);
            SDL_SetRenderDrawColor(renderer, 255, 80, 80, 255);
            SDL_RenderFillRect(renderer, &remote_rect);

            // Draw coin (yellow)
            if (rs.coin_active) {
                SDL_Rect coin_rect;
                coin_rect.w = (int)(proto::COIN_RADIUS * 2.0f);
                coin_rect.h = (int)(proto::COIN_RADIUS * 2.0f);
                coin_rect.x = (int)(rs.coin_x - proto::COIN_RADIUS);
                coin_rect.y = (int)(rs.coin_y - proto::COIN_RADIUS);
                SDL_SetRenderDrawColor(renderer, 240, 220, 50, 255);
                SDL_RenderFillRect(renderer, &coin_rect);
            }
        }

        SDL_RenderPresent(renderer);

        // Cap to ~60 FPS
        const double frame_target = 1.0 / 60.0;
        double frame_end = now_seconds();
        double frame_time = frame_end - now;
        if (frame_time < frame_target) {
            sleep_for_seconds(frame_target - frame_time);
        }
    }

    // Cleanup
    cout << "Shutting down client.\n";
    ::close(g_sock);
    net_thread.join();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;

}
