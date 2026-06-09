;== Deadfall SNES - LoROM header / interrupt vectors ==
; Sized at 8 Mbit (1 MB) with headroom; bump ROMBANKS/ROMSIZE as art+audio grow.

.MEMORYMAP
  SLOTSIZE $8000
  DEFAULTSLOT 0
  SLOT 0 $8000
  SLOT 1 $0 $2000
  SLOT 2 $2000 $E000
  SLOT 3 $0 $10000
.ENDME

.ROMBANKSIZE $8000
.ROMBANKS 64                    ; 16 Mbit (2 MB) LoROM - 10 per-level 32KB bg textures
                               ; need their own banks (2 MB is the LoROM ceiling)

.SNESHEADER
  ID "SNES"

  NAME "DEADFALL             "  ; exactly 21 bytes
  ;    "123456789012345678901"

  LOROM
  FASTROM                       ; documentation only: this WLA-DX still emits $20, so
                                ; the Makefile patches $7FD5 -> $30 post-link
                                ; (tools/set_fastrom.py); matches the LoROM_FastROM
                                ; crt0 in the linkfile

  CARTRIDGETYPE $02             ; ROM + RAM + battery (SRAM save: high score + progress)
  ROMSIZE $0B                   ; 16 Megabits (2 MB)
  SRAMSIZE $01                  ; 16 Kilobits (2 KB) battery-backed SRAM
  COUNTRY $01                   ; USA / NTSC
  LICENSEECODE $00
  VERSION $00
.ENDSNES

.SNESNATIVEVECTOR
  COP EmptyHandler
  BRK EmptyHandler
  ABORT EmptyHandler
  NMI VBlank
  IRQ EmptyHandler
.ENDNATIVEVECTOR

.SNESEMUVECTOR
  COP EmptyHandler
  ABORT EmptyHandler
  NMI EmptyHandler
  RESET tcc__start
  IRQBRK EmptyHandler
.ENDEMUVECTOR
