# Parrock: Low Power Deterministic IoT System

Parrock is an autonomous, battery-powered IoT system built around a **dual-MCU architecture (STM32 + ESP32)** with a dedicated backend layer.

It implements a **deterministic, event-driven execution model** with hardware-assisted peripheral control, interrupt validation, and strict task segregation.

The STM32 acts as a low-power real-time controller designed for `STOP` mode operation, while the ESP32 functions as a burst-activated co-processor for networking and audio pipelines.

---

## 🚀 Project Showcases

1. **STM32 Standalone System (Sensors + UI + STOP Mode):**  
   [Watch on YouTube](https://www.youtube.com/shorts/Pw4e5nR__rQ)  
   *Early prototype: wake-up, DHT11 sampling, and OLED UI rendering without ESP32 dependency.*

2. **Dual-MCU Handshake & Audio Playback:**  
   [Watch on YouTube](https://www.youtube.com/shorts/rwVhf8vsigk)  
   *UART synchronization between STM32 and ESP32 with SD-based WAV playback.*

3. **Full System Integration (MQTT + Audio Streaming):**  
   [Watch on YouTube](https://www.youtube.com/shorts/1OuUkGBmAgo)  
   *Motion-triggered telemetry, I2S recording, and HTTP batch streaming to the backend.*

---

## 🧠 System Architecture

### 1. STM32 Master Domain (Deterministic Controller)

The STM32F103C6T6 operates as a **real-time deterministic controller** optimized for ultra-low power consumption.

- **Power State Management:** Operates in `STOP` mode with fast wake-up from hardware interrupts.
- **Pipeline Sleep Gate:** Validates RTOS flags, EXTI pending bits, and NVIC state before sleep entry to avoid race conditions and false wakeups.
- **Sensor Layer:** Handles PIR motion detection, DHT11 sampling, and battery monitoring via ADC.
- **Timekeeping:** Uses a DS1302 RTC for offline scheduling and alarms.
- **UI Layer:** Manages the OLED display and non-blocking WS2812B animations using timer-assisted signaling.
- **Co-processor Control:** Controls the ESP32 wake-up via a dedicated GPIO line.

### 2. ESP32 Slave Domain (Burst Co-Processor)

The ESP32 acts as a **demand-activated compute accelerator**, remaining in deep sleep until explicitly triggered by the STM32.

- **UART Handshake Protocol:** Boots and responds with `R` (Ready) to confirm availability.
- **Networking:** Wi-Fi connectivity and MQTT telemetry publishing.
- **Audio Pipeline:** INMP441 I2S recording and SD card playback through a MAX98357A I2S amplifier.
- **Autonomous Sleep:** Returns to deep sleep independently after completing its task.

### 3. Backend Domain (FastAPI + MQTT)

- **FastAPI:** HTTP endpoint for audio batch ingestion and static file serving.
- **MQTT Broker:** Sensor telemetry ingestion and event logging.
- **Storage:** Timestamped JSONL logging and segmented audio archives.

---

## 🔌 Power Architecture

- **V_RAW (3.7V - 4.2V):** Direct battery rail for high-current components such as WS2812 LEDs, the relay, and the audio amplifier.
- **3.3V Logic Rail:** Regulated supply from a buck converter for the MCUs and sensitive sensors.

All grounds are shared. The logic rail is isolated from current spikes on the high-current rail.

---

## ⚙️ Execution Model

1. **Idle State:** STM32 is in `STOP` mode; ESP32 is in deep sleep.
2. **Wake Trigger:** A hardware interrupt from PIR or RTC wakes the STM32.
3. **Deterministic Init:** STM32 validates interrupts via the Pipeline Sleep Gate, updates the UI, and asserts the ESP wake GPIO.
4. **Handshake:** ESP32 boots and sends `R` via UART. STM32 responds with a telemetry frame such as `[CMD:MOTION][BAT:85%][T:22C][H:45%]`.
5. **Task Execution:** ESP32 publishes telemetry to MQTT and performs audio recording, upload, or playback depending on the event.
6. **Race-to-Sleep:** ESP32 returns to deep sleep; STM32 turns off peripherals and re-enters `STOP` mode.

---

## 📡 Communication Protocol

**UART Frame Format:** `[CMD:<type>][BAT:<x%>][T:<temp>][H:<hum>]`  
**Handshake:** `R` from ESP32 confirms readiness before telemetry is sent.

---

## 🧩 Key Design Principles

- **Determinism:** Logic flow over event chaos.
- **Power Control:** Explicit hardware-assisted peripheral activation and sleep orchestration.
- **Observability:** State transitions are visible and traceable.
- **Asymmetry:** Low-power control on STM32, burst compute on ESP32.

---

## 🔮 Future Improvements

To further push the system toward true ultra-low power operation, the following hardware enhancements are planned:

### 🔋 MOSFET-Based Power Gating

The current prototype uses GPIO-controlled wake and enable lines to manage peripheral activity, such as the ESP32 wake pin and relay control. While effective for sequencing and control, this approach does not physically disconnect power from high-current components.

A future hardware revision may introduce **MOSFET-based load switching**, enabling:

- **True power isolation** of the ESP32 and selected peripherals  
- **Lower idle current** by removing standby consumption  
- **Cleaner power-domain control** independent of firmware state  
- **Improved battery life** in long-term deployments  

This would move the system from **GPIO-based peripheral control** toward **true hardware power gating**.

### ⚙️ Low-Power Sleep Refinement

The firmware is already structured around low-power operation and wake-on-interrupt behavior, with a deterministic execution model built around event-driven wake-up.

Future iterations may include:

- More efficient deep-sleep utilization  
- Faster and more reliable wake-up sequences  
- Hardware platforms better optimized for long-duration battery operation  
- Further reduction of idle power consumption  

Together, these changes will improve overall system efficiency, scalability, and long-term stability in autonomous IoT deployments.

---

The goal is to evolve Parrock from a low-power prototype into a **fully power-optimized embedded platform** for autonomous sensor, telemetry, and audio applications, suitable for long-term battery-powered deployments.

## 📁 Repository Structure

```plaintext
firmware/
├── Stm32Parrock/        # Master node: FreeRTOS + HAL + deterministic control
├── ESP32Parrock/        # Slave node: ESP-IDF + Wi-Fi + I2S pipelines
└── README.md            # System overview
parrock_backend/
├── app/                 # FastAPI + MQTT ingestion
├── data/                # Logs & recordings
└── README.md            # System overview
```
