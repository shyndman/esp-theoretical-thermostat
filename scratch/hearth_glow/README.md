# Hearth Glow LED Effect

Organic firelight simulation using layered Perlin noise. Each pixel breathes independently with slow, irregular brightness variation, like embers glowing in a fireplace.

## Effect Description
- Each pixel has a unique phase offset for desynchronized breathing
- Primary noise layer: slow, organic brightness undulation
- Secondary flicker layer: faster variation adds crackle/spark moments
- Hue dances between deep red and warm orange
- Saturation varies subtly for depth
- Minimum brightness floor prevents harsh blackouts

## Tunable Parameters
| Parameter | Default | Description |
|-----------|---------|-------------|
| `NOISE_SCALE` | 60 | Spatial coherence (lower = more correlated neighbors) |
| `NOISE_SPEED` | 3 | Primary breathing rate |
| `MIN_BRIGHTNESS` | 40 | Floor brightness (0-255) |
| `MAX_BRIGHTNESS` | 255 | Peak brightness |
| `FLICKER_SCALE` | 120 | Spatial scale of fast flicker layer |
| `FLICKER_SPEED` | 7 | Speed multiplier for flicker vs breathing |
| `FLICKER_AMOUNT` | 50 | Max brightness reduction from flicker |
| `HUE_MIN` | 0 | Deep red end of color range |
| `HUE_MAX` | 28 | Warm orange end of color range |

## Hardware Assumptions
- 39 addressable LEDs in U-shape: 15 left, 8 top, 16 right
- GPIO 33, GRB color order
- See `scratch/sparkle/README.md` for full hardware setup and upload instructions

## Upload
```sh
arduino-cli compile --fqbn esp32:esp32:esp32p4 scratch/hearth_glow
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32p4 scratch/hearth_glow
```
