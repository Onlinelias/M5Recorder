#include "TouchStabilizer.h"

#ifdef TOUCH_STABILIZER_DEBUG
#include <Arduino.h>
#ifndef TOUCH_STABILIZER_DEBUG_PORT
#define TOUCH_STABILIZER_DEBUG_PORT Serial
#endif
#endif

TouchStabilizer::TouchStabilizer()
    : config_(Config())
{
}

TouchStabilizer::TouchStabilizer(const Config &config)
    : config_(config)
{
}

TouchStabilizer::Event TouchStabilizer::update(const RawTouch &raw, uint32_t nowMs)
{
    Event event = Event::NONE;

    switch (state_)
    {
    case State::IDLE:
        if (raw.active)
        {
            event = transitionToPressDetected(raw, nowMs);
        }
        break;

    case State::PRESS_DETECTED:
        if (!raw.active)
        {
            reset();
            event = Event::NONE;
        }
        else if (nowMs - pressCandidateSinceMs_ >= config_.stablePressMs)
        {
            activeTouch_ = raw;
            event = confirmPress(nowMs);
        }
        break;

    case State::PRESSED:
        event = handlePressed(raw, nowMs);
        break;

    case State::WAIT_FOR_STABLE_RELEASE:
        event = handleReleaseCandidate(raw, nowMs);
        break;
    }

    lastEvent_ = event;
    debugPrint(raw, event);
    return event;
}

void TouchStabilizer::reset()
{
    state_ = State::IDLE;
    lastEvent_ = Event::NONE;
    activeTouch_ = RawTouch();
    pressCandidateSinceMs_ = 0;
    pressedSinceMs_ = 0;
    releaseCandidateSinceMs_ = 0;
    releaseCandidateSamples_ = 0;
}

TouchStabilizer::State TouchStabilizer::state() const
{
    return state_;
}

TouchStabilizer::Event TouchStabilizer::lastEvent() const
{
    return lastEvent_;
}

TouchStabilizer::RawTouch TouchStabilizer::activeTouch() const
{
    return activeTouch_;
}

uint32_t TouchStabilizer::holdMs(uint32_t nowMs) const
{
    if (state_ != State::PRESSED && state_ != State::WAIT_FOR_STABLE_RELEASE)
    {
        return 0;
    }

    return nowMs - pressedSinceMs_;
}

const char *TouchStabilizer::eventName(Event event)
{
    switch (event)
    {
    case Event::NONE:
        return "NONE";
    case Event::PRESS:
        return "PRESS";
    case Event::HOLD:
        return "HOLD";
    case Event::RELEASE:
        return "RELEASE";
    }

    return "?";
}

const char *TouchStabilizer::stateName(State state)
{
    switch (state)
    {
    case State::IDLE:
        return "IDLE";
    case State::PRESS_DETECTED:
        return "PRESS_DETECTED";
    case State::PRESSED:
        return "PRESSED";
    case State::WAIT_FOR_STABLE_RELEASE:
        return "WAIT_FOR_STABLE_RELEASE";
    }

    return "?";
}

TouchStabilizer::Event TouchStabilizer::transitionToPressDetected(
    const RawTouch &raw,
    uint32_t nowMs)
{
    activeTouch_ = raw;
    pressCandidateSinceMs_ = nowMs;
    state_ = State::PRESS_DETECTED;

    if (config_.stablePressMs == 0)
    {
        return confirmPress(nowMs);
    }

    return Event::NONE;
}

TouchStabilizer::Event TouchStabilizer::confirmPress(uint32_t nowMs)
{
    state_ = State::PRESSED;
    pressedSinceMs_ = nowMs;
    releaseCandidateSinceMs_ = 0;
    releaseCandidateSamples_ = 0;
    return Event::PRESS;
}

TouchStabilizer::Event TouchStabilizer::handlePressed(
    const RawTouch &raw,
    uint32_t nowMs)
{
    if (raw.active)
    {
        activeTouch_ = raw;
        return Event::HOLD;
    }

    state_ = State::WAIT_FOR_STABLE_RELEASE;
    releaseCandidateSinceMs_ = nowMs;
    releaseCandidateSamples_ = 1;
    return Event::NONE;
}

TouchStabilizer::Event TouchStabilizer::handleReleaseCandidate(
    const RawTouch &raw,
    uint32_t nowMs)
{
    if (raw.active)
    {
        activeTouch_ = raw;
        state_ = State::PRESSED;
        releaseCandidateSinceMs_ = 0;
        releaseCandidateSamples_ = 0;
        return Event::HOLD;
    }

    if (releaseCandidateSamples_ < 255)
    {
        releaseCandidateSamples_++;
    }

    if (!releaseIsStable(nowMs))
    {
        return Event::NONE;
    }

    state_ = State::IDLE;
    releaseCandidateSinceMs_ = 0;
    releaseCandidateSamples_ = 0;
    activeTouch_ = RawTouch();
    return Event::RELEASE;
}

bool TouchStabilizer::releaseIsStable(uint32_t nowMs) const
{
    bool samplesStable = releaseCandidateSamples_ >= config_.stableReleaseSamples;
    bool timeStable = nowMs - releaseCandidateSinceMs_ >= config_.stableReleaseMs;
    return samplesStable && timeStable;
}

void TouchStabilizer::debugPrint(const RawTouch &raw, Event event) const
{
#ifdef TOUCH_STABILIZER_DEBUG
    TOUCH_STABILIZER_DEBUG_PORT.printf(
        "TouchStabilizer state=%s rawActive=%d id=%d x=%d y=%d size=%d event=%s\n",
        stateName(state_),
        raw.active ? 1 : 0,
        raw.id,
        raw.x,
        raw.y,
        raw.size,
        eventName(event));
#else
    (void)raw;
    (void)event;
#endif
}
