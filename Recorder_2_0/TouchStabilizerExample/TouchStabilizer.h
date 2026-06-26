#ifndef TOUCH_STABILIZER_H
#define TOUCH_STABILIZER_H

#include <stdint.h>

class TouchStabilizer
{
public:
    enum class Event
    {
        NONE,
        PRESS,
        HOLD,
        RELEASE
    };

    enum class State
    {
        IDLE,
        PRESS_DETECTED,
        PRESSED,
        WAIT_FOR_STABLE_RELEASE
    };

    struct RawTouch
    {
        bool active = false;
        int id = -1;
        int x = -1;
        int y = -1;
        int size = -1;
    };

    struct Config
    {
        uint32_t stablePressMs = 0;
        uint32_t stableReleaseMs = 120;
        uint8_t stableReleaseSamples = 2;
    };

    TouchStabilizer();
    explicit TouchStabilizer(const Config &config);

    Event update(const RawTouch &raw, uint32_t nowMs);
    void reset();

    State state() const;
    Event lastEvent() const;
    RawTouch activeTouch() const;
    uint32_t holdMs(uint32_t nowMs) const;

    static const char *eventName(Event event);
    static const char *stateName(State state);

private:
    Config config_;
    State state_ = State::IDLE;
    Event lastEvent_ = Event::NONE;
    RawTouch activeTouch_;
    uint32_t pressCandidateSinceMs_ = 0;
    uint32_t pressedSinceMs_ = 0;
    uint32_t releaseCandidateSinceMs_ = 0;
    uint8_t releaseCandidateSamples_ = 0;

    Event transitionToPressDetected(const RawTouch &raw, uint32_t nowMs);
    Event confirmPress(uint32_t nowMs);
    Event handlePressed(const RawTouch &raw, uint32_t nowMs);
    Event handleReleaseCandidate(const RawTouch &raw, uint32_t nowMs);
    bool releaseIsStable(uint32_t nowMs) const;
    void debugPrint(const RawTouch &raw, Event event) const;
};

#endif
