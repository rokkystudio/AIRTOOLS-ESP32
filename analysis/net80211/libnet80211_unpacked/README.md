# Распакованная библиотека libnet80211.a для ESP32-C6

Папка содержит **все** ELF-объекты из текущей vendor-библиотеки PlatformIO.

- `original/libnet80211.a` — точная копия исходного архива.
- `objects/` — извлечённые RISC-V ELF32 relocatable файлы `.o`.
- `manifest.json` — SHA-256, размеры и порядок объектов.

Источник: `C:\Users\rokky\.platformio\packages\framework-arduinoespressif32-libs\esp32c6\lib\libnet80211.a`.

SHA-256 архива: `9535c632d74bbf7d4997dd8306a27227ad588b80e0c60c03b7a1e5a41c5c0e4a`.

Объектов: **50**. Каждый объект побайтно сверен с исходным членом архива.

## Импорт в Ghidra

Для анализа политики raw-TX импортируйте `objects/ieee80211_output.o` через `File -> Import File`.
Формат: `Executable and Linking Format (ELF)`, язык: `RISCV:LE:32:AndeStar_v5`, compiler spec: `gcc`.
Целевая функция: `ieee80211_raw_frame_sanity_check`.

## Воспроизведение извлечения

Из каталога `objects` выполнить в PowerShell:

```powershell
& "C:\Users\rokky\.platformio\packages\toolchain-riscv32-esp\bin\riscv32-esp-elf-ar.exe" x "..\original\libnet80211.a"
```

Команда извлекает все элементы архива в текущую папку. Для проверки байтов и SHA-256 используйте `manifest.json`.
Оригинальный архив внутри PlatformIO и существующие результаты предыдущего исследования не меняются.
