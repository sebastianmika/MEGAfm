# MEGAfm Factory Preset Decoder

Decodes the 50 factory presets from `kFactoryPresets` (in `include/constants.h`) into a pandas DataFrame. Each row is one preset, and columns represent MIDI CC parameters with human-readable names.

## Setup

```bash
cd preset_loader
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

## Usage

```bash
source venv/bin/activate
python decode_presets.py
```

This will:
1. Decode all 50 factory presets
2. Print column names and byte accounting documentation
3. Print the first 5 presets (transposed)
4. Save all presets to `factory_presets.csv`

### As a library

```python
from decode_presets import decode_all_presets, VOICE_MODES, ARP_MODES, LFO_SHAPES, ENVELOPE_MODES

df = decode_all_presets()
# Filter presets using a specific voice mode
unison = df[df["CC78+Voice Mode+(0-5)"] == 3]
```

## Column naming convention

All columns (except `Preset Number`) follow the format:

```
CC{num}+{name}+({value_range})
```

Use `col.split("+")` to extract the three parts:
1. **CC number** — e.g. `CC18`, `CC78`, `CC-1`
2. **Parameter name** — e.g. `Op1 Detune`, `LFO2 Link Op2 Sustain`
3. **CC value range** — e.g. `(0-127)`, `(50-51)`, `(N/A)`

Examples:

| Column | CC | Name | Value range |
|--------|-----|------|-------------|
| `CC18+Op1 Detune+(0-127)` | 18 | Op1 Detune | 0-127 (continuous) |
| `CC78+Voice Mode+(0-5)` | 78 | Voice Mode | 0-5 (multi-valued) |
| `CC72+LFO2 Link Op2 Sustain+(50-51)` | 72 | LFO2 Link Op2 Sustain | 50=unlinked, 51=linked |
| `CC70+LFO1 Shape+(0-5)` | 70 | LFO1 Shape | 0-5 (multi-valued) |
| `CC-1+Seq[0]+(N/A)` | -1 | Seq[0] | no CC sent |

## Enum dictionaries

The module exports these dictionaries for mapping numeric values to names:

- `VOICE_MODES` — 0: Poly12, 1: Wide6, 2: DualCh3, 3: Unison, 4: Wide4, 5: Wide3
- `ARP_MODES` — 0: Off, 1: Up, 2: Down, 3: Up/Down, 4: Random1, 5: Random2, 6: Sequence1, 7: Sequence2
- `LFO_SHAPES` — 0: Square, 1: Inv Square, 2: Triangle, 3: Saw, 4: Inv Saw, 5: Random Inf, 6-8: Random 8/16/32
- `ENVELOPE_MODES` — 0: Off, 1: Forward, 2: Ping Pong

## Sending presets via MIDI

`send_presets.py` loads `factory_presets.csv` and sends each preset as MIDI CC messages.

```bash
# List available MIDI ports
python send_presets.py --list-ports

# Send all presets to a specific port on channel 0
python send_presets.py --port "MEGAfm" --channel 0

# Send specific presets
python send_presets.py --port 0 --presets 0,5,12

# Adjust timing (ms between CCs / ms between presets)
python send_presets.py --port 0 --delay 10 --preset-delay 1000
```

Options:
- `--csv` — CSV file path (default: `factory_presets.csv`)
- `--port` — MIDI port name or index (interactive if omitted)
- `--channel` — MIDI channel 0-15 (default: 0)
- `--delay` — ms between individual CC messages (default: 5)
- `--preset-delay` — ms between presets (default: 500)
- `--presets` — comma-separated preset numbers to send (default: all)

The script parses column headers (`CC{num}+{name}+({range})`) to determine which CC to send and how to convert values. Columns with `CC-1` or `Name` in the header are skipped.

## Byte accounting

Each preset occupies 79 bytes. 78 are used, 1 is padding. See `BYTE_ACCOUNTING` in the script or run it for full documentation. Key points:

- **Bytes 0-23**: 4 operators x 6 bytes (detune, mult, level, RS+AR, D1R+flags, sustain+release)
- **Bytes 24-38**: fmBase[36-50] (LFO, arp, vib, algo, feedback, fat + 2 unused)
- **Bytes 39-40**: Voice mode, arp mode, LFO shapes, retrig, looping
- **Bytes 41-58**: LFO link bitfields (3 LFOs x 47 targets)
- **Bytes 59-74**: Arp sequence (16 steps, no CC)
- **Byte 75**: Seq length + glide
- **Bytes 76-77**: Fine tuning + volume
- **Byte 78**: Unused padding

Parameters with no CC representation (`CC-1`):
- `fmBase[44]`, `fmBase[45]` — loaded but never sent as CC or mapped to any knob
- `v4 Preset Flag` — firmware version marker
- `Seq[0..15]` and `Seq Length` — arp sequence, stored per-preset but not sent via CC
- Padding byte
