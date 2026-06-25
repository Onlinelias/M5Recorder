
/*
RC24 TODO (current investigation status)

DONE
- Recorder engine stable
- WAV writing stable
- Auto-restart eliminated
- RAW touch diagnostics added
- RAW1 vs RAW5 tested
- Ghost touch patterns identified

CURRENT HYPOTHESIS
- GT911 reports persistent phantom contacts after record cycle.
- Raw touch layer never returns to released state.

RC24 EXPERIMENT
- Add diagnostics for VALID touch detection after edge filtering.
- Determine whether real touches ever reach raw layer.

NEXT
- If valid touches appear -> refine raw-touch state machine.
- If no valid touches appear -> GT911 wakeup/reset experiment (RC25).
*/


/*
RC21_GT911_INVESTIGATION

New finding:
- CoreS3 uses GT911 touch controller.
- GT911 driver contains init(), sleep(), wakeup().
- Next goal: identify safe public access path from M5Unified.

No functional changes in this build.
Diagnostic baseline before first GT911 recovery attempt.
*/


/*
RC18_DOCUMENTED_RAW_TOUCH

CONFIRMED STABLE
----------------
- Audio engine
- WAV generation
- SD writing
- 60 second timeout
- Green save screen
- waitForRelease protection

CONFIRMED FALSE
---------------
- Not a recorder bug
- Not a transport bug
- Not a getDetail() bug
- Not a wasPressed()/wasClicked() bug

CONFIRMED TRUE
--------------
- RAW count = 0 before recording
- RAW count = 2 after recording
- Ghost contacts originate at raw touch level
- User touches do not clear ghost contacts
- M5Unified reports raw data correctly

CURRENT INVESTIGATION
---------------------
Why does the touch controller enter a persistent
2-contact state after a recording cycle?
*/


/*
RC17_TOUCH_REINIT_INVESTIGATION

Based on RC16.

NO transport changes.
NO recorder engine changes.
NO waitForRelease changes.

Purpose:
Investigate touch-controller recovery after recording.

Notes from RC16:
- RAW count = 0 before recording.
- RAW count = 2 after recording.
- Ghost contacts exist at raw-touch level.
- Problem is below M5.Touch.getDetail().

Next step:
Search for touch-controller reinitialization/reset options.
This build preserves behavior and marks the investigation branch.
*/


/*
RC15_RAW_TOUCH_DIAGNOSTICS

No behavior changes.
No transport changes.
No recorder changes.

Added diagnostics only:
- Board ID already present.
- Placeholder section for raw touch investigation.

Next test goal:
Compare M5.Touch.getDetail() with raw touch controller data.
*/

// RECORDER 2.0 RC1 WORKING BRANCH
// GOAL: Replace transport with finite state machine.
// ENGINE FROZEN.
// TODO:
// [ ] Remove touchArmed globals/usages
// [ ] Remove lastPressed edge detector
// [ ] Introduce enum UiState {READY,STARTING,RECORDING,SAVED};
// [ ] Single start path using M5Unified getDetail()
// [ ] Keep recorder engine untouched
//
// RC6 PLAN
// Transport simplification target:
// - single M5.update() in loop
// - use M5.Touch.getDetail().wasClicked()
// - remove touchArmed/touchStartMs/manual edge detector
// - preserve recorder engine/audio task/UI
//
// TODO markers inserted below.
#include <M5Unified.h>
#include "M5Module_Audio.h"
#include "SD.h"


// ---- Dual core command architecture (step alpha) ----
enum RecorderCommand { CMD_NONE, CMD_START, CMD_STOP };
volatile RecorderCommand recorderCommand = CMD_NONE;
TaskHandle_t recorderTaskHandle=nullptr;


volatile uint32_t ignoreTouchUntil = 0;
volatile bool readyForTouch = true;
volatile bool wasPressedGlobal = false;

volatile bool transitionBusy = false;
static bool waitForRelease=false;
extern bool isRecording;
static bool diagRawTouchActiveState=false;
static bool diagStopTouchActiveState=false;

static const char *diagCommandName(RecorderCommand cmd)
{
    switch (cmd)
    {
        case CMD_START: return "CMD_START";
        case CMD_STOP: return "CMD_STOP";
        default: return "CMD_NONE";
    }
}

static void diagSetTransitionBusy(bool value, const char *reason, const char *source)
{
    if (transitionBusy != value)
    {
        Serial.printf(
            "STATE transitionBusy=%d reason=%s source=%s ms=%lu\n",
            value,
            reason,
            source,
            millis());
    }

    transitionBusy = value;
}

static void diagSetWaitForRelease(bool value, const char *reason, const char *source)
{
    if (waitForRelease != value)
    {
        Serial.printf(
            "STATE waitForRelease=%d reason=%s source=%s ms=%lu\n",
            value,
            reason,
            source,
            millis());
    }

    waitForRelease = value;
}

static void diagSetRawTouchActive(bool *state, bool value, const char *reason, const char *source)
{
    if (*state != value)
    {
        Serial.printf(
            "STATE rawTouchActive=%d reason=%s source=%s ms=%lu\n",
            value,
            reason,
            source,
            millis());
    }

    *state = value;
    diagRawTouchActiveState = value;
}

static void diagSetStopTouchActive(bool *state, bool value, const char *reason, const char *source)
{
    if (*state != value)
    {
        Serial.printf(
            "STATE stopTouchActive=%d reason=%s source=%s ms=%lu\n",
            value,
            reason,
            source,
            millis());
    }

    *state = value;
    diagStopTouchActiveState = value;
}

static void diagSetRecorderCommand(RecorderCommand value, const char *reason, const char *source)
{
    if (recorderCommand != value)
    {
        Serial.printf(
            "STATE transportCommand=%s reason=%s source=%s ms=%lu\n",
            diagCommandName(value),
            reason,
            source,
            millis());
    }

    recorderCommand = value;
}

static void diagCmdStartRequest(const char *reason, const char *source, bool rawTouchActive, bool validTouch)
{
    Serial.printf(
        "CMD_START reason=%s source=%s ms=%lu transitionBusy=%d isRecording=%d waitForRelease=%d rawTouchActive=%d stopTouchActive=%d validTouch=%d touchCount=%d\n",
        reason,
        source,
        millis(),
        transitionBusy,
        isRecording,
        waitForRelease,
        rawTouchActive,
        diagStopTouchActiveState,
        validTouch,
        M5.Touch.getCount());
}

static void diagCmdStopRequest(const char *reason, const char *source, bool stopTouchActive, bool stopValidTouch)
{
    Serial.printf(
        "CMD_STOP reason=%s source=%s ms=%lu transitionBusy=%d isRecording=%d waitForRelease=%d rawTouchActive=%d stopTouchActive=%d stopValidTouch=%d touchCount=%d\n",
        reason,
        source,
        millis(),
        transitionBusy,
        isRecording,
        waitForRelease,
        diagRawTouchActiveState,
        stopTouchActive,
        stopValidTouch,
        M5.Touch.getCount());
}

enum UIRequest
{
    UI_NONE,
    UI_READY,
    UI_RECORDING,
    UI_SAVED,
    UI_ERROR
};

volatile UIRequest uiRequest = UI_NONE;
uint32_t uiSavedTime = 0;


// ---- end dual core scaffold ----

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
Serial.println("DBG startRecording ENTER");
readyForTouch = false;
isRecording = true;
    currentFilename = getNextFilename();


    SD.remove(currentFilename);


    wavFile =
        SD.open(
            currentFilename,
            FILE_WRITE);

    if (!wavFile)
    {
        M5.Display.fillScreen(RED);
        M5.Display.setCursor(20,20);
        M5.Display.println("FILE ERROR");
        return;
    }


    writeWavHeader(
        wavFile,
        24000,
        0);


    bytesWritten = 0;





    recordStartTime = millis();

         uiRequest = UI_RECORDING;

    Serial.println("DBG startRecording EXIT");
    Serial.println("ENGINE START");
    Serial.println("RECORD START");
}



// --------------------------------------------------
// STOP RECORDING
// --------------------------------------------------

void stopRecording()
{
    Serial.println("DBG stopRecording ENTER");
    

    Serial.printf("ENGINE STOP bytesWritten=%lu\\n",(unsigned long)bytesWritten);

    writeWavHeader(
        wavFile,
        24000,
        bytesWritten);

    wavFile.flush();

    delay(100);

    wavFile.close();

    Serial.println("FILE CLOSED");

    uint32_t sec =
        (millis() - recordStartTime) / 1000;

        (void)sec;

    readyForTouch = false;
    uiRequest = UI_SAVED;
    uiSavedTime = millis();

    ignoreTouchUntil = millis() + 3000;

    Serial.println("DBG stopRecording EXIT");
    Serial.println("RECORD STOP");
    diagSetWaitForRelease(true, "record stopped", "stopRecording");

    isRecording = false;
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------

void RecorderTask(void *pv)
{
  Serial.println("ENGINE TASK STARTED");
  for(;;){
    switch(recorderCommand){
      case CMD_START:
    Serial.println("ENGINE START REQUEST");

    diagSetRecorderCommand(CMD_NONE, "start command consumed", "RecorderTask");

    startRecording();
    diagSetTransitionBusy(false, "start complete", "RecorderTask");

    break;
      case CMD_STOP:
    Serial.println("ENGINE STOP REQUEST");

    diagSetRecorderCommand(CMD_NONE, "stop command consumed", "RecorderTask");

    stopRecording();
    diagSetTransitionBusy(false, "stop complete", "RecorderTask");

    break;
      default: break;
    }
    vTaskDelay(1);
  }
}



void setup()
{
    auto cfg = M5.config();

    cfg.serial_baudrate = 115200;

    M5.begin(cfg);
  xTaskCreatePinnedToCore(RecorderTask,"RecorderTask",4096,NULL,1,&recorderTaskHandle,0);

    Serial.begin(115200);

    M5.Display.setTextSize(2);

    // ---------------------------------
    // SD
    // ---------------------------------

    if (!SD.begin(GPIO_NUM_4)) {

        M5.Display.fillScreen(RED);

        M5.Display.setCursor(20,20);

        M5.Display.println("SD FAIL");

        while(1);
    }

    // ---------------------------------
    // AUDIO
    // ---------------------------------

    if (!device.begin(Wire)) {

        M5.Display.fillScreen(RED);

        M5.Display.setCursor(20,20);

        M5.Display.println("AUDIO FAIL");

        while(1);
    }

    device.setHPMode(AUDIO_HPMODE_NATIONAL);

    device.setMICStatus(AUDIO_MIC_OPEN);

    device.setMicInputLine(
        ADC_INPUT_LINPUT1_RINPUT1
    );

    device.setMicGain(MIC_GAIN_0DB);

    device.setMicAdcVolume(70);

    device.setBitsSample(
        ES_MODULE_ADC_DAC,
        BIT_LENGTH_16BITS
    );

    device.setSampleRate(SAMPLE_RATE_24K);

    // ---------------------------------
    // READY SCREEN
    // ---------------------------------

    M5.Display.fillScreen(BLACK);

    M5.Display.setTextColor(WHITE);

    M5.Display.setCursor(20,20);

    M5.Display.println("READY");

    M5.Display.setCursor(20,60);

    M5.Display.println("TOUCH TO RECORD");
    readyForTouch = true;
}

// --------------------------------------------------
// LOOP
// --------------------------------------------------


void loop()
{
    M5.update();
  static uint32_t dbgLast=0;
  if(!isRecording && readyForTouch && millis()-dbgLast>1000){dbgLast=millis(); auto &td=M5.Touch.getDetail(); Serial.printf("IDLE count=%d state=%d wasP=%d wasC=%d\n",M5.Touch.getCount(),td.state,td.wasPressed(),td.wasClicked()); lgfx::touch_point_t tp[5]; int rawCount=M5.Display.getTouchRaw(tp,5); Serial.printf("RAW count=%d\n",rawCount); for(int i=0;i<rawCount;++i){ Serial.printf("RAW[%d] id=%d x=%d y=%d size=%d\n",i,tp[i].id,tp[i].x,tp[i].y,tp[i].size);} }

    if (transitionBusy) return;
if (uiRequest == UI_RECORDING)
{
    M5.Display.fillScreen(BLACK);

    M5.Display.setTextColor(RED);

    M5.Display.setCursor(20,20);

    M5.Display.println("REC");

    M5.Display.setTextColor(WHITE);

    M5.Display.setCursor(20,60);

    M5.Display.println(currentFilename);

    uiRequest = UI_NONE;
}

if (uiRequest == UI_NONE && !isRecording && !readyForTouch)
{
    if (millis() - uiSavedTime > 2000)
    {
        M5.Display.fillScreen(BLACK);
        M5.Display.setTextColor(WHITE);
        M5.Display.setCursor(20,20);
        M5.Display.println("READY");
        M5.Display.setCursor(20,60);
        M5.Display.println("TOUCH TO RECORD");
        readyForTouch = true;
        Serial.printf("READY ENTER ignore=%ld count=%d state=%d\n",
                      (long)(ignoreTouchUntil - millis()),
                      M5.Touch.getCount(),
                      M5.Touch.getDetail().state);
        // touchArmed assignment removed
    }
}

if (uiRequest == UI_SAVED)
{
    M5.Display.fillScreen(DARKGREEN);

    M5.Display.setTextColor(WHITE);

    M5.Display.setCursor(20,20);

    M5.Display.println("SAVED");

    M5.Display.setCursor(20,60);

    M5.Display.println(currentFilename);

    uint32_t sec =
        (millis() - recordStartTime) / 1000;

    M5.Display.setCursor(20,100);

    M5.Display.printf(
        "%02d:%02d",
        sec / 60,
        sec % 60);

    uiRequest = UI_NONE;
}
    // ---------------------------------
    // START RECORDING
    // ---------------------------------

    if (!isRecording)
{
    auto touch = M5.Touch.getDetail();

    if (M5.Touch.getCount() == 0)
    {
        diagSetWaitForRelease(false, "touch count released while idle", "loop.START");
    }

    if (millis() < ignoreTouchUntil)
    {
        return;
    }



   static bool rawTouchActive = false;
   lgfx::touch_point_t rawtp[5];
   int rawCountStart = M5.Display.getTouchRaw(rawtp, 5);

bool validTouch = false;

for (int i = 0; i < rawCountStart; ++i)
{
    auto &t = rawtp[i];

    bool edgeTouch =
        (t.x < 200) ||
        (t.x > 3000) ||
        (t.y < 200) ||
        (t.y > 3000);

    if (!edgeTouch)
    {
        validTouch = true;

        Serial.printf(
            "VALID TOUCH id=%d x=%d y=%d size=%d\n",
            t.id,
            t.x,
            t.y,
            t.size);

        break;
    }
}

Serial.printf(
    "VALID=%d RAW=%d\n",
    validTouch,
    rawCountStart);

bool rawNow = validTouch;

   if (!rawTouchActive && rawNow)
   {
        diagSetRawTouchActive(&rawTouchActive, true, "valid raw touch detected", "loop.START");
        Serial.println("RAW TOUCH START DETECTED");

        if (recorderCommand == CMD_NONE &&
            readyForTouch)
        {
            Serial.println("LEGACY TRANSPORT REMOVED");
            diagSetTransitionBusy(true, "start request accepted", "loop.START");
            readyForTouch = false;
            Serial.printf("START GATE RAW ready=%d cmd=%d trans=%d rec=%d raw=%d\n", readyForTouch, recorderCommand, transitionBusy, isRecording, rawCountStart);
            diagSetWaitForRelease(true, "start request waiting for release", "loop.START");
            diagCmdStartRequest("valid raw touch start gate", "loop.START", rawTouchActive, validTouch);
            diagSetRecorderCommand(CMD_START, "valid raw touch start gate", "loop.START");
        }
   }

   if (rawTouchActive && !rawNow)
   {
        diagSetRawTouchActive(&rawTouchActive, false, "valid raw touch released", "loop.START");
        Serial.println("RAW TOUCH RELEASE DETECTED");
   }

    

    return;
}


// ---------------------------------
    // POWER BUTTON TEST
    // ---------------------------------

    if (M5.BtnPWR.wasClicked())
    {
        Serial.println("STOP CLICK DETECTED");
    }

    // ---------------------------------
    // AUDIO RECORD
    // ---------------------------------

    bool ok =
        device.record(
            audio_buf,
            sizeof(audio_buf));

    if (!ok)
    {
        Serial.println("RECORD FAIL");
        return;
    }

    // ---------------------------------
    // SD WRITE
    // ---------------------------------

    size_t written =
        wavFile.write(
            audio_buf,
            sizeof(audio_buf));

    if (written == sizeof(audio_buf))
    {
        bytesWritten += written;
    }

    // ---------------------------------
    // TIMER DISPLAY
    // ---------------------------------

    static uint32_t lastSec = 999;

    uint32_t sec =
        (millis() - recordStartTime) / 1000;

    if (sec != lastSec)
    {
        lastSec = sec;

        M5.Display.fillRect(
            20,
            120,
            200,
            40,
            BLACK);

        M5.Display.setCursor(20,120);

        M5.Display.printf(
            "%02d:%02d",
            sec / 60,
            sec % 60);
    }

    // ---------------------------------
    // MANUAL STOP ONLY (RC27)
    // ---------------------------------

    static bool stopTouchActive = false;
    static uint32_t stopArmTime = 0;
    if (recordStartTime != 0 && stopArmTime == 0) stopArmTime = millis() + 1000;

    lgfx::touch_point_t stoptp[5];
    int stopCount = M5.Display.getTouchRaw(stoptp, 5);

    bool stopValidTouch = false;

    for (int i = 0; i < stopCount; ++i)
    {
        auto &t = stoptp[i];

        bool edgeTouch =
            (t.x < 200) ||
            (t.x > 3000) ||
            (t.y < 200) ||
            (t.y > 3000);

        if (!edgeTouch)
        {
            stopValidTouch = true;
            break;
        }
    }

    if (waitForRelease && !stopValidTouch)
    {
        diagSetWaitForRelease(false, "start touch released before stop gate", "loop.STOP");
    }

    if (!waitForRelease && millis() >= stopArmTime && !stopTouchActive && stopValidTouch)
    {
        diagSetStopTouchActive(&stopTouchActive, true, "valid stop touch detected", "loop.STOP");
        Serial.println("STOP TOUCH DETECTED");

        if (recorderCommand == CMD_NONE)
        {
            diagSetTransitionBusy(true, "stop request accepted", "loop.STOP");
            diagCmdStopRequest("valid stop touch stop gate", "loop.STOP", stopTouchActive, stopValidTouch);
            diagSetRecorderCommand(CMD_STOP, "valid stop touch stop gate", "loop.STOP");
        }
    }

    if (stopTouchActive && !stopValidTouch)
    {
        diagSetStopTouchActive(&stopTouchActive, false, "valid stop touch released", "loop.STOP");
    }
}