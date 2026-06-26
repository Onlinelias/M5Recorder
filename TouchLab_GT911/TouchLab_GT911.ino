#include <M5Unified.h>

static constexpr int MAX_TOUCHES = 5;
static constexpr int MAX_TRACKED_IDS = 16;
static constexpr int NO_VALUE = -1;

struct TouchSample
{
    bool present = false;
    int id = NO_VALUE;
    int screenX = NO_VALUE;
    int screenY = NO_VALUE;
    int rawX = NO_VALUE;
    int rawY = NO_VALUE;
    int size = NO_VALUE;
    int state = NO_VALUE;
    bool wasPressed = false;
    bool wasReleased = false;
    bool wasClicked = false;
    uint32_t holdMs = 0;
    const char *continuity = "NONE";
    uint32_t gapMs = 0;
};

struct TouchTrack
{
    bool used = false;
    bool active = false;
    bool seenThisLoop = false;
    int id = NO_VALUE;
    uint32_t activeSince = 0;
    uint32_t lastSeen = 0;
    uint32_t lastRelease = 0;
};

static TouchTrack tracks[MAX_TRACKED_IDS];
static String lastSerialSnapshot;
static uint32_t lastDisplayMs = 0;

static TouchTrack *findTrack(int id)
{
    for (int i = 0; i < MAX_TRACKED_IDS; ++i)
    {
        if (tracks[i].used && tracks[i].id == id)
        {
            return &tracks[i];
        }
    }

    return nullptr;
}

static TouchTrack *getTrack(int id)
{
    TouchTrack *track = findTrack(id);

    if (track != nullptr)
    {
        return track;
    }

    for (int i = 0; i < MAX_TRACKED_IDS; ++i)
    {
        if (!tracks[i].used)
        {
            tracks[i].used = true;
            tracks[i].id = id;
            return &tracks[i];
        }
    }

    return nullptr;
}

static int findRawIndexById(const lgfx::touch_point_t *raw, int rawCount, int id)
{
    for (int i = 0; i < rawCount; ++i)
    {
        if (raw[i].id == id)
        {
            return i;
        }
    }

    return -1;
}

static void appendValue(String &out, const char *label, int value)
{
    out += label;

    if (value == NO_VALUE)
    {
        out += "-";
    }
    else
    {
        out += value;
    }
}

static void buildSamples(
    TouchSample *samples,
    int &sampleCount,
    lgfx::touch_point_t *raw,
    int rawCount)
{
    uint32_t now = millis();
    int touchCount = M5.Touch.getCount();
    sampleCount = touchCount > rawCount ? touchCount : rawCount;

    if (sampleCount > MAX_TOUCHES)
    {
        sampleCount = MAX_TOUCHES;
    }

    for (int i = 0; i < MAX_TRACKED_IDS; ++i)
    {
        tracks[i].seenThisLoop = false;
    }

    for (int i = 0; i < sampleCount; ++i)
    {
        TouchSample &sample = samples[i];
        sample = TouchSample();
        sample.present = true;

        if (i < rawCount)
        {
            sample.id = raw[i].id;
            sample.rawX = raw[i].x;
            sample.rawY = raw[i].y;
            sample.size = raw[i].size;
        }
        else
        {
            sample.id = i;
        }

        if (i < touchCount)
        {
            auto detail = M5.Touch.getDetail(i);
            sample.screenX = detail.x;
            sample.screenY = detail.y;
            sample.state = detail.state;
            sample.wasPressed = detail.wasPressed();
            sample.wasReleased = detail.wasReleased();
            sample.wasClicked = detail.wasClicked();
        }

        if (i >= rawCount)
        {
            int rawIndex = findRawIndexById(raw, rawCount, sample.id);

            if (rawIndex >= 0)
            {
                sample.rawX = raw[rawIndex].x;
                sample.rawY = raw[rawIndex].y;
                sample.size = raw[rawIndex].size;
            }
        }

        TouchTrack *track = getTrack(sample.id);

        if (track == nullptr)
        {
            sample.continuity = "TRACK_FULL";
            continue;
        }

        track->seenThisLoop = true;

        if (!track->active && track->lastSeen == 0)
        {
            sample.continuity = "NEW TOUCH";
            track->activeSince = now;
        }
        else if (track->active)
        {
            sample.continuity = "CONTINUING TOUCH";
        }
        else
        {
            sample.continuity = "REAPPEARED";
            sample.gapMs = now - track->lastRelease;
            track->activeSince = now;
        }

        sample.holdMs = now - track->activeSince;
        track->active = true;
        track->lastSeen = now;
    }
}

static void appendSample(String &out, const TouchSample &sample)
{
    out += sample.continuity;
    out += " id=";
    out += sample.id;
    appendValue(out, " screenX=", sample.screenX);
    appendValue(out, " screenY=", sample.screenY);
    appendValue(out, " rawX=", sample.rawX);
    appendValue(out, " rawY=", sample.rawY);
    appendValue(out, " size=", sample.size);
    appendValue(out, " state=", sample.state);
    out += " wasPressed=";
    out += sample.wasPressed ? 1 : 0;
    out += " wasReleased=";
    out += sample.wasReleased ? 1 : 0;
    out += " wasClicked=";
    out += sample.wasClicked ? 1 : 0;

    if (sample.gapMs != 0)
    {
        out += " gapMs=";
        out += sample.gapMs;
    }
}

static String buildSnapshot(
    const TouchSample *samples,
    int sampleCount,
    const lgfx::touch_point_t *raw,
    int rawCount)
{
    String out;
    out.reserve(1024);

    out += "count=";
    out += M5.Touch.getCount();
    out += " rawCount=";
    out += rawCount;

    for (int i = 0; i < sampleCount; ++i)
    {
        out += "\n";
        appendSample(out, samples[i]);
    }

    for (int i = 0; i < MAX_TRACKED_IDS; ++i)
    {
        if (tracks[i].used && tracks[i].active && !tracks[i].seenThisLoop)
        {
            out += "\nRELEASE id=";
            out += tracks[i].id;
            out += " heldMs=";
            out += millis() - tracks[i].activeSince;
        }
    }

    for (int i = 0; i < rawCount; ++i)
    {
        out += "\nRAW[";
        out += i;
        out += "] id=";
        out += raw[i].id;
        out += " rawX=";
        out += raw[i].x;
        out += " rawY=";
        out += raw[i].y;
        out += " size=";
        out += raw[i].size;
    }

    return out;
}

static void printIfChanged(
    const String &snapshot,
    const TouchSample *samples,
    int sampleCount,
    const lgfx::touch_point_t *raw,
    int rawCount)
{
    if (snapshot == lastSerialSnapshot)
    {
        return;
    }

    Serial.printf("\nms=%lu touchCount=%d rawCount=%d\n",
                  (unsigned long)millis(),
                  M5.Touch.getCount(),
                  rawCount);

    for (int i = 0; i < sampleCount; ++i)
    {
        String line;
        line.reserve(192);
        appendSample(line, samples[i]);
        Serial.println(line);
    }

    for (int i = 0; i < MAX_TRACKED_IDS; ++i)
    {
        if (tracks[i].used && tracks[i].active && !tracks[i].seenThisLoop)
        {
            Serial.printf("RELEASE id=%d heldMs=%lu\n",
                          tracks[i].id,
                          (unsigned long)(millis() - tracks[i].activeSince));
        }
    }

    Serial.printf("M5.Display.getTouchRaw() count=%d\n", rawCount);

    for (int i = 0; i < rawCount; ++i)
    {
        Serial.printf("RAW[%d] id=%d rawX=%d rawY=%d size=%d\n",
                      i,
                      raw[i].id,
                      raw[i].x,
                      raw[i].y,
                      raw[i].size);
    }

    lastSerialSnapshot = snapshot;
}

static void closeReleasedTracks()
{
    uint32_t now = millis();

    for (int i = 0; i < MAX_TRACKED_IDS; ++i)
    {
        if (tracks[i].used && tracks[i].active && !tracks[i].seenThisLoop)
        {
            tracks[i].active = false;
            tracks[i].lastRelease = now;
        }
    }
}

static void drawValue(int x, int y, const char *label, int value)
{
    M5.Display.setCursor(x, y);
    M5.Display.print(label);

    if (value == NO_VALUE)
    {
        M5.Display.print("-");
    }
    else
    {
        M5.Display.print(value);
    }
}

static void drawDisplay(
    const TouchSample *samples,
    int sampleCount,
    const lgfx::touch_point_t *raw,
    int rawCount)
{
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(4, 4);
    M5.Display.printf("TouchLab GT911  ms %lu\n", (unsigned long)millis());
    M5.Display.printf("Touch Count: %d   Raw Count: %d\n", M5.Touch.getCount(), rawCount);

    int y = 34;

    for (int i = 0; i < sampleCount && y < M5.Display.height() - 42; ++i)
    {
        const TouchSample &sample = samples[i];
        M5.Display.setCursor(4, y);
        M5.Display.printf("ID %d  %s\n", sample.id, sample.continuity);
        y += 12;

        drawValue(4, y, "Screen X:", sample.screenX);
        drawValue(92, y, " Y:", sample.screenY);
        drawValue(160, y, " State:", sample.state);
        y += 12;

        drawValue(4, y, "Raw X:", sample.rawX);
        drawValue(92, y, " Y:", sample.rawY);
        drawValue(160, y, " Size:", sample.size);
        y += 12;

        M5.Display.setCursor(4, y);
        M5.Display.printf("P:%d R:%d C:%d Hold:%lums",
                          sample.wasPressed ? 1 : 0,
                          sample.wasReleased ? 1 : 0,
                          sample.wasClicked ? 1 : 0,
                          (unsigned long)sample.holdMs);

        if (sample.gapMs != 0)
        {
            M5.Display.printf(" Gap:%lums", (unsigned long)sample.gapMs);
        }

        y += 16;
    }

    M5.Display.setCursor(4, M5.Display.height() - 36);
    M5.Display.print("Raw:");

    for (int i = 0; i < rawCount; ++i)
    {
        M5.Display.printf(" [%d]id%d %d,%d sz%d",
                          i,
                          raw[i].id,
                          raw[i].x,
                          raw[i].y,
                          raw[i].size);
    }
}

void setup()
{
    M5.begin();
    Serial.begin(115200);
    delay(200);

    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(4, 4);
    M5.Display.println("TouchLab GT911");
    M5.Display.println("M5Unified touch diagnostics");

    Serial.println("TouchLab_GT911 START");
}

void loop()
{
    M5.update();

    lgfx::touch_point_t raw[MAX_TOUCHES];
    int rawCount = M5.Display.getTouchRaw(raw, MAX_TOUCHES);
    TouchSample samples[MAX_TOUCHES];
    int sampleCount = 0;

    buildSamples(samples, sampleCount, raw, rawCount);

    String snapshot = buildSnapshot(samples, sampleCount, raw, rawCount);
    printIfChanged(snapshot, samples, sampleCount, raw, rawCount);

    if (millis() - lastDisplayMs >= 100)
    {
        lastDisplayMs = millis();
        drawDisplay(samples, sampleCount, raw, rawCount);
    }

    closeReleasedTracks();
}
