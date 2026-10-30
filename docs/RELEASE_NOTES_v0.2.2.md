# AIRTOOLS ESP32-C6 v0.2.2

Прошивка для профиля `esp32-c6-touch-lcd147` и пропатченная vendor-библиотека
`libnet80211.a`, открывающая передачу Deauthentication/Disassociation через
`esp_wifi_80211_tx`.

## Изменения относительно v0.2.1

- `/networks` объявляет `ssid_encoding=hex` и передаёт сохранённые байты SSID
  шестнадцатеричными символами. Формат не зависит от корректности UTF-8 в имени
  точки доступа.
- Версия `v0.2.2` на дисплее выводится шрифтом того же размера, что и заголовок
  `AIRTOOLS`.
- В ответе `/status` публикуется `firmware=0.2.2` и `version=0.2.2`.
- Числовая версия в конфигурации PlatformIO — `0x000202`.

Включает все изменения v0.2.1: устойчивый поиск LLC/SNAP в EAPOL-кадрах
(QoS, HE/HT Control, A-MSDU), явный promiscuous-фильтр `ALL`, очередь захвата
16 кадров и `/replay` по умолчанию на 10 deauth-кадров.

## Совместимость

| Компонент | Версия |
|---|---|
| Плата | ESP32-C6, 8 MB flash, `default_8MB.csv` |
| platform-espressif32 (pioarduino) | 53.3.10 |
| Arduino Core | 3.1.0 |
| framework-arduinoespressif32-libs | 5.3.0+sha.083aad99cf (IDF release_v5.3) |
| NimBLE-Arduino | 2.3.8 |
| GFX Library for Arduino | 1.6.8 |
| Android-клиент | AIRTOOLS 1.14+ (понимает `ssid_encoding=hex`) |

Старые версии приложения покажут SSID в hex-виде; BLE-протокол, UUID и маркер
конца ответа не меняются.

## Файлы релиза

| Файл | Назначение | SHA-256 |
|---|---|---|
| `airtools-esp32-c6-v0.2.2-full.bin` | полный образ с `0x0` (bootloader + partitions + boot_app0 + app) | `36d9ce2c887f3037574e236565c1fb651988acdd7193dedf25905c76297029bd` |
| `airtools-esp32-c6-v0.2.2-app.bin` | только приложение (offset `0x10000`) | `4cd3c20db5d2529cb52246103121f197f6ef5cff8da91080e88a0dff41fcd931` |
| `libnet80211-esp32c6-libs-5.3.0-patched.a` | пропатченная `libnet80211.a` | `c8f14b954b1598c02e6aefa0dded4ab9feaaf78bf25e67fd7f4228dfb84d3c05` |
| `ieee80211_output-esp32c6-libs-5.3.0-patched.o` | пропатченный член архива | `143086488637c90f3d147951574e54db2fcd0f2212ca2e4e1d6500a89887995e` |

### Прошивка

Полный образ (чистая плата):

```powershell
esptool.py --chip esp32c6 -p COM7 -b 460800 write_flash 0x0 airtools-esp32-c6-v0.2.2-full.bin
```

Только приложение (обновление существующей прошивки):

```powershell
esptool.py --chip esp32c6 -p COM7 -b 460800 write_flash 0x10000 airtools-esp32-c6-v0.2.2-app.bin
```

### Библиотека

`libnet80211-esp32c6-libs-5.3.0-patched.a` собран **ровно** из
`framework-arduinoespressif32-libs` 5.3.0+sha.083aad99cf:

- оригинальный архив: `dc4d02328e1ea3ec1d87487a52ef1d9de720f9777ab4a6452470791bb6cb02d6`
- член `ieee80211_output.o` до патча: `f750817425dd3f454c54fb245c6f689425739557a739f274e6545e0a5afb9c25`
- член после патча: `143086488637c90f3d147951574e54db2fcd0f2212ca2e4e1d6500a89887995e`
- патч: три инструкции в `ieee80211_raw_frame_sanity_check`

| Смещение в секции | Смещение в файле | Было → Стало | Инструкция |
|---:|---:|---|---|
| `0x0E4` | `0x3094` | `0F06F793` → `0C06F793` | `andi a5, a3, 0xC0` |
| `0x120` | `0x30D0` | `0D000693` → `0C000613` | `li a2, 0xC0` |
| `0x124` | `0x30D4` | `02D78C63` → `02C68C63` | `beq a3, a2, .L1040` |

Для другой версии `framework-arduinoespressif32-libs` эта сборка не подходит —
смещения и регистры там другие.

Установка с резервной копией:

```powershell
.\analysis\net80211\patchcheck\apply_net80211_patch.ps1 -Install
```

Скрипт проверяет SHA-256, патчит только `ieee80211_output.o`, проверяет, что
остальные 51 член архива не изменились, и сохраняет оригинал в
`libnet80211.a.orig-backup` рядом с библиотекой и в
`analysis/net80211/backup/libnet80211.a.orig`.

Подробный разбор: `analysis/net80211/PATCH.md`.

## Проверка этого релиза

- Чистая сборка `pio run -e esp32-c6-touch-lcd147` из коммита `a7ae2e5` — SUCCESS.
- SHA-256 `libnet80211.a` до и после сборки: `c8f14b95…` — патч на месте,
  PlatformIO библиотеку не переустанавливал.
- В `firmware.elf` присутствует патченная последовательность
  (`and a5,a3,192`, `and a3,a3,224`, `li a2,192`, `beq a3,a2`).
- В образе есть `ssid_encoding=hex`, версия `0.2.2`, строка `OK replay bssid=`
  и отсутствует `esp32_c6_raw_deauth_not_supported`.

## Ограничения

- Вместе с `0xA0`/`0xC0` whitelist разрешает `0x90` (ATIM) и `0xB0` (Auth) —
  это неизбежно при трёх слотах сравнения с масками `0xC0`/`0xE0`.
- Захват handshake зависит от радиоэфира: клиент должен переподключиться на том
  же канале 2.4 ГГц и пройти 4-way handshake, пока устройство слушает.
- Патч меняет vendor-библиотеку: после переустановки пакета PlatformIO
  библиотеку нужно пропатчить заново.
