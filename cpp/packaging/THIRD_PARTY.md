# Сторонние лицензии (C++ Win32)

В портативной папке и установщике рядом с `TLing.exe`
лежат копии текстов лицензий (каталог `licenses/`), если скрипт упаковки
нашёл их в дереве зависимостей. Ниже — указатели на исходные проекты.
Авторские права принадлежат их правообладателям; этот файл не заменяет
и не переписывает их лицензии.

## Приложение

- **TLing** — MIT. Текст: `LICENSE` в корне репозитория
  (`licenses/LICENSE-offline-translator.txt` в поставке).

## Библиотеки инференса и сериализации

| Компонент | Лицензия (upstream) | Откуда брать текст |
| --- | --- | --- |
| [CTranslate2](https://github.com/OpenNMT/CTranslate2) | MIT | `CTranslate2/LICENSE` |
| [OpenBLAS](https://github.com/OpenMathLib/OpenBLAS) | BSD-3-Clause | vcpkg `share/openblas/copyright` |
| [Protocol Buffers](https://github.com/protocolbuffers/protobuf) | BSD-3-Clause | vcpkg `share/protobuf/copyright` |
| [Abseil](https://github.com/abseil/abseil-cpp) | Apache-2.0 | vcpkg `share/abseil/copyright` |
| [SentencePiece](https://github.com/google/sentencepiece) | Apache-2.0 | vcpkg `share/sentencepiece/copyright` (статически связан) |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | vcpkg `share/nlohmann-json/copyright` (header-only) |
| utf8_range (зависимость protobuf) | как у protobuf | vcpkg `share/utf8-range/copyright` |

Модели Argos Translate и NLLB-200, если вы кладёте их в `data/`,
распространяются на условиях их авторов (не часть MIT-лицензии приложения).

## Среда выполнения Microsoft

Рядом с exe копируются распространяемые DLL Visual C++
(`vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`,
`msvcp140_atomic_wait.dll`) и OpenMP (`vcomp140.dll`) из каталога
VC Redist Visual Studio. Это компоненты Microsoft; условия — лицензия
Visual Studio / Visual C++ Redistributable, не лицензия этого проекта.

UCRT (`api-ms-win-crt-*.dll`, `ucrtbase.dll`) входит в Windows 10/11
и в поставку не копируется.
