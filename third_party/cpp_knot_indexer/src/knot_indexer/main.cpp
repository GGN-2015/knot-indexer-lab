#include "database.hpp"
#include "homfly_backend.hpp"
#include "khovanov_backend.hpp"
#include "pd_simplify_backend.hpp"
#include "pd_code.hpp"
#include "path_utils.hpp"
#include "process_runner.hpp"
#include "runtime_control.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace hki {

namespace {

struct Options {
    std::string pdText;
    std::filesystem::path dataFolder;
    int timeoutSeconds = 60;
    bool verbose = false;
    bool printInvariants = false;
    bool banSimplify = false;
};

struct DataPaths {
    std::filesystem::path homflyDb;
    std::filesystem::path khovanovDb;
    std::filesystem::path normalizerDir;
};

std::string readWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open file: " + cki::platform::displayPath(path));
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void writeWholeFile(const std::filesystem::path& path, const std::string& value) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write file: " + cki::platform::displayPath(path));
    out << value;
}

void usage(std::ostream& out) {
    out << "Usage:\n"
        << "  cpp_knot_indexer --pd-code \"[[1,5,2,4],...]\" [--timeout SEC]\n"
        << "  cpp_knot_indexer --pd-file code.txt [--timeout SEC]\n"
        << "  cpp_knot_indexer < code.txt\n\n"
        << "Options:\n"
        << "  --timeout SEC       Max seconds for HOMFLY-PT and Khovanov each (0 disables timeout; default 60).\n"
        << "  --data-folder PATH  Folder containing knotname-reg/, homfly/, and khovanov/ data.\n"
        << "  --ban-simplify      Do not simplify the input PD code before invariant lookup.\n"
        << "  --print-invariants  Print computed invariant strings to stderr.\n"
        << "  --verbose           Print worker status, failures, and invariant strings to stderr.\n";
}

std::filesystem::path currentExecutablePath(const std::filesystem::path& argv0) {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (n > 0 && n < buffer.size()) {
        buffer.resize(n);
        return std::filesystem::path(buffer);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        char resolved[PATH_MAX];
        if (realpath(buffer.c_str(), resolved)) return std::filesystem::path(resolved);
        return std::filesystem::path(buffer.c_str());
    }
#else
    char buffer[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (n > 0) {
        buffer[n] = '\0';
        return std::filesystem::path(buffer);
    }
#endif
    return std::filesystem::absolute(argv0);
}

bool existsPath(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool isDirectory(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

std::filesystem::path absolutePath(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return ec ? path : absolute;
}

std::optional<DataPaths> tryResolveDataFolder(const std::filesystem::path& folder) {
    const std::filesystem::path homDir = folder / "homfly";
    const std::filesystem::path khoDir = folder / "khovanov";
    const std::filesystem::path normDir = folder / "knotname-reg";
    if (!isDirectory(normDir)) return std::nullopt;

    DataPaths paths;
    paths.homflyDb = homDir / "sorted_HOMFLY-PT.txt";
    paths.khovanovDb = khoDir / "sorted_khovanov.txt";
    paths.normalizerDir = normDir;

    if (existsPath(paths.homflyDb) && existsPath(paths.khovanovDb)) return paths;
    return std::nullopt;
}

std::string dataFolderRequirement() {
    return "data folder must contain knotname-reg/, "
           "homfly/sorted_HOMFLY-PT.txt, and khovanov/sorted_khovanov.txt";
}

DataPaths findDataPaths(const std::filesystem::path& executable,
                        const std::filesystem::path& userFolder) {
    if (!userFolder.empty()) {
        std::filesystem::path folder = absolutePath(userFolder);
        if (auto paths = tryResolveDataFolder(folder)) return *paths;
        throw std::runtime_error("invalid --data-folder: " + cki::platform::displayPath(folder) + "; " + dataFolderRequirement());
    }

    std::filesystem::path exeDir = executable.parent_path();
    std::filesystem::path first = exeDir / "data";
    if (auto paths = tryResolveDataFolder(first)) return *paths;

    std::filesystem::path second = exeDir.parent_path() / "data";
    if (auto paths = tryResolveDataFolder(second)) return *paths;

    throw std::runtime_error("cannot locate default data folder at " + cki::platform::displayPath(first) +
                             " or " + cki::platform::displayPath(second) + "; pass --data-folder. " +
                             dataFolderRequirement());
}

Options parseOptions(const std::vector<cki::platform::ProgramArg>& args) {
    Options options;
    for (int i = 1; i < static_cast<int>(args.size()); ++i) {
        std::string arg = args[static_cast<std::size_t>(i)].text;
        auto needValue = [&](const char* name) -> const cki::platform::ProgramArg& {
            if (++i >= static_cast<int>(args.size())) throw std::runtime_error(std::string(name) + " needs a value");
            return args[static_cast<std::size_t>(i)];
        };

        if (arg == "--help" || arg == "-h") {
            usage(std::cout);
            std::exit(0);
        } else if (arg == "--pd-code") {
            options.pdText = needValue("--pd-code").text;
        } else if (arg == "--pd-file") {
            options.pdText = readWholeFile(needValue("--pd-file").path);
        } else if (arg == "--timeout") {
            options.timeoutSeconds = std::stoi(needValue("--timeout").text);
            if (options.timeoutSeconds < 0) throw std::runtime_error("--timeout must be >= 0");
        } else if (arg == "--data-folder") {
            options.dataFolder = needValue("--data-folder").path;
        } else if (arg == "--ban-simplify") {
            options.banSimplify = true;
        } else if (arg == "--print-invariants") {
            options.printInvariants = true;
        } else if (arg == "--verbose") {
            options.verbose = true;
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }

    if (options.pdText.empty()) {
        std::ostringstream buffer;
        buffer << std::cin.rdbuf();
        options.pdText = buffer.str();
    }
    if (options.pdText.empty()) throw std::runtime_error("no PD code was provided");
    return options;
}

std::vector<std::string> sortedUnique(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

std::vector<std::string> intersectNames(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::vector<std::string> left = sortedUnique(a);
    std::vector<std::string> right = sortedUnique(b);
    std::vector<std::string> out;
    std::set_intersection(left.begin(), left.end(), right.begin(), right.end(), std::back_inserter(out));
    return out;
}

std::vector<std::string> mergeCandidates(const WorkerResult& khResult,
                                         const std::vector<std::string>& khNames,
                                         const WorkerResult& homResult,
                                         const std::vector<std::string>& homNames) {
    if (khResult.success && homResult.success && !khNames.empty() && !homNames.empty()) {
        return intersectNames(khNames, homNames);
    }
    std::vector<std::string> combined;
    if (khResult.success) combined.insert(combined.end(), khNames.begin(), khNames.end());
    if (homResult.success) combined.insert(combined.end(), homNames.begin(), homNames.end());
    return sortedUnique(combined);
}

enum class InvariantKind {
    Homfly,
    Khovanov,
};

struct AttemptRecord {
    InvariantKind kind = InvariantKind::Homfly;
    std::string source;
    WorkerResult result;
};

struct SelectedInvariant {
    WorkerResult result;
    std::string source;
    std::vector<AttemptRecord> attempts;
};

struct RunningAttempt {
    InvariantKind kind = InvariantKind::Homfly;
    std::string source;
    std::unique_ptr<WorkerProcess> process;
};

struct InvariantPipelineResult {
    SelectedInvariant homfly;
    SelectedInvariant khovanov;
    WorkerResult simplify;
    std::string originalPd;
    std::string simplifiedPd;
    bool simplifyEnabled = true;
    bool simplifiedUsable = false;
};

std::string workerNameFor(InvariantKind kind) {
    return kind == InvariantKind::Homfly ? "homfly" : "khovanov";
}

const char* displayNameFor(InvariantKind kind) {
    return kind == InvariantKind::Homfly ? "HOMFLY-PT" : "Khovanov";
}

SelectedInvariant& selectedFor(InvariantPipelineResult& result, InvariantKind kind) {
    return kind == InvariantKind::Homfly ? result.homfly : result.khovanov;
}

const SelectedInvariant& selectedFor(const InvariantPipelineResult& result, InvariantKind kind) {
    return kind == InvariantKind::Homfly ? result.homfly : result.khovanov;
}

bool hasRunningSource(const std::vector<RunningAttempt>& running,
                      InvariantKind kind,
                      const std::string& source) {
    return std::any_of(running.begin(), running.end(), [kind, &source](const RunningAttempt& attempt) {
        return attempt.kind == kind && attempt.source == source;
    });
}

bool hasAttemptSource(const SelectedInvariant& selected, const std::string& source) {
    return std::any_of(selected.attempts.begin(), selected.attempts.end(),
                       [&source](const AttemptRecord& attempt) {
                           return attempt.source == source;
                       });
}

void recordAttempt(SelectedInvariant& selected,
                   InvariantKind kind,
                   const std::string& source,
                   const WorkerResult& result) {
    selected.attempts.push_back(AttemptRecord{kind, source, result});
    if (result.success && !selected.result.success) {
        selected.result = result;
        selected.source = source;
    } else if (!selected.result.success && !result.cancelled) {
        selected.result = result;
        selected.source = source;
    }
}

void cancelRunningKind(std::vector<RunningAttempt>& running, InvariantKind kind) {
    for (RunningAttempt& attempt : running) {
        if (attempt.kind == kind) {
            cancelWorkerProcess(*attempt.process);
        }
    }
}

void eraseFinishedCancelled(std::vector<RunningAttempt>& running) {
    running.erase(
        std::remove_if(running.begin(), running.end(), [](const RunningAttempt& attempt) {
            return attempt.process->result().cancelled;
        }),
        running.end());
}

InvariantPipelineResult computeInvariantPipeline(const std::filesystem::path& executable,
                                                 const std::string& canonicalPd,
                                                 int timeoutSeconds,
                                                 bool enableSimplify) {
    InvariantPipelineResult result;
    result.originalPd = canonicalPd;
    result.simplifyEnabled = enableSimplify;

    const auto deadline = timeoutSeconds > 0
        ? std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds)
        : std::chrono::steady_clock::time_point::max();

    std::vector<RunningAttempt> running;
    auto startInvariant = [&](InvariantKind kind, const std::string& source, const std::string& pdText) {
        SelectedInvariant& selected = selectedFor(result, kind);
        if (selected.result.success || hasAttemptSource(selected, source) ||
            hasRunningSource(running, kind, source)) {
            return;
        }
        running.push_back(RunningAttempt{
            kind,
            source,
            startWorkerProcess(executable, workerNameFor(kind), pdText),
        });
    };

    startInvariant(InvariantKind::Homfly, "original", canonicalPd);
    startInvariant(InvariantKind::Khovanov, "original", canonicalPd);
    std::unique_ptr<WorkerProcess> simplify =
        enableSimplify ? startWorkerProcess(executable, "simplify", canonicalPd) : nullptr;
    bool simplifyFinished = !enableSimplify;
    bool simplifiedAttemptsStarted = false;

    auto maybeStartSimplifiedAttempts = [&]() {
        if (simplifiedAttemptsStarted || !result.simplifiedUsable) return;
        simplifiedAttemptsStarted = true;
        if (!selectedFor(result, InvariantKind::Homfly).result.success) {
            startInvariant(InvariantKind::Homfly, "simplified", result.simplifiedPd);
        }
        if (!selectedFor(result, InvariantKind::Khovanov).result.success) {
            startInvariant(InvariantKind::Khovanov, "simplified", result.simplifiedPd);
        }
    };

    while (true) {
        if (interrupted()) {
            if (simplify && !simplifyFinished) cancelWorkerProcess(*simplify);
            for (RunningAttempt& attempt : running) cancelWorkerProcess(*attempt.process);
            result.homfly.result.interrupted = true;
            result.khovanov.result.interrupted = true;
            break;
        }

        if (simplify && !simplifyFinished && pollWorkerProcess(*simplify, deadline)) {
            result.simplify = finishWorkerProcess(*simplify);
            simplifyFinished = true;
            if (result.simplify.success) {
                result.simplifiedPd = result.simplify.output;
                result.simplifiedUsable = !result.simplifiedPd.empty() && result.simplifiedPd != canonicalPd;
                maybeStartSimplifiedAttempts();
            }
        }

        for (std::size_t i = 0; i < running.size();) {
            RunningAttempt& attempt = running[i];
            if (!pollWorkerProcess(*attempt.process, deadline)) {
                ++i;
                continue;
            }

            WorkerResult workerResult = finishWorkerProcess(*attempt.process);
            SelectedInvariant& selected = selectedFor(result, attempt.kind);
            const bool wasAlreadySelected = selected.result.success;
            recordAttempt(selected, attempt.kind, attempt.source, workerResult);
            const bool selectedNow = workerResult.success && !wasAlreadySelected;
            if (selectedNow) {
                cancelRunningKind(running, attempt.kind);
            }
            running.erase(running.begin() + static_cast<std::ptrdiff_t>(i));
            eraseFinishedCancelled(running);
        }

        if (result.homfly.result.success && result.khovanov.result.success &&
            simplify && !simplifyFinished) {
            cancelWorkerProcess(*simplify);
            result.simplify = finishWorkerProcess(*simplify);
            simplifyFinished = true;
        }

        if (simplifyFinished) {
            maybeStartSimplifiedAttempts();
        }

        const bool noMoreSimplifyWork = simplifyFinished || !simplify;
        if (running.empty() && noMoreSimplifyWork) {
            break;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            if (simplify && !simplifyFinished) {
                pollWorkerProcess(*simplify, deadline);
                result.simplify = finishWorkerProcess(*simplify);
                simplifyFinished = true;
            }
            for (RunningAttempt& attempt : running) {
                pollWorkerProcess(*attempt.process, deadline);
                WorkerResult workerResult = finishWorkerProcess(*attempt.process);
                recordAttempt(selectedFor(result, attempt.kind), attempt.kind, attempt.source, workerResult);
            }
            running.clear();
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return result;
}

void printWorkerStatus(const char* name, const WorkerResult& result) {
    std::cerr << name << ": ";
    if (result.success) {
        std::cerr << "ok";
    } else if (result.cancelled) {
        std::cerr << "cancelled";
    } else if (result.timedOut) {
        std::cerr << "timeout";
    } else if (result.interrupted) {
        std::cerr << "interrupted";
    } else {
        std::cerr << "failed(exit=" << result.exitCode << ")";
    }
    std::cerr << ", " << result.seconds << "s\n";
}

void printWorkerDetails(const char* name, const WorkerResult& result, bool printValue) {
    printWorkerStatus(name, result);
    if (result.success) {
        if (printValue) std::cerr << name << " result: " << result.output << "\n";
        return;
    }
    if (result.cancelled) {
        std::cerr << name << " detail: cancelled after a competing worker finished\n";
    } else if (result.timedOut) {
        std::cerr << name << " detail: timed out after " << result.seconds << "s\n";
    } else if (result.interrupted) {
        std::cerr << name << " detail: interrupted\n";
    } else {
        std::cerr << name << " detail: failed with exit code " << result.exitCode << "\n";
    }
    if (!result.error.empty()) {
        std::cerr << name << " stderr:\n" << result.error << "\n";
    }
}

void printPipelineDetails(const InvariantPipelineResult& pipeline, bool printValue) {
    for (InvariantKind kind : {InvariantKind::Khovanov, InvariantKind::Homfly}) {
        const SelectedInvariant& selected = selectedFor(pipeline, kind);
        for (const AttemptRecord& attempt : selected.attempts) {
            const std::string name = std::string(displayNameFor(kind)) + " (" + attempt.source + ")";
            printWorkerDetails(name.c_str(), attempt.result, false);
        }
    }

    if (pipeline.simplifyEnabled) {
        printWorkerDetails("Simplify", pipeline.simplify, false);
        if (pipeline.simplify.success) {
            std::cerr << "Simplify result: " << pipeline.simplify.output << "\n";
        }
    } else {
        std::cerr << "Simplify: disabled by --ban-simplify\n";
    }

    const SelectedInvariant& khovanov = pipeline.khovanov;
    const SelectedInvariant& homfly = pipeline.homfly;
    if (khovanov.result.success) {
        std::cerr << "Khovanov selected source: " << khovanov.source << "\n";
        if (printValue) std::cerr << "Khovanov result: " << khovanov.result.output << "\n";
    } else {
        printWorkerDetails("Khovanov", khovanov.result, false);
    }
    if (homfly.result.success) {
        std::cerr << "HOMFLY-PT selected source: " << homfly.source << "\n";
        if (printValue) std::cerr << "HOMFLY-PT result: " << homfly.result.output << "\n";
    } else {
        printWorkerDetails("HOMFLY-PT", homfly.result, false);
    }
}

int workerMain(const std::vector<cki::platform::ProgramArg>& args) {
    std::string worker;
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;

    for (int i = 1; i < static_cast<int>(args.size()); ++i) {
        std::string arg = args[static_cast<std::size_t>(i)].text;
        auto needValue = [&](const char* name) -> const cki::platform::ProgramArg& {
            if (++i >= static_cast<int>(args.size())) throw std::runtime_error(std::string(name) + " needs a value");
            return args[static_cast<std::size_t>(i)];
        };
        if (arg == "--worker") worker = needValue("--worker").text;
        else if (arg == "--input") inputPath = needValue("--input").path;
        else if (arg == "--output") outputPath = needValue("--output").path;
    }

    if (worker.empty() || inputPath.empty() || outputPath.empty()) {
        throw std::runtime_error("bad worker invocation");
    }

    PDCode pd = parsePDCode(readWholeFile(inputPath));
    std::string value;
    if (worker == "khovanov") value = computeKhovanov(pd);
    else if (worker == "homfly") value = computeHomflyPT(pd);
    else if (worker == "simplify") value = computeSimplifiedPDCode(pd);
    else throw std::runtime_error("unknown worker: " + worker);
    writeWholeFile(outputPath, value);
    return 0;
}

} // namespace

} // namespace hki

int main(int argc, char** argv) {
    try {
        const std::vector<cki::platform::ProgramArg> args = cki::platform::programArguments(argc, argv);
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i].text == "--worker") {
                hki::installInterruptHandlers();
                return hki::workerMain(args);
            }
        }

        hki::installInterruptHandlers();
        hki::Options options = hki::parseOptions(args);
        hki::PDCode pd = hki::parsePDCode(options.pdText);
        std::string canonicalPd = hki::formatPDCodeList(pd);
        std::filesystem::path executable = hki::currentExecutablePath(args.empty() ? std::filesystem::path() : args[0].path);
        hki::DataPaths dataPaths = hki::findDataPaths(executable, options.dataFolder);

        hki::InvariantPipelineResult pipeline =
            hki::computeInvariantPipeline(executable, canonicalPd, options.timeoutSeconds, !options.banSimplify);
        hki::WorkerResult khResult = pipeline.khovanov.result;
        hki::WorkerResult homResult = pipeline.homfly.result;

        if (hki::interrupted() || khResult.interrupted || homResult.interrupted) {
            std::cerr << "Interrupted; exiting cleanly.\n";
            return 130;
        }

        if (options.verbose) {
            hki::printPipelineDetails(pipeline, true);
        } else if (options.printInvariants) {
            std::cerr << "Khovanov result: " << (khResult.success ? khResult.output : "<failed>") << "\n";
            std::cerr << "HOMFLY-PT result: " << (homResult.success ? homResult.output : "<failed>") << "\n";
            if (pipeline.simplifyEnabled && pipeline.simplify.success) {
                std::cerr << "Simplified PD code result: " << pipeline.simplify.output << "\n";
            }
        }

        if (!khResult.success && !homResult.success) {
            std::cerr << "Both invariant computations failed or timed out.\n";
            return 2;
        }

        hki::NameNormalizer normalizer(dataPaths.normalizerDir);
        std::vector<std::string> candidates;

        if (options.verbose) {
            std::cerr << "Invariant data source: text files "
                      << cki::platform::displayPath(dataPaths.homflyDb) << " and "
                      << cki::platform::displayPath(dataPaths.khovanovDb) << "\n";
        }
        hki::InvariantMap khDb = hki::loadInvariantMap(dataPaths.khovanovDb, normalizer);
        hki::InvariantMap homDb = hki::loadInvariantMap(dataPaths.homflyDb, normalizer);

        std::vector<std::string> khNames = khResult.success ? hki::lookupInvariant(khDb, khResult.output) : std::vector<std::string>{};
        std::vector<std::string> homNames = homResult.success ? hki::lookupInvariant(homDb, homResult.output) : std::vector<std::string>{};
        candidates = hki::mergeCandidates(khResult, khNames, homResult, homNames);
        if (options.verbose) {
            std::cerr << "Text candidate count: " << candidates.size() << "\n";
        }

        for (const std::string& name : candidates) std::cout << name << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
