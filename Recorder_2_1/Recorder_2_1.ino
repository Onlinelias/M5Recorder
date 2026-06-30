// M5Recorder 2.1
// Clean transport/UI rebuild; recorder engine preserved.

#include <M5Unified.h>
#include "M5Module_Audio.h"
#include "SD.h"

// --------------------------------------------------
// COMMAND / APP STATE
// --------------------------------------------------

enum RecorderCommand { CMD_NONE, CMD_START, CMD_STOP };
volatile RecorderCommand recorderCommand = CMD_NONE;
TaskHandle_t recorderTaskHandle = nullptr;
volatile bool transitionBusy = false;

enum AppState
{
    APP_READY,
    APP_STARTING,
    APP_RECORDING,
    APP_STOPPING,
    APP_SAVED,
    APP_ERROR
};

AppState appState = APP_READY;
AppState lastLoggedState = APP_ERROR;
uint32_t savedShownMs = 0;
uint32_t savedDurationSec = 0;
uint32_t lastDisplayedSec = 0xFFFFFFFF;

static const uint8_t transportButtonPin = 8;  // CoreS3 Port B white wire / PB IN / GPIO8.
static const uint8_t transportButtonLedPin = 9;  // CoreS3 Port B yellow wire / PB OUT / GPIO9, reserved for Unit Key LED.
static const uint32_t transportButtonDebounceMs = 60;
static bool transportButtonRawPressed = false;
static bool transportButtonStablePressed = false;
static uint32_t transportButtonChangedMs = 0;

static bool transportTouchArmed = true;
static uint8_t transportTouchClearSamples = 0;
static const uint8_t transportTouchClearTarget = 12;
static const bool touchDiagnosticsEnabled = false;
static const bool transportDiagnosticsEnabled = false;
static bool transportTouchConsumed = true;
static uint32_t lastRawTouchDiagnosticMs = 0;
static uint32_t lastRawTouchDiagnosticHash = 0xFFFFFFFF;

static void resetTransportTouchLatch(bool allowNewTouch);
static void handleSerialTransport();
static void handleButtonTransport();

M5ModuleAudio device;

// --------------------------------------------------
// AUDIO BUFFER
// --------------------------------------------------

static uint8_t audio_buf[4096];
File wavFile;

// --------------------------------------------------
// RECORD STATE
// --------------------------------------------------

bool isRecording = false;
uint32_t recordStartTime = 0;
uint32_t bytesWritten = 0;
String currentFilename;

// --------------------------------------------------
// FILE COUNTER
// --------------------------------------------------

int fileCounter = 1;

static const char *appStateName(AppState state)
{
    switch (state)
    {
    case APP_READY: return "READY";
    case APP_STARTING: return "STARTING";
    case APP_RECORDING: return "RECORDING";
    case APP_STOPPING: return "STOPPING";
    case APP_SAVED: return "SAVED";
    case APP_ERROR: return "ERROR";
    }

    return "UNKNOWN";
}

static bool shouldLogState(AppState state)
{
    return state == APP_READY ||
           state == APP_RECORDING ||
           state == APP_SAVED ||
           state == APP_ERROR;
}

static void setAppState(AppState state)
{
    appState = state;

    if (state != lastLoggedState && shouldLogState(state))
    {
        Serial.printf("STATE %s\n", appStateName(state));
        lastLoggedState = state;
    }
}

static void drawReadyScreen()
{
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.setCursor(20, 20);
    M5.Display.println("READY");

    int zoneTop = M5.Display.height() - 80;
    M5.Display.fillRect(0, zoneTop, M5.Display.width(), 80, DARKGREEN);
    M5.Display.setTextColor(WHITE, DARKGREEN);
    M5.Display.setCursor(20, zoneTop + 25);
    M5.Display.println("BUTTON / SERIAL S");
}

static void drawRecordingScreen()
{
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(RED, BLACK);
    M5.Display.setCursor(20, 20);
    M5.Display.println("REC");
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.setCursor(20, 60);
    M5.Display.println(currentFilename);

    int zoneTop = M5.Display.height() - 80;
    M5.Display.fillRect(0, zoneTop, M5.Display.width(), 80, MAROON);
    M5.Display.setTextColor(WHITE, MAROON);
    M5.Display.setCursor(20, zoneTop + 25);
    M5.Display.println("BUTTON / SERIAL X");

    lastDisplayedSec = 0xFFFFFFFF;
}

static void drawRecordingTimer()
{
    uint32_t sec = (millis() - recordStartTime) / 1000;

    if (sec == lastDisplayedSec)
    {
        return;
    }

    lastDisplayedSec = sec;
    M5.Display.fillRect(20, 120, 200, 40, BLACK);
    M5.Display.setCursor(20, 120);
    M5.Display.printf("%02lu:%02lu", sec / 60, sec % 60);
}

static void drawSavedScreen()
{
    M5.Display.fillScreen(DARKGREEN);
    M5.Display.setTextColor(WHITE, DARKGREEN);
    M5.Display.setCursor(20, 20);
    M5.Display.println("SAVED");
    M5.Display.setCursor(20, 60);
    M5.Display.println(currentFilename);
    M5.Display.setCursor(20, 100);
    M5.Display.printf("%02lu:%02lu", savedDurationSec / 60, savedDurationSec % 60);
}

static void drawErrorScreen(const char *message)
{
    M5.Display.fillScreen(RED);
    M5.Display.setTextColor(WHITE, RED);
    M5.Display.setCursor(20, 20);
    M5.Display.println(message);
    setAppState(APP_ERROR);
}

// --------------------------------------------------
// WAV HEADER
// --------------------------------------------------

void writeWavHeader(
    File file,
    uint32_t sampleRate,
    uint32_t dataSize)
{
    uint32_t fileSize = dataSize + 36;

    uint32_t currentPos = file.position();

    file.seek(0);

    file.write((const uint8_t*)"RIFF", 4);
    file.write((uint8_t*)&fileSize, 4);
    file.write((const uint8_t*)"WAVE", 4);

    file.write((const uint8_t*)"fmt ", 4);

    uint32_t subChunk1Size = 16;

    uint16_t audioFormat = 1;

    uint16_t numChannels = 2;

    uint16_t bitsPerSample = 16;

    uint32_t byteRate =
        sampleRate *
        numChannels *
        bitsPerSample / 8;

    uint16_t blockAlign =
        numChannels *
        bitsPerSample / 8;

    file.write((uint8_t*)&subChunk1Size, 4);
    file.write((uint8_t*)&audioFormat, 2);
    file.write((uint8_t*)&numChannels, 2);
    file.write((uint8_t*)&sampleRate, 4);
    file.write((uint8_t*)&byteRate, 4);
    file.write((uint8_t*)&blockAlign, 2);
    file.write((uint8_t*)&bitsPerSample, 2);

    file.write((const uint8_t*)"data", 4);
    file.write((uint8_t*)&dataSize, 4);

    file.seek(currentPos);
}

// --------------------------------------------------
// NEXT FILE NAME
// --------------------------------------------------

String getNextFilename()
{
    while (true) {

        char filename[32];

        sprintf(
            filename,
            "/REC%04d.wav",
            fileCounter);

        fileCounter++;

        if (!SD.exists(filename)) {

            return String(filename);
        }
    }
}

// --------------------------------------------------
// START RECORDING
// --------------------------------------------------

void startRecording()
{
    isRecording = true;
    currentFilename = getNextFilename();

    SD.remove(currentFilename);

    wavFile =
        SD.open(
            currentFilename,
            FILE_WRITE);

    if (!wavFile)
    {
        drawErrorScreen("FILE ERROR");
        isRecording = false;
        return;
    }

    writeWavHeader(
        wavFile,
        24000,
        0);

    bytesWritten = 0;
    recordStartTime = millis();

    Serial.println("ENGINE START");
    Serial.println("RECORD START");
}

// --------------------------------------------------
// STOP RECORDING
// --------------------------------------------------

void stopRecording()
{
    Serial.println("ENGINE STOP");

    writeWavHeader(
        wavFile,
        24000,
        bytesWritten);

    wavFile.flush();

    delay(100);

    wavFile.close();

    Serial.println("FILE CLOSED");

    savedDurationSec = (millis() - recordStartTime) / 1000;

    Serial.println("RECORD STOP");

    isRecording = false;
}

// --------------------------------------------------
// RECORDER TASK
// --------------------------------------------------

void RecorderTask(void *pv)
{
    for (;;)
    {
        switch (recorderCommand)
        {
        case CMD_START:
            recorderCommand = CMD_NONE;
            startRecording();
            transitionBusy = false;
            break;

        case CMD_STOP:
            recorderCommand = CMD_NONE;
            stopRecording();
            transitionBusy = false;
            break;

        default:
            break;
        }

        vTaskDelay(1);
    }
}

static void requestStart(const char *inputLabel)
{
    if (appState != APP_READY || recorderCommand != CMD_NONE || transitionBusy)
    {
        return;
    }

    Serial.println(inputLabel);
    transitionBusy = true;
    setAppState(APP_STARTING);
    recorderCommand = CMD_START;
    Serial.println("CMD_START");
}

static void requestStop(const char *inputLabel)
{
    if (appState != APP_RECORDING || recorderCommand != CMD_NONE || transitionBusy)
    {
        return;
    }

    if (inputLabel != nullptr)
    {
        Serial.println(inputLabel);
    }

    transitionBusy = true;
    setAppState(APP_STOPPING);
    recorderCommand = CMD_STOP;
    Serial.println("CMD_STOP");
}

static void handleSerialTransport()
{
    while (Serial.available() > 0)
    {
        char c = static_cast<char>(Serial.read());
        if (c == 's' || c == 'S')
        {
            requestStart("INPUT SERIAL_START");
        }
        else if (c == 'x' || c == 'X')
        {
            requestStop("INPUT SERIAL_STOP");
        }
    }
}

static bool transportButtonPressed()
{
    bool rawPressed = digitalRead(transportButtonPin) == LOW;
    uint32_t nowMs = millis();

    if (rawPressed != transportButtonRawPressed)
    {
        transportButtonRawPressed = rawPressed;
        transportButtonChangedMs = nowMs;
    }

    if (nowMs - transportButtonChangedMs < transportButtonDebounceMs)
    {
        return false;
    }

    if (rawPressed != transportButtonStablePressed)
    {
        transportButtonStablePressed = rawPressed;
        return transportButtonStablePressed;
    }

    return false;
}

static void handleButtonTransport()
{
    if (!transportButtonPressed())
    {
        return;
    }

    if (appState == APP_READY)
    {
        requestStart("INPUT BUTTON_START");
    }
    else if (appState == APP_RECORDING)
    {
        requestStop("INPUT BUTTON_STOP");
    }
}

static void recordAudioChunk()
{
    bool ok =
        device.record(
            audio_buf,
            sizeof(audio_buf));

    if (!ok)
    {
        return;
    }

    size_t written =
        wavFile.write(
            audio_buf,
            sizeof(audio_buf));

    if (written == sizeof(audio_buf))
    {
        bytesWritten += written;
    }
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------

void setup()
{
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;

    M5.begin(cfg);
    Serial.begin(115200);
    Serial.println("BUILD Recorder_2_1");
    Serial.printf("BUTTON GPIO: %u\n", transportButtonPin);

    xTaskCreatePinnedToCore(RecorderTask, "RecorderTask", 4096, NULL, 1, &recorderTaskHandle, 0);

    M5.Display.setTextSize(2);

    pinMode(transportButtonPin, INPUT_PULLUP);
    pinMode(transportButtonLedPin, OUTPUT);
    digitalWrite(transportButtonLedPin, LOW);
    transportButtonRawPressed = digitalRead(transportButtonPin) == LOW;
    transportButtonStablePressed = transportButtonRawPressed;
    transportButtonChangedMs = millis();

    if (!SD.begin(GPIO_NUM_4))
    {
        drawErrorScreen("SD FAIL");
        while (1);
    }

    if (!device.begin(Wire))
    {
        drawErrorScreen("AUDIO FAIL");
        while (1);
    }

    device.setHPMode(AUDIO_HPMODE_NATIONAL);
    device.setMICStatus(AUDIO_MIC_OPEN);
    device.setMicInputLine(ADC_INPUT_LINPUT1_RINPUT1);
    device.setMicGain(MIC_GAIN_0DB);
    device.setMicAdcVolume(70);
    device.setBitsSample(ES_MODULE_ADC_DAC, BIT_LENGTH_16BITS);
    device.setSampleRate(SAMPLE_RATE_24K);

    drawReadyScreen();
    setAppState(APP_READY);
    resetTransportTouchLatch(false);
}


static const char *transportTouchRejectReason(const m5::touch_detail_t &touch)
{
    if (touch.x < 0 || touch.y < 0)
    {
        return "negative";
    }

    // Known GT911/M5Unified ghost points from Recorder_2_1 diagnostics.
    if (touch.x == 0 && touch.y == 0)
    {
        return "zero";
    }

    if (touch.x < 100)
    {
        return "x_low";
    }

    if (touch.y < 20)
    {
        return "y_low";
    }

    if (touch.x > 3300)
    {
        return "x_high";
    }

    if (touch.y > 3000)
    {
        return "y_high";
    }

    return nullptr;
}

static bool isTransportTouchCandidate(const m5::touch_detail_t &touch)
{
    return transportTouchRejectReason(touch) == nullptr;
}

static bool hasTransportTouchCandidate()
{
    uint8_t count = M5.Touch.getCount();

    for (uint8_t i = 0; i < count; ++i)
    {
        auto touch = M5.Touch.getDetail(i);
        if (isTransportTouchCandidate(touch))
        {
            return true;
        }
    }

    return false;
}

static void logRawTransportTouchDiagnostics()
{
    if (!transportDiagnosticsEnabled)
    {
        return;
    }

    uint8_t count = M5.Touch.getCount();
    if (count == 0)
    {
        return;
    }

    uint32_t hash = count;
    for (uint8_t i = 0; i < count; ++i)
    {
        auto touch = M5.Touch.getDetail(i);
        hash = hash * 131UL + static_cast<uint8_t>(touch.state);
        hash = hash * 131UL + static_cast<uint16_t>(touch.x);
        hash = hash * 131UL + static_cast<uint16_t>(touch.y);
    }

    uint32_t nowMs = millis();
    if (hash == lastRawTouchDiagnosticHash && nowMs - lastRawTouchDiagnosticMs < 250)
    {
        return;
    }

    lastRawTouchDiagnosticHash = hash;
    lastRawTouchDiagnosticMs = nowMs;

    Serial.printf("TOUCH_RAW app=%s count=%u armed=%d consumed=%d\n",
                  appStateName(appState),
                  count,
                  transportTouchArmed ? 1 : 0,
                  transportTouchConsumed ? 1 : 0);

    uint8_t limit = count < 4 ? count : 4;
    for (uint8_t i = 0; i < limit; ++i)
    {
        auto touch = M5.Touch.getDetail(i);
        const char *rejectReason = transportTouchRejectReason(touch);
        Serial.printf("  raw index=%u state=%d x=%d y=%d reject=%s pressed=%d clicked=%d released=%d\n",
                      i,
                      static_cast<int>(touch.state),
                      touch.x,
                      touch.y,
                      rejectReason == nullptr ? "none" : rejectReason,
                      touch.wasPressed() ? 1 : 0,
                      touch.wasClicked() ? 1 : 0,
                      touch.wasReleased() ? 1 : 0);
    }
}
static void resetTransportTouchLatch(bool allowNewTouch)
{
    transportTouchArmed = false;
    transportTouchClearSamples = 0;
    transportTouchConsumed = !allowNewTouch;
}

static void logTouchScanDiagnostics()
{
    if (!touchDiagnosticsEnabled)
    {
        return;
    }
    static uint32_t lastHash = 0xFFFFFFFF;
    uint8_t count = M5.Touch.getCount();
    uint32_t hash = count;

    for (uint8_t i = 0; i < count; ++i)
    {
        auto touch = M5.Touch.getDetail(i);
        hash = hash * 131UL + static_cast<uint16_t>(touch.x);
        hash = hash * 131UL + static_cast<uint16_t>(touch.y);
        hash = hash * 131UL + static_cast<uint8_t>(touch.state);
    }

    if (hash == lastHash)
    {
        return;
    }

    lastHash = hash;
    Serial.printf("TOUCH_SCAN count=%u\n", count);

    for (uint8_t i = 0; i < count; ++i)
    {
        auto touch = M5.Touch.getDetail(i);
        Serial.printf("  index=%u state=%d x=%d y=%d candidate=%d pressed=%d clicked=%d released=%d\n",
                      i,
                      static_cast<int>(touch.state),
                      touch.x,
                      touch.y,
                      isTransportTouchCandidate(touch) ? 1 : 0,
                      touch.wasPressed() ? 1 : 0,
                      touch.wasClicked() ? 1 : 0,
                      touch.wasReleased() ? 1 : 0);
    }
}
static bool transportTouchBegan()
{
    logRawTransportTouchDiagnostics();
    if (transportTouchConsumed)
    {
        return false;
    }

    if (!hasTransportTouchCandidate())
    {
        if (transportTouchClearSamples < transportTouchClearTarget)
        {
            transportTouchClearSamples++;
        }

        if (!transportTouchArmed && transportTouchClearSamples >= transportTouchClearTarget)
        {
            transportTouchArmed = true;
            Serial.println("TOUCH_CLEARED");
        }

        return false;
    }

    transportTouchClearSamples = 0;

    if (!transportTouchArmed)
    {
        return false;
    }

    uint8_t count = M5.Touch.getCount();
    for (uint8_t i = 0; i < count; ++i)
    {
        auto touch = M5.Touch.getDetail(i);
        if (touch.state != m5::touch_state_t::touch_begin)
        {
            continue;
        }

        const char *rejectReason = transportTouchRejectReason(touch);
        if (transportDiagnosticsEnabled)
        {
            Serial.printf("TOUCH_BEGIN app=%s index=%u state=%d x=%d y=%d count=%u reject=%s armed=%d consumed=%d\n",
                          appStateName(appState),
                          i,
                          static_cast<int>(touch.state),
                          touch.x,
                          touch.y,
                          count,
                          rejectReason == nullptr ? "none" : rejectReason,
                          transportTouchArmed ? 1 : 0,
                          transportTouchConsumed ? 1 : 0);
        }

        if (rejectReason != nullptr)
        {
            continue;
        }

        Serial.printf("TOUCH_ACCEPT app=%s index=%u x=%d y=%d count=%u\n", appStateName(appState), i, touch.x, touch.y, count);
        transportTouchConsumed = true;
        transportTouchArmed = false;
        transportTouchClearSamples = 0;
        return true;
    }

    return false;
}
// --------------------------------------------------
// LOOP
// --------------------------------------------------

void loop()
{
    M5.update();
    handleSerialTransport();
    handleButtonTransport();
    logTouchScanDiagnostics();

    switch (appState)
    {
    case APP_READY:
    {
        break;
    }

    case APP_STARTING:
        if (!transitionBusy && isRecording)
        {
            drawRecordingScreen();
            setAppState(APP_RECORDING);
            resetTransportTouchLatch(false);
        }
        break;

    case APP_RECORDING:
        if (millis() - recordStartTime >= 60000)
        {
            requestStop("INPUT AUTO_STOP");
            break;
        }

        recordAudioChunk();
        drawRecordingTimer();
        break;

    case APP_STOPPING:
        if (!transitionBusy && !isRecording)
        {
            drawSavedScreen();
            savedShownMs = millis();
            setAppState(APP_SAVED);
        }
        break;

    case APP_SAVED:
        if (millis() - savedShownMs >= 2000)
        {
            drawReadyScreen();
            setAppState(APP_READY);
            resetTransportTouchLatch(false);
        }
        break;

    case APP_ERROR:
        break;
    }
}
