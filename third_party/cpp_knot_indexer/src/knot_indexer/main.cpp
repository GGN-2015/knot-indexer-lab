#include "database.hpp"
#include "homfly_backend.hpp"
#include "khovanov_backend.hpp"
#include "pd_simplify_backend.hpp"
#include "pd_code.hpp"
#include "process_runner.hpp"
#include "runtime_control.hpp"
#include "sqlite_database.hpp"

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
    std::filesystem::path sqliteDb;
    int timeoutSeconds = 60;
    bool verbose = false;
    bool printInvariants = false;
};

struct DataPaths {
    std::filesystem::path homflyDb;
    std::filesystem::path khovanovDb;
    std::filesystem::path normalizerDir;
    std::optional<std::filesystem::path> sqliteDb;
    bool textDbsAvailable = false;
};

std::string readWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open file: " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void writeWholeFile(const std::filesystem::path& path, const std::string& value) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write file: " + path.string());
    out << value;
}

void usage(std::ostream& out) {
    out << "Usage:\n"
        << "  cpp_knot_indexer --pd-code \"[[1,5,2,4],...]\" [--timeout SEC]\n"
        << "  cpp_knot_indexer --pd-file code.txt [--timeout SEC]\n"
        << "  cpp_knot_indexer < code.txt\n\n"
        << "Options:\n"
        << "  --timeout SEC       Max seconds for HOMFLY-PT and Khovanov each (0 disables timeout; default 60).\n"
        << "  --data-folder PATH  Folder containing knotname-reg/ and text or SQLite invariant data.\n"
        << "  --sqlite-db PATH    Explicit SQLite invariant database. Overrides auto-detected SQLite files.\n"
        << "  --print-invariants  Print computed invariant strings to stderr.\n"
        << "  --verbose           Print worker status, failures, and invariant strings to stderr.\n";
}

std::filesystem::path currentExecutablePath(const char* argv0) {
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
    return std::filesystem::absolute(argv0 ? argv0 : "");
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

std::vector<std::filesystem::path> sqliteCandidates(const std::filesystem::path& folder) {
    return {
        folder / "knot-index.sqlite",
        folder / "knot_index.sqlite",
        folder / "invariants.sqlite",
        folder / "name-pd" / "PD_m_3-16.sqlite",
    };
}

std::optional<std::filesystem::path> findSqliteDatabase(const std::filesystem::path& folder) {
    for (const std::filesystem::path& candidate : sqliteCandidates(folder)) {
        if (existsPath(candidate)) return candidate;
    }
    return std::nullopt;
}

std::optional<DataPaths> tryResolveDataFolder(
    const std::filesystem::path& folder,
    const std::optional<std::filesystem::path>& explicitSqliteDb) {
    const std::filesystem::path homDir = folder / "homfly";
    const std::filesystem::path khoDir = folder / "khovanov";
    const std::filesystem::path normDir = folder / "knotname-reg";
    if (!isDirectory(normDir)) return std::nullopt;

    DataPaths paths;
    paths.homflyDb = homDir / "sorted_HOMFLY-PT.txt";
    paths.khovanovDb = khoDir / "sorted_khovanov.txt";
    paths.normalizerDir = normDir;
    paths.textDbsAvailable = existsPath(paths.homflyDb) && existsPath(paths.khovanovDb);
    paths.sqliteDb = explicitSqliteDb.has_value() ? explicitSqliteDb : findSqliteDatabase(folder);

    if (paths.textDbsAvailable || paths.sqliteDb.has_value()) return paths;
    return std::nullopt;
}

std::string dataFolderRequirement() {
    return "data folder must contain knotname-reg/ and either "
           "homfly/sorted_HOMFLY-PT.txt plus khovanov/sorted_khovanov.txt, "
           "or a SQLite invariant database named knot-index.sqlite, knot_index.sqlite, "
           "invariants.sqlite, or name-pd/PD_m_3-16.sqlite";
}

DataPaths findDataPaths(const std::filesystem::path& executable,
                        const std::filesystem::path& userFolder,
                        const std::filesystem::path& userSqliteDb) {
    std::optional<std::filesystem::path> explicitSqliteDb;
    if (!userSqliteDb.empty()) {
        explicitSqliteDb = absolutePath(userSqliteDb);
        if (!existsPath(*explicitSqliteDb)) {
            throw std::runtime_error("invalid --sqlite-db: " + explicitSqliteDb->string() +
                                     "; file does not exist");
        }
    }

    if (!userFolder.empty()) {
        std::filesystem::path folder = absolutePath(userFolder);
        if (auto paths = tryResolveDataFolder(folder, explicitSqliteDb)) return *paths;
        throw std::runtime_error("invalid --data-folder: " + folder.string() + "; " + dataFolderRequirement());
    }

    std::filesystem::path exeDir = executable.parent_path();
    std::filesystem::path first = exeDir / "data";
    if (auto paths = tryResolveDataFolder(first, explicitSqliteDb)) return *paths;

    std::filesystem::path second = exeDir.parent_path() / "data";
    if (auto paths = tryResolveDataFolder(second, explicitSqliteDb)) return *paths;

    throw std::runtime_error("cannot locate default data folder at " + first.string() +
                             " or " + second.string() + "; pass --data-folder. " +
                             dataFolderRequirement());
}

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto needValue = [&](const char* name) -> std::string {
            if (++i >= argc) throw std::runtime_error(std::string(name) + " needs a value");
            return argv[i];
        };

        if (arg == "--help" || arg == "-h") {
            usage(std::cout);
            std::exit(0);
        } else if (arg == "--pd-code") {
            options.pdText = needValue("--pd-code");
        } else if (arg == "--pd-file") {
            options.pdText = readWholeFile(needValue("--pd-file"));
        } else if (arg == "--timeout") {
            options.timeoutSeconds = std::stoi(needValue("--timeout"));
            if (options.timeoutSeconds < 0) throw std::runtime_error("--timeout must be >= 0");
        } else if (arg == "--data-folder") {
            options.dataFolder = needValue("--data-folder");
        } else if (arg == "--sqlite-db") {
            options.sqliteDb = needValue("--sqlite-db");
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
                                                 int timeoutSeconds) {
    InvariantPipelineResult result;
    result.originalPd = canonicalPd;

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
        startWorkerProcess(executable, "simplify", canonicalPd);
    bool simplifyFinished = false;
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

    printWorkerDetails("Simplify", pipeline.simplify, false);
    if (pipeline.simplify.success) {
        std::cerr << "Simplify result: " << pipeline.simplify.output << "\n";
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

int workerMain(int argc, char** argv) {
    std::string worker;
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto needValue = [&](const char* name) -> std::string {
            if (++i >= argc) throw std::runtime_error(std::string(name) + " needs a value");
            return argv[i];
        };
        if (arg == "--worker") worker = needValue("--worker");
        else if (arg == "--input") inputPath = needValue("--input");
        else if (arg == "--output") outputPath = needValue("--output");
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
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--worker") {
                hki::installInterruptHandlers();
                return hki::workerMain(argc, argv);
            }
        }

        hki::installInterruptHandlers();
        hki::Options options = hki::parseOptions(argc, argv);
        hki::PDCode pd = hki::parsePDCode(options.pdText);
        std::string canonicalPd = hki::formatPDCodeList(pd);
        std::filesystem::path executable = hki::currentExecutablePath(argv[0]);
        hki::DataPaths dataPaths = hki::findDataPaths(executable, options.dataFolder, options.sqliteDb);

        hki::InvariantPipelineResult pipeline =
            hki::computeInvariantPipeline(executable, canonicalPd, options.timeoutSeconds);
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
        }

        if (!khResult.success && !homResult.success) {
            std::cerr << "Both invariant computations failed or timed out.\n";
            return 2;
        }

        hki::NameNormalizer normalizer(dataPaths.normalizerDir);
        std::optional<std::string> khovanovInvariant = khResult.success ? std::optional<std::string>(khResult.output) : std::nullopt;
        std::optional<std::string> homflyInvariant = homResult.success ? std::optional<std::string>(homResult.output) : std::nullopt;
        std::vector<std::string> candidates;

        if (dataPaths.sqliteDb.has_value()) {
            hki::SqliteInvariantDatabase sqliteDb(*dataPaths.sqliteDb, normalizer);
            if (options.verbose) {
                std::cerr << "Invariant data source: " << sqliteDb.statusMessage() << "\n";
            }
            if (sqliteDb.hasInvariantRecords()) {
                candidates = sqliteDb.lookup(homflyInvariant, khovanovInvariant);
                if (options.verbose) {
                    std::cerr << "SQLite candidate count: " << candidates.size() << "\n";
                }
            } else if (!dataPaths.textDbsAvailable) {
                throw std::runtime_error("no usable invariant data source: " + sqliteDb.statusMessage());
            }
        }

        if (candidates.empty() && dataPaths.textDbsAvailable) {
            if (options.verbose) {
                std::cerr << "Invariant data source: text files "
                          << dataPaths.homflyDb << " and " << dataPaths.khovanovDb << "\n";
            }
            hki::InvariantMap khDb = hki::loadInvariantMap(dataPaths.khovanovDb, normalizer);
            hki::InvariantMap homDb = hki::loadInvariantMap(dataPaths.homflyDb, normalizer);

            std::vector<std::string> khNames = khResult.success ? hki::lookupInvariant(khDb, khResult.output) : std::vector<std::string>{};
            std::vector<std::string> homNames = homResult.success ? hki::lookupInvariant(homDb, homResult.output) : std::vector<std::string>{};
            candidates = hki::mergeCandidates(khResult, khNames, homResult, homNames);
            if (options.verbose) {
                std::cerr << "Text candidate count: " << candidates.size() << "\n";
            }
        }

        for (const std::string& name : candidates) std::cout << name << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
