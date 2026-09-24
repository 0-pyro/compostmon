# 🌿 Compost Telemetry & Motor Monitor

An IoT compost monitoring system and Windows 95-themed telemetry dashboard powered by ESP32, Arduino Uno, and HiveMQ MQTT.

---

## 📁 Repository Structure

```text
compost/
├── dashboard/                     # Web Dashboard (Deploy this to Vercel)
│   └── index.html                # Retro Win95 Telemetry & Motor Control UI
├── compost/                       # ESP32 Firmware (PlatformIO Project)
│   ├── platformio.ini             # PlatformIO environment & dependencies
│   ├── src/
│   │   └── main.cpp               # ESP32 sensor acquisition & L298N motor driver
│   ├── include/
│   └── lib/
├── .gitignore
└── README.md
```

---

## 🚀 Deploying to Vercel

1. Push this repository to GitHub.
2. Go to [vercel.com](https://vercel.com) and click **"Add New Project"**.
3. Select your GitHub repository.
4. In the configuration screen, look for **Root Directory**:
   - Click **Edit** next to **Root Directory**.
   - Select or type: `dashboard`
5. Click **Deploy**.

Vercel will deploy `dashboard/index.html` as your live web app. Any future `git push` to your repository will automatically redeploy the dashboard.

---

## ⚡ Features & Architecture

### 1. Dashboard (`dashboard/index.html`)
- **Dual Connection Modes**:
  - **MQTT (Remote / Vercel)**: Connects over secure WebSockets (`wss://broker.hivemq.com:8884/mqtt`) — zero mixed-content issues on HTTPS.
  - **WebSocket (Local)**: Connects directly to ESP32 on LAN (`ws://compostpro.local:81`).
- **Telemetry Display**: Live gas readings ($O_2$, $CO_2$, Compost Temp, Ambient Temp, Humidity, $NH_3$ / MQ137, $NO_2$ / MICS2714, $CO$ / MQ9).
- **Motor Control**:
  - Start / Stop toggle for aeration motor / pump.
  - Auto-off timer (10s, 30s, 1m, 5m) & 5-second aeration pulse button.
  - Active runtime counter.

### 2. ESP32 Firmware (`compost/src/main.cpp`)
- **L298N Motor Driver**:
  - `IN3` -> GPIO 14 (`D14`)
  - `IN4` -> GPIO 27 (`D27`)
- **UART Communication**: Reads sensor telemetry from Arduino Uno over `Serial2` (`GPIO 16`, `GPIO 17`).
- **Gas Sensors**: MQ9 (`GPIO 34`), MQ137 (`GPIO 35`), MICS2714 (`GPIO 32`).
- **MQTT Topics**:
  - Telemetry: `compost/sensors`
  - Motor Commands: `compost/motor` (`MOTOR_ON`, `MOTOR_OFF`)
