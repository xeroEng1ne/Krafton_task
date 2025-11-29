
# 🕹️ Multiplayer Coin Collector Game

This project implements a lightweight real-time multiplayer 2D game prototype using **C++**, **Sockets**, and **SDL2**, without relying on external networking or game engines.
It demonstrates:

* Client-side prediction
* Server-authoritative world state
* Snapshot interpolation for remote entities
* Simulated network latency
* Player movement and collectible scoring

---

## 🚀 Features

| Feature                                | Status |
| -------------------------------------- | ------ |
| Two-player online multiplayer          | ✔️     |
| Authoritative server tick loop         | ✔️     |
| Client input → prediction              | ✔️     |
| Snapshot state replication             | ✔️     |
| Interpolation for remote player        | ✔️     |
| Server-side latency simulation (200ms) | ✔️     |
| Scoring + collectible coin spawning    | ✔️     |
| SDL2 rendering (players + coin)        | ✔️     |

---

## 🛠️ Requirements

Tested on:

* **Arch Linux**
* `g++ / clang`
* `cmake`
* `SDL2`

Install dependencies (Arch):

```bash
sudo pacman -S cmake sdl2 gcc make git
```

---

## 📁 Project Structure

```
/multiplayer-game
 ├── client/
 │    └── client.cpp        ← networking + prediction + rendering
 ├── server/
 │    └── server.cpp        ← authoritative game loop
 ├── common/
 │    ├── protocol.hpp      ← message format + constants
 │    └── utils.hpp         ← timing helpers
 ├── CMakeLists.txt
 └── README.md
```

---

## 🔧 Build Instructions

```bash
mkdir build
cd build
cmake ..
make
```

This produces:

* `server/server`
* `client/client`

---

## 🎮 How to Run

Open **three terminals**:

### 1️⃣ Start the server:

```bash
./server/server
```

### 2️⃣ Launch Client 1:

```bash
./client/client
```

### 3️⃣ Launch Client 2:

```bash
./client/client
```

---

## 🧠 How the Networking Works

### 🔹 Input → Server

Clients send input as compact messages:

```
INPUT <seq> <dx> <dy>
```

The server does **not** store velocity permanently.
Each tick, movement is updated based on the last received input.

---

### 🔹 Server Simulation (Authoritative)

The server runs at a **fixed update rate**:

```
TICK_RATE = 30 Hz
```

During each tick:

1. Apply queued inputs (with artificial delay to simulate lag)
2. Update world (position, coin pickup, scoring)
3. Broadcast snapshot:

```
STATE tick time px1 py1 score1 px2 py2 score2 coin_x coin_y active
```

---

### 🔹 Client Rendering Strategy

| Local Player                              | Remote Player                        |
| ----------------------------------------- | ------------------------------------ |
| Client-side prediction (instant movement) | Interpolated between older snapshots |

This hides latency and prevents jitter.

---

## ⏳ Simulated Latency

The server intentionally delays processing input:

```
SIMULATED_LATENCY = 0.2s
```

This demonstrates realistic networking behavior such as:

* input buffering,
* delayed correction,
* state smoothing.

---

## 🎨 Controls

| Action     | Key          |
| ---------- | ------------ |
| Move Up    | `W` or `↑`   |
| Move Down  | `S` or `↓`   |
| Move Left  | `A` or `←`   |
| Move Right | `D` or `→`   |
| Quit       | Close window |

---

## 🧪 Known Limitations / To-Do

* No player-player collision yet
* No UI text rendering yet (scores visible only in terminal)

---
