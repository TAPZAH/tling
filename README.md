# TLing — оффлайн-переводчик для Windows

**TLing** — настольный переводчик, который работает **без интернета** и без
облачных сервисов: все модели выполняются локально на CPU. Windows 10/11
(x64), установщик или портативная папка. Версия **beta 0.99.5**, автор —
**TAP3AH**.

**TLing** is an offline translator for Windows 10/11 (x64). Everything runs
locally on CPU — no internet, no API keys, no telemetry. Version **beta 0.99.5**.

---

## Возможности

- **Четыре оффлайн-движка** — можно переключать в настройках:
  - **Argos Translate** — компактные пакеты по парам языков;
  - **NLLB-200 Distilled 600M** — одна модель Meta на ~200 языков (INT8,
    CTranslate2 + oneDNN);
  - **Firefox Translations** — лёгкие модели Mozilla (tiny/base) через
    нативный Rust-движок `fxtranslate`;
  - **MarianMT** — модели Helsinki-NLP OPUS-MT (CTranslate2, каталог
    Hugging Face).
- **Перевод через английский**, если для пары нет прямой модели.
- **Автоопределение языка источника**: пункт «Авто» в списке «С языка» —
  язык определяется по тексту (детектор на 60+ языков, офлайн) с учётом
  установленных моделей, и список сам переключается на найденный язык.
- **Перевод выделенного текста**: кнопка у курсора, режим «только Ctrl+C+C»,
  настраиваемая клавиша удержания (не требуется / Ctrl / Alt / Shift).
- **Кнопка «Замена»** — перевод вставляется в исходное приложение поверх
  выделения.
- **История копирований** (до 50 записей) в меню трея: копировать /
  перевести / очистить.
- **Автокопирование выделенного текста** — включается в настройках или из
  меню трея.
- **Турбо перевод** — перевод выделенного текста сразу после выделения,
  без кнопки у курсора: включается и выключается в меню трея
  (правый клик по иконке).
- **Многоязычный интерфейс**: русский, English, Deutsch, Français, Español;
  определяется по языку системы.
- **Светлая и тёмная темы** (включая заголовки окон).
- **Автозагрузка** вместе с Windows.
- **Проверка обновлений** через GitHub Releases.
- **Портативный режим** — Python не нужен, настройки и модели рядом с exe.

## Скачать

В релизах на GitHub ([github.com/TAPZAH/tling/releases](https://github.com/TAPZAH/tling/releases)):

| Файл | Что внутри |
|------|------------|
| `tling-0.99.5-beta-setup.exe` | Установщик без моделей (~11 МБ): модели ставятся из окна «Пакеты» |
| `tling-0.99.5-beta-portable.zip` | Портативная папка без моделей (модели ставятся из окна «Пакеты») |
| `tling-0.99.5-beta-src.zip` | Исходники |

Сборки с предустановленными моделями (`setup-with-models.exe` ~900 МБ и
`portable-with-models.zip`) собираются локально скриптом
`cpp\build_release.ps1`.

После распаковки портатива откройте `TLing.exe`. Папки `assets`, `data` и
`licenses` удалять нельзя.

## Как пользоваться

1. Откройте окно «Пакеты» и установите нужные языковые модели
   (или возьмите сборку `setup` с уже включёнными моделями).
2. Выберите движок и языки в главном окне и нажмите «Перевести» — либо
   выделите текст в любой программе и нажмите кнопку у курсора
   (или Ctrl+C+C).
3. В окне результата: «Копировать», «Замена» (вставить в исходное окно),
   «Закрыть»/клик по окну — в зависимости от выбранного режима.

Окно «Настройки» содержит разделы: **Языки**, **Поведение**, **Темы**,
**Язык интерфейса**, **О программе**.

## Сборка из исходников

Нужны CMake 3.20+, Visual Studio 2022, vcpkg и — для движка Firefox —
Rust toolchain. Подробности: [`cpp/README.md`](cpp/README.md).

```powershell
cmake -S cpp -B cpp/build-ctranslate2 -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DENABLE_CTRANSLATE2=ON `
  -DCTranslate2_ROOT=C:/deps/CTranslate2 `
  -DCTranslate2_BUILD_DIR=C:/deps/CTranslate2/build-openblas-dnnl/install/lib/cmake/ctranslate2
cmake --build cpp/build-ctranslate2 --config Release
ctest --test-dir cpp/build-ctranslate2 -C Release --output-on-failure
```

Полный релизный конвейер (портативы, установщик, исходники в `release/`):

```powershell
powershell -ExecutionPolicy Bypass -File .\cpp\build_release.ps1
```

## Лицензии моделей

- Argos Translate — пакеты Argos Open Tech / LibreTranslate;
- NLLB-200 Distilled 600M — модель Meta, конвертация CTranslate2;
- Firefox Translations — модели Mozilla;
- MarianMT — модели Helsinki-NLP OPUS-MT, конвертация CTranslate2.

Сама программа распространяется по лицензии **MIT** — см. `LICENSE`.
Автор — **TAP3AH**. Модели скачиваются отдельно и подчиняются своим лицензиям.
