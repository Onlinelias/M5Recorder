# AGENTS.md

# M5Recorder Development Rules

This repository contains firmware for a portable stereo WAV recorder based on the M5Stack CoreS3.

These rules apply to every code change.

---

# Development Philosophy

One functional change per build.

One diagnostic change per build.

Never combine multiple behavioural changes into a single experiment.

If evidence contradicts the current hypothesis, stop and explain before modifying code.

---

# Primary Goal

Maintain a stable recorder while systematically debugging the touch and transport layers.

Do not redesign working subsystems.

---

# Stable Subsystems

Unless explicitly requested, do NOT modify:

* RecorderTask
* Audio DMA
* WAV writing
* SD writing
* FreeRTOS task architecture
* Audio buffers
* Display rendering
* Auto-stop timer

These are currently considered stable.

---

# Preferred Investigation Order

When debugging:

1. Observe
2. Instrument
3. Explain
4. Modify
5. Test

Never skip directly to speculative fixes.

---

# State Machines

Before modifying any state machine:

* identify every read of the affected variables
* identify every write of the affected variables
* explain the complete state transition

Examples:

* waitForRelease
* rawTouchActive
* stopTouchActive
* transitionBusy
* recorderCommand

---

# Diagnostics

Diagnostics should:

* print only on state changes
* avoid continuous loop printing
* avoid flooding Serial
* preserve recorder timing

Prefer event logging over polling logs.

---

# File Editing

Always:

* edit the existing .ino file in place
* preserve formatting
* preserve comments
* save the modified file locally
* produce a unified diff

Do NOT generate a completely new sketch.

---

# Git

Do not push automatically.

Never rewrite Git history.

Only the user decides whether a version becomes the new baseline.

---

# Communication

Before changing code:

* explain the hypothesis
* explain why the proposed change is minimal
* explain why stable subsystems are unaffected

After changing code:

* explain every modified block
* describe expected behaviour
* describe possible failure modes

---

# Project Principle

Small, explainable, reversible changes always beat large speculative rewrites.
