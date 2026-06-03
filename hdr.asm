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

  SLOWROM
  LOROM

  CARTRIDGETYPE $00             ; ROM only (set $02 if SRAM added)
  ROMSIZE $0B                   ; 16 Megabits (2 MB)
  SRAMSIZE $00
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
