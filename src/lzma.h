#pragma once
#include <stdint.h>
#include <string>

bool lzmaCompress(std::string inPath, std::string outPath, uint32_t preset);