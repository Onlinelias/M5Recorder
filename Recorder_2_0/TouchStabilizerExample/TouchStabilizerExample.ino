#include <M5Unified.h>

#include "../TouchStabilizer.h"
#include "../TouchStabilizer.cpp"

static TouchStabilizer::Config makeTouchConfig()
{
    TouchStabilizer::Config config;
    config.stablePressMs = 0;
    config.stableReleaseMs = 120;
    config.stableReleaseSamples = 2;
    return config;
}

TouchStabilizer touchStabilizer(makeTouchConfig());

static TouchStabilizer::RawTouch readRawTouch()
{
    lgfx::touch_point_t points[5];
    int count = M5.Display.getTouchRaw(points, 5);

    if (count <= 0)
    {
        return TouchStabilizer::RawTouch();
    }

    TouchStabilizer::RawTouch raw;
    raw.active = true;
    raw.id = points[0].id;
    raw.x = points[0].x;
    raw.y = points[0].y;
    raw.size = points[0].size;
    return raw;
}

static void drawStatus(
    const TouchStabilizer::RawTouch &raw,
    TouchStabilizer::Event event)
{
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(8, 8);
    M5.Display.println("TouchStabilizer Example");
    M5.Display.printf("State: %s\n", TouchStabilizer::stateName(touchStabilizer.state()));
    M5.Display.printf("Event: %s\n", TouchStabilizer::eventName(event));
    M5.Display.printf("Raw active: %d\n", raw.active ? 1 : 0);
    M5.Display.printf("ID: %d\n", raw.id);
    M5.Display.printf("Raw X/Y: %d / %d\n", raw.x, raw.y);
    M5.Display.printf("Size: %d\n", raw.size);
    M5.Display.printf("Hold: %lums\n", (unsigned long)touchStabilizer.holdMs(millis()));
}

void setup()
{
    M5.begin();
    Serial.begin(115200);
    delay(200);

    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(8, 8);
    M5.Display.println("TouchStabilizer Example");

}

void loop()
{
    M5.update();

    TouchStabilizer::RawTouch raw = readRawTouch();
    TouchStabilizer::Event event = touchStabilizer.update(raw, millis());

    if (event == TouchStabilizer::Event::PRESS ||
        event == TouchStabilizer::Event::RELEASE)
    {
        Serial.println(TouchStabilizer::eventName(event));
    }

    static uint32_t lastDrawMs = 0;

    if (millis() - lastDrawMs >= 100)
    {
        lastDrawMs = millis();
        drawStatus(raw, event);
    }
}
