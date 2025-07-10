#ifndef DECODER_H
#define DECODER_H

extern "C"
{
#include "DecParamParser.h"
#include "SemaphoreApp.h"
#include "UtilityApp.h"
}
// Используем только типы и макросы из DecAppMain.c
class Decoder {
public:
    static DecoderConfig_t parseConfig(int argc, char* argv[]);

    explicit Decoder(const DecoderConfig_t& cfg);
    ~Decoder();
    // Пока просто заглушка
    SvtJxsErrorType_t getFrame();
private:
    DecoderConfig_t config_;
    // позже добавим handle, пул, поток и т.п.
};

#endif // DECODER_H
