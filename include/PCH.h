#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX


#include <RE/Starfield.h>
#include <REL/Relocation.h>
#include <SFSE/SFSE.h>
#include <Windows.h>

#include <SimpleIni.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace logger = SFSE::log;
using namespace std::literals;
