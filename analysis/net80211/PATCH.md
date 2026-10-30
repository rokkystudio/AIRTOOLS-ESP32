# Патч `libnet80211.a` для raw deauth на ESP32-C6

Документ описывает бинарный патч vendor-библиотеки, который включает передачу
Deauthentication (`0xC0`) и Disassociation (`0xA0`) через `esp_wifi_80211_tx`.
Без патча vendor-проверка `ieee80211_raw_frame_sanity_check()` отбрасывает эти
подтипы management-кадров с кодом `0x102`.

## 1. Зачем нужен патч

Цепочка вызова:

```text
esp_wifi_80211_tx()
└─ ieee80211_raw_frame_sanity_check()   <-- whitelist подтипов
   └─ ic_ebuf_alloc() ... ppTxPkt() ... LMAC ... HAL
```

`sanity_check` пропускает только management-подтипы `0x40/0x50` (probe),
`0x80` (beacon) и `0xD0` (action). Deauth `0xC0` и disassoc `0xA0` попадают
в ветку `.L10xx` с логом `unsupport frame type: %x%x` и кодом `0x102`
(`ESP_ERR_INVALID_ARG`), поэтому кадр вообще не доходит до радио.

## 2. Целевая библиотека

| Параметр | Значение |
|---|---|
| PlatformIO package | `framework-arduinoespressif32-libs` 5.3.0+sha.083aad99cf |
| Набор | Arduino Core 3.1.0, IDF `release_v5.3` (platform-espressif32 53.3.10) |
| Архив | `C:\Users\rokky\.platformio\packages\framework-arduinoespressif32-libs\esp32c6\lib\libnet80211.a` |
| Размер / SHA-256 | 1 333 844 байт / `dc4d02328e1ea3ec1d87487a52ef1d9de720f9777ab4a6452470791bb6cb02d6` |
| Членов в архиве | 52 |
| Целевой член | `ieee80211_output.o`, 86 712 байт, SHA-256 `f750817425dd3f454c54fb245c6f689425739557a739f274e6545e0a5afb9c25` |
| Секция | `.text.ieee80211_raw_frame_sanity_check`, файловое смещение `0x2FB0`, размер `0x20A` |

Именно этот архив линкуется текущей сборкой `esp32-c6-touch-lcd147`
(подтверждено по `firmware.map`).

## 3. Сам патч

Три 4-байтные инструкции внутри `ieee80211_raw_frame_sanity_check`:

| Смещение в секции | Смещение в файле | Было | Стало | Инструкция |
|---:|---:|---|---|---|
| `0x0E4` | `0x3094` | `0x0F06F793` | `0x0C06F793` | `andi a5, a3, 0xC0` |
| `0x120` | `0x30D0` | `0x0D000693` | `0x0C000613` | `li a2, 0xC0` |
| `0x124` | `0x30D4` | `0x02D78C63` | `0x02C68C63` | `beq a3, a2, .L1040` |

Контекст после патча:

```asm
0x0E4  and  a5, a3, 0xC0     ; a5 = FC0 & 0xC0
0x104  li   a2, 8
0x106  beq  a4, a2, .L1038   ; Data -> отдельная ветка
0x10A  bnez a4, .L1039       ; Control/Extension -> reject
0x10C  li   a2, 0x80
0x110  beq  a5, a2, .L1040   ; 0x80, 0x90, 0xA0, 0xB0
0x114  and  a3, a3, 0xE0     ; a3 = FC0 & 0xE0
0x118  li   a2, 0x40
0x11C  beq  a3, a2, .L1040   ; 0x40, 0x50
0x120  li   a2, 0xC0
0x124  beq  a3, a2, .L1040   ; 0xC0, 0xD0
0x128  .L1039                ; reject -> 0x102
```

Итоговый whitelist management-подтипов:

| Подтип | Было | Стало |
|---|---|---|
| `0x40`, `0x50` | allow | allow |
| `0x80` Beacon | allow | allow |
| `0x90` ATIM | reject | **allow** |
| `0xA0` Disassoc | reject | **allow** |
| `0xB0` Auth | reject | **allow** |
| `0xC0` Deauth | reject | **allow** |
| `0xD0` Action | allow | allow |
| `0xE0`, `0xF0`, остальные | reject | reject |

Data/QoS-ветка (`0x146`: `sll/sra/bgez`) проверяет только бит 7 регистра `a5`,
а он равен биту 7 `FC0` при любой из масок `0xF0`/`0xC0`, поэтому вердикт для
Data-кадров не меняется.

## 4. Почему патч не ломает линковку

- На `0x0E4` и `0x120` релокаций нет.
- На `0x124` стоит `R_RISCV_BRANCH` на `.L1040`. Линкер пересчитывает только
  immediate-биты ветки (`S + A - P`); поля `rs1`/`rs2` не трогаются, а
  immediate в патче сохранён (`+0x38` до `0x15C`).
- Инструкции 4-байтные, размер секции, объекта и архива не меняется,
  число членов архива остаётся 52.

## 5. Что происходит после whitelist

`sanity_check` — не единственный фильтр, но для нашего сценария остальные
проверки не срабатывают:

1. длина кадра `24..1500` — deauth-кадр 26 байт;
2. бит Protected (`FC1 & 0x40`) — 0;
3. `memcmp(own_mac, frame+10, 6)`: `frame+10` — это Address 2, в
   `WifiAir::sendDeauth()` там BSSID, а не MAC интерфейса. Функция возвращает
   `0` сразу, не применяя проверки `en_sys_seq`, `FC1 & 0x3C` и ToDS/FromDS;
4. `WifiAir::transmitRawFrame()` всё равно вызывает
   `esp_wifi_80211_tx(WIFI_IF_STA, frame, len, true)`, то есть `en_sys_seq=true`;
5. `FC1` кадра равен `0x00`.

## 6. Установка и резервные копии

Скрипт: `analysis/net80211/patchcheck/apply_net80211_patch.ps1`.

```powershell
# только собрать патченный архив и проверить его
.\apply_net80211_patch.ps1

# собрать, проверить и заменить файл в PlatformIO (с бэкапом)
.\apply_net80211_patch.ps1 -Install
```

### Каталог замены

```text
C:\Users\rokky\.platformio\packages\framework-arduinoespressif32-libs\esp32c6\lib\libnet80211.a
```

### Резервные копии

```text
C:\Users\rokky\.platformio\packages\framework-arduinoespressif32-libs\esp32c6\lib\libnet80211.a.orig-backup
D:\PROJECTS\AIRTOOLS-ESP32\analysis\net80211\backup\libnet80211.a.orig
```

Обе копии — оригинал, SHA-256 `dc4d02328e1ea3ec1d87487a52ef1d9de720f9777ab4a6452470791bb6cb02d6`.

### Патченные файлы в репозитории

```text
analysis/net80211/patchcheck/libnet80211.patched.a   SHA-256 c8f14b954b1598c02e6aefa0dded4ab9feaaf78bf25e67fd7f4228dfb84d3c05
analysis/net80211/patchcheck/ieee80211_output.patched.o
```

### Восстановление оригинала

```powershell
Copy-Item "C:\Users\rokky\.platformio\packages\framework-arduinoespressif32-libs\esp32c6\lib\libnet80211.a.orig-backup" `
          "C:\Users\rokky\.platformio\packages\framework-arduinoespressif32-libs\esp32c6\lib\libnet80211.a" -Force
Remove-Item "D:\PROJECTS\AIRTOOLS-ESP32\.pio\build\esp32-c6-touch-lcd147" -Recurse -Force
```

Если пакет PlatformIO будет переустановлен, архив вернётся к оригиналу —
достаточно повторно выполнить `apply_net80211_patch.ps1 -Install`.

## 7. Проверка

Дизассемблер по патченному члену архива:

```powershell
$tmp = Join-Path $env:TEMP "net80211-verify"
New-Item -ItemType Directory -Path $tmp -Force | Out-Null
Set-Location $tmp
& "C:\Users\rokky\.platformio\packages\toolchain-riscv32-esp\bin\riscv32-esp-elf-ar.exe" `
  x "D:\PROJECTS\AIRTOOLS-ESP32\analysis\net80211\patchcheck\libnet80211.patched.a" ieee80211_output.o
& "C:\Users\rokky\.platformio\packages\toolchain-riscv32-esp\bin\riscv32-esp-elf-objdump.exe" `
  -d -j .text.ieee80211_raw_frame_sanity_check --start-address=0x104 --stop-address=0x130 ".\ieee80211_output.o"
```

Ожидаемый фрагмент:

```asm
114: 0e06f693  and a3,a3,224
118: 04000613  li  a2,64
11c: 04c68063  beq a3,a2,15c
120: 0c000613  li  a2,192
124: 02c68c63  beq a3,a2,15c
```

Для уже слинкованной прошивки:

```powershell
& "C:\Users\rokky\.platformio\packages\toolchain-riscv32-esp\bin\riscv32-esp-elf-objdump.exe" `
  -d --disassemble=ieee80211_raw_frame_sanity_check `
  "D:\PROJECTS\AIRTOOLS-ESP32\.pio\build\esp32-c6-touch-lcd147\firmware.elf"
```

Проверка SHA-256 установленного архива:

```powershell
(Get-FileHash -Algorithm SHA256 `
  "C:\Users\rokky\.platformio\packages\framework-arduinoespressif32-libs\esp32c6\lib\libnet80211.a").Hash
# c8f14b954b1598c02e6aefa0dded4ab9feaaf78bf25e67fd7f4228dfb84d3c05
```

### Проверка на устройстве

- Чип: ESP32-C6FH4 (QFN32), rev v0.2, MAC `ac:eb:e6:2b:68:00`, порт `COM7`.
- Сборка `esp32-c6-touch-lcd147`: SUCCESS, Flash 42.5 %, RAM 29.8 %.
- `firmware.bin` SHA-256: `bbb548c07bd9140efa187754fe37e2aa36d84f3c32fc6896d4318d7343987845`.
- Обратное чтение флеша (`esptool read_flash 0x10000 1459536`) вернуло тот же SHA-256,
  то есть на устройстве ровно собранный образ с патченным whitelist.

## 8. Вторая (старая) версия архива

В `analysis/net80211/libnet80211_unpacked/` лежит другой архив: 50 объектов,
1 428 030 байт, SHA-256 `9535c632d74bbf7d4997dd8306a27227ad588b80e0c60c03b7a1e5a41c5c0e4a`.
Текущая сборка его **не использует** — PlatformIO 53.3.10 линкует
`framework-arduinoespressif32-libs` 5.3.0 (52 объекта).

Скрипт `apply_net80211_patch.ps1` распознаёт оба варианта по SHA-256 члена
`ieee80211_output.o`; для старого варианта смещения другие
(`0x32B8`, `0x32F4`, `0x32F8`). Подробный разбор старого варианта — в
`analysis/net80211/patchcheck/REPORT.md`.

## 9. Ограничения

- Вместе с `0xA0`/`0xC0` неизбежно разрешаются `0x90` (ATIM) и `0xB0`
  (Authentication): при трёх слотах сравнения и масках `0xC0`/`0xE0` иначе
  `0xA0` + `0xC0` не добавить без роста кода.
- Патч меняет vendor-библиотеку. Держите резервные копии из раздела 6.
- Патч проверен на ESP32-C6; для других чипов используются свои архивы
  `libnet80211.a` и смещения там не совпадают.
