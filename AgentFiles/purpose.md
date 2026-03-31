# Orbit12 — Project Purpose

## Overview

**Orbit12** is a compact, extensible software platform designed for the Adafruit MacroPad (RP2040).  
It aims to transform the device into a powerful, modular, and highly interactive control surface.

---

## Core Goals

### 1. Embedded App Framework (Mini OS)

Orbit12 provides a lightweight, OS-like framework that allows multiple apps to run on the MacroPad.

Key features:
- App launcher system
- App-per-folder architecture
- Shared platform services (input, display, LEDs, storage, MIDI, transport)
- Consistent interaction model (encoder + keys + shift behavior)
- Persistent storage for user data and settings

This allows the device to evolve beyond a single-purpose tool into a flexible platform.

---

### 2. Modular MIDI Control Engine (Primary Focus)

The main application within Orbit12 is a **modular MIDI processing engine**.

This system is built around a chain of MIDI modules that process events sequentially:

Input → Module Chain → Output

Each module can:
- Transform incoming MIDI events
- Generate new musical events
- Schedule future events (time-based behavior)

---

## MIDI System Capabilities

The MIDI engine is designed to support:

### 🎹 Note Generation
- Key-based input (12 keys)
- Scale-aware note layouts
- Octave shifting and transformations

### 🎼 Musical Processing Modules
- Chord generators
- Arpeggiators
- Euclidean sequencers
- Randomizers
- Humanization tools
- Transpose and scale quantization

### ⏱ Timing & Transport
- Internal clock (BPM-based)
- External MIDI clock (future support)
- Swing and rate control

### 🔗 Chainable Architecture
Modules can be arranged in sequence to build complex behaviors:
- Chord → Arp → Humanize
- Euclidean → Chord → Velocity shaping
- Input → Scale → Randomize → Output

---

## Design Philosophy

Orbit12 follows these guiding principles:

- **Modular** — everything is composed of reusable building blocks
- **Event-driven** — all systems communicate via events
- **Minimal UI** — optimized for small screens and limited controls
- **Deterministic** — predictable behavior is prioritized over complexity
- **Extensible** — new apps and modules can be added easily
- **Efficient** — designed for constrained hardware (RP2040)

---

## Long-Term Vision

Orbit12 aims to become:
- A portable MIDI brain
- A creative tool for generative music
- A flexible embedded control platform
- A foundation for custom hardware/software musical instruments

---

## Summary

Orbit12 combines:
- A lightweight embedded app framework
- A powerful modular MIDI engine

into a single cohesive system designed specifically for creative control and musical exploration on constrained hardware.
