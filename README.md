# spotify desk thing
### custom made pcb to be like the [spotify car thing](https://support.spotify.com/us/article/car-thing-discontinued/) (sadly discontinued)

yeah this is something i literally thought of while sitting and listening to music and i think it'd look cool lol

<img width="1015" height="606" alt="image" src="https://github.com/user-attachments/assets/9339c4c0-b422-476d-8c0d-4120aff5c89e" />
<img width="966" height="634" alt="image" src="https://github.com/user-attachments/assets/28c07b35-4f49-4757-8061-c50b0c3904a7" />
<img width="938" height="648" alt="image" src="https://github.com/user-attachments/assets/8299b425-75d0-4b51-b0bc-224f9176b240" />

## Features

- ESP32-S3
- 3.5” 480×320 SPI TFT display
- Capacitive touchscreen (GT911)
- SD card music playback
- I²S DAC (PCM5242)
- Differential headphone amp (TPA6132A2)
- Rotary encoder for volume + play/pause
- Headphone output (3.5mm jack)

---

## Hardware Overview

### Main Components

- **ESP32-S3-WROOM-1**
- **ST7796 3.5” TFT Display**
- **GT911 Capacitive Touch (on display)**
- **PCM5242 DAC**
- **TPA6132A2 Headphone Amplifier**
- **MicroSD Card **
- **Rotary Encoder with push button**

## Firmware Overview

The firmware is written using **ESP-IDF**.

### Tasks

- **UI Task**
  - Draws the interface
  - Handles play/pause and volume updates

- **Input Task**
  - Reads touch input
  - Reads rotary encoder events
  - Pushes events into a queue

- **Audio Task**
  - Plays WAV files
  - Handles beeps and stop commands
 
## Future Ideas

- Spotify / Wi-Fi streaming
- Album art
- Playlists
- Battery power
- Better UI animations
