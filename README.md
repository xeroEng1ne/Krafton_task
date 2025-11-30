# 🕹️ **LAN Multiplayer Coin Collector (C++ + SDL2)**

A lightweight 2-player real-time multiplayer minigame:

✔️ Lockstep networking
✔️ Client-side prediction
✔️ Server reconciliation
✔️ Tick-based world snapshots
✔️ Latency-friendly interpolation
✔️ Collision sound effects and background music
✔️ Waiting state until both players join
✔️ Score UI

Two players connect over LAN and race to collect coins. First to collect more coins wins.

---

## 📁 Project Structure

```
.
├── assets/
│   ├── font.ttf
│   ├── music.mp3
│   ├── coin.wav
│   └── bump.wav
├── build/
├── client/
│   └── client.cpp
├── server/
│   └── server.cpp
├── common/
│   ├── protocol.hpp
│   └── utils.hpp
└── CMakeLists.txt
```

---

## 🚀 Build & Run Instructions

### 🔧 Requirements

| Component                     | Required |
| ----------------------------- | -------- |
| C++ compiler (GCC/Clang/MSVC) | ✔️       |
| SDL2                          | ✔️       |
| SDL2_ttf                      | ✔️       |
| SDL2_mixer                    | ✔️       |
| CMake ≥ 3.12                  | ✔️       |

---

### 🐧 Ubuntu / Debian (Recommended)

```bash
sudo apt update
sudo apt install build-essential cmake libsdl2-dev libsdl2-ttf-dev libsdl2-mixer-dev
git clone <repo>
cd <repo>
mkdir build && cd build
cmake ..
make
```

Run server:

```bash
./server/server
```

Run client:

```bash
./client/client <SERVER_IP>
```

---

### 🐧 Arch Linux (Recommended)

```bash
sudo pacman -S cmake sdl2 sdl2_ttf sdl2_mixer
git clone <repo>
cd <repo>
mkdir build && cd build
cmake ..
make
```

Run the game as above.

---

### 🍎 macOS

Install dependencies:

```bash
brew install cmake sdl2 sdl2_ttf sdl2_mixer
```

Build:

```bash
git clone <repo>
cd <repo>
mkdir build && cd build
cmake ..
make
```

Run normally.

> [!NOTE]
> SDL2 sometimes can cause errors in Apple silicon chips

---

### 🪟 Windows

#### Option A — MSYS2

```bash
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 \
mingw-w64-x86_64-SDL2_mixer mingw-w64-x86_64-SDL2_ttf
```

Then:

```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
mingw32-make
```

#### Option B — Visual Studio + vcpkg

```bash
vcpkg install sdl2 sdl2-mixer sdl2-ttf
```

Open in Visual Studio & build.

>[!NOTE]
> A smoother way to run in windows is to install WSL (Windows Subsystem for Linux) and then follow the linux instructions

---

## 🖧 Multiplayer Instructions

1. **Pick one machine as server**

   ```bash
   ./server/server
   ```

2. Get its LAN IP:

```bash
ipconfig       # Windows  
ifconfig       # macOS  
ip addr        # Linux
```

Example: `10.184.24.106`

3. **Other machine(s) connect using that IP:**

```bash
./client/client 10.184.24.106
```

4. Game begins **only when both players have joined**.
   Until then, the client displays:

> ⏳ *Waiting for another player to join…*

---

## 🎮 Controls

| Action | Key                |
| ------ | ------------------ |
| Move   | WASD or Arrow keys |
| Quit   | Close window       |

>[!TIP]
> Use the bumping mechanic!

## ✨ Features Implemented

* 📡 Robust TCP client/server architecture
* 🎯 Deterministic game state with authoritative server control
* 🚀 Client-side prediction
* 🤝 Smooth remote interpolation
* ⏱ 20 Hz server tick + 60 FPS rendering
* 🎵 Background music + WAV sound effects
* 👀 Connection lobby with UI feedback
* 🏆 Real-time score display
* 💥 Collision alert sound

---

## 🧪 Notes on Networking Approach

This project intentionally avoids:

🚫 Unity Netcode
🚫 Third-party authoritative sync libraries
🚫 High-level replication frameworks

Instead, it demonstrates:

* Manual TCP messaging
* Serialized world state messages
* Tick-based synchronization
* Prediction/reconciliation pattern

