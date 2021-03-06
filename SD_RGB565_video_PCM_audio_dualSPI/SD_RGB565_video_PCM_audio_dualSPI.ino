#define AUDIO_FILENAME "/48000_u16le.pcm"
#define VIDEO_WIDTH 220L
#define VIDEO_HEIGHT 132L
#define FPS 10
#define VIDEO_FILENAME "/220_10fps.rgb"

#define SD_SPIHOST HSPI
#define SD_MISO 12
#define SD_MOSI 13
#define SD_SCK 14
#define SD_CS 15

#define TFT_SPIHOST VSPI
#define TFT_MISO -1
#define TFT_MOSI 23
#define TFT_SCK 18
#define TFT_CS 5
#define TFT_DC 27
#define TFT_RST 33
#define TFT_BL 22

#include <WiFi.h>
#include <FS.h>
#include <SD.h>
#include <driver/i2s.h>
#include <Arduino_ESP32SPI.h>
#include <Arduino_Display.h>
Arduino_ESP32SPI *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO, TFT_SPIHOST);
Arduino_ILI9225 *gfx = new Arduino_ILI9225(bus, TFT_RST, 3 /* rotation */);

void setup()
{
  WiFi.mode(WIFI_OFF);
  Serial.begin(115200);

  // Init Video
  gfx->begin();
  gfx->fillScreen(BLACK);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

  // Init SD card
  SPIClass spi = SPIClass(SD_SPIHOST);
  spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, spi, 80000000))
  {
    Serial.println(F("ERROR: Card Mount Failed!"));
    gfx->println(F("ERROR: Card Mount Failed!"));
  }
  else
  {
    // Init Audio
    i2s_config_t i2s_config_dac = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
        .sample_rate = 48000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_PCM | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // lowest interrupt priority
        .dma_buf_count = 7,
        .dma_buf_len = 800,
        .use_apll = false,
    };
    Serial.printf("%p\n", &i2s_config_dac);
    if (i2s_driver_install(I2S_NUM_0, &i2s_config_dac, 0, NULL) != ESP_OK)
    {
      Serial.println(F("ERROR: Unable to install I2S drives!"));
      gfx->println(F("ERROR: Unable to install I2S drives!"));
    }
    else
    {
      i2s_set_pin((i2s_port_t)0, NULL);
      i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);
      i2s_zero_dma_buffer((i2s_port_t)0);

      File aFile = SD.open(AUDIO_FILENAME);
      if (!aFile || aFile.isDirectory())
      {
        Serial.println(F("ERROR: Failed to open audio file for reading!"));
        gfx->println(F("ERROR: Failed to open audio file for reading!"));
      }
      else
      {
        File vFile = SD.open(VIDEO_FILENAME);
        if (!vFile || vFile.isDirectory())
        {
          Serial.println(F("ERROR: Failed to open video file for reading"));
          gfx->println(F("ERROR: Failed to open video file for reading"));
        }
        else
        {
          uint8_t *buf = (uint8_t *)malloc(VIDEO_WIDTH * VIDEO_HEIGHT * 2);

          Serial.println(F("Start audio video"));
          gfx->setAddrWindow((gfx->width() - VIDEO_WIDTH) / 2, (gfx->height() - VIDEO_HEIGHT) / 2, VIDEO_WIDTH, VIDEO_HEIGHT);
          int next_frame = 0;
          int skipped_frames = 0;
          int total_remain = 0;
          unsigned long start_ms = millis();
          unsigned long next_frame_ms = start_ms + (++next_frame * 1000 / FPS);
          while (vFile.available() && aFile.available())
          {
            // Dump audio
            aFile.read(buf, 9600);
            i2s_write_bytes((i2s_port_t)0, (char *)buf, 1600, 0);
            i2s_write_bytes((i2s_port_t)0, (char *)(buf + 1600), 1600, 0);
            i2s_write_bytes((i2s_port_t)0, (char *)(buf + 3200), 1600, 0);
            i2s_write_bytes((i2s_port_t)0, (char *)(buf + 4800), 1600, 0);
            i2s_write_bytes((i2s_port_t)0, (char *)(buf + 6400), 1600, 0);
            i2s_write_bytes((i2s_port_t)0, (char *)(buf + 8000), 1600, 0);

            // Dump video
            uint32_t l = vFile.read(buf, VIDEO_WIDTH * VIDEO_HEIGHT * 2);
            if (millis() < next_frame_ms) // check show frame or skip frame
            {
              gfx->startWrite();
              gfx->writePixels((uint16_t *)buf, l >> 1);
              gfx->endWrite();
              int remain_ms = next_frame_ms - millis();
              if (remain_ms)
              {
                total_remain += remain_ms;
                delay(remain_ms);
              }
            }
            else
            {
              ++skipped_frames;
              Serial.println(F("Skip frame"));
            }

            next_frame_ms = start_ms + (++next_frame * 1000 / FPS);
          }
          int time_used = millis() - start_ms;
          int played_frames = next_frame - 1 - skipped_frames;
          float fps = 1000.0 * played_frames / time_used;
          Serial.println(F("End audio video"));
          Serial.printf("Played frame: %d\nSkipped frames: %d (%f %%)\nTime used: %d ms\nRemain: %d ms (%f %%)\nExpected FPS: %d\nActual FPS: %f\n", played_frames, skipped_frames, 100.0 * skipped_frames / played_frames, time_used, total_remain, 100.0 * total_remain / time_used, FPS, fps);
          gfx->setCursor(0, 0);
          gfx->setTextColor(WHITE, BLACK);
          gfx->printf("Played frame: %d\nSkipped frames: %d (%f %%)\nTime used: %d ms\nRemain: %d ms (%f %%)\nExpected FPS: %d\nActual FPS: %f\n", played_frames, skipped_frames, 100.0 * skipped_frames / played_frames, time_used, total_remain, 100.0 * total_remain / time_used, FPS, fps);

          i2s_driver_uninstall((i2s_port_t)0); //stop & destroy i2s driver

#ifdef TFT_BL
          delay(60000);
          digitalWrite(TFT_BL, LOW);
#endif
        }
      }
    }
  }
}

void loop(void)
{
}
