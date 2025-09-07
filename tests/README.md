Тесты интеграции: переопределение входов и конфигураций

- Каталог `tests/data/override/` позволяет локально подменять большие файлы и конфиги для интеграционных тестов буферных приложений без изменения репозитория.
- Поддерживаются переменные окружения для явной передачи путей к конфигам:
  - `BUFFER_TEST_ENC_CFG` — путь к `enc.cfg` для энкодера
  - `BUFFER_TEST_DEC_CFG` — путь к `dec.cfg` для декодера

Правила выбора файлов

- Конфиги `enc.cfg`/`dec.cfg` читаются в таком порядке приоритета:
  1) путь из переменной окружения (`BUFFER_TEST_ENC_CFG`/`BUFFER_TEST_DEC_CFG`), если задан и существует;
  2) `tests/data/override/enc.cfg` или `tests/data/override/dec.cfg`, если существуют;
  3) стандартные файлы из `tests/data/enc.cfg` и `tests/data/dec.cfg`.
- Если используются переопределённые большие файлы `tests/data/override/input.yuv` или `tests/data/override/stream.jxs` (уже есть в репозитории, 3840×2160), то:
  - требуется наличие соответствующих `enc.cfg`/`dec.cfg` в том же каталоге `tests/data/override/`, если не заданы переменные окружения.

Примеры

- Использовать предустановленные большие файлы и конфиги из override:
  - положить/проверить наличие `tests/data/override/input.yuv`, `tests/data/override/stream.jxs`, `tests/data/override/enc.cfg`, `tests/data/override/dec.cfg`;
  - запустить тесты обычным способом — они автоматически выберут override-конфиги.
- Использовать произвольные пути к конфигам:
  - `set BUFFER_TEST_ENC_CFG=C:\\path\\to\\enc.cfg`
  - `set BUFFER_TEST_DEC_CFG=C:\\path\\to\\dec.cfg`

Примечания

- Локальные конфиги `tests/data/override/*.cfg` игнорируются Git (см. `.gitignore`). Сами большие файлы (`input.yuv`, `stream.jxs`) уже находятся в репозитории.
