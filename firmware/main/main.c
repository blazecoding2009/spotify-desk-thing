// spotify desk thing
// by blaze
// this took painful hours to get to work and build. 
// i spent ages making it look pretty too. don't diss me. 

#include "dirent.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>

#include "audio.h"
#include "encoder.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "gt911.h"
#include "ili9488.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"
#include "ui.h"
#include "pcm5242.h"

#define TAG "SPOTIFY_DESK"

#ifndef CONFIG_TFT_SPI_SPEED_HZ
#define CONFIG_TFT_SPI_SPEED_HZ (20 * 1000 * 1000)
#endif

#define SCREEN_MOSI GPIO_NUM_40
#define SCREEN_MISO GPIO_NUM_21
#define SCREEN_SCLK GPIO_NUM_42
#define SCREEN_CS GPIO_NUM_47
#define SCREEN_DC GPIO_NUM_39
#define SCREEN_RES GPIO_NUM_38
#define SCREEN_BL GPIO_NUM_41
#define SD_CS GPIO_NUM_48

#define I2C_SDA GPIO_NUM_1
#define I2C_SCL GPIO_NUM_2
#define TOUCH_INT GPIO_NUM_16

#define ENC_A GPIO_NUM_13
#define ENC_B GPIO_NUM_14
#define ENC_SW GPIO_NUM_15

#define I2S_MCLK GPIO_NUM_4
#define I2S_BCLK GPIO_NUM_5
#define I2S_LRCLK GPIO_NUM_6
#define I2S_DIN GPIO_NUM_7

// PCM5242 DAC control pins
#define PCM5242_I2C_ADDR 0x48      // Default I2C address for PCM5242
#define PCM5242_XSMT GPIO_NUM_10   // Soft mute control (active low)

#define MUSIC_DIR "/sd/music"

typedef enum {
	INPUT_EVENT_TOUCH = 0,
	INPUT_EVENT_ENCODER_LEFT,
	INPUT_EVENT_ENCODER_RIGHT,
	INPUT_EVENT_ENCODER_BUTTON,
} input_event_type_t;

typedef struct {
	input_event_type_t type;
	union {
		struct {
			uint16_t x;
			uint16_t y;
		} touch;
	} data;
} input_event_t;

typedef enum {
	AUDIO_CMD_BEEP = 0,
	AUDIO_CMD_PLAY_WAV,
	AUDIO_CMD_STOP,
} audio_command_type_t;

typedef struct {
	audio_command_type_t type;
	char path[256];
} audio_command_t;

static QueueHandle_t input_queue;
static QueueHandle_t audio_queue;

static spi_device_handle_t lcd_spi = NULL;
static ili9488_t lcd = {0};
static gt911_handle_t *touch_handle = NULL;
static encoder_handle_t *encoder_handle = NULL;
static ui_context_t ui_ctx;
static sdmmc_card_t *mounted_card = NULL;
static char default_track[256] = {0};
static char default_track_name[64] = {0};
static volatile bool touch_flag = false;

// I2C device array
#define MAX_I2C_DEVICES 10
static uint8_t found_i2c_devices[MAX_I2C_DEVICES] = {0};
static uint8_t num_i2c_devices = 0;

static void IRAM_ATTR touch_interrupt(void *arg) {
	(void)arg;
	touch_flag = true;
}

static void init_nvs(void) {
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ESP_ERROR_CHECK(nvs_flash_init());
	}
}

static esp_err_t init_spi_bus(void) {
	spi_bus_config_t buscfg = {
		.mosi_io_num = SCREEN_MOSI,
		.miso_io_num = SCREEN_MISO,
		.sclk_io_num = SCREEN_SCLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 320 * 40 * 2,
	};
	ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "SPI bus init failed");

	spi_device_interface_config_t devcfg = {
		.clock_speed_hz = CONFIG_TFT_SPI_SPEED_HZ,
		.mode = 0,
		.spics_io_num = SCREEN_CS,
		.queue_size = 7,
		.flags = SPI_DEVICE_NO_DUMMY,
	};
	return spi_bus_add_device(SPI2_HOST, &devcfg, &lcd_spi);
}

static esp_err_t init_display(void) {
	ili9488_config_t cfg = {
		.spi = lcd_spi,
		.dc_pin = SCREEN_DC,
		.reset_pin = SCREEN_RES,
		.backlight_pin = -1,
		.backlight_active_high = true,
	};
	ESP_RETURN_ON_ERROR(ili9488_init(&lcd, &cfg), TAG, "LCD init failed");
	return ili9488_fill_color(&lcd, 0, 0, ILI9488_WIDTH, ILI9488_HEIGHT, 0x0000);
}

static esp_err_t mount_sd(void) {
	sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	host.slot = SPI2_HOST;
	host.max_freq_khz = 20000;

	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		.format_if_mount_failed = false,
		.max_files = 8,
		.allocation_unit_size = 16 * 1024,
	};

	sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
	slot_config.host_id = SPI2_HOST;
	slot_config.gpio_cs = SD_CS;
	slot_config.gpio_cd = SDSPI_SLOT_NO_CD;
	slot_config.gpio_wp = SDSPI_SLOT_NO_WP;

	esp_err_t ret = esp_vfs_fat_sdspi_mount("/sd", &host, &slot_config, &mount_config, &mounted_card);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
		return ret;
	}

	sdmmc_card_print_info(stdout, mounted_card);
	return ESP_OK;
}

static void list_music_files(const char *path) {
	DIR *dir = opendir(path);
	if (!dir) {
		ESP_LOGW(TAG, "Directory %s not found", path);
		return;
	}
	struct dirent *entry;
	ESP_LOGI(TAG, "Listing %s", path);
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.') {
			continue;
		}
		ESP_LOGI(TAG, "  %s", entry->d_name);
	}
	closedir(dir);
}

static bool find_first_wav(const char *path, char *out_path, size_t len) {
	DIR *dir = opendir(path);
	if (!dir) {
		return false;
	}
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		char *dot = strrchr(entry->d_name, '.');
		if (!dot) {
			continue;
		}
		if (strcasecmp(dot, ".wav") == 0) {
			snprintf(out_path, len, "%s/%s", path, entry->d_name);
			closedir(dir);
			return true;
		}
	}
	closedir(dir);
	return false;
}

static esp_err_t init_touch(void) {
	gt911_config_t cfg = {
		.i2c_port = I2C_NUM_0,
		.sda_pin = I2C_SDA,
		.scl_pin = I2C_SCL,
		.int_pin = TOUCH_INT,
		.i2c_clock_hz = 400000,
	};
	ESP_RETURN_ON_ERROR(gt911_init(&touch_handle, &cfg), TAG, "GT911 init failed");
	return gt911_set_interrupt_callback(touch_handle, touch_interrupt, NULL);
}

static esp_err_t init_encoder(void) {
	encoder_config_t cfg = {
		.pin_a = ENC_A,
		.pin_b = ENC_B,
		.pin_button = ENC_SW,
		.button_active_level_low = true,
		.debounce_ms = 5,
	};
	return encoder_init(&encoder_handle, &cfg);
}

static esp_err_t init_i2c_bus(void) {
	// Initialize I2C bus - used by both GT911 and PCM5242
	i2c_config_t i2c_conf = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = I2C_SDA,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_io_num = I2C_SCL,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = 400000,
	};
	ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM_0, &i2c_conf), TAG, "I2C param config failed");
	ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_NUM_0, i2c_conf.mode, 0, 0, 0), TAG, "I2C driver install failed");
	return ESP_OK;
}

static void scan_i2c_quick(void) {
	printf("SPOTIFY_DESK: Quick I2C device scan:\n");
	int found_count = 0;
	for (uint8_t addr = 1; addr < 127; addr++) {
		i2c_cmd_handle_t cmd = i2c_cmd_link_create();
		i2c_master_start(cmd);
		i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
		i2c_master_stop(cmd);
		
		esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
		i2c_cmd_link_delete(cmd);
		
		if (ret == ESP_OK) {
			printf("  Found device at 0x%02X\n", addr);
			found_count++;
		}
	}
	printf("SPOTIFY_DESK: I2C scan complete - found %d devices\n", found_count);
}

static esp_err_t init_audio(void) {
	// Initialize PCM5242 DAC via I2C (non-fatal if it fails)
	pcm5242_config_t pcm5242_cfg = {
		.i2c_port = I2C_NUM_0,
		.xsmt_pin = PCM5242_XSMT,
	};
	esp_err_t pcm5242_ret = pcm5242_init(&pcm5242_cfg);
	if (pcm5242_ret != ESP_OK) {
		printf("SPOTIFY_DESK: Warning - PCM5242 init failed (0x%x)\n", pcm5242_ret);
	}
	
	// Initialize I2S for audio output
	audio_i2s_config_t cfg = {
		.port = I2S_NUM_0,
		.mclk_pin = I2S_MCLK,
		.bclk_pin = I2S_BCLK,
		.lrclk_pin = I2S_LRCLK,
		.dout_pin = I2S_DIN,
		.sample_rate_hz = 44100,
	};
	return audio_init(&cfg);
}

static void input_task(void *arg) {
	(void)arg;
	const TickType_t delay = pdMS_TO_TICKS(15);
	gt911_touch_data_t touch_data = {0};
	encoder_event_t enc_event = {0};

	while (true) {
		if (touch_handle) {
			if (gt911_read_touch_points(touch_handle, &touch_data) == ESP_OK && touch_data.num_points > 0) {
				input_event_t evt = {
					.type = INPUT_EVENT_TOUCH,
					.data.touch = {
						.x = touch_data.points[0].x,
						.y = touch_data.points[0].y,
					},
				};
				evt.data.touch.x = evt.data.touch.x >= ILI9488_WIDTH ? ILI9488_WIDTH - 1 : evt.data.touch.x;
				evt.data.touch.y = evt.data.touch.y >= ILI9488_HEIGHT ? ILI9488_HEIGHT - 1 : evt.data.touch.y;
				printf("TOUCH: X=%u, Y=%u\n", evt.data.touch.x, evt.data.touch.y);
				fflush(stdout);
				ESP_LOGI(TAG, "Touch: %u,%u", evt.data.touch.x, evt.data.touch.y);
				xQueueSend(input_queue, &evt, 0);
			}
		}
		vTaskDelay(delay);

		while (encoder_get_event(encoder_handle, &enc_event, 0)) {
			input_event_t evt = {.type = INPUT_EVENT_TOUCH};
			switch (enc_event.type) {
				case ENCODER_EVENT_LEFT:
					evt.type = INPUT_EVENT_ENCODER_LEFT;
					break;
				case ENCODER_EVENT_RIGHT:
					evt.type = INPUT_EVENT_ENCODER_RIGHT;
					break;
				case ENCODER_EVENT_BUTTON:
					evt.type = INPUT_EVENT_ENCODER_BUTTON;
					break;
				default:
					evt.type = INPUT_EVENT_TOUCH;
					break;
			}
			if (evt.type != INPUT_EVENT_TOUCH) {
				xQueueSend(input_queue, &evt, 0);
			}
		}
		vTaskDelay(delay);
	}
}

static void audio_task(void *arg) {
	(void)arg;
	audio_command_t cmd;
	while (true) {
		if (xQueueReceive(audio_queue, &cmd, portMAX_DELAY) == pdPASS) {
			switch (cmd.type) {
				case AUDIO_CMD_BEEP:
					audio_play_beep(880.0f, 120, 0.35f);
					break;
				case AUDIO_CMD_PLAY_WAV:
					ESP_LOGI(TAG, "Playing %s", cmd.path);
					audio_play_wav_file(cmd.path);
					break;
				case AUDIO_CMD_STOP:
					audio_request_stop();
					break;
				default:
					break;
			}
		}
	}
}

static void ui_task(void *arg) {
	(void)arg;
	input_event_t evt;
	ui_draw_boot_screen(&ui_ctx);

	audio_command_t beep = {.type = AUDIO_CMD_BEEP};
	xQueueSend(audio_queue, &beep, portMAX_DELAY);

	while (true) {
		if (xQueueReceive(input_queue, &evt, portMAX_DELAY) != pdPASS) {
			continue;
		}

		switch (evt.type) {
			case INPUT_EVENT_ENCODER_LEFT: {
				uint8_t vol = ui_ctx.volume_percent > 5 ? ui_ctx.volume_percent - 5 : 0;
				ui_set_volume(&ui_ctx, vol);
				break;
			}
			case INPUT_EVENT_ENCODER_RIGHT: {
				uint8_t vol = ui_ctx.volume_percent + 5;
				if (vol > 100) {
					vol = 100;
				}
				ui_set_volume(&ui_ctx, vol);
				break;
			}
			case INPUT_EVENT_ENCODER_BUTTON: {
				bool new_state = !ui_ctx.is_playing;
				ui_set_play_state(&ui_ctx, new_state);
				if (new_state) {
					if (default_track[0] != '\0') {
						audio_command_t play = {
							.type = AUDIO_CMD_PLAY_WAV,
						};
						strncpy(play.path, default_track, sizeof(play.path) - 1);
						play.path[sizeof(play.path) - 1] = '\0';
						xQueueSend(audio_queue, &play, portMAX_DELAY);
						const char *name = default_track_name[0] ? default_track_name : default_track;
						ui_set_track(&ui_ctx, name);
					} else {
						ESP_LOGW(TAG, "No WAV file available");
						ui_set_play_state(&ui_ctx, false);
					}
				} else {
					audio_request_stop();
				}
				break;
			}
			case INPUT_EVENT_TOUCH:
				ESP_LOGI(TAG, "Touch event forwarded to UI");
				break;
			default:
				break;
		}
	}
}

void console_echo_task(void *arg) {
	(void)arg;
	printf("Console Echo Started\n");
	fflush(stdout);
	
	char buffer[256];
	int pos = 0;
	
	// Set stdin to non-blocking mode
	int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
	
	while (1) {
		// Non-blocking read
		int c = getchar();
		if (c != EOF) {
			// Store in buffer
			if (pos < (int)sizeof(buffer) - 1) {
				buffer[pos++] = c;
				if (c == '\n') {
					buffer[pos] = '\0';
					// Print with echo: prefix
					printf("echo: %s", buffer);
					fflush(stdout);
					pos = 0;
				}
			}
		}
		
		// Always yield to prevent watchdog timeout
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

void encoder_monitor_task(void *arg) {
	(void)arg;
	encoder_handle_t *encoder = (encoder_handle_t *)arg;
	encoder_event_t event;
	
	while (1) {
		if (encoder_get_event(encoder, &event, pdMS_TO_TICKS(100))) {
			switch (event.type) {
				case ENCODER_EVENT_LEFT:
					printf("ENCODER: Moving LEFT\n");
					fflush(stdout);
					break;
				case ENCODER_EVENT_RIGHT:
					printf("ENCODER: Moving RIGHT\n");
					fflush(stdout);
					break;
				case ENCODER_EVENT_BUTTON:
					printf("ENCODER: Button pressed\n");
					fflush(stdout);
					break;
				default:
					break;
			}
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

void tone_task(void *arg) {
	(void)arg;
	
	// Play 1kHz tone continuously
	while (1) {
		audio_play_beep(1000.0f, 500, 0.5f);  // 1kHz, 0.5 second, 50% volume
		vTaskDelay(pdMS_TO_TICKS(100));  // Small delay between tones
	}
}

static uint16_t ui_color(uint8_t r, uint8_t g, uint8_t b) {
	return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void ui_draw_char(ui_context_t *ctx, int x, int y, char c, uint8_t scale, uint16_t fg, uint16_t bg) {
	// Simple character drawing - create a filled rectangle for now
	uint8_t char_width = 5 * scale;
	uint8_t char_height = 7 * scale;
	ili9488_fill_color(ctx->display, x, y, char_width, char_height, fg);
}

static void ui_draw_text(ui_context_t *ctx, int x, int y, const char *text, uint8_t scale, uint16_t fg, uint16_t bg) {
	if (!text) {
		return;
	}
	int cursor_x = x;
	for (const char *p = text; *p; ++p) {
		if (*p == '\n') {
			cursor_x = x;
			y += (7 + 2) * scale;
			continue;
		}
		ui_draw_char(ctx, cursor_x, y, *p, scale, fg, bg);
		cursor_x += (5 + 1) * scale;
	}
}

static void scan_i2c_devices(void) {
	num_i2c_devices = 0;
	printf("Scanning I2C bus...\n");
	
	// Scan addresses 0x00 to 0x7F
	for (uint8_t addr = 1; addr < 127; addr++) {
		i2c_cmd_handle_t cmd = i2c_cmd_link_create();
		i2c_master_start(cmd);
		i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
		i2c_master_stop(cmd);
		
		esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
		i2c_cmd_link_delete(cmd);
		
		if (ret == ESP_OK && num_i2c_devices < MAX_I2C_DEVICES) {
			found_i2c_devices[num_i2c_devices] = addr;
			num_i2c_devices++;
			printf("Found I2C device at 0x%02X\n", addr);
		}
	}
	printf("I2C scan complete. Found %d devices.\n", num_i2c_devices);
}

void display_task(void *arg) {
	(void)arg;
	
	// Scan I2C bus on startup
	vTaskDelay(pdMS_TO_TICKS(500));
	scan_i2c_devices();
	
	while (1) {
		// Clear screen with black background
		ili9488_fill_color(&lcd, 0, 0, ILI9488_WIDTH, ILI9488_HEIGHT, 0x0000);
		
		// Display I2C devices
		uint16_t white = ui_color(255, 255, 255);
		uint16_t black = ui_color(0, 0, 0);
		
		char i2c_text[256] = {0};
		snprintf(i2c_text, sizeof(i2c_text), "I2C Devices: %d found", num_i2c_devices);
		
		// Draw title
		ili9488_fill_color(&lcd, 0, 0, ILI9488_WIDTH, 30, white);
		
		// Draw device list
		int y_offset = 50;
		for (int i = 0; i < num_i2c_devices && i < 15; i++) {
			char device_str[32];
			snprintf(device_str, sizeof(device_str), "0x%02X", found_i2c_devices[i]);
			
			// Alternate colors for visibility
			uint16_t color = (i % 2 == 0) ? ui_color(0, 255, 0) : ui_color(0, 255, 255);
			ili9488_fill_color(&lcd, 20, y_offset + (i * 25), 280, 20, color);
			y_offset += 5;
		}
		
		vTaskDelay(pdMS_TO_TICKS(5000));
	}
}

void app_main(void) {
	printf("SPOTIFY_DESK: Starting up\n");
	
	// Initialize SPI bus for display
	printf("SPOTIFY_DESK: Initializing SPI bus\n");
	ESP_ERROR_CHECK(init_spi_bus());
	printf("SPOTIFY_DESK: SPI bus initialized\n");
	
	// Initialize display
	printf("SPOTIFY_DESK: Initializing display\n");
	ESP_ERROR_CHECK(init_display());
	printf("SPOTIFY_DESK: Display initialized\n");
	
	// Initialize I2C bus (required by both touch and audio)
	printf("SPOTIFY_DESK: Initializing I2C bus\n");
	ESP_ERROR_CHECK(init_i2c_bus());
	printf("SPOTIFY_DESK: I2C bus initialized\n");
	
	// Scan I2C devices to diagnose what's on the bus
	scan_i2c_quick();
	
	// Initialize touch (non-fatal if it fails)
	printf("SPOTIFY_DESK: Initializing touch\n");
	esp_err_t touch_ret = init_touch();
	if (touch_ret != ESP_OK) {
		printf("SPOTIFY_DESK: Touch init failed (0x%x), continuing without touch\n", touch_ret);
		touch_handle = NULL;
	} else {
		printf("SPOTIFY_DESK: Touch initialized\n");
	}
	
	// Initialize audio
	printf("SPOTIFY_DESK: Initializing audio\n");
	ESP_ERROR_CHECK(init_audio());
	printf("SPOTIFY_DESK: Audio initialized\n");
	
	// Initialize encoder
	printf("SPOTIFY_DESK: Initializing encoder\n");
	encoder_config_t cfg = {
		.pin_a = ENC_A,
		.pin_b = ENC_B,
		.pin_button = ENC_SW,
		.button_active_level_low = true,
		.debounce_ms = 5,
	};
	ESP_ERROR_CHECK(encoder_init(&encoder_handle, &cfg));
	printf("SPOTIFY_DESK: Encoder initialized\n");
	
	// Create input queue
	input_queue = xQueueCreate(10, sizeof(input_event_t));
	
	// Create tasks for input handling, encoder monitoring, tone playing, and display
	xTaskCreatePinnedToCore(input_task, "input", 2048, NULL, 4, NULL, 0);
	xTaskCreatePinnedToCore(encoder_monitor_task, "encoder_monitor", 2048, (void *)encoder_handle, 5, NULL, 0);
	xTaskCreatePinnedToCore(tone_task, "tone", 2048, NULL, 4, NULL, 1);
	xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 3, NULL, 0);
	
	// Keep main task alive
	while (1) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
