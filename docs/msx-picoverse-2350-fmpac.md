# MSX PicoVerse 2350 - MSX-MUSIC / FM-PAC Implementation

The PicoVerse 2350 LoadROM firmware can emulate the MSX-MUSIC YM2413 sound chip and expose an FM-PAC compatible BIOS area for games that do not drive the YM2413 directly. This is enabled by the LoadROM tool options `-f`, `-fmpac`, or `--fmpac` when building a UF2 for a non-SYSTEM external ROM.

The implementation has two parts:

- Pico-side YM2413 synthesis using the emu2413 OPLL emulator and the PicoVerse I2S DAC.
- FM-PAC BIOS exposure in the cartridge slot so software that uses MSX-MUSIC BIOS calls can find the BIOS routines and reach the emulated OPLL.

This document describes the RP2350 LoadROM implementation in `2350/software/loadrom.pio`.

---

## Goals

The FM-PAC/MSX-MUSIC support was added for ROMs that expect MSX-MUSIC hardware to be present on the cartridge. Some titles write directly to the YM2413 I/O ports, while others use BIOS routines or FM-PAC memory-mapped registers. The firmware supports both access styles.

The design goals are:

- Keep MSX-MUSIC as a single cartridge audio profile, mutually exclusive with SCC, SCC+, and Dual PSG.
- Allow `-f` / `-fmpac` with supported non-SYSTEM external ROM mappers, excluding SCC-class mappers.
- Embed the FM-PAC BIOS in the UF2 image, immediately after the game ROM payload.
- Present the selected game ROM mapper and the FM-PAC BIOS in one expanded cartridge slot layout.
- Route both direct YM2413 I/O writes and FM-PAC memory-mapped register writes to the same OPLL emulator instance.

---

## User-facing behavior

### Command-line options

```
loadrom.exe -f <romfile>
loadrom.exe -fmpac <romfile>
loadrom.exe --fmpac <romfile>
```

All three options enable the same feature. The tool prints:

```
MSX-MUSIC/FM-PAC Emulation: Enabled with BIOS and YM2413 ports 0x7C/0x7D
```

### Valid combinations

`-f` / `-fmpac` is valid only with non-SYSTEM external ROM files. It is rejected for the embedded Sunrise/Carnivore2 system modes (`-s1`, `-m1`, `-s2`, `-m2`, `-c1`, `-c2`) and for Konami SCC or Manbow2 mapper ROMs.

Only one cartridge audio engine can be active in a UF2 image. The tool rejects combinations of:

- `-scc`
- `-sccplus`
- `-d` / `--dual-psg`
- `-f` / `-fmpac` / `--fmpac`

Konami SCC and Manbow2 mapper ROMs are not accepted for `-f` / `-fmpac` builds.

---

## Source files

### Tool side

| File | Role |
| --- | --- |
| `2350/software/loadrom.pio/tool/src/loadrom.c` | Parses `-f` / `-fmpac`, sets the MSX-MUSIC flag, appends the FM-PAC BIOS payload, and writes the UF2. |
| `2350/software/loadrom.pio/tool/Makefile` | Generates `tool/src/fmpac_bios.h` from `../fmpac/FMPCCMFC.BIN` using `utils/xxd -i`. |
| `2350/software/loadrom.pio/tool/src/fmpac_bios.h` | Generated C array containing the 64 KB FM-PAC BIOS. |
| `2350/software/loadrom.pio/fmpac/FMPCCMFC.BIN` | English FM-PAC BIOS binary embedded into `-f` / `-fmpac` UF2 images. |

### Firmware side

| File | Role |
| --- | --- |
| `2350/software/loadrom.pio/pico/loadrom/loadrom.c` | Implements audio mode selection, YM2413 initialization, direct I/O handling, FM-PAC register handling, and the expanded-slot FM-PAC mapper wrapper. |
| `2350/software/loadrom.pio/pico/loadrom/loadrom.h` | Defines MSX-MUSIC flags, ports, clocks, sample rate, and FM-PAC BIOS size. |
| `2350/software/loadrom.pio/pico/loadrom/emu2413.c` | YM2413/OPLL emulator implementation. |
| `2350/software/loadrom.pio/pico/loadrom/emu2413.h` | YM2413/OPLL emulator API. |
| `2350/software/loadrom.pio/pico/loadrom/CMakeLists.txt` | Includes `emu2413.c` in the LoadROM firmware build. |

---

## Build integration

The top-level LoadROM build is driven by `2350/software/loadrom.pio/Makefile`.

The PC tool build depends on the FM-PAC BIOS binary:

```
FMPAC_BIOS := ../fmpac/FMPCCMFC.BIN
FMPAC_BIOS_H := $(SRCDIR)/fmpac_bios.h
```

The header is generated with the local `xxd` utility:

```
$(FMPAC_BIOS_H): $(FMPAC_BIOS)
	$(UTLDIR)/$(XXD) -i $< $@
```

The generated symbol names are:

```
___fmpac_FMPCCMFC_BIN[]
___fmpac_FMPCCMFC_BIN_len
```

The current BIOS payload length is 65536 bytes.

---

## UF2 payload layout

LoadROM UF2 images are written by `create_uf2_file()` in the tool. With `-f` / `-fmpac`, the binary stream is:

| Order | Payload | Description |
| --- | --- | --- |
| 1 | Firmware blob | RP2350 LoadROM firmware from `pico/loadrom/dist/loadrom.bin`. |
| 2 | Configuration record | 59-byte record containing ROM name, mapper/audio flags, ROM size, and ROM flash offset. |
| 3 | Game ROM | The external ROM file passed on the command line. |
| 4 | FM-PAC BIOS | `FMPCCMFC.BIN`, appended only when MSX-MUSIC/FM-PAC is enabled. |

The firmware uses the game ROM size from the configuration record to find the BIOS:

```
fmpac_bios_base = rom + offset + active_rom_size
```

This keeps the existing ROM payload format intact while reusing the extra embedded ROM mechanism already used by other LoadROM modes.

---

## Configuration flags

The configuration record stores mapper and audio selection in one byte. For non-SYSTEM ROMs, bit `0x20` is used as the MSX-MUSIC flag:

| Bit | Firmware constant | Meaning in non-SYSTEM ROM builds |
| --- | --- | --- |
| `0x80` | `SCC_FLAG` | SCC emulation requested. |
| `0x40` | `SCC_PLUS_FLAG` | SCC+ emulation requested. |
| `0x20` | `MSX_MUSIC_FLAG` | MSX-MUSIC / FM-PAC requested. |
| `0x10` | `DUAL_PSG_FLAG` | Dual PSG requested. |
| `0x0F` | Base mapper ID | Selected ROM mapper. |

The same `0x20` bit is used as `WIFI_FLAG` in Sunrise system modes. The firmware distinguishes those cases by checking the base ROM type and resolving `system_mode` before selecting an audio engine.

---

## Audio mode selection

The firmware resolves cartridge audio through `resolve_audio_mode()`.

The modes are:

| Mode | Meaning |
| --- | --- |
| `AUDIO_MODE_NONE` | No Pico-side cartridge audio engine. |
| `AUDIO_MODE_SCC` | Standard SCC emulation. |
| `AUDIO_MODE_SCC_PLUS` | SCC+ emulation. |
| `AUDIO_MODE_DUAL_PSG` | Secondary AY-3-8910 on ports `0x10`/`0x11`. |
| `AUDIO_MODE_MSX_MUSIC` | YM2413/MSX-MUSIC on ports `0x7C`/`0x7D` plus FM-PAC BIOS exposure. |

System ROM modes always resolve to `AUDIO_MODE_NONE` for this path. Because the tool enforces mutual exclusion, a valid `-f` / `-fmpac` image contains only the MSX-MUSIC flag and therefore resolves to `AUDIO_MODE_MSX_MUSIC`.

When `AUDIO_MODE_MSX_MUSIC` is selected, `main()` initializes the YM2413 engine and then dispatches to `loadrom_fmpac()` before the normal mapper switch.

---

## YM2413 / OPLL audio engine

### Emulation library

The firmware uses emu2413 by Mitsutaka Okazaki. Initialization creates one OPLL instance:

```
OPLL_new(MSX_MUSIC_CLOCK, MSX_MUSIC_SAMPLE_RATE)
OPLL_setChipType(..., OPLL_2413_TONE)
OPLL_resetPatch(..., OPLL_2413_TONE)
```

The LoadROM constants are:

| Constant | Value |
| --- | --- |
| `MSX_MUSIC_CLOCK` | 3579545 Hz |
| `MSX_MUSIC_SAMPLE_RATE` | 44100 Hz |
| `MSX_MUSIC_PORT_REG` | `0x7C` |
| `MSX_MUSIC_PORT_DATA` | `0x7D` |

### Core usage

| Core | Role |
| --- | --- |
| Core 0 | Serves the MSX memory bus through PIO0 and routes FM-PAC memory-mapped writes to the OPLL. |
| Core 1 | Generates YM2413 audio, services direct OPLL I/O writes from PIO1, and feeds the I2S DAC. |

The audio path uses the existing PicoVerse I2S output on GPIOs 29-32:

| Signal | GPIO |
| --- | --- |
| DATA | 29 |
| BCLK | 30 |
| WSEL / LRCLK | 31 |
| MUTE | 32 |

### Locking

Both direct I/O writes and memory-mapped FM-PAC writes can reach the OPLL while core 1 is calculating audio samples. The firmware protects the OPLL instance with a Pico SDK spin lock:

- `msx_music_calc_sample()` locks around `OPLL_calc()`.
- `msx_music_write_io()` locks around `OPLL_writeIO()`.

This keeps core 0 register writes and core 1 audio generation from mutating the emulator state at the same time.

---

## Direct YM2413 I/O handling

The firmware initializes a PIO1 I/O bus captor with `msx_pio_io_bus_init()` when MSX-MUSIC is enabled.

`msx_music_service_io()` consumes captured I/O writes. Writes to ports `0x7C` and `0x7D` are forwarded to:

```
msx_music_write_io(port, data)
```

I/O reads are acknowledged with `0xFF`. The YM2413 path currently only needs write capture for register select and data writes.

When audio generation is running on core 1, direct I/O servicing is owned by core 1. This prevents missed port writes while core 0 is busy serving ROM or mapper cycles.

---

## FM-PAC expanded slot model

The firmware presents FM-PAC builds as an expanded cartridge slot. This follows the same general strategy used by Sunrise/Carnivore2-style expanded-slot modes, but with the game mapper and FM-PAC BIOS sharing the slot.

`loadrom_fmpac()` starts by waiting for the expanded-slot bootstrap, freezes the bus with `/WAIT`, prepares the ROM source/cache, then reinitializes the PIO0 MSX memory bus.

The expanded slot layout is:

| Subslot | Function |
| --- | --- |
| 0 | Selected game ROM mapper. |
| 1 | Unused, returns `0xFF`. |
| 2 | Unused, returns `0xFF`. |
| 3 | FM-PAC BIOS, FM-PAC control/page registers, PAC SRAM window, and memory-mapped OPLL write registers. |

The expanded slot selection register is handled at address `0xFFFF`. Writes update the local `subslot_reg`; reads return the inverted value, matching the standard expanded-slot register behavior.

For each read or write, the firmware computes the active subslot for the current 16 KB page:

```
page = address >> 14
active_subslot = (subslot_reg >> (page * 2)) & 0x03
```

If the active subslot is 0, the access is routed to the game mapper. If the active subslot is 3, the access is routed to the FM-PAC handler.

---

## FM-PAC memory map

The FM-PAC side is modeled by `fmpac_state_t`:

| Field | Purpose |
| --- | --- |
| `page` | Low two bits select one 16 KB page from the 64 KB BIOS. |
| `control` | Value stored by writes to `0x7FF6`; initialized to `0x10`. |
| `sram_key_5ffe` | First PAC SRAM enable key byte. |
| `sram_key_5fff` | Second PAC SRAM enable key byte. |
| `sram[8192]` | Volatile 8 KB PAC SRAM window. |

### Registers and windows

| Address/range | Access | Behavior |
| --- | --- | --- |
| `0x4000`-`0x7FFF` | Read | FM-PAC BIOS page selected by `0x7FF7`, unless PAC SRAM is enabled for `0x4000`-`0x5FFF`. |
| `0x4000`-`0x5FFF` | Read/write | Volatile PAC SRAM when keys `0x5FFE = 0x4D` and `0x5FFF = 0x69` are active. |
| `0x5FFE` | Write | Stores PAC SRAM key byte `0x4D`. |
| `0x5FFF` | Write | Stores PAC SRAM key byte `0x69`. |
| `0x7FF4` | Write | FM-PAC memory-mapped YM2413 register select; forwarded as port `0x7C`. |
| `0x7FF5` | Write | FM-PAC memory-mapped YM2413 data; forwarded as port `0x7D`. |
| `0x7FF6` | Read/write | FM-PAC control register shadow. |
| `0x7FF7` | Read/write | BIOS page register; low two bits select one of four 16 KB pages. |

The BIOS lookup is:

```
relative = ((page & 0x03) << 14) | (address & 0x3FFF)
```

Because `FMPAC_BIOS_SIZE` is 65536 bytes, the four pages cover the whole embedded BIOS.

---

## Supported game mappers inside FM-PAC mode

`loadrom_fmpac()` keeps the selected game mapper active in subslot 0. The mapper ID is still selected by the normal LoadROM detection/forcing path; `-f` / `-fmpac` only changes the audio and slot wrapper.

| Mapper ID | Mapper | FM-PAC wrapper behavior |
| --- | --- | --- |
| 1 | Plain 16 KB | Serves the fixed ROM window through the plain mapper path. |
| 2 | Plain 32 KB | Serves the fixed ROM window through the plain mapper path. |
| 3 | Konami SCC mapper | Not supported for FM-PAC/MSX-MUSIC builds. |
| 4 | Planar 48 KB | Serves the linear 48 KB style address range. |
| 5 | ASCII8 | Uses 8 KB ASCII bank registers. |
| 6 | ASCII16 | Uses 16 KB ASCII bank registers. |
| 7 | Konami | Uses Konami 8 KB bank registers. |
| 8 | NEO8 | Uses six 8 KB NEO bank registers. |
| 9 | NEO16 | Uses three 16 KB NEO bank registers. |
| 12 | ASCII16-X | Uses the ASCII16-X bank register layout for mirrored 16 KB pages. |
| 13 | Planar 64 KB | Serves the full 64 KB address space linearly. |
| 14 | Manbow2 | Not supported for FM-PAC/MSX-MUSIC builds. |

For unmapped subslots or unsupported windows, reads return `0xFF`.

---

## Access flow examples

### Direct YM2413 write

1. Z80 writes to I/O port `0x7C` or `0x7D`.
2. PIO1 captures the I/O write.
3. Core 1 drains the I/O FIFO while generating audio.
4. `msx_music_write_io()` forwards the write to emu2413.
5. Core 1 continues producing PCM samples for the I2S DAC.

### FM-PAC BIOS call path

1. Game calls MSX-MUSIC BIOS routines.
2. The MSX slot selection targets the FM-PAC subslot for the BIOS page.
3. Firmware reads bytes from the embedded `FMPCCMFC.BIN` image.
4. BIOS code writes FM-PAC OPLL registers at `0x7FF4` / `0x7FF5`, or direct ports depending on the routine.
5. Firmware forwards those writes to the same emu2413 OPLL instance.

### Game mapper access

1. Game selects the primary game subslot for its cartridge pages.
2. Firmware routes reads and mapper writes to the selected mapper implementation.
3. The game ROM continues to run from its normal mapper while the FM-PAC BIOS remains available from subslot 3.

---

## Compatibility notes

- `-f` and `-fmpac` are synonyms in the LoadROM tool.
- The feature is currently implemented for RP2350 LoadROM (`2350/software/loadrom.pio`).
- The FM-PAC BIOS is embedded into every `-f` / `-fmpac` non-SYSTEM external ROM UF2.
- ROMs that directly write YM2413 ports `0x7C`/`0x7D` are supported.
- ROMs that rely on MSX-MUSIC BIOS calls are supported by the embedded FM-PAC BIOS exposure.
- ROMs that use FM-PAC memory-mapped OPLL registers `0x7FF4`/`0x7FF5` are routed to the same YM2413 emulator.
- Konami SCC and Manbow2 mapper ROMs are rejected for FM-PAC/MSX-MUSIC builds.
- Audio is generated by the PicoVerse I2S DAC, not by the host MSX audio circuit.

---

## Limitations

- Only one Pico-side cartridge audio engine is active per UF2 image. MSX-MUSIC cannot be combined with SCC, SCC+, or Dual PSG in the same image.
- FM-PAC/MSX-MUSIC is not enabled for embedded Sunrise/Carnivore2 system modes.
- PAC SRAM is volatile. It is modeled in RAM and is not persisted across power cycles.
- The FM-PAC BIOS payload is an embedded third-party BIOS binary; distribution and use must respect the rights of the BIOS copyright holders.
- The implementation models the FM-PAC BIOS/register behavior needed by LoadROM titles, not a complete standalone FM-PAC cartridge with persistent SRAM management.

---

## Validation performed

The implementation was validated with the LoadROM build flow:

```
make firmware
make tool
```

The tool was also checked with an R-Type build:

```
loadrom.exe -fmpac -o rtype_fmpac.uf2 "R-Type - IREM.rom"
```

The generated output reported ASCII8 mapper detection, MSX-MUSIC/FM-PAC enablement with BIOS and ports `0x7C`/`0x7D`, and a larger UF2 due to the appended 64 KB BIOS payload.

Invalid combinations were checked to ensure the option rules still reject mutually exclusive audio modes and SYSTEM builds.

---

## Credits

- YM2413/OPLL emulation uses emu2413 by Mitsutaka Okazaki, licensed under the MIT License.
- `FMPCCMFC.BIN` is the English FM-PAC BIOS. The FM-PAC BIOS ROM is copyrighted by Matsushita Corp.; the English translation is credited to 232, Max Iwamoto, and GDX.

---

Author: Cristiano Almeida Goncalves

Last updated: 05/17/2026