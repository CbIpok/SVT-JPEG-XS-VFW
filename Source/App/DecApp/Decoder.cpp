#include <cstring>
#include <cstdio>

#include "Decoder.h"

Decoder::Decoder(const DecoderConfig_t& cfg)
  : config_(cfg)
{
    // Заглушка, пока ничего не делаем
}

Decoder::~Decoder() {
    // Заглушка, пока ничего не делаем
}

// 2.2.1. Статический метод разбора аргументов
DecoderConfig_t Decoder::parseConfig(int argc, char* argv[]) {
    DecoderConfig_t cfg;
    // Шаг 2.2.1.1: Обнуляем структуру
    std::memset(&cfg, 0, sizeof(cfg));

    // Шаг 2.2.1.2: Проверяем запрос справки
    if (get_help(argc, argv)) {
        // Если нужно вывести help и выйти, оставим cfg с нулями.
        return cfg;
    }

    // Шаг 2.2.1.3: Читаем параметры
    if (read_command_line(argc, argv, &cfg) != 0) {
        std::fprintf(stderr,
                     "Error in configuration, could not begin decoding!\n"
                     "Run %s --help for a list of options\n",
                     argv[0]);
        // В случае ошибки возвращаем cfg, содержащий флаг ошибки
        // (тесты могут это проверить по полям cfg).
    }

    return cfg;
}

SvtJxsErrorType_t Decoder::getFrame() {
    return SvtJxsErrorNone; // заглушка
}
