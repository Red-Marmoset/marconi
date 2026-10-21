// Copyright (c) 2026 Red Marmoset Studios Ltd.
// EncoderTrial: tries each H.264 encoder Windows offers and reports which low-latency settings it takes and how fast
// it is (PERFORMANCE_PLAN.md step 1, "the encoder trial"). A measuring tool, not part of Marconi.

#include "EncoderTrial.h"

int main(int argc, char** argv)
{
    return RunEncoderTrial(argc, argv);
}
