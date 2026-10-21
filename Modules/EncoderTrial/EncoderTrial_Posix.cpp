// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The encoder trial elsewhere: it tries the encoders Windows exposes through Media Foundation, so it only runs there

#include "EncoderTrial.h"

#include <cstdio>

int RunEncoderTrial(int /*argc*/, char** /*argv*/)
{
    std::printf("The encoder trial tries Windows' video encoders, so it only runs on Windows.\n");
    return 1;
}
