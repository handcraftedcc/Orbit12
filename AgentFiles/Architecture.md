# System Architecture — Orbit12

## Layers

### Platform Layer
Handles hardware (input, display, LEDs, MIDI, storage, transport)

### App Host
- manages active app
- handles switching
- runs main loop

### Apps
Each app:
- has its own folder
- defines entry file
- implements handle_events(), update(), render()

### MIDI Engine
Pipeline:
Input → Module Chain → Output

Modules:
- transform events
- maintain internal state
- schedule future events

## Data Flow
input → events → modules → output

## Key Concepts

### Event
- data packet passed through system

### Context
- shared state

### Scheduler
- list of future events

## Storage
/userdata
    global_settings.json
    /AppName
        settings.json
        /presets
