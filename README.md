# Rotifera

Rotifera bunddles two frameworks:
- *base* contains utilities to communicate with the robot. It is meant to run on a distant computer.
- *robot* contains the arbiter's files, the arduino's firmware and a python framework to build custom behaviors. It is meant to run on the embedded computer.

## NUC consumption

Idle NUC consumption: `0.220 mA`, `19 V`, `4.18 W`

Measured NUC consumption while running a CPU and memory benchmark (`stress-ng --cpu 8 --io 4 --vm 2 --vm-bytes 1G --timeout 60s --metrics-brief`):

| Intensity | Tension | Power     |
|:---------:|:-------:|:---------:|
| `1.050 A` |  `19 V` | `19.95 W` |
| `1.150 A` |  `17 V` | `19.55 W` |
| `1.250 A` |  `15 V` | `18.75 W` |
| `1.360 A` |  `14 V` | `19.04 W` |
| `1.500 A` |  `13 V` | `19.50 W` |
| `1.550 A` |  `12 V` | `18.60 W` |
| `1.720 A` |  `11 V` | `18.92 W` |
| `1.900 A` |  `10 V` | `19.00 W` |
| `2.150 A` |   `9 V` | `19.35 W` |

## Thrust range

Backward speed enable: `1578 us`
Backward speed disable (once enabled): `1576 us`

Forward speed enable: `1527 us`
Forward speed disbale (once enabled): `1529 us`

Theoretical zero: `(1578 + 1527) / 2 = 1552.5 us`
