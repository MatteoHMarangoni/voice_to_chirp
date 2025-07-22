#include <Arduino.h>
#include <driver/i2s.h>
#include <FS.h>
#include <SPIFFS.h>

// Hardware Configuration
#define I2S_MIC_NUM     I2S_NUM_0
#define I2S_MIC_BCK     17 //bck
#define I2S_MIC_WS      16
#define I2S_MIC_DATA    15

#define I2S_SPK_NUM     I2S_NUM_1
#define I2S_SPK_BCK     42 //bck
#define I2S_SPK_WS      41 //lrc
#define I2S_SPK_DATA    40 //din

#define BUTTON_REC      4
#define BUTTON_PLAY     5
#define LED_PIN         2

// Audio Settings
#define SAMPLE_RATE     16000 
#define BUFFER_SIZE     1024
#define RECORD_SECONDS  2
#define SAMPLES_TO_RECORD (SAMPLE_RATE * RECORD_SECONDS)

// Global Variables
int16_t audioBuffer[BUFFER_SIZE];
bool isRecording = false;
bool isPlaying = false;

void setupI2SMic() {
    // Simple uninstall - will fail silently if not installed
    i2s_driver_uninstall(I2S_MIC_NUM);

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_MIC_BCK,
        .ws_io_num = I2S_MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_MIC_DATA
    };

    i2s_driver_install(I2S_MIC_NUM, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_MIC_NUM, &pin_config);
}

void setupI2SSpeaker() {
    // Simple uninstall - will fail silently if not installed
    i2s_driver_uninstall(I2S_SPK_NUM);

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SPK_BCK,
        .ws_io_num = I2S_SPK_WS,
        .data_out_num = I2S_SPK_DATA,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_SPK_NUM, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_SPK_NUM, &pin_config);
}

void setup() {
    Serial.begin(115200);
   // while(!Serial); // Wait for serial connection
    
    // Initialize SPIFFS
    if(!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        while(1);
    }

   //  if (SPIFFS.exists("/recording.raw")) {
   //     SPIFFS.remove("/recording.raw");
   //     Serial.println("Old recording deleted at startup.");
   // } 
    setupI2SSpeaker();

            // Play audio with modifications (2x, with gain and speed control)
            if(SPIFFS.exists("/recording.raw")) {
                File file = SPIFFS.open("/recording.raw", FILE_READ);
                if(file) {
                    // Play twice
                    for(int repeat = 0; repeat < 2; repeat++) {
                        file.seek(0); // Rewind file for each repeat
                        
                        float gain = 10;
                        int speedFactor = 3;
                        int16_t tempBuffer[BUFFER_SIZE];
                        
                        while(file.available()) {
                            size_t bytesRead = file.read((uint8_t*)audioBuffer, sizeof(audioBuffer));
                            size_t numSamples = bytesRead / sizeof(int16_t);
                            size_t tempIndex = 0;
                            
                            // Apply gain and speed modification
                            for(size_t i = 0; i < numSamples; i += speedFactor) {
                                tempBuffer[tempIndex++] = (int16_t)(audioBuffer[i] * gain);
                            }
                            
                            // Write modified audio
                            size_t bytesWritten = 0;
                            i2s_write(I2S_SPK_NUM, tempBuffer, tempIndex * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
                        }
                    }
                    file.close();
                } else {
                    //Serial.println("Failed to open file for reading");
                }
            } else {
                //Serial.println("No recording found");
            }

            // Clean up
            i2s_driver_uninstall(I2S_SPK_NUM);

    // Initialize GPIO
    pinMode(BUTTON_REC, INPUT_PULLUP);
    pinMode(BUTTON_PLAY, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    //Serial.println("System Ready");
}

void loop() {
    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 50;

    // Record Button Pressed
if(digitalRead(BUTTON_REC) == LOW && !isRecording && !isPlaying) {
    if(millis() - lastDebounceTime > debounceDelay) {
        lastDebounceTime = millis();
        isRecording = true;
        
        // Setup I2S for recording
        setupI2SMic();

        //Serial.println("Recording started...");
        digitalWrite(LED_PIN, HIGH);

        // Open file in write mode (this will truncate existing file)
        File file = SPIFFS.open("/recording.raw", FILE_WRITE);
        if(file) {
            //Simply opening in write mode truncates the file
            // Now record new audio
            size_t totalBytes = 0;
            while(totalBytes < SAMPLES_TO_RECORD * sizeof(int16_t)) {
                size_t bytesRead = 0;
                i2s_read(I2S_MIC_NUM, audioBuffer, sizeof(audioBuffer), &bytesRead, portMAX_DELAY);
                if(bytesRead > 0) {
                    size_t bytesWritten = file.write((uint8_t*)audioBuffer, bytesRead);
                    if(bytesWritten != bytesRead) {
                        Serial.println("Write error during recording");
                        break;
                    }
                    totalBytes += bytesWritten;
                }
            }
            
            file.close();
            //Serial.printf("Recording complete. Saved %d bytes\n", totalBytes);
        } else {
            //Serial.println("Failed to open file for writing");
        }

        // Clean up
        i2s_driver_uninstall(I2S_MIC_NUM);
        digitalWrite(LED_PIN, LOW);
        isRecording = false;
    }
} 
// Play Button Pressed
    if(digitalRead(BUTTON_PLAY) == LOW && !isPlaying && !isRecording) {
        if(millis() - lastDebounceTime > debounceDelay) {
            lastDebounceTime = millis();
            isPlaying = true;
            //Serial.println("Playback started...");
            //digitalWrite(LED_PIN, HIGH);

            // Setup I2S for playback
            setupI2SSpeaker();

            // Play audio with modifications (2x, with gain and speed control)
            if(SPIFFS.exists("/recording.raw")) {
                File file = SPIFFS.open("/recording.raw", FILE_READ);
                if(file) {
                    // Play twice
                    for(int repeat = 0; repeat < 3; repeat++) {
                        file.seek(0); // Rewind file for each repeat
                        
                        float gain = 5;
                        int speedFactor = 3;
                        int16_t tempBuffer[BUFFER_SIZE];
                        
                        while(file.available()) {
                            size_t bytesRead = file.read((uint8_t*)audioBuffer, sizeof(audioBuffer));
                            size_t numSamples = bytesRead / sizeof(int16_t);
                            size_t tempIndex = 0;
                            
                            // Apply gain and speed modification
                            for(size_t i = 0; i < numSamples; i += speedFactor) {
                                tempBuffer[tempIndex++] = (int16_t)(audioBuffer[i] * gain);
                            }
                            
                            // Write modified audio
                            size_t bytesWritten = 0;
                            i2s_write(I2S_SPK_NUM, tempBuffer, tempIndex * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
                        }
                        
                    }
                    file.close();
                } else {
                    //Serial.println("Failed to open file for reading");
                }
            } else {
                //Serial.println("No recording found");
            }

            // Clean up
            i2s_driver_uninstall(I2S_SPK_NUM);
            //digitalWrite(LED_PIN, LOW);
            isPlaying = false;
            //Serial.println("Playback complete");
        }
    }

    delay(10); // Small delay to reduce CPU usage
}