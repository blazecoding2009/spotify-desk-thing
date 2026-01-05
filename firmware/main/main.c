// spotify desk thing
// by blaze
// this took painful hours to get to work and build. 
// i spent ages making it look pretty too. don't diss me. 

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

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
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "gt911.h"
#include "ili9488.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"
#include "ui.h"

#define TAG "SPOTIFY_DESK"

#ifndef CONFIG_TFT_SPI_SPEED_HZ
#define CONFIG_TFT_SPI_SPEED_HZ (20 * 1000 * 1000)
#endif

#define SCREEN_MOSI GPIO_NUM_28
#define SCREEN_MISO GPIO_NUM_30
#define SCREEN_SCLK GPIO_NUM_29
#define SCREEN_CS GPIO_NUM_33
#define SCREEN_DC GPIO_NUM_34
#define SCREEN_RES GPIO_NUM_35
#define SCREEN_BL GPIO_NUM_2
#define SD_CS GPIO_NUM_10

#define I2C_SDA GPIO_NUM_39
#define I2C_SCL GPIO_NUM_38
#define TOUCH_INT GPIO_NUM_9

#define ENC_A GPIO_NUM_18
#define ENC_B GPIO_NUM_17
#define ENC_SW GPIO_NUM_8

#define I2S_MCLK GPIO_NUM_4
#define I2S_BCLK GPIO_NUM_5
#define I2S_LRCLK GPIO_NUM_6
#define I2S_DOUT GPIO_NUM_7

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
		.backlight_pin = SCREEN_BL,
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

static esp_err_t init_audio(void) {
	audio_i2s_config_t cfg = {
		.port = I2S_NUM_0,
		.mclk_pin = I2S_MCLK,
		.bclk_pin = I2S_BCLK,
		.lrclk_pin = I2S_LRCLK,
		.dout_pin = I2S_DOUT,
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
			if (touch_flag) {
				touch_flag = false;
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
				ESP_LOGI(TAG, "Touch: %u,%u", evt.data.touch.x, evt.data.touch.y);
				xQueueSend(input_queue, &evt, 0);
			}
		}
		}

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

void app_main(void) {
	ESP_LOGI(TAG, "Spotify Desk Thing boot");
	init_nvs();
	ESP_ERROR_CHECK(init_spi_bus());
	ESP_ERROR_CHECK(init_display());
	ESP_ERROR_CHECK(init_touch());
	ESP_ERROR_CHECK(init_encoder());
	ESP_ERROR_CHECK(init_audio());

	input_queue = xQueueCreate(16, sizeof(input_event_t));
	audio_queue = xQueueCreate(8, sizeof(audio_command_t));
	if (!input_queue || !audio_queue) {
		ESP_LOGE(TAG, "Failed to allocate queues");
		return;
	}

	ui_config_t ui_cfg = {
		.display = &lcd,
		.background_color = 0,
		.accent_color = 0,
	};
	ESP_ERROR_CHECK(ui_init(&ui_ctx, &ui_cfg));

	if (mount_sd() == ESP_OK) {
		list_music_files(MUSIC_DIR);
		if (find_first_wav(MUSIC_DIR, default_track, sizeof(default_track))) {
			ESP_LOGI(TAG, "Default track: %s", default_track);
			const char *name = strrchr(default_track, '/');
			name = name ? name + 1 : default_track;
			
			strncpy(default_track_name, name, sizeof(default_track_name) - 1);
			default_track_name[sizeof(default_track_name) - 1] = '\0';
			ui_set_track(&ui_ctx, default_track_name);
		} else {
			ESP_LOGW(TAG, "No WAV files found in %s", MUSIC_DIR);
		}
	}

	xTaskCreatePinnedToCore(ui_task, "ui_task", 4096, NULL, 5, NULL, 1);
	xTaskCreatePinnedToCore(input_task, "input_task", 4096, NULL, 6, NULL, 0);
	xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, NULL, 5, NULL, 0);
}
