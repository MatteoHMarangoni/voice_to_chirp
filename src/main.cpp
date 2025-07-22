#include <Arduino.h>
#include <driver/i2s.h>

// Hardware Configuration (pins for I2S microphone and speaker)
#define I2S_MIC_NUM    I2S_NUM_0
#define I2S_MIC_BCK    17  // Bit Clock for I2S microphone
#define I2S_MIC_WS     16  // Word Select (LR clock) for I2S microphone
#define I2S_MIC_DATA   15  // Data in for I2S microphone

#define I2S_SPK_NUM    I2S_NUM_1
#define I2S_SPK_BCK    42  // Bit Clock for I2S speaker
#define I2S_SPK_WS     41  // Word Select (LR clock) for I2S speaker
#define I2S_SPK_DATA   40  // Data out for I2S speaker

#define BUTTON_REC     4   // GPIO for Record button (active LOW)
#define BUTTON_PLAY    5   // GPIO for Play button (active LOW)
#define LED_PIN        2   // On-board LED for status

// Audio Settings
#define SAMPLE_RATE      16000                   // 16 kHz sample rate
#define BUFFER_SIZE      1024                    // I2S read/write buffer chunk size (in samples)
#define RECORD_SECONDS   20                      // Record 10 seconds of audio
#define SAMPLES_TO_RECORD (SAMPLE_RATE * RECORD_SECONDS)

// Global Variables
int16_t *audioBuffer = NULL;                     // Recording buffer (allocated in PSRAM if available)
size_t recordedSamples = 0;                      // Number of samples actually recorded in the buffer
bool isRecording = false;
bool isPlaying   = false;

void setupI2SMic() {
    // Uninstall any existing I2S driver for microphone (to reset configuration)
    i2s_driver_uninstall(I2S_MIC_NUM);
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),   // Master receive mode (microphone)
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,           // Mono input
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
        .ws_io_num  = I2S_MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,  // Not used for RX
        .data_in_num  = I2S_MIC_DATA
    };
    i2s_driver_install(I2S_MIC_NUM, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_MIC_NUM, &pin_config);
}

void setupI2SSpeaker() {
    // Uninstall any existing I2S driver for speaker (to reset configuration)
    i2s_driver_uninstall(I2S_SPK_NUM);
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),   // Master transmit mode (speaker)
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,           // Mono output
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
        .ws_io_num  = I2S_SPK_WS,
        .data_out_num = I2S_SPK_DATA,
        .data_in_num  = I2S_PIN_NO_CHANGE   // Not used for TX
    };
    i2s_driver_install(I2S_SPK_NUM, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_SPK_NUM, &pin_config);
}

void setup() {
    Serial.begin(115200);
    // while(!Serial); // (Optional) wait for serial connection

    // Allocate recording buffer (prefer PSRAM for large buffer)
    size_t bufferSizeBytes = SAMPLES_TO_RECORD * sizeof(int16_t);
    if (psramFound()) {
        audioBuffer = (int16_t*) ps_malloc(bufferSizeBytes);
    } else {
        audioBuffer = (int16_t*) malloc(bufferSizeBytes);
    }
    if (!audioBuffer) {
        Serial.println("Failed to allocate recording buffer");
        while (1) { /* Halt if allocation fails */ }
    }

    // Initialize buttons and LED
    pinMode(BUTTON_REC, INPUT_PULLUP);
    pinMode(BUTTON_PLAY, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    delay(1000);
    digitalWrite(LED_PIN, LOW);

    // Serial.println("System Ready");  // Uncomment for debug indication
}

void loop() {
    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 100;  // Debounce delay in milliseconds

    static int recbuttonenable = 1;   // 1 = allow new recording, 0 = must wait for release

    // --- Record Button Release detection: re-arm for next record ---
    if (digitalRead(BUTTON_REC) == HIGH && !isRecording) {
        recbuttonenable = 1;
        // Serial.println("Record button released, re-armed for next recording");
    }

    // --- Record Button Pressed and armed ---
    if (digitalRead(BUTTON_REC) == LOW && !isRecording && !isPlaying && recbuttonenable == 1) {
        if (millis() - lastDebounceTime > debounceDelay) {
            lastDebounceTime = millis();
            isRecording = true;
            setupI2SMic();                     // Configure I2S for microphone input
            recbuttonenable = 0;               // Block further recordings until released

            // Record audio samples into the buffer for the specified duration
            size_t totalBytes = 0;
            unsigned long recStart = millis();
            digitalWrite(LED_PIN, HIGH);       // Turn on LED to indicate recording

            while (totalBytes < SAMPLES_TO_RECORD * sizeof(int16_t) && digitalRead(BUTTON_REC) == LOW) {
                size_t bytesRead = 0;
                size_t bytesToRead = BUFFER_SIZE * sizeof(int16_t);
                if ((SAMPLES_TO_RECORD * sizeof(int16_t) - totalBytes) < bytesToRead) {
                    bytesToRead = (SAMPLES_TO_RECORD * sizeof(int16_t)) - totalBytes;
                }
                i2s_read(I2S_MIC_NUM, (uint8_t*)audioBuffer + totalBytes, bytesToRead, &bytesRead, portMAX_DELAY);
                if (bytesRead > 0) {
                    totalBytes += bytesRead;
                } else {
                    break;
                }
                // Optional: Also break if max time exceeded (as backup to buffer full)
                if (millis() - recStart > RECORD_SECONDS * 1000) break;
            }
            recordedSamples = totalBytes / sizeof(int16_t);  // Calculate number of samples recorded

            // Stop I2S and finish recording
            i2s_driver_uninstall(I2S_MIC_NUM);
            digitalWrite(LED_PIN, LOW);
            isRecording = false;
            // Serial.println("Recording complete");  // Debug log (optional)
        }
    }

    // --- Play Button Pressed ---
    if (digitalRead(BUTTON_PLAY) == LOW && !isPlaying && !isRecording) {
        if (millis() - lastDebounceTime > debounceDelay) {
            lastDebounceTime = millis();
            isPlaying = true;
            setupI2SSpeaker();  // Configure I2S for speaker output

            // Play audio from the buffer with gain and speed modifications
         if (recordedSamples > 0) {
                float gain = 5.0f;
                int speedFactor = 3;
                int16_t tempBuffer[BUFFER_SIZE];
                const size_t nSamplesToPlay = recordedSamples;
                for (int repeat = 0; repeat < 2; repeat++) {
                    Serial.print("Playback repeat "); Serial.println(repeat+1);
                    size_t offset = 0;
                    while (offset < nSamplesToPlay) {
                        size_t chunkSamples = BUFFER_SIZE;
                        if (offset + chunkSamples > nSamplesToPlay) {
                            chunkSamples = nSamplesToPlay - offset;
                        }
                        size_t tempIndex = 0;
                        for (size_t i = 0; i < chunkSamples; i += speedFactor) {
                            long amplified = (long)audioBuffer[offset + i] * (long)gain;
                            if (amplified > 32767) amplified = 32767;
                            if (amplified < -32768) amplified = -32768;
                            tempBuffer[tempIndex++] = (int16_t)amplified;
                        }
                        if (tempIndex == 0 && chunkSamples > 0) {
                            long amplified = (long)audioBuffer[offset + chunkSamples - 1] * (long)gain;
                            if (amplified > 32767) amplified = 32767;
                            if (amplified < -32768) amplified = -32768;
                            tempBuffer[tempIndex++] = (int16_t)amplified;
                        }
                        size_t bytesWritten = 0;
                        i2s_write(I2S_SPK_NUM, (const char*)tempBuffer, tempIndex * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
                        offset += chunkSamples;
                    }
                    Serial.println("End of repeat.");
                }
            }


            i2s_driver_uninstall(I2S_SPK_NUM);
            isPlaying = false;
        }
    }

    delay(10);  // Small delay to debounce and reduce CPU usage
}
