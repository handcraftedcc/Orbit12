# Dev Guidelines — Orbit12 Platform

## Target Hardware
- RP2040 (Adafruit MacroPad)
- ~264KB RAM
- 8MB Flash (shared with firmware, libs, user files)

## Core Principles

### 1. Keep Code Lightweight
- Avoid large data structures
- Avoid unnecessary object creation in loops
- Prefer simple lists over nested dicts where possible
- Avoid recursion

### 2. Single Loop Architecture
- No threading
- No asyncio
- Everything runs inside a main loop

### 3. Event-Driven System
- Use Event objects for all runtime communication
- No callbacks between modules
- No direct module-to-module calls

### 4. Separation of Concerns
- Platform Layer → Hardware
- App Layer → UI + logic
- Engine Layer → MIDI processing

### 5. Memory Awareness
- Avoid large JSON loads
- Avoid keeping unused data
- Prefer compact representations

### 6. File System Rules
- Use absolute paths (/userdata/...)
- Avoid frequent writes
- Batch saves

### 7. Import Rules
- Avoid dynamic imports
- Use explicit imports
- Keep module depth shallow

### 8. UI Constraints
- Small screen
- Minimal redraw
- Avoid heavy animations

### 9. Debugging
- Use print() sparingly
- Avoid prints in tight loops
- Use debug flags

### 10. Module Design
- Modules receive events and return events
- No side effects outside module state
