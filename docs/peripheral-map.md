# GD32F303CB peripheral register map

Reference for the on-chip peripherals this firmware touches — distilled from the GD32F30x
standard peripheral library / datasheet, with the values confirmed against our own dump called
out. GD32F303 is register-compatible with STM32F103 (GD just renames the registers).

## Peripheral bases
| Periph | Base | Bus / clock-enable |
|---|---|---|
| CAN0 | 0x40006400 | APB1 |
| USART0 | 0x40013800 | APB2 (RCU_APB2EN bit14) |
| USART1 | 0x40004400 | APB1 (RCU_APB1EN bit17) — **the handle-link TX** |
| USART2 | 0x40004800 | APB1 (RCU_APB1EN bit18) |
| SPI0 | 0x40013000 | APB2 (bit12) |
| SPI1 | 0x40003800 | APB1 (bit14) |
| SPI2 | 0x40003C00 | APB1 (bit15) |
| GPIOA / B / C | 0x40010800 / 0x40010C00 / 0x40011000 | |
| AFIO | 0x40010000 | |
| RCU | 0x40021000 | AHBEN@0x14, APB2EN@0x18, APB1EN@0x1C |
| FMC | 0x40022000 | see below |

## Register offsets
- **USART:** STAT0=0x00 (TBE bit7, RBNE bit5), DATA=0x04, BAUD=0x08, CTL0=0x0C (RBNEIE bit5).
- **GPIO:** CTL0=0x00, CTL1=0x04, ISTAT (in)=0x08, OCTL (out)=0x0C, BOP (set)=0x10, BC (clear)=0x14.
- **SPI:** CTL0=0x00, CTL1=0x04, STAT=0x08 (TBE bit1, RBNE bit0, BSY bit7), DATA=0x0C.

## FMC (flash controller — the self-flasher)
Offsets from 0x40022000: WS=0x00, KEY0=0x04, STAT0=0x0C, CTL0=0x10, ADDR0=0x14, WP=0x20.
- CTL0 bits: PG=0, PER=1, MER=2, OBPG=4, OBER=5, **START=6**, **LK=7**.
- STAT0 bits: **BUSY=0**, PGERR=2, WPERR=4, ENDF=5.
- Unlock keys stored to KEY0: `0x45670123` then `0xCDEF89AB` (little-endian bytes `23 01 67 45`,
  `AB 89 EF CD` — searchable in the image) → lands on `fmc_unlock` (0x08000c50). Page = **2 KB
  (0x800)**, 64 pages total.
- Update loop = unlock → per page { wait BUSY; CTL0|=PER; ADDR0=page; CTL0|=START; wait BUSY;
  CTL0&=~PER; then word-program: wait BUSY; CTL0|=PG; `*(u32*)addr = data`; wait BUSY; CTL0&=~PG }
  → CTL0|=LK.

## CAN0 receive idiom (the Tesla-bus decode path)
RX0 IRQ is unused (default handler) — the firmware **polls**: read RFIFO0 (0x4000640C), mask
bits[1:0] = pending count; if >0 read the RX mailbox (RFIFOMI0=+0x1B0, MP0=+0x1B4, MDATA0=+0x1B8,
MDATA1=+0x1BC); **release the frame by setting RFIFO0 bit5 (RFD0)**. RX struct: sfid(u32) efid(u32)
ff(u8) ft(u8) dlen(u8) fi(u8) data[8]. Filters: FCTL=+0x200, FMCFG=+0x204, FSCFG=+0x20C,
FAFIFO=+0x214, FW=+0x21C, filter banks @+0x240.

## GD25Q16 SPI-NOR (the radios' external flash)
The **FR8010HA** BLE SoC (phone link) boots from an external SPI NOR — this GD25Q16; the **PHY6212**
(handle link) runs from internal flash. Command set: 0x03 read, 0x02 page-program, 0x20 4 KB erase,
0x06 WREN, 0x05 RDSR (WIP=bit0), 0x9F JEDEC ID (`EF 40 15`).

## FatFs (the CX_CAN.BIN reader in the bootloader)
elm-chan FatFs. `f_mount(FATFS*, path, opt)`, `f_open(FIL*, path, mode)` (FA_READ=0x01),
`f_read(FIL*, buf, btr, br*)`, `f_lseek(FIL*, ofs)`, `f_close(FIL*)`. FRESULT: OK=0, DISK_ERR=1,
NO_FILE=4, NO_FILESYSTEM=13. On-disk 8.3 name = `"CX_CAN  BIN"` (space-padded, no dot); C literal
`"CX_CAN.BIN"`. FIL ≈ 552 B (40 B header + 512 B buffer), FATFS ≈ 560 B — the 512-byte tail buffer
is the struct's giveaway in the dump.
