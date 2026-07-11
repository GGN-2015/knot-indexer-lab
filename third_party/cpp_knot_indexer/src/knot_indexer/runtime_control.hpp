#pragma once

#include <atomic>
#include <csignal>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hki {

inline void installInterruptHandlers();
inline bool interrupted();
inline void requestInterrupt();

namespace runtime_control_detail {

inline std::atomic_bool gInterrupted{false};

inline void signalHandler(int) {
    gInterrupted.store(true);
}

#ifdef _WIN32
inline BOOL WINAPI consoleHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT ||
        type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        gInterrupted.store(true);
        return TRUE;
    }
    return FALSE;
}
#endif

}  // namespace runtime_control_detail

inline void installInterruptHandlers() {
    std::signal(SIGINT, runtime_control_detail::signalHandler);
    std::signal(SIGTERM, runtime_control_detail::signalHandler);
#ifdef _WIN32
    SetConsoleCtrlHandler(runtime_control_detail::consoleHandler, TRUE);
#endif
}

inline bool interrupted() {
    return runtime_control_detail::gInterrupted.load();
}

inline void requestInterrupt() {
    runtime_control_detail::gInterrupted.store(true);
}

}  // namespace hki
