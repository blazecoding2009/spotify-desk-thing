# spotify desk thing
### custom made pcb to be like the [spotify car thing](https://support.spotify.com/us/article/car-thing-discontinued/) (sadly discontinued)

yeah this is something i literally thought of while sitting and listening to music and i think it'd look cool lol

<img width="943" height="528" alt="image" src="https://github.com/user-attachments/assets/2e3d793b-31e7-4be5-a76f-ae14f464c783" />

<img width="1157" height="692" alt="image" src="https://github.com/user-attachments/assets/f8da136f-9c3b-475f-a4b2-7d8a0f59a228" />
<img width="942" height="574" alt="image" src="https://github.com/user-attachments/assets/ae8f1beb-d2d1-449d-833c-9357cfa4fada" />
<img width="949" height="608" alt="image" src="https://github.com/user-attachments/assets/a69d2e6e-39cf-422b-ba7e-15975e30cf6c" />

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
