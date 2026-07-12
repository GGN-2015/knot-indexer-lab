#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace cki::platform {

struct ProgramArg {
    std::string text;
    std::filesystem::path path;
};

#ifdef _WIN32

inline std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return "";
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("failed to convert Windows Unicode text to UTF-8");
    std::string output(static_cast<std::size_t>(size), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                            output.data(), size, nullptr, nullptr);
    if (written != size) throw std::runtime_error("failed to convert Windows Unicode text to UTF-8");
    return output;
}

inline std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return L"";
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) throw std::runtime_error("failed to convert UTF-8 text to Windows Unicode");
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), output.data(), size);
    if (written != size) throw std::runtime_error("failed to convert UTF-8 text to Windows Unicode");
    return output;
}

#endif

inline std::string utf8StringFromPath(const std::filesystem::path& path) {
    auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

inline std::string displayPath(const std::filesystem::path& path) {
#ifdef _WIN32
    if constexpr (std::is_same_v<std::filesystem::path::value_type, wchar_t>) {
        return wideToUtf8(path.native());
    } else {
        return utf8StringFromPath(path);
    }
#else
    return path.string();
#endif
}

inline std::vector<ProgramArg> programArguments(int argc, char** argv) {
    std::vector<ProgramArg> args;
#ifdef _WIN32
    int wideArgc = 0;
    LPWSTR* wideArgv = CommandLineToArgvW(GetCommandLineW(), &wideArgc);
    if (wideArgv) {
        args.reserve(static_cast<std::size_t>(wideArgc));
        for (int i = 0; i < wideArgc; ++i) {
            std::wstring wide = wideArgv[i] ? std::wstring(wideArgv[i]) : std::wstring();
            args.push_back(ProgramArg{wideToUtf8(wide), std::filesystem::path(wide)});
        }
        LocalFree(wideArgv);
        return args;
    }
#endif

    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        std::string text = argv && argv[i] ? std::string(argv[i]) : std::string();
        args.push_back(ProgramArg{text, std::filesystem::path(text)});
    }
    return args;
}

}  // namespace cki::platform
