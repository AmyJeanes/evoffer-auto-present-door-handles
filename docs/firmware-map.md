# Firmware map (GD32F303CB, 128 KB flash @ 0x08000000)

Verified full SWD dump: `reference/gd32_backup.bin` (sha256 `970934547eb7…`, 131072 B).
Factory app carved out to `reference/CX_CAN_original.bin`.

## Regions
| Range | Contents |
|---|---|
| 0x08000000–~0x08003800 | **Bootloader** ("BIOS beta2 2023-12-16"). Vector table @0x08000000 (SP 0x20004380, reset 0x08000120). SD updater + FMC self-flasher. |
| 0x08004000–0x08005000 | **Settings page** (mostly erased 0xFF). 30-byte record, magic `a0 a6` + checksum — LED mode + turn-flash enable. |
| **0x08005000**–~0x0800b200 | **App** ("CX_CAN v101 Beta5 2024-1208"). Own vector table @0x08005000 (SP 0x20002638, reset 0x08005100, default IRQ 0x0800511a). ~25 KB. |
| 0x0801f000 | config page |
| rest | erased 0xFF |

App reserved slot = **52 KB (0xd000) at 0x08005000** — the bootloader erases exactly this on update.

## Key functions (names as set in the Ghidra project)
### App
| Addr | Name | Role |
|---|---|---|
| 0x08005100 | App_Reset_Handler | app entry |
| 0x08005924 | CAN_decode_car_tsl | Tesla CAN frame decoder (car_tsl.c) |
| 0x08007ea8 | HandleCmd_BuildFrame | build the 23-byte handle command frame |
| 0x08006614 | HandleCmd_TX_start_DMA | kick DMA0 CH6 (= USART1 TX) |
| 0x08009e90 | HandleCmd_Dispatch | per-handle animation state machine |
| 0x0800a220 | Main_periodic_tick | main tick: CAN drain + handle tasks |
| 0x0800a2a0 | CAN_RX_poll | drain the 60-entry CAN RX ring |
| 0x0800a17c | BLE_link_RX_task | radio RX: phone frames + tasks |
| 0x080085dc | BLE_phone_rx_handler | phone 'U' frames: auth + OTA |
| 0x0800a3a8 | SWC_KeyIndex_RGB_gesture | steering-wheel RGB gesture |
| 0x0800a654 | Schedule_handle_action | queue a handle action slot |
| 0x0800acd4 | Save_settings_to_flash | write the 30-byte a0a6 settings record |
| 0x080073b4 | Wireless_wake_car_comms | timing-critical bit-banged wake/comms |

### Bootloader
| Addr | Name | Role |
|---|---|---|
| 0x08003298 | Boot_main | boot/update orchestrator |
| 0x0800194c | SD_flash_CX_CAN_raw | flash CX_CAN.BIN raw → 0x08005000 |
| 0x08003550 | Boot_check_and_jump_to_app | app SP sanity check + jump |
| 0x08002fe8 | Boot_apply_staged_OTA | apply a BLE-staged image (magic AA/A5/BB) |
| 0x08003650 | Flash_write_bytes | flash writer |
| 0x0800316c | Flash_erase_region | flash erase |

## Key RAM
| Addr | Name |
|---|---|
| 0x20000108 | g_car_tsl_state (struct base) |
| +4 → 0x2000010c | g_lock_state |
| +5 → 0x2000010d | g_gear_is_drive (0=Park, 1=Drive) |
| +0x18 → 0x20000120 | g_park_present_flag |
| 0x2000003d | g_present_req_counter — the pop trigger, forwarded as frame byte 14 |
| 0x20000020 | PA8 Bluetooth-proximity state |
| 0x20000064 | PC14 button indicator |
| 0x20000a1a | handle-command frame TX buffer (23 B) |
