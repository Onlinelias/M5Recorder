# ARCHITECTURE.md

# M5Recorder Firmware Architecture

## Overview

The firmware is intentionally divided into largely independent subsystems.

Whenever possible, changes should remain inside a single subsystem.

---

# System Overview

GT911 Touch Controller

↓

Touch Processing

↓

Transport State Machine

↓

Recorder Commands

↓

Recorder Engine

↓

WAV Writer

↓

SD Card

---

# Recorder Engine

Responsibilities:

* audio capture
* DMA
* I2S
* RecorderTask
* WAV writing
* SD file management
* automatic recording timeout

Status:

Stable.

Do not modify unless explicitly requested.

---

# Touch Layer

Responsibilities:

* raw GT911 acquisition
* touch filtering
* edge rejection
* validTouch generation

Known issues are currently believed to exist in this subsystem.

---

# Transport Layer

Responsibilities:

* convert touch events into transport commands
* START
* STOP
* transport timing
* waitForRelease handling
* transitionBusy handling

This layer should not perform audio operations.

It only generates recorder commands.

---

# UI Layer

Responsibilities:

* idle screen
* recording screen
* green save screen

The UI should not directly control recorder internals.

---

# Command Flow

User Touch

↓

Raw Touch

↓

Edge Filter

↓

validTouch

↓

Transport Decision

↓

CMD_START / CMD_STOP

↓

RecorderTask

↓

Recorder Engine

---

# Current Debugging Strategy

The recorder engine has been validated.

Current investigation focuses on:

Touch

↓

validTouch generation

↓

Transport

Unexpected START and STOP behaviour is currently being investigated.

---

# Engineering Principles

* Keep subsystems independent.
* Preserve recorder engine stability.
* Prefer instrumentation before modification.
* Make one behavioural change per build.
* Keep every experiment reversible.

---

# Current Baseline

The latest tested GitHub commit is always considered the canonical firmware baseline.

Historical versions are reference material only.
