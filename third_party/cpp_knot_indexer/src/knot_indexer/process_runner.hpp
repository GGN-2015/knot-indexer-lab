#pragma once

#include "path_utils.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace hki {

struct WorkerResult {
    bool success = false;
    bool timedOut = false;
    bool interrupted = false;
    bool cancelled = false;
    int exitCode = -1;
    double seconds = 0.0;
    std::string output;
    std::string error;
};

WorkerResult runWorkerProcess(const std::filesystem::path& executable,
                              const std::string& workerName,
                              const std::string& input,
                              int timeoutSeconds);

class WorkerProcess;

std::unique_ptr<WorkerProcess> startWorkerProcess(const std::filesystem::path& executable,
                                                  const std::string& workerName,
                                                  const std::string& input);
bool pollWorkerProcess(WorkerProcess& process,
                       std::chrono::steady_clock::time_point deadline);
WorkerResult finishWorkerProcess(WorkerProcess& process);
void cancelWorkerProcess(WorkerProcess& process);

} // namespace hki

#include "runtime_control.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace hki {

namespace process_runner_detail {

inline std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string value = buffer.str();
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value;
}

inline void writeFile(const std::filesystem::path& path, const std::string& value) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write temporary input file: " + cki::platform::displayPath(path));
    out << value;
}

inline std::filesystem::path uniqueTempPath(const char* suffix) {
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
#ifdef _WIN32
    unsigned long pid = GetCurrentProcessId();
#else
    unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    std::ostringstream name;
    name << "hki_" << pid << "_" << now << suffix;
    return std::filesystem::temp_directory_path() / name.str();
}

struct TempFiles {
    std::filesystem::path input;
    std::filesystem::path output;
    std::filesystem::path error;

    TempFiles() : input(uniqueTempPath(".in")), output(uniqueTempPath(".out")), error(uniqueTempPath(".err")) {}
    ~TempFiles() {
        std::error_code ec;
        std::filesystem::remove(input, ec);
        std::filesystem::remove(output, ec);
        std::filesystem::remove(error, ec);
    }
};

#ifdef _WIN32

inline std::wstring quoteWindowsArg(const std::wstring& arg) {
    std::wstring out = L"\"";
    unsigned backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(c);
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            backslashes = 0;
            out.push_back(c);
        }
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

#endif

} // namespace process_runner_detail

class WorkerProcess {
public:
    WorkerProcess(const std::filesystem::path& executable,
                  const std::string& workerName,
                  const std::string& input)
        : started_(std::chrono::steady_clock::now()) {
        process_runner_detail::writeFile(temp_.input, input);
        start(executable, workerName);
    }

    ~WorkerProcess() {
        if (!finished_) {
            terminate(false, true);
        }
        closeHandles();
    }

    WorkerProcess(const WorkerProcess&) = delete;
    WorkerProcess& operator=(const WorkerProcess&) = delete;

    bool poll(std::chrono::steady_clock::time_point deadline) {
        if (finished_) return true;
        if (interrupted()) {
            terminate(true, false);
            result_.exitCode = 130;
            finalizeOutput();
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            terminate(false, false);
            result_.timedOut = true;
            result_.exitCode = 124;
            finalizeOutput();
            return true;
        }
        if (pollExit()) {
            finalizeOutput();
            return true;
        }
        return false;
    }

    void cancel() {
        if (finished_) return;
        terminate(false, true);
        result_.cancelled = true;
        result_.exitCode = 125;
        finalizeOutput();
    }

    WorkerResult finish() {
        while (!finished_) {
            poll(std::chrono::steady_clock::time_point::max());
            if (!finished_) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return result_;
    }

    const WorkerResult& result() const {
        return result_;
    }

private:
    process_runner_detail::TempFiles temp_;
    std::chrono::steady_clock::time_point started_;
    WorkerResult result_;
    bool finished_ = false;

#ifdef _WIN32
    PROCESS_INFORMATION pi_{};
#else
    pid_t pid_ = -1;
    int status_ = 0;
#endif

    void start(const std::filesystem::path& executable, const std::string& workerName) {
#ifdef _WIN32
        std::wstring command = process_runner_detail::quoteWindowsArg(executable.wstring()) +
            L" --worker " + process_runner_detail::quoteWindowsArg(std::wstring(workerName.begin(), workerName.end())) +
            L" --input " + process_runner_detail::quoteWindowsArg(temp_.input.wstring()) +
            L" --output " + process_runner_detail::quoteWindowsArg(temp_.output.wstring());

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        si.wShowWindow = SW_HIDE;

        HANDLE stderrHandle = CreateFileW(temp_.error.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                          CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (stderrHandle == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create temporary stderr file");
        SetHandleInformation(stderrHandle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = stderrHandle;

        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');

        BOOL ok = CreateProcessW(executable.wstring().c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi_);
        CloseHandle(stderrHandle);
        if (!ok) {
            throw std::runtime_error("CreateProcessW failed for worker");
        }
#else
        pid_ = fork();
        if (pid_ < 0) throw std::runtime_error("fork failed for worker");
        if (pid_ == 0) {
            std::string exe = executable.string();
            std::string in = temp_.input.string();
            std::string out = temp_.output.string();
            std::string err = temp_.error.string();
            FILE* errFile = std::fopen(err.c_str(), "wb");
            if (errFile) {
                dup2(fileno(errFile), STDERR_FILENO);
                std::fclose(errFile);
            }
            execl(exe.c_str(), exe.c_str(), "--worker", workerName.c_str(), "--input", in.c_str(),
                  "--output", out.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
#endif
    }

    bool pollExit() {
#ifdef _WIN32
        DWORD wait = WaitForSingleObject(pi_.hProcess, 0);
        if (wait != WAIT_OBJECT_0) return false;
        DWORD exitCode = 0;
        GetExitCodeProcess(pi_.hProcess, &exitCode);
        result_.exitCode = static_cast<int>(exitCode);
        return true;
#else
        pid_t done = waitpid(pid_, &status_, WNOHANG);
        if (done != pid_) {
            if (done < 0) {
                result_.exitCode = -1;
                return true;
            }
            return false;
        }
        if (WIFEXITED(status_)) result_.exitCode = WEXITSTATUS(status_);
        else if (WIFSIGNALED(status_)) result_.exitCode = 128 + WTERMSIG(status_);
        return true;
#endif
    }

    void terminate(bool interrupt, bool cancelOnly) {
#ifdef _WIN32
        TerminateProcess(pi_.hProcess, interrupt ? 130 : (cancelOnly ? 125 : 124));
        WaitForSingleObject(pi_.hProcess, INFINITE);
#else
        kill(pid_, SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        kill(pid_, SIGKILL);
        waitpid(pid_, &status_, 0);
#endif
        result_.interrupted = interrupt;
        result_.cancelled = cancelOnly;
        if (interrupt) result_.exitCode = 130;
        else if (cancelOnly) result_.exitCode = 125;
    }

    void finalizeOutput() {
        if (finished_) return;
        result_.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
        result_.success = !result_.timedOut && !result_.interrupted && !result_.cancelled && result_.exitCode == 0;
        if (result_.success) result_.output = process_runner_detail::readFile(temp_.output);
        result_.error = process_runner_detail::readFile(temp_.error);
        finished_ = true;
        closeHandles();
    }

    void closeHandles() {
#ifdef _WIN32
        if (pi_.hThread) {
            CloseHandle(pi_.hThread);
            pi_.hThread = nullptr;
        }
        if (pi_.hProcess) {
            CloseHandle(pi_.hProcess);
            pi_.hProcess = nullptr;
        }
#endif
    }
};

inline std::unique_ptr<WorkerProcess> startWorkerProcess(const std::filesystem::path& executable,
                                                  const std::string& workerName,
                                                  const std::string& input) {
    return std::unique_ptr<WorkerProcess>(new WorkerProcess(executable, workerName, input));
}

inline bool pollWorkerProcess(WorkerProcess& process,
                       std::chrono::steady_clock::time_point deadline) {
    return process.poll(deadline);
}

inline WorkerResult finishWorkerProcess(WorkerProcess& process) {
    return process.finish();
}

inline void cancelWorkerProcess(WorkerProcess& process) {
    process.cancel();
}

inline WorkerResult runWorkerProcess(const std::filesystem::path& executable,
                              const std::string& workerName,
                              const std::string& input,
                              int timeoutSeconds) {
    const auto deadline = timeoutSeconds > 0
        ? std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds)
        : std::chrono::steady_clock::time_point::max();
    std::unique_ptr<WorkerProcess> process = startWorkerProcess(executable, workerName, input);
    while (!pollWorkerProcess(*process, deadline)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return finishWorkerProcess(*process);
}

} // namespace hki
