# Rising Warmth LED Effect

Heat rises: warm orange waves travel upward from the bottom of both edges, meeting at the top center. Reinforces the physical metaphor of convective heat.

## Effect Description
- Multiple concurrent waves (default 3) spawn at the bottom of the U-shaped bezel
- Each wave rises up both the left and right edges simultaneously
- Waves meet and bloom across the top bar
- Trailing gradient creates a comet-like effect
- Colors blend from deep amber (tail) to bright orange (head)

## Tunable Parameters
| Parameter | Default | Description |
|-----------|---------|-------------|
| `WAVE_COUNT` | 3 | Max concurrent rising waves |
| `WAVE_LENGTH` | 6 | Pixels in each wave's gradient tail |
| `WAVE_SPEED` | 0.08 | Position increment per frame (0-1 scale) |
| `WAVE_SPAWN_CHANCE` | 0.03 | Probability of spawning new wave per frame |
| `HEAT_CORE` | #FF8020 | Bright orange at wave head |
| `HEAT_EDGE` | #A03000 | Deep amber at wave tail |

## Hardware Assumptions
- 39 addressable LEDs in U-shape: 15 left (bottom-to-top), 8 top (left-to-right), 16 right (top-to-bottom)
- GPIO 33, GRB color order
- See `scratch/sparkle/README.md` for full hardware setup and upload instructions

## Upload
```sh
arduino-cli compile --fqbn esp32:esp32:esp32p4 scratch/rising_warmth
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32p4 scratch/rising_warmth
```
