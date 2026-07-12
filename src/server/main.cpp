#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

#include "homfly_backend.hpp"
#include "khovanov_backend.hpp"
#include "link_pd_code.hpp"
#include "pd_code.hpp"
#include "pd_simplify_backend.hpp"
#include "path_utils.hpp"
#include "process_runner.hpp"
#include "runtime_control.hpp"
#include "sqlite3.h"

#ifndef DEBUG
#define DEBUG 0
#define LAB_DEFINED_PD_DIAGRAM_DEBUG 1
#endif
#include "PdToDiagram2d.h"
#ifdef LAB_DEFINED_PD_DIAGRAM_DEBUG
#undef DEBUG
#undef LAB_DEFINED_PD_DIAGRAM_DEBUG
#endif
#ifdef SHOW_CERTAIN_DEBUG_MESSAGE
#undef SHOW_CERTAIN_DEBUG_MESSAGE
#endif
#ifdef SHOW_DEBUG_MESSAGE
#undef SHOW_DEBUG_MESSAGE
#endif
#ifdef THROW_EXCEPTION
#undef THROW_EXCEPTION
#endif
#ifdef DEFINE_EXCEPTION
#undef DEFINE_EXCEPTION
#endif
#ifdef PROCESS_EXCEPTION
#undef PROCESS_EXCEPTION
#endif

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#elif !defined(_WIN32)
#include <limits.h>
#endif

namespace lab {
namespace {

constexpr int kDefaultPort = 5000;
constexpr int kMaxComputeTimeoutSeconds = 20 * 60;
constexpr int kDefaultMaxCrossing = 14;
constexpr int kHardMaxCrossing = 16;
constexpr std::size_t kMaxHeaderBytes = 64 * 1024;
constexpr std::size_t kMaxBodyBytes = 8 * 1024 * 1024;

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
#endif

struct Options {
    std::string host = "0.0.0.0";
    int port = kDefaultPort;
    int timeoutSeconds = kMaxComputeTimeoutSeconds;
    bool buildSqlite = false;
    bool buildPdIndex = false;
    std::size_t indexLimit = 0;
    std::size_t indexWorkers = 0;
    std::size_t indexBatchSize = 256;
    int indexProgressSeconds = 5;
    int maxCrossing = kDefaultMaxCrossing;
    std::filesystem::path dataFolder;
    std::filesystem::path webRoot;
};

struct DataPaths {
    std::filesystem::path root;
    std::filesystem::path namePdFile;
    std::filesystem::path sqliteFile;
    std::filesystem::path invariantIndexFile;
    std::filesystem::path knotNameRegDir;
};

struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct Response {
    int status = 200;
    std::string reason = "OK";
    std::map<std::string, std::string> headers;
    std::string body;

    std::string serialize() const {
        std::ostringstream out;
        out << "HTTP/1.1 " << status << " " << reason << "\r\n";
        out << "Content-Length: " << body.size() << "\r\n";
        out << "Connection: close\r\n";
        for (const auto& item : headers) {
            out << item.first << ": " << item.second << "\r\n";
        }
        out << "\r\n";
        out << body;
        return out.str();
    }
};

struct LookupResult {
    std::string canonicalPd;
    std::string pdDiagramSvg;
    std::string pdDiagramError;
    std::string homfly;
    std::string khovanov;
    std::vector<std::string> candidates;
    std::string candidateError;
    hki::WorkerResult homflyWorker;
    hki::WorkerResult khovanovWorker;
};

template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(std::size_t capacity) : capacity_(std::max<std::size_t>(1, capacity)) {}

    bool push(T item, const std::atomic_bool& stopRequested) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&] {
            return closed_ || stopRequested.load() || items_.size() < capacity_;
        });
        if (closed_ || stopRequested.load()) return false;
        items_.push_back(std::move(item));
        cv_.notify_all();
        return true;
    }

    bool pop(T& item, const std::atomic_bool& stopRequested) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&] {
            return closed_ || stopRequested.load() || !items_.empty();
        });
        if (items_.empty()) return false;
        item = std::move(items_.front());
        items_.pop_front();
        cv_.notify_all();
        return true;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

private:
    std::size_t capacity_;
    std::deque<T> items_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool closed_ = false;
};

struct IndexBuildStats {
    std::atomic<std::size_t> queued{0};
    std::atomic<std::size_t> processed{0};
    std::atomic<std::size_t> succeeded{0};
    std::atomic<std::size_t> written{0};
    std::atomic<std::size_t> skipped{0};
    std::atomic<std::size_t> failed{0};
};

struct IndexBuildResult {
    std::size_t written = 0;
    std::size_t skipped = 0;
    std::size_t failed = 0;
    std::size_t totalTarget = 0;
};

std::string formatDurationHms(std::uint64_t totalSeconds) {
    const std::uint64_t hours = totalSeconds / 3600;
    const std::uint64_t minutes = (totalSeconds / 60) % 60;
    const std::uint64_t seconds = totalSeconds % 60;
    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << hours
        << ":" << std::setw(2) << minutes
        << ":" << std::setw(2) << seconds;
    return out.str();
}

std::string formatRate(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(value >= 100.0 ? 1 : 2) << value;
    return out.str();
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), value.begin());
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
}

std::string trim(const std::string& value) {
    const char* ws = " \t\r\n";
    const std::size_t first = value.find_first_not_of(ws);
    if (first == std::string::npos) return "";
    const std::size_t last = value.find_last_not_of(ws);
    return value.substr(first, last - first + 1);
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string localTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return buffer;
}

std::string readWholeFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open file: " + cki::platform::displayPath(path));
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void writeWholeFile(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot write file: " + cki::platform::displayPath(path));
    output << value;
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
    std::filesystem::path out = std::filesystem::absolute(path, ec);
    return ec ? path : out;
}

std::string pathUtf8(const std::filesystem::path& path) {
    return cki::platform::sqliteOpenPath(path);
}

std::filesystem::path currentExecutablePath(const std::filesystem::path& argv0) {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
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
    const ssize_t n = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (n > 0) {
        buffer[n] = '\0';
        return std::filesystem::path(buffer);
    }
#endif
    return std::filesystem::absolute(argv0);
}

std::optional<DataPaths> tryResolveDataFolder(const std::filesystem::path& folder) {
    if (!isDirectory(folder)) return std::nullopt;
    const std::filesystem::path nested = folder / "name-pd" / "PD_m_3-16.sorted.txt";
    const std::filesystem::path flat = folder / "PD_m_3-16.sorted.txt";
    const bool hasNestedFolder = isDirectory(folder / "name-pd");
    const std::filesystem::path namePdFile = (existsPath(nested) || (!existsPath(flat) && hasNestedFolder)) ? nested : flat;
    const std::filesystem::path sqliteFile = namePdFile.parent_path() / "PD_m_3-16.sqlite";
    const std::filesystem::path indexFile = namePdFile.parent_path() / "PD_m_3-16.invariants.tsv";
    return DataPaths{folder, namePdFile, sqliteFile, indexFile, folder / "knotname-reg"};
}

DataPaths resolveDataFolder(const std::filesystem::path& executable, const std::filesystem::path& userFolder) {
    std::vector<std::filesystem::path> candidates;
    if (!userFolder.empty()) candidates.push_back(userFolder);
    const std::filesystem::path exeDir = executable.parent_path();
    candidates.push_back(exeDir / "data");
    candidates.push_back(exeDir.parent_path() / "data");
    candidates.push_back(std::filesystem::current_path() / "data");

    for (const std::filesystem::path& candidate : candidates) {
        if (auto paths = tryResolveDataFolder(absolutePath(candidate))) return *paths;
    }
    throw std::runtime_error("cannot locate data folder; pass --data-folder");
}

std::filesystem::path resolveWebRoot(const std::filesystem::path& executable, const std::filesystem::path& userRoot) {
    std::vector<std::filesystem::path> candidates;
    if (!userRoot.empty()) candidates.push_back(userRoot);
    const std::filesystem::path exeDir = executable.parent_path();
    candidates.push_back(exeDir / "web");
    candidates.push_back(exeDir.parent_path() / "web");
    candidates.push_back(std::filesystem::current_path() / "web");

    for (const std::filesystem::path& candidate : candidates) {
        std::filesystem::path root = absolutePath(candidate);
        if (existsPath(root / "index.html") && isDirectory(root / "static")) return root;
    }
    throw std::runtime_error("cannot locate web root; pass --web-root");
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    const char* hex = "0123456789abcdef";
                    out << "\\u00" << hex[c >> 4] << hex[c & 0x0f];
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

std::string jsonArray(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << ",";
        out << "\"" << jsonEscape(values[i]) << "\"";
    }
    out << "]";
    return out.str();
}

Response makeText(int status, const std::string& reason, const std::string& body, const std::string& contentType) {
    Response response;
    response.status = status;
    response.reason = reason;
    response.body = body;
    response.headers["Content-Type"] = contentType;
    return response;
}

Response makeJsonStatus(const std::string& status, const std::string& message) {
    return makeText(200, "OK",
                    "{\"status\":\"" + jsonEscape(status) + "\",\"message\":\"" + jsonEscape(message) + "\"}",
                    "application/json; charset=utf-8");
}

Response makeJsonSuccess(const std::string& message) {
    return makeJsonStatus("success", message);
}

Response makeJsonError(const std::string& message) {
    return makeJsonStatus("error", message);
}

int fromHex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string urlDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = fromHex(value[i + 1]);
            const int lo = fromHex(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

std::string base64Decode(const std::string& encoded) {
    static const std::array<int, 256> table = [] {
        std::array<int, 256> t{};
        t.fill(-1);
        const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < static_cast<int>(alphabet.size()); ++i) {
            t[static_cast<unsigned char>(alphabet[static_cast<std::size_t>(i)])] = i;
        }
        return t;
    }();

    std::string out;
    int value = 0;
    int bits = -8;
    for (unsigned char c : encoded) {
        if (std::isspace(c)) continue;
        if (c == '=') break;
        const int digit = table[c];
        if (digit < 0) throw std::runtime_error("invalid base64 input");
        value = (value << 6) + digit;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((value >> bits) & 0xff));
            bits -= 8;
        }
    }
    return out;
}

std::string base64Encode(const unsigned char* data, std::size_t size) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    for (std::size_t i = 0; i < size; i += 3) {
        const unsigned int b0 = data[i];
        const unsigned int b1 = i + 1 < size ? data[i + 1] : 0;
        const unsigned int b2 = i + 2 < size ? data[i + 2] : 0;
        out.push_back(alphabet[(b0 >> 2) & 0x3f]);
        out.push_back(alphabet[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0f)]);
        out.push_back(i + 1 < size ? alphabet[((b1 & 0x0f) << 2) | ((b2 >> 6) & 0x03)] : '=');
        out.push_back(i + 2 < size ? alphabet[b2 & 0x3f] : '=');
    }
    return out;
}

std::string base64Encode(const std::array<unsigned char, 20>& data) {
    return base64Encode(data.data(), data.size());
}

std::uint32_t rotateLeft(std::uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

std::array<unsigned char, 20> sha1Bytes(const std::string& input) {
    std::vector<unsigned char> data(input.begin(), input.end());
    const std::uint64_t bitLength = static_cast<std::uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0);
    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<unsigned char>((bitLength >> (i * 8)) & 0xff));
    }

    std::uint32_t h0 = 0x67452301;
    std::uint32_t h1 = 0xefcdab89;
    std::uint32_t h2 = 0x98badcfe;
    std::uint32_t h3 = 0x10325476;
    std::uint32_t h4 = 0xc3d2e1f0;

    for (std::size_t offset = 0; offset < data.size(); offset += 64) {
        std::array<std::uint32_t, 80> w{};
        for (int i = 0; i < 16; ++i) {
            const std::size_t j = offset + static_cast<std::size_t>(i) * 4;
            w[i] = (static_cast<std::uint32_t>(data[j]) << 24) |
                   (static_cast<std::uint32_t>(data[j + 1]) << 16) |
                   (static_cast<std::uint32_t>(data[j + 2]) << 8) |
                   static_cast<std::uint32_t>(data[j + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;

        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5a827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdc;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6;
            }
            const std::uint32_t temp = rotateLeft(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotateLeft(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<unsigned char, 20> digest{};
    const std::array<std::uint32_t, 5> words{h0, h1, h2, h3, h4};
    for (std::size_t i = 0; i < words.size(); ++i) {
        digest[i * 4] = static_cast<unsigned char>((words[i] >> 24) & 0xff);
        digest[i * 4 + 1] = static_cast<unsigned char>((words[i] >> 16) & 0xff);
        digest[i * 4 + 2] = static_cast<unsigned char>((words[i] >> 8) & 0xff);
        digest[i * 4 + 3] = static_cast<unsigned char>(words[i] & 0xff);
    }
    return digest;
}

std::string webSocketAcceptKey(const std::string& key) {
    static const std::string guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    return base64Encode(sha1Bytes(key + guid));
}

std::string generateUuid() {
    static const char* hex = "0123456789abcdef";
    std::array<unsigned char, 16> bytes{};
    std::random_device random;
    for (unsigned char& b : bytes) b = static_cast<unsigned char>(random());
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);

    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(hex[(bytes[i] >> 4) & 0x0f]);
        out.push_back(hex[bytes[i] & 0x0f]);
    }
    return out;
}

bool isUuidLike(const std::string& value) {
    if (value.size() != 36) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> cookieValue(const Request& request, const std::string& name) {
    const auto found = request.headers.find("cookie");
    if (found == request.headers.end()) return std::nullopt;
    std::size_t start = 0;
    while (start <= found->second.size()) {
        const std::size_t sep = found->second.find(';', start);
        std::string part = trim(found->second.substr(start, sep == std::string::npos ? std::string::npos : sep - start));
        const std::size_t eq = part.find('=');
        if (eq != std::string::npos && trim(part.substr(0, eq)) == name) {
            return trim(part.substr(eq + 1));
        }
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return std::nullopt;
}

struct ClientSession {
    std::string id;
    bool needsCookie = false;
};

ClientSession clientSessionFromRequest(const Request& request) {
    const auto existing = cookieValue(request, "kil_client_id");
    if (existing.has_value() && isUuidLike(*existing)) return ClientSession{*existing, false};
    return ClientSession{generateUuid(), true};
}

std::string clientCookieHeaderValue(const ClientSession& session) {
    return "kil_client_id=" + session.id + "; Path=/; Max-Age=31536000; SameSite=Lax; HttpOnly";
}

void attachClientCookie(Response& response, const ClientSession& session) {
    if (!session.needsCookie) return;
    response.headers["Set-Cookie"] = clientCookieHeaderValue(session);
}

std::optional<std::string> extractJsonString(const std::string& body, const std::string& key) {
    const std::string quotedKey = "\"" + key + "\"";
    std::size_t pos = body.find(quotedKey);
    if (pos == std::string::npos) return std::nullopt;
    pos = body.find(':', pos + quotedKey.size());
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
    if (pos >= body.size() || body[pos] != '"') return std::nullopt;
    ++pos;

    std::string out;
    while (pos < body.size()) {
        char c = body[pos++];
        if (c == '"') return out;
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (pos >= body.size()) return std::nullopt;
        const char esc = body[pos++];
        switch (esc) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
                if (pos + 4 > body.size()) return std::nullopt;
                out.push_back('?');
                pos += 4;
                break;
            default:
                return std::nullopt;
        }
    }
    return std::nullopt;
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

std::vector<std::string> mergeCandidates(const hki::WorkerResult& khResult,
                                         const std::vector<std::string>& khNames,
                                         const hki::WorkerResult& homResult,
                                         const std::vector<std::string>& homNames) {
    if (khResult.success && homResult.success && !khNames.empty() && !homNames.empty()) {
        return intersectNames(khNames, homNames);
    }
    std::vector<std::string> combined;
    if (khResult.success) combined.insert(combined.end(), khNames.begin(), khNames.end());
    if (homResult.success) combined.insert(combined.end(), homNames.begin(), homNames.end());
    return sortedUnique(combined);
}

std::string join(const std::vector<std::string>& values, const std::string& sep) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << sep;
        out << values[i];
    }
    return out.str();
}

std::string workerFailureMessage(const char* name, const hki::WorkerResult& result) {
    std::ostringstream out;
    out << name << " ";
    if (result.cancelled) {
        out << "was cancelled";
    } else if (result.timedOut) {
        out << "timed out after " << result.seconds << "s";
    } else if (result.interrupted) {
        out << "was interrupted";
    } else {
        out << "failed with exit code " << result.exitCode;
    }
    if (!result.error.empty()) out << ": " << trim(result.error);
    return out.str();
}

std::string workerStatusText(const hki::WorkerResult& result) {
    if (result.success) return "success";
    if (result.cancelled) return "cancelled";
    if (result.timedOut) return "timed_out";
    if (result.interrupted) return "interrupted";
    if (result.exitCode != -1) return "failed";
    return "pending";
}

struct TaskRecord {
    std::uint64_t id = 0;
    std::string clientId;
    std::string status = "running";
    std::string inputType;
    std::string input;
    std::string canonicalPd;
    std::string pdDiagramSvg;
    std::string pdDiagramError;
    std::string knotTypes;
    std::string homflyStatus = "pending";
    std::string homflyResult;
    std::string homflyError;
    std::string khovanovStatus = "pending";
    std::string khovanovResult;
    std::string khovanovError;
    std::string error;
    std::string startedAt;
    std::string endedAt;
    bool cancelRequested = false;
    std::shared_ptr<hki::CancellationToken> cancellation;
};

class TaskManager {
public:
    using TaskPtr = std::shared_ptr<TaskRecord>;

    TaskPtr create(std::string clientId, std::string inputType, std::string input) {
        auto task = std::make_shared<TaskRecord>();
        task->id = nextId_.fetch_add(1, std::memory_order_relaxed);
        task->clientId = std::move(clientId);
        task->inputType = std::move(inputType);
        task->input = std::move(input);
        task->startedAt = localTimestamp();
        task->cancellation = std::make_shared<hki::CancellationToken>();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push_back(task);
        }
        return task;
    }

    bool cancel(std::uint64_t id, std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& task : tasks_) {
            if (task->id != id) continue;
            if (task->status != "running") {
                message = "task is not running.";
                return false;
            }
            task->cancelRequested = true;
            if (task->cancellation) task->cancellation->cancel();
            message = "termination requested.";
            return true;
        }
        message = "task not found.";
        return false;
    }

    void setCanonicalPd(const TaskPtr& task,
                        const std::string& canonicalPd,
                        const std::string& pdDiagramSvg,
                        const std::string& pdDiagramError) {
        if (!task) return;
        std::lock_guard<std::mutex> lock(mutex_);
        task->canonicalPd = canonicalPd;
        task->pdDiagramSvg = pdDiagramSvg;
        task->pdDiagramError = pdDiagramError;
    }

    void complete(const TaskPtr& task, const LookupResult& result) {
        if (!task) return;
        std::lock_guard<std::mutex> lock(mutex_);
        task->canonicalPd = result.canonicalPd;
        task->pdDiagramSvg = result.pdDiagramSvg;
        task->pdDiagramError = result.pdDiagramError;
        task->knotTypes = join(result.candidates, "; ");
        task->homflyStatus = workerStatusText(result.homflyWorker);
        task->homflyResult = result.homfly;
        if (!result.homflyWorker.success) {
            task->homflyError = workerFailureMessage("HOMFLY-PT", result.homflyWorker);
        }
        task->khovanovStatus = workerStatusText(result.khovanovWorker);
        task->khovanovResult = result.khovanov;
        if (!result.khovanovWorker.success) {
            task->khovanovError = workerFailureMessage("Khovanov", result.khovanovWorker);
        }
        if (!result.candidateError.empty()) task->error = result.candidateError;
        task->endedAt = localTimestamp();
        if (result.homflyWorker.cancelled || result.khovanovWorker.cancelled || task->cancelRequested) {
            task->status = "cancelled";
        } else {
            task->status = "completed";
        }
    }

    void fail(const TaskPtr& task, const std::string& error) {
        if (!task) return;
        std::lock_guard<std::mutex> lock(mutex_);
        task->endedAt = localTimestamp();
        task->error = error;
        task->status = task->cancelRequested ? "cancelled" : "failed";
        if (task->cancelRequested) {
            if (task->homflyStatus == "pending") task->homflyStatus = "cancelled";
            if (task->khovanovStatus == "pending") task->khovanovStatus = "cancelled";
        }
    }

    std::string toJson(const std::string& clientId = "") const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream out;
        out << "{\"status\":\"success\",\"client_id\":\"" << jsonEscape(clientId) << "\",\"tasks\":[";
        for (std::size_t i = 0; i < tasks_.size(); ++i) {
            const auto& task = tasks_[i];
            if (i) out << ",";
            appendTaskJson(out, *task);
        }
        out << "],\"session_tasks\":[";
        bool first = true;
        const TaskRecord* latest = nullptr;
        for (const auto& task : tasks_) {
            if (task->clientId != clientId) continue;
            if (!first) out << ",";
            appendTaskJson(out, *task);
            first = false;
            latest = task.get();
        }
        out << "],\"last_session_task\":";
        if (latest) appendTaskJson(out, *latest);
        else out << "null";
        out << "}";
        return out.str();
    }

private:
    std::atomic_uint64_t nextId_{1};
    mutable std::mutex mutex_;
    std::vector<TaskPtr> tasks_;

    static void appendTaskJson(std::ostringstream& out, const TaskRecord& task) {
        out << "{"
            << "\"id\":" << task.id << ","
            << "\"client_id\":\"" << jsonEscape(task.clientId) << "\","
            << "\"status\":\"" << jsonEscape(task.status) << "\","
            << "\"input_type\":\"" << jsonEscape(task.inputType) << "\","
            << "\"input\":\"" << jsonEscape(task.input) << "\","
            << "\"canonical_pd\":\"" << jsonEscape(task.canonicalPd) << "\","
            << "\"pd_diagram_svg\":\"" << jsonEscape(task.pdDiagramSvg) << "\","
            << "\"pd_diagram_error\":\"" << jsonEscape(task.pdDiagramError) << "\","
            << "\"started_at\":\"" << jsonEscape(task.startedAt) << "\","
            << "\"ended_at\":\"" << jsonEscape(task.endedAt) << "\","
            << "\"knot_types\":\"" << jsonEscape(task.knotTypes) << "\","
            << "\"homfly_status\":\"" << jsonEscape(task.homflyStatus) << "\","
            << "\"homfly_result\":\"" << jsonEscape(task.homflyResult) << "\","
            << "\"homfly_error\":\"" << jsonEscape(task.homflyError) << "\","
            << "\"khovanov_status\":\"" << jsonEscape(task.khovanovStatus) << "\","
            << "\"khovanov_result\":\"" << jsonEscape(task.khovanovResult) << "\","
            << "\"khovanov_error\":\"" << jsonEscape(task.khovanovError) << "\","
            << "\"error\":\"" << jsonEscape(task.error) << "\","
            << "\"cancel_requested\":" << (task.cancelRequested ? "true" : "false")
            << "}";
    }
};

int workerMain(const std::vector<cki::platform::ProgramArg>& args) {
    std::string worker;
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;

    for (int i = 1; i < static_cast<int>(args.size()); ++i) {
        const std::string arg = args[static_cast<std::size_t>(i)].text;
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

    const hki::PDCode pd = hki::parsePDCode(readWholeFile(inputPath));
    std::string value;
    if (worker == "homfly") value = hki::computeHomflyPT(pd);
    else if (worker == "khovanov") value = hki::computeKhovanov(pd);
    else if (worker == "simplify") value = hki::computeSimplifiedPDCode(pd);
    else throw std::runtime_error("unknown worker: " + worker);
    writeWholeFile(outputPath, value);
    return 0;
}

std::string normalizeNameSyntax(const std::string& rawName) {
    std::string out;
    for (unsigned char c : rawName) {
        if (std::isspace(c)) continue;
        char v = static_cast<char>(std::tolower(c));
        if (v == 'k') v = 'K';
        out.push_back(v);
    }
    return out;
}

std::optional<int> parsePrimeFactorCrossings(const std::string& rawFactor) {
    std::string factor = normalizeNameSyntax(rawFactor);
    if (factor.empty()) return std::nullopt;
    if (factor.size() > 1 && factor[0] == 'm') factor = factor.substr(1);
    if (factor.size() < 4 || factor[0] != 'K') return std::nullopt;

    std::size_t pos = 1;
    if (pos >= factor.size() || !std::isdigit(static_cast<unsigned char>(factor[pos]))) {
        return std::nullopt;
    }

    int crossings = 0;
    while (pos < factor.size() && std::isdigit(static_cast<unsigned char>(factor[pos]))) {
        crossings = crossings * 10 + (factor[pos] - '0');
        if (crossings > 1000) return std::nullopt;
        ++pos;
    }

    if (pos >= factor.size() || (factor[pos] != 'a' && factor[pos] != 'n')) return std::nullopt;
    ++pos;
    if (pos >= factor.size()) return std::nullopt;
    while (pos < factor.size()) {
        if (!std::isdigit(static_cast<unsigned char>(factor[pos]))) return std::nullopt;
        ++pos;
    }

    return crossings;
}

std::optional<int> totalKnotCrossings(const std::string& rawName) {
    int total = 0;
    bool hasFactor = false;
    std::size_t start = 0;
    while (start <= rawName.size()) {
        const std::size_t pos = rawName.find(',', start);
        const std::string factor = rawName.substr(
            start,
            pos == std::string::npos ? std::string::npos : pos - start);
        const std::optional<int> crossings = parsePrimeFactorCrossings(factor);
        if (!crossings.has_value()) return std::nullopt;
        total += *crossings;
        if (total > 1000) return std::nullopt;
        hasFactor = true;
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    if (!hasFactor) return std::nullopt;
    return total;
}

class NameCanonicalizer {
public:
    explicit NameCanonicalizer(const std::filesystem::path& knotNameRegDir) {
        loadNamePairs(knotNameRegDir / "name_pair.txt");
        loadAmphichiral(knotNameRegDir / "amphichiral_list.txt");
    }

    std::string normalize(const std::string& rawName) const {
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (start <= rawName.size()) {
            const std::size_t pos = rawName.find(',', start);
            parts.push_back(normalizePrime(rawName.substr(start, pos == std::string::npos ? std::string::npos : pos - start)));
            if (pos == std::string::npos) break;
            start = pos + 1;
        }

        parts.erase(std::remove_if(parts.begin(), parts.end(), [](const std::string& value) {
            return value.empty();
        }), parts.end());
        if (parts.empty()) return "";
        std::sort(parts.begin(), parts.end());
        return join(parts, ",");
    }

    std::string statusMessage() const {
        std::ostringstream out;
        out << "name canonicalizer: " << legacyToModern_.size() << " legacy aliases, "
            << amphichiralModern_.size() << " amphichiral prime knots";
        return out.str();
    }

private:
    std::unordered_map<std::string, std::string> legacyToModern_;
    std::unordered_map<std::string, std::string> modernToLegacy_;
    std::unordered_set<std::string> amphichiralLegacy_;
    std::unordered_set<std::string> amphichiralModern_;

    std::string normalizePrime(const std::string& rawName) const {
        std::string name = normalizeNameSyntax(rawName);
        if (name.empty()) return "";

        bool mirror = false;
        if (name.size() > 1 && name[0] == 'm') {
            mirror = true;
            name = name.substr(1);
        }

        const std::string base = modernize(name);
        if (isAmphichiral(base)) return base;
        return mirror ? ("m" + base) : base;
    }

    std::string modernize(const std::string& rawBase) const {
        const std::string base = normalizeNameSyntax(rawBase);
        const auto legacy = legacyToModern_.find(base);
        if (legacy != legacyToModern_.end()) return legacy->second;
        return base;
    }

    bool isAmphichiral(const std::string& rawBase) const {
        const std::string base = normalizeNameSyntax(rawBase);
        if (amphichiralModern_.find(base) != amphichiralModern_.end()) return true;
        if (amphichiralLegacy_.find(base) != amphichiralLegacy_.end()) return true;

        const auto legacy = modernToLegacy_.find(base);
        if (legacy != modernToLegacy_.end() &&
            amphichiralLegacy_.find(legacy->second) != amphichiralLegacy_.end()) {
            return true;
        }

        const auto modern = legacyToModern_.find(base);
        return modern != legacyToModern_.end() &&
               amphichiralModern_.find(modern->second) != amphichiralModern_.end();
    }

    void loadNamePairs(const std::filesystem::path& file) {
        std::ifstream input(file);
        if (!input) return;

        std::string line;
        while (std::getline(input, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            std::istringstream row(line);
            std::string legacyRaw;
            std::string modernRaw;
            row >> legacyRaw >> modernRaw;
            const std::string legacy = normalizeNameSyntax(legacyRaw);
            const std::string modern = normalizeNameSyntax(modernRaw);
            if (legacy.empty() || modern.empty()) continue;
            legacyToModern_[legacy] = modern;
            modernToLegacy_[modern] = legacy;
        }
    }

    void loadAmphichiral(const std::filesystem::path& file) {
        std::ifstream input(file);
        if (!input) return;

        std::string line;
        while (std::getline(input, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            const std::string legacy = normalizeNameSyntax(line);
            if (legacy.empty()) continue;
            amphichiralLegacy_.insert(legacy);
            amphichiralModern_.insert(modernize(legacy));
        }
    }
};

std::string cleanFieldForTsv(std::string value) {
    for (char& c : value) {
        if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    }
    return trim(value);
}

std::vector<std::string> splitTabLine(const std::string& line) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t pos = line.find('\t', start);
        parts.push_back(line.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return parts;
}

bool parseNamePdRecord(const std::string& rawLine, std::string& name, std::string& pd) {
    std::size_t start = 0;
    std::size_t end = rawLine.size();
    while (start < end && std::isspace(static_cast<unsigned char>(rawLine[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(rawLine[end - 1]))) --end;
    if (start >= end || rawLine[start] == '#') return false;
    if (rawLine[start] == '[' && rawLine[end - 1] == ']') {
        ++start;
        --end;
    }
    const std::size_t sep = rawLine.find('|', start);
    if (sep == std::string::npos || sep >= end) return false;
    name = trim(rawLine.substr(start, sep - start));
    pd = trim(rawLine.substr(sep + 1, end - sep - 1));
    return !name.empty() && !pd.empty();
}

bool parseNamePdName(const std::string& rawLine, std::string& name) {
    std::size_t start = 0;
    std::size_t end = rawLine.size();
    while (start < end && std::isspace(static_cast<unsigned char>(rawLine[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(rawLine[end - 1]))) --end;
    if (start >= end || rawLine[start] == '#') return false;
    if (rawLine[start] == '[' && rawLine[end - 1] == ']') {
        ++start;
        --end;
    }
    const std::size_t sep = rawLine.find('|', start);
    if (sep == std::string::npos || sep >= end) return false;
    name = trim(rawLine.substr(start, sep - start));
    return !name.empty();
}

std::optional<std::string> canonicalizePdText(const std::string& pdText) {
    try {
        return hki::formatPDCodeList(hki::parsePDCode(pdText));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string xmlEscape(const std::string& text) {
    std::string out;
    for (char ch : text) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string svgNumber(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value;
    return out.str();
}

constexpr int kDiagramTop = 1 << 0;
constexpr int kDiagramRight = 1 << 1;
constexpr int kDiagramBottom = 1 << 2;
constexpr int kDiagramLeft = 1 << 3;

struct DiagramMatrixBounds {
    int minRow = 0;
    int minCol = 0;
    int maxRow = -1;
    int maxCol = -1;

    bool empty() const {
        return maxRow < minRow || maxCol < minCol;
    }

    int rows() const {
        return empty() ? 0 : maxRow - minRow + 1;
    }

    int cols() const {
        return empty() ? 0 : maxCol - minCol + 1;
    }
};

struct DiagramLayoutCandidate {
    IntMatrix matrix;
    double score = 0.0;
    unsigned int seed = 0;
    int borderSocket = -1;

    DiagramLayoutCandidate(IntMatrix matrixIn,
                           double scoreIn,
                           unsigned int seedIn,
                           int borderSocketIn)
        : matrix(std::move(matrixIn)),
          score(scoreIn),
          seed(seedIn),
          borderSocket(borderSocketIn) {}
};

std::mutex& pdDiagramLayoutMutex() {
    static std::mutex mutex;
    return mutex;
}

hki::PDCode renumberPdLabelsForDiagram(const hki::PDCode& pd) {
    hki::validatePDCode(pd);
    std::map<int, int> labelMap;
    for (const hki::Crossing& crossing : pd) {
        for (int label : crossing) {
            labelMap.emplace(label, 0);
        }
    }

    int nextLabel = 1;
    for (auto& item : labelMap) {
        item.second = nextLabel++;
    }

    hki::PDCode normalized = pd;
    for (hki::Crossing& crossing : normalized) {
        for (int& label : crossing) {
            label = labelMap.at(label);
        }
    }
    return normalized;
}

std::vector<int> diagramBorderSocketCandidates(const hki::PDCode& pd) {
    std::vector<int> candidates{-1};
    if (pd.empty()) return candidates;

    std::map<int, std::set<int>> graph;
    for (const hki::Crossing& crossing : pd) {
        graph[crossing[0]].insert(crossing[2]);
        graph[crossing[2]].insert(crossing[0]);
        graph[crossing[1]].insert(crossing[3]);
        graph[crossing[3]].insert(crossing[1]);
    }

    std::set<int> visited;
    for (const auto& item : graph) {
        const int start = item.first;
        if (visited.count(start)) continue;

        int representative = start;
        std::vector<int> stack{start};
        visited.insert(start);
        while (!stack.empty()) {
            const int now = stack.back();
            stack.pop_back();
            representative = std::min(representative, now);
            for (int next : graph[now]) {
                if (!visited.count(next)) {
                    visited.insert(next);
                    stack.push_back(next);
                }
            }
        }
        candidates.push_back(representative);
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    if (candidates.size() > 5) candidates.resize(5);
    if (std::find(candidates.begin(), candidates.end(), -1) == candidates.end()) {
        candidates.insert(candidates.begin(), -1);
    }
    return candidates;
}

DiagramMatrixBounds diagramMatrixBounds(const IntMatrix& matrix) {
    DiagramMatrixBounds bounds;
    for (int row = 0; row < matrix.getRowCnt(); ++row) {
        for (int col = 0; col < matrix.getColCnt(); ++col) {
            if (matrix.getPos(row, col) == 0) continue;
            if (bounds.empty()) {
                bounds.minRow = bounds.maxRow = row;
                bounds.minCol = bounds.maxCol = col;
            } else {
                bounds.minRow = std::min(bounds.minRow, row);
                bounds.maxRow = std::max(bounds.maxRow, row);
                bounds.minCol = std::min(bounds.minCol, col);
                bounds.maxCol = std::max(bounds.maxCol, col);
            }
        }
    }
    return bounds;
}

int diagramBitCount4(int mask) {
    int count = 0;
    for (int bit : {kDiagramTop, kDiagramRight, kDiagramBottom, kDiagramLeft}) {
        if (mask & bit) ++count;
    }
    return count;
}

int diagramLineMask(const IntMatrix& matrix, int row, int col) {
    const int val = matrix.getPos(row, col);
    if (val <= 0) return 0;

    int mask = 0;
    const int top = matrix.getPos(row - 1, col);
    const int right = matrix.getPos(row, col + 1);
    const int bottom = matrix.getPos(row + 1, col);
    const int left = matrix.getPos(row, col - 1);
    if (top == val || top < 0) mask |= kDiagramTop;
    if (right == val || right < 0) mask |= kDiagramRight;
    if (bottom == val || bottom < 0) mask |= kDiagramBottom;
    if (left == val || left < 0) mask |= kDiagramLeft;
    return mask;
}

bool isDiagramStraightMask(int mask) {
    return mask == (kDiagramTop | kDiagramBottom) ||
           mask == (kDiagramLeft | kDiagramRight);
}

bool isDiagramCornerMask(int mask) {
    return mask == (kDiagramTop | kDiagramRight) ||
           mask == (kDiagramRight | kDiagramBottom) ||
           mask == (kDiagramBottom | kDiagramLeft) ||
           mask == (kDiagramLeft | kDiagramTop);
}

double scoreDiagramMatrix(const IntMatrix& matrix) {
    const DiagramMatrixBounds bounds = diagramMatrixBounds(matrix);
    if (bounds.empty()) return 0.0;

    const int rows = bounds.rows();
    const int cols = bounds.cols();
    const int area = rows * cols;
    int nonZero = 0;
    int turns = 0;
    int unsupported = 0;
    int endpointPenalty = 0;

    for (int row = bounds.minRow; row <= bounds.maxRow; ++row) {
        for (int col = bounds.minCol; col <= bounds.maxCol; ++col) {
            const int val = matrix.getPos(row, col);
            if (val == 0) continue;
            ++nonZero;
            if (val < 0) continue;

            const int mask = diagramLineMask(matrix, row, col);
            const int degree = diagramBitCount4(mask);
            if (isDiagramCornerMask(mask)) ++turns;
            if (!isDiagramCornerMask(mask) && !isDiagramStraightMask(mask)) ++unsupported;
            if (degree != 2) endpointPenalty += std::abs(degree - 2);
        }
    }

    const int blanks = area - nonZero;
    const int imbalance = std::abs(rows - cols);
    return static_cast<double>(area) * 1000.0 +
           static_cast<double>(blanks) * 25.0 +
           static_cast<double>(turns) * 12.0 +
           static_cast<double>(imbalance) * 5.0 +
           static_cast<double>(unsupported) * 100000.0 +
           static_cast<double>(endpointPenalty) * 50000.0;
}

IntMatrix buildOptimizedDiagramMatrix(const hki::PDCode& normalizedPd) {
    constexpr unsigned int kSeedStart = 42;
    constexpr int kSeedAttemptsPerBorder = 64;
    constexpr int kMinimumAttemptsAfterSuccess = 16;
    constexpr auto kOptimizeTimeBudget = std::chrono::milliseconds(2000);

    const std::string pdInput = hki::formatPDCodeList(normalizedPd);
    const std::vector<int> borderSockets = diagramBorderSocketCandidates(normalizedPd);
    PdToDiagram2d converter;
    std::optional<DiagramLayoutCandidate> best;
    std::string lastError;
    int attempts = 0;
    const auto started = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(pdDiagramLayoutMutex());
    for (int borderSocket : borderSockets) {
        for (int seedOffset = 0; seedOffset < kSeedAttemptsPerBorder; ++seedOffset) {
            const unsigned int seed = kSeedStart + static_cast<unsigned int>(seedOffset);
            ++attempts;
            try {
                std::stringstream input(pdInput);
                auto layout = converter.tryConvertOnce(seed, borderSocket, input);
                IntMatrix matrix = std::get<1>(layout);
                const double score = scoreDiagramMatrix(matrix);
                if (!best || score < best->score) {
                    best.emplace(std::move(matrix), score, seed, borderSocket);
                }
            } catch (const std::exception& error) {
                lastError = error.what();
            } catch (...) {
                lastError = "unknown layout error";
            }

            if (best && attempts >= kMinimumAttemptsAfterSuccess &&
                std::chrono::steady_clock::now() - started >= kOptimizeTimeBudget) {
                return best->matrix;
            }
        }
    }

    if (!best) {
        std::string message = "pd-code-to-diagram could not lay out this PD code";
        if (!lastError.empty()) message += ": " + lastError;
        throw std::runtime_error(message);
    }
    return best->matrix;
}

void appendSvgLine(std::ostringstream& svg,
                   double x1,
                   double y1,
                   double x2,
                   double y2) {
    svg << "<line class=\"strand\" x1=\"" << svgNumber(x1)
        << "\" y1=\"" << svgNumber(y1)
        << "\" x2=\"" << svgNumber(x2)
        << "\" y2=\"" << svgNumber(y2) << "\"/>\n";
}

void appendSvgCorner(std::ostringstream& svg,
                     double sx,
                     double sy,
                     double cx,
                     double cy,
                     double ex,
                     double ey) {
    svg << "<path class=\"strand\" d=\"M " << svgNumber(sx) << " " << svgNumber(sy)
        << " Q " << svgNumber(cx) << " " << svgNumber(cy)
        << " " << svgNumber(ex) << " " << svgNumber(ey) << "\"/>\n";
}

void appendSvgRegularTile(std::ostringstream& svg,
                          const IntMatrix& matrix,
                          int row,
                          int col,
                          double x,
                          double y,
                          double tile) {
    const double midX = x + tile / 2.0;
    const double midY = y + tile / 2.0;
    const int mask = diagramLineMask(matrix, row, col);

    switch (mask) {
        case kDiagramTop | kDiagramRight:
            appendSvgCorner(svg, midX, y, x + tile, y, x + tile, midY);
            return;
        case kDiagramRight | kDiagramBottom:
            appendSvgCorner(svg, x + tile, midY, x + tile, y + tile, midX, y + tile);
            return;
        case kDiagramBottom | kDiagramLeft:
            appendSvgCorner(svg, midX, y + tile, x, y + tile, x, midY);
            return;
        case kDiagramLeft | kDiagramTop:
            appendSvgCorner(svg, x, midY, x, y, midX, y);
            return;
        case kDiagramTop | kDiagramBottom:
            appendSvgLine(svg, midX, y, midX, y + tile);
            return;
        case kDiagramLeft | kDiagramRight:
            appendSvgLine(svg, x, midY, x + tile, midY);
            return;
        default:
            break;
    }

    if (mask & kDiagramTop) appendSvgLine(svg, midX, midY, midX, y);
    if (mask & kDiagramRight) appendSvgLine(svg, midX, midY, x + tile, midY);
    if (mask & kDiagramBottom) appendSvgLine(svg, midX, midY, midX, y + tile);
    if (mask & kDiagramLeft) appendSvgLine(svg, midX, midY, x, midY);
}

void appendSvgCrossingTile(std::ostringstream& svg,
                           int crossingValue,
                           double x,
                           double y,
                           double tile) {
    const double midX = x + tile / 2.0;
    const double midY = y + tile / 2.0;
    const double gapRadius = 6.0;

    if (crossingValue == -1) {
        appendSvgLine(svg, midX, y, midX, y + tile);
        svg << "<circle class=\"gap\" cx=\"" << svgNumber(midX)
            << "\" cy=\"" << svgNumber(midY)
            << "\" r=\"" << svgNumber(gapRadius) << "\"/>\n";
        appendSvgLine(svg, x, midY, x + tile, midY);
    } else if (crossingValue == -2) {
        appendSvgLine(svg, x, midY, x + tile, midY);
        svg << "<circle class=\"gap\" cx=\"" << svgNumber(midX)
            << "\" cy=\"" << svgNumber(midY)
            << "\" r=\"" << svgNumber(gapRadius) << "\"/>\n";
        appendSvgLine(svg, midX, y, midX, y + tile);
    }
}

std::string renderUnknotSvg(const std::string& title) {
    constexpr int width = 180;
    constexpr int height = 150;
    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
        << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << " " << height
        << "\" role=\"img\" aria-label=\"" << xmlEscape(title) << "\">\n";
    svg << "<title>" << xmlEscape(title) << "</title>\n";
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    svg << "<circle cx=\"90\" cy=\"75\" r=\"48\" fill=\"none\" stroke=\"#111827\""
        << " stroke-width=\"4\"/>\n";
    svg << "</svg>";
    return svg.str();
}

std::string renderPdMatrixSvg(const IntMatrix& matrix, const std::string& title) {
    const DiagramMatrixBounds bounds = diagramMatrixBounds(matrix);
    if (bounds.empty()) return renderUnknotSvg(title);

    constexpr double tile = 30.0;
    constexpr double padding = 14.0;
    const int rows = bounds.rows();
    const int cols = bounds.cols();
    const int width = static_cast<int>(cols * tile + padding * 2.0);
    const int height = static_cast<int>(rows * tile + padding * 2.0);

    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
        << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << " " << height
        << "\" role=\"img\" aria-label=\"" << xmlEscape(title) << "\">\n";
    svg << "<title>" << xmlEscape(title) << "</title>\n";
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    svg << "<style>"
        << ".strand{fill:none;stroke:#111827;stroke-width:4;stroke-linecap:butt;"
        << "stroke-linejoin:round;shape-rendering:geometricPrecision}"
        << ".gap{fill:white;stroke:white;stroke-width:0}"
        << "</style>\n";

    for (int row = bounds.minRow; row <= bounds.maxRow; ++row) {
        for (int col = bounds.minCol; col <= bounds.maxCol; ++col) {
            const int value = matrix.getPos(row, col);
            if (value <= 0) continue;
            const double x = padding + static_cast<double>(col - bounds.minCol) * tile;
            const double y = padding + static_cast<double>(row - bounds.minRow) * tile;
            appendSvgRegularTile(svg, matrix, row, col, x, y, tile);
        }
    }

    for (int row = bounds.minRow; row <= bounds.maxRow; ++row) {
        for (int col = bounds.minCol; col <= bounds.maxCol; ++col) {
            const int value = matrix.getPos(row, col);
            if (value != -1 && value != -2) continue;
            const double x = padding + static_cast<double>(col - bounds.minCol) * tile;
            const double y = padding + static_cast<double>(row - bounds.minRow) * tile;
            appendSvgCrossingTile(svg, value, x, y, tile);
        }
    }

    svg << "</svg>";
    return svg.str();
}

struct PdDiagram {
    std::string svg;
    std::string error;
};

PdDiagram buildPdDiagram(const std::string& pdText) {
    try {
        hki::PDCode pd = hki::parsePDCode(pdText);
        if (pd.empty()) return PdDiagram{renderUnknotSvg("PD Diagram"), ""};
        pd = renumberPdLabelsForDiagram(pd);
        const IntMatrix matrix = buildOptimizedDiagramMatrix(pd);
        return PdDiagram{renderPdMatrixSvg(matrix, "PD Diagram"), ""};
    } catch (const std::exception& error) {
        return PdDiagram{"", error.what()};
    }
}

std::string sqliteError(sqlite3* db) {
    return db ? sqlite3_errmsg(db) : "sqlite handle is null";
}

bool isCorruptSqliteMessage(const std::string& message) {
    const std::string lower = lowerCopy(message);
    return lower.find("database disk image is malformed") != std::string::npos ||
           lower.find("file is not a database") != std::string::npos ||
           lower.find("malformed") != std::string::npos;
}

class SqliteStatement {
public:
    SqliteStatement(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
            throw std::runtime_error("sqlite prepare failed: " + sqliteError(db_));
        }
    }

    ~SqliteStatement() {
        if (stmt_) sqlite3_finalize(stmt_);
    }

    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;

    void bindText(int index, const std::string& value) {
        if (sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
            throw std::runtime_error("sqlite bind failed: " + sqliteError(db_));
        }
    }

    void bindInt64(int index, sqlite3_int64 value) {
        if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) {
            throw std::runtime_error("sqlite bind failed: " + sqliteError(db_));
        }
    }

    int step() {
        const int rc = sqlite3_step(stmt_);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
            throw std::runtime_error("sqlite step failed: " + sqliteError(db_));
        }
        return rc;
    }

    void stepDone() {
        const int rc = step();
        if (rc != SQLITE_DONE) throw std::runtime_error("sqlite statement returned an unexpected row");
    }

    std::string columnText(int index) const {
        const unsigned char* value = sqlite3_column_text(stmt_, index);
        return value ? reinterpret_cast<const char*>(value) : "";
    }

    std::size_t columnSize(int index) const {
        return static_cast<std::size_t>(sqlite3_column_int64(stmt_, index));
    }

    void reset() {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

class PdSqliteStore {
public:
    struct Record {
        std::string name;
        std::string pd;
    };

    struct InvariantRecord {
        std::string name;
        std::string canonicalPd;
        std::string homfly;
        std::string khovanov;
    };

    PdSqliteStore(std::filesystem::path file, const NameCanonicalizer& names)
        : file_(std::move(file)), names_(names) {
        if (existsPath(file_)) {
            try {
                openReadOnly();
                refreshCounts();
            } catch (const std::exception& error) {
                statusMessage_ = "SQLite database could not be opened at " + cki::platform::displayPath(file_) + ": " + error.what();
                close();
            }
        }
    }

    ~PdSqliteStore() {
        close();
    }

    PdSqliteStore(const PdSqliteStore&) = delete;
    PdSqliteStore& operator=(const PdSqliteStore&) = delete;

    bool hasPdRecords() const {
        return db_ && pdRecordCount_ > 0;
    }

    bool hasInvariantRecords() const {
        return db_ && invariantRecordCount_ > 0;
    }

    std::string statusMessage() const {
        if (!db_) {
            if (!statusMessage_.empty()) return statusMessage_;
            return "SQLite database not found at " + cki::platform::displayPath(file_) + ".";
        }
        return "SQLite database: " + std::to_string(pdRecordCount_) + " PD records and " +
               std::to_string(invariantRecordCount_) + " invariant records from " + cki::platform::displayPath(file_);
    }

    std::optional<std::string> lookupPd(const std::string& rawName) const {
        if (!hasPdRecords()) return std::nullopt;
        const std::string name = names_.normalize(rawName);
        if (name.empty()) return std::nullopt;

        std::lock_guard<std::mutex> lock(mutex_);
        SqliteStatement stmt(db_, "SELECT pd FROM pd_records WHERE name = ?");
        stmt.bindText(1, name);
        if (stmt.step() != SQLITE_ROW) return std::nullopt;
        return stmt.columnText(0);
    }

    std::vector<std::string> lookupInvariants(const std::optional<std::string>& homfly,
                                              const std::optional<std::string>& khovanov) const {
        if (!hasInvariantRecords()) return {};
        if (homfly.has_value() && khovanov.has_value()) {
            std::vector<std::string> names = queryNames(
                "SELECT name FROM invariants WHERE homfly = ? AND khovanov = ? ORDER BY name",
                {*homfly, *khovanov});
            if (!names.empty()) return names;
        }
        if (khovanov.has_value()) {
            std::vector<std::string> names = queryNames(
                "SELECT name FROM invariants WHERE khovanov = ? ORDER BY name",
                {*khovanov});
            if (!names.empty()) return names;
        }
        if (homfly.has_value()) {
            return queryNames("SELECT name FROM invariants WHERE homfly = ? ORDER BY name", {*homfly});
        }
        return {};
    }

    std::vector<std::string> lookupByCanonicalPd(const std::string& canonicalPd) const {
        if (!hasPdRecords() || canonicalPd.empty()) return {};
        return queryNames("SELECT name FROM pd_records WHERE pd = ? ORDER BY name", {canonicalPd});
    }

    bool hasInvariantName(const std::string& rawName) const {
        if (!hasInvariantRecords()) return false;
        const std::string name = names_.normalize(rawName);
        if (name.empty()) return false;

        std::lock_guard<std::mutex> lock(mutex_);
        SqliteStatement stmt(db_, "SELECT 1 FROM invariants WHERE name = ? LIMIT 1");
        stmt.bindText(1, name);
        return stmt.step() == SQLITE_ROW;
    }

    void appendInvariant(const std::string& rawName,
                         const std::string& canonicalPd,
                         const std::string& homfly,
                         const std::string& khovanov) {
        appendInvariantBatch({InvariantRecord{rawName, canonicalPd, homfly, khovanov}});
    }

    std::size_t appendInvariantBatch(const std::vector<InvariantRecord>& records) {
        if (records.empty()) return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        openWritableIfNeeded();
        ensureSchemaNoLock();

        std::size_t written = 0;
        execNoLock("BEGIN IMMEDIATE");
        try {
            SqliteStatement stmt(
                db_,
                "INSERT OR REPLACE INTO invariants(name, canonical_pd, homfly, khovanov) VALUES (?, ?, ?, ?)");
            for (const InvariantRecord& record : records) {
                const std::string name = names_.normalize(record.name);
                if (name.empty() || record.canonicalPd.empty() || record.homfly.empty() || record.khovanov.empty()) continue;
                stmt.bindText(1, name);
                stmt.bindText(2, record.canonicalPd);
                stmt.bindText(3, record.homfly);
                stmt.bindText(4, record.khovanov);
                stmt.stepDone();
                written += static_cast<std::size_t>(sqlite3_changes(db_) > 0 ? 1 : 0);
                stmt.reset();
            }
            execNoLock("COMMIT");
        } catch (...) {
            try {
                execNoLock("ROLLBACK");
            } catch (const std::exception&) {
            }
            throw;
        }
        invariantRecordCount_ += written;
        return written;
    }

    std::size_t importNamePdText(const std::filesystem::path& textFile, std::size_t limit) {
        try {
            return importNamePdTextOnce(textFile, limit);
        } catch (const std::exception& error) {
            if (!isCorruptSqliteMessage(error.what())) throw;
            std::cerr << "warning: SQLite database at " << file_
                      << " is malformed; deleting it and rebuilding from PD_m text data.\n";
            removeDatabaseFilesForRebuild();
            return importNamePdTextOnce(textFile, limit);
        }
    }

    void beginInvariantBulkBuild() {
        std::lock_guard<std::mutex> lock(mutex_);
        openWritableIfNeeded();
        execNoLock("PRAGMA journal_mode=WAL");
        execNoLock("PRAGMA synchronous=NORMAL");
        execNoLock("PRAGMA temp_store=MEMORY");
        execNoLock("PRAGMA cache_size=-262144");
        ensureSchemaNoLock();
        execNoLock("DROP INDEX IF EXISTS idx_invariants_homfly");
        execNoLock("DROP INDEX IF EXISTS idx_invariants_khovanov");
        execNoLock("DROP INDEX IF EXISTS idx_invariants_pair");
        refreshCountsNoLock();
    }

    void finishInvariantBulkBuild() {
        std::lock_guard<std::mutex> lock(mutex_);
        openWritableIfNeeded();
        ensureSchemaNoLock();
        ensureIndexesNoLock();
        execNoLock("PRAGMA optimize");
        refreshCountsNoLock();
    }

    std::size_t countUnindexedRecords() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return 0;
        openWritableIfNeeded();
        ensureSchemaNoLock();
        SqliteStatement stmt(
            db_,
            "SELECT COUNT(*) "
            "FROM pd_records p LEFT JOIN invariants i ON i.name = p.name "
            "WHERE i.name IS NULL");
        if (stmt.step() != SQLITE_ROW) return 0;
        return stmt.columnSize(0);
    }

    std::vector<Record> fetchUnindexedRecordsAfter(const std::string& lastName, std::size_t limit) {
        if (limit == 0) return {};
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return {};
        openWritableIfNeeded();
        ensureSchemaNoLock();

        SqliteStatement stmt(
            db_,
            "SELECT p.name, p.pd "
            "FROM pd_records p LEFT JOIN invariants i ON i.name = p.name "
            "WHERE i.name IS NULL AND (?1 = '' OR p.name > ?1) "
            "ORDER BY p.name LIMIT ?2");
        stmt.bindText(1, lastName);
        stmt.bindInt64(2, static_cast<sqlite3_int64>(limit));

        std::vector<Record> records;
        records.reserve(limit);
        while (stmt.step() == SQLITE_ROW) {
            records.push_back(Record{stmt.columnText(0), stmt.columnText(1)});
        }
        return records;
    }

    std::vector<std::string> fetchUnindexedNamesAfter(const std::string& lastName, std::size_t limit) {
        if (limit == 0) return {};
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return {};
        openWritableIfNeeded();
        ensureSchemaNoLock();

        SqliteStatement stmt(
            db_,
            "SELECT p.name "
            "FROM pd_records p LEFT JOIN invariants i ON i.name = p.name "
            "WHERE i.name IS NULL AND (?1 = '' OR p.name > ?1) "
            "ORDER BY p.name LIMIT ?2");
        stmt.bindText(1, lastName);
        stmt.bindInt64(2, static_cast<sqlite3_int64>(limit));

        std::vector<std::string> names;
        names.reserve(limit);
        while (stmt.step() == SQLITE_ROW) {
            names.push_back(stmt.columnText(0));
        }
        return names;
    }

    std::vector<Record> fetchRecordsByNames(const std::vector<std::string>& names) {
        if (names.empty()) return {};
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return {};
        openWritableIfNeeded();
        ensureSchemaNoLock();

        SqliteStatement stmt(db_, "SELECT pd FROM pd_records WHERE name = ?1");
        std::vector<Record> records;
        records.reserve(names.size());
        for (const std::string& name : names) {
            stmt.bindText(1, name);
            if (stmt.step() == SQLITE_ROW) {
                records.push_back(Record{name, stmt.columnText(0)});
            }
            stmt.reset();
        }
        return records;
    }

    template <typename Callback>
    void forEachUnindexedRecord(Callback callback) {
        if (!hasPdRecords()) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            openWritableIfNeeded();
            ensureSchemaNoLock();
        }
        std::string name;
        std::string pd;
        SqliteStatement stmt(
            db_,
            "SELECT p.name, p.pd "
            "FROM pd_records p LEFT JOIN invariants i ON i.name = p.name "
            "WHERE i.name IS NULL ORDER BY p.name");

        while (!hki::interrupted()) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const int rc = stmt.step();
                if (rc == SQLITE_DONE) break;
                name = stmt.columnText(0);
                pd = stmt.columnText(1);
            }
            if (!callback(Record{name, pd})) break;
        }
    }

private:
    std::filesystem::path file_;
    const NameCanonicalizer& names_;
    mutable std::mutex mutex_;
    sqlite3* db_ = nullptr;
    bool writable_ = false;
    std::size_t pdRecordCount_ = 0;
    std::size_t invariantRecordCount_ = 0;
    std::string statusMessage_;

    std::size_t importNamePdTextOnce(const std::filesystem::path& textFile, std::size_t limit) {
        if (!existsPath(textFile)) {
            throw std::runtime_error("PD_m text database not found at " + cki::platform::displayPath(textFile) + ".");
        }
        std::ifstream input(textFile, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open PD_m text database at " + cki::platform::displayPath(textFile) + ".");

        std::lock_guard<std::mutex> lock(mutex_);
        openWritableIfNeeded();
        configureBulkImportNoLock();
        ensureSchemaNoLock();
        execNoLock("BEGIN IMMEDIATE");

        std::size_t imported = 0;
        std::size_t seen = 0;
        try {
            SqliteStatement stmt(db_, "INSERT OR IGNORE INTO pd_records(name, pd) VALUES (?, ?)");
            std::string line;
            while (std::getline(input, line)) {
                if (limit > 0 && imported >= limit) break;
                std::string name;
                std::string pd;
                if (!parseNamePdRecord(line, name, pd)) continue;
                name = names_.normalize(name);
                if (name.empty()) continue;

                stmt.bindText(1, name);
                stmt.bindText(2, pd);
                stmt.stepDone();
                imported += static_cast<std::size_t>(sqlite3_changes(db_) > 0 ? 1 : 0);
                stmt.reset();
                ++seen;

                if (seen % 50000 == 0) {
                    execNoLock("COMMIT");
                    std::cerr << "SQLite import progress: read " << seen << " rows, inserted "
                              << imported << " canonical rows.\n";
                    execNoLock("BEGIN IMMEDIATE");
                }
            }
        execNoLock("COMMIT");
        } catch (...) {
            try {
                execNoLock("ROLLBACK");
            } catch (const std::exception&) {
            }
            throw;
        }

        ensureIndexesNoLock();
        refreshCountsNoLock();
        std::cerr << "SQLite PD_m import complete: inserted " << imported
                  << " canonical rows into " << file_ << ".\n";
        return imported;
    }

    void removeDatabaseFilesForRebuild() {
        std::lock_guard<std::mutex> lock(mutex_);
        close();

        std::vector<std::filesystem::path> files;
        files.push_back(file_);
        std::filesystem::path wal = file_;
        wal += "-wal";
        files.push_back(wal);
        std::filesystem::path shm = file_;
        shm += "-shm";
        files.push_back(shm);
        std::filesystem::path journal = file_;
        journal += "-journal";
        files.push_back(journal);

        for (const std::filesystem::path& path : files) {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) continue;
            ec.clear();
            std::filesystem::remove(path, ec);
            if (ec) {
                throw std::runtime_error("cannot delete malformed SQLite file " +
                                         cki::platform::displayPath(path) + ": " + ec.message());
            }
        }

        statusMessage_.clear();
    }

    void close() {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        writable_ = false;
        pdRecordCount_ = 0;
        invariantRecordCount_ = 0;
    }

    void openReadOnly() {
        open(SQLITE_OPEN_READONLY, false);
    }

    void openWritableIfNeeded() {
        if (db_ && writable_) return;
        if (db_) close();
        std::filesystem::create_directories(file_.parent_path());
        open(SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, true);
    }

    void open(int flags, bool writable) {
        sqlite3* db = nullptr;
        const std::string filename = pathUtf8(file_);
        const int rc = sqlite3_open_v2(filename.c_str(), &db, flags, nullptr);
        if (rc != SQLITE_OK) {
            std::string message = db ? sqlite3_errmsg(db) : "unknown sqlite open error";
            if (db) sqlite3_close(db);
            throw std::runtime_error(message);
        }
        db_ = db;
        writable_ = writable;
        sqlite3_busy_timeout(db_, 30000);
    }

    void execNoLock(const std::string& sql) {
        char* error = nullptr;
        const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
        if (rc != SQLITE_OK) {
            std::string message = error ? error : sqliteError(db_);
            sqlite3_free(error);
            throw std::runtime_error("sqlite exec failed: " + message);
        }
    }

    void configureBulkImportNoLock() {
        execNoLock("PRAGMA journal_mode=OFF");
        execNoLock("PRAGMA synchronous=OFF");
        execNoLock("PRAGMA temp_store=MEMORY");
        execNoLock("PRAGMA locking_mode=EXCLUSIVE");
    }

    void ensureSchemaNoLock() {
        execNoLock(
            "CREATE TABLE IF NOT EXISTS pd_records("
            "name TEXT PRIMARY KEY,"
            "pd TEXT NOT NULL)");
        execNoLock(
            "CREATE TABLE IF NOT EXISTS invariants("
            "name TEXT PRIMARY KEY,"
            "canonical_pd TEXT NOT NULL,"
            "homfly TEXT NOT NULL,"
            "khovanov TEXT NOT NULL)");
    }

    void ensureIndexesNoLock() {
        execNoLock("CREATE INDEX IF NOT EXISTS idx_pd_records_pd ON pd_records(pd)");
        execNoLock("CREATE INDEX IF NOT EXISTS idx_invariants_homfly ON invariants(homfly)");
        execNoLock("CREATE INDEX IF NOT EXISTS idx_invariants_khovanov ON invariants(khovanov)");
        execNoLock("CREATE INDEX IF NOT EXISTS idx_invariants_pair ON invariants(homfly, khovanov)");
    }

    bool tableExistsNoLock(const std::string& table) const {
        SqliteStatement stmt(db_, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1");
        stmt.bindText(1, table);
        return stmt.step() == SQLITE_ROW;
    }

    std::size_t countRowsNoLock(const std::string& table) const {
        SqliteStatement stmt(db_, "SELECT COUNT(*) FROM " + table);
        if (stmt.step() != SQLITE_ROW) return 0;
        return stmt.columnSize(0);
    }

    void refreshCounts() {
        std::lock_guard<std::mutex> lock(mutex_);
        refreshCountsNoLock();
    }

    void refreshCountsNoLock() {
        pdRecordCount_ = tableExistsNoLock("pd_records") ? countRowsNoLock("pd_records") : 0;
        invariantRecordCount_ = tableExistsNoLock("invariants") ? countRowsNoLock("invariants") : 0;
    }

    std::vector<std::string> queryNames(const std::string& sql, const std::vector<std::string>& values) const {
        std::lock_guard<std::mutex> lock(mutex_);
        SqliteStatement stmt(db_, sql);
        for (std::size_t i = 0; i < values.size(); ++i) {
            stmt.bindText(static_cast<int>(i + 1), values[i]);
        }
        std::vector<std::string> names;
        while (stmt.step() == SQLITE_ROW) {
            names.push_back(stmt.columnText(0));
        }
        return sortedUnique(std::move(names));
    }
};

class PdRecordDatabase {
public:
    struct Record {
        std::string name;
        std::string pd;
        std::string canonicalPd;
    };

    PdRecordDatabase(const std::filesystem::path& file, const NameCanonicalizer& names, bool loadTextIndex)
        : file_(file), names_(names) {
        if (loadTextIndex) {
            load(file);
        } else {
            loadMessage_ = "PD_m text index skipped because SQLite data is available.";
        }
        addFallback("K0a1", "[]");
        addFallback("K3a1", "[[1,5,2,4],[3,1,4,6],[5,3,6,2]]");
    }

    std::optional<std::string> lookup(const std::string& rawName, std::string& error) const {
        const std::string name = names_.normalize(rawName);
        if (name.empty()) {
            error = "knot name should not be empty.";
            return std::nullopt;
        }
        const auto exact = nameToOffset_.find(name);
        if (exact != nameToOffset_.end()) {
            const auto pd = readPdAt(exact->second);
            if (pd.has_value()) return pd;
            error = "could not read PD_m record for " + name + ".";
            return std::nullopt;
        }
        const auto fallback = fallbackNameToPd_.find(name);
        if (fallback != fallbackNameToPd_.end()) return fallback->second;
        error = loaded_ ? "knot not found in PD_m_3-16.sorted.txt." : loadMessage_;
        return std::nullopt;
    }

    std::vector<std::string> lookupByCanonicalPd(const std::string& canonicalPd) const {
        const auto found = fallbackCanonicalPdToNames_.find(canonicalPd);
        if (found == fallbackCanonicalPdToNames_.end()) return {};
        return found->second;
    }

    template <typename Callback>
    void forEachRecord(Callback callback) const {
        if (!loaded_) return;
        std::ifstream input(file_, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open PD_m database at " + cki::platform::displayPath(file_) + ".");

        std::string line;
        while (std::getline(input, line)) {
            std::string name;
            std::string pd;
            if (!parseNamePdRecord(line, name, pd)) continue;
            name = names_.normalize(name);
            if (name.empty()) continue;
            Record record{name, pd, ""};
            if (!callback(record)) break;
        }
    }

    bool loaded() const {
        return loaded_;
    }

    std::string statusMessage() const {
        return loaded_ ? ("indexed " + std::to_string(recordCount_) + " PD_m record names from " + cki::platform::displayPath(file_)) : loadMessage_;
    }

private:
    std::filesystem::path file_;
    const NameCanonicalizer& names_;
    std::unordered_map<std::string, std::streamoff> nameToOffset_;
    std::unordered_map<std::string, std::string> fallbackNameToPd_;
    std::unordered_map<std::string, std::vector<std::string>> fallbackCanonicalPdToNames_;
    std::size_t recordCount_ = 0;
    bool loaded_ = false;
    std::string loadMessage_ = "PD_m database is not installed.";

    void addFallback(const std::string& rawName, const std::string& pd) {
        const std::string name = names_.normalize(rawName);
        if (fallbackNameToPd_.find(name) != fallbackNameToPd_.end()) return;
        const std::optional<std::string> canonical = canonicalizePdText(pd);
        fallbackNameToPd_[name] = pd;
        if (canonical.has_value()) fallbackCanonicalPdToNames_[*canonical].push_back(name);
    }

    std::optional<std::string> readPdAt(std::streamoff offset) const {
        std::ifstream input(file_, std::ios::binary);
        if (!input) return std::nullopt;
        input.seekg(offset);
        std::string line;
        if (!std::getline(input, line)) return std::nullopt;
        std::string name;
        std::string pd;
        if (!parseNamePdRecord(line, name, pd)) return std::nullopt;
        return pd;
    }

    void load(const std::filesystem::path& file) {
        if (!existsPath(file)) {
            loadMessage_ = "PD_m database not found at " + cki::platform::displayPath(file) + ".";
            return;
        }
        std::ifstream input(file, std::ios::binary);
        if (!input) {
            loadMessage_ = "cannot open PD_m database at " + cki::platform::displayPath(file) + ".";
            return;
        }

        std::string line;
        std::streamoff offset = static_cast<std::streamoff>(input.tellg());
        while (std::getline(input, line)) {
            std::string name;
            if (!parseNamePdName(line, name)) {
                offset = static_cast<std::streamoff>(input.tellg());
                continue;
            }
            name = names_.normalize(name);
            if (!name.empty() && nameToOffset_.find(name) == nameToOffset_.end()) {
                nameToOffset_[name] = offset;
            }
            ++recordCount_;
            offset = static_cast<std::streamoff>(input.tellg());
        }
        loaded_ = recordCount_ > 0;
        if (!loaded_) loadMessage_ = "PD_m database was found but no records could be loaded.";
        for (auto& item : fallbackCanonicalPdToNames_) item.second = sortedUnique(std::move(item.second));
    }
};

class PdInvariantIndex {
public:
    PdInvariantIndex(const std::filesystem::path& file, const NameCanonicalizer& names)
        : file_(file), names_(names) {
        load();
    }

    bool loaded() const {
        return loaded_;
    }

    std::size_t size() const {
        return indexedNames_.size();
    }

    bool hasName(const std::string& name) const {
        return indexedNames_.find(name) != indexedNames_.end();
    }

    std::vector<std::string> lookup(const std::optional<std::string>& homfly,
                                    const std::optional<std::string>& khovanov) const {
        if (homfly.has_value() && khovanov.has_value()) {
            const auto pairFound = pairToNames_.find(pairKey(*homfly, *khovanov));
            if (pairFound != pairToNames_.end()) return pairFound->second;

            const auto homFound = homflyToNames_.find(*homfly);
            const auto khoFound = khovanovToNames_.find(*khovanov);
            if (homFound != homflyToNames_.end() && khoFound != khovanovToNames_.end()) {
                return intersectNames(homFound->second, khoFound->second);
            }
        }
        if (khovanov.has_value()) {
            const auto found = khovanovToNames_.find(*khovanov);
            if (found != khovanovToNames_.end()) return found->second;
        }
        if (homfly.has_value()) {
            const auto found = homflyToNames_.find(*homfly);
            if (found != homflyToNames_.end()) return found->second;
        }
        return {};
    }

    std::string statusMessage() const {
        if (!loaded_) return "PD_m invariant index not found at " + cki::platform::displayPath(file_) + ".";
        return "loaded " + std::to_string(indexedNames_.size()) + " PD_m invariant records from " + cki::platform::displayPath(file_);
    }

    void append(const std::string& name,
                const std::string& canonicalPd,
                const std::string& homfly,
                const std::string& khovanov) {
        const std::string canonicalName = names_.normalize(name);
        if (canonicalName.empty() || indexedNames_.find(canonicalName) != indexedNames_.end()) return;
        std::filesystem::create_directories(file_.parent_path());
        std::ofstream out(file_, std::ios::app);
        if (!out) throw std::runtime_error("cannot append PD_m invariant index: " + cki::platform::displayPath(file_));
        out << cleanFieldForTsv(canonicalName) << '\t'
            << cleanFieldForTsv(canonicalPd) << '\t'
            << cleanFieldForTsv(homfly) << '\t'
            << cleanFieldForTsv(khovanov) << '\n';
        add(canonicalName, homfly, khovanov);
        loaded_ = true;
    }

private:
    std::filesystem::path file_;
    const NameCanonicalizer& names_;
    bool loaded_ = false;
    std::unordered_set<std::string> indexedNames_;
    std::unordered_map<std::string, std::vector<std::string>> homflyToNames_;
    std::unordered_map<std::string, std::vector<std::string>> khovanovToNames_;
    std::unordered_map<std::string, std::vector<std::string>> pairToNames_;

    static std::string pairKey(const std::string& homfly, const std::string& khovanov) {
        return homfly + '\x1f' + khovanov;
    }

    void add(const std::string& rawName, const std::string& homfly, const std::string& khovanov) {
        const std::string name = names_.normalize(rawName);
        if (name.empty() || homfly.empty() || khovanov.empty()) return;
        indexedNames_.insert(name);
        homflyToNames_[homfly].push_back(name);
        khovanovToNames_[khovanov].push_back(name);
        pairToNames_[pairKey(homfly, khovanov)].push_back(name);
    }

    void load() {
        if (!existsPath(file_)) return;
        std::ifstream input(file_);
        if (!input) throw std::runtime_error("cannot open PD_m invariant index: " + cki::platform::displayPath(file_));
        std::string line;
        while (std::getline(input, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            const std::vector<std::string> parts = splitTabLine(line);
            if (parts.size() < 4) continue;
            add(parts[0], parts[2], parts[3]);
        }
        loaded_ = !indexedNames_.empty();
        for (auto& item : homflyToNames_) item.second = sortedUnique(std::move(item.second));
        for (auto& item : khovanovToNames_) item.second = sortedUnique(std::move(item.second));
        for (auto& item : pairToNames_) item.second = sortedUnique(std::move(item.second));
    }
};

class KnotEngine {
public:
    KnotEngine(std::filesystem::path executable,
               DataPaths dataPaths,
               int timeoutSeconds,
               int maxCrossing,
               bool loadTextFallback)
        : executable_(std::move(executable)),
          dataPaths_(std::move(dataPaths)),
          timeoutSeconds_(timeoutSeconds),
          maxCrossing_(maxCrossing),
          nameCanonicalizer_(dataPaths_.knotNameRegDir),
          sqliteStore_(dataPaths_.sqliteFile, nameCanonicalizer_),
          pdRecords_(dataPaths_.namePdFile, nameCanonicalizer_, loadTextFallback && !sqliteStore_.hasPdRecords()),
          invariantIndex_(dataPaths_.invariantIndexFile, nameCanonicalizer_) {}

    LookupResult lookup(const std::string& pdText,
                        const std::shared_ptr<hki::CancellationToken>& cancellation = nullptr) {
        const hki::PDCode pd = hki::parsePDCode(pdText);
        const std::string canonical = hki::formatPDCodeList(pd);
        const PdDiagram diagram = buildPdDiagram(canonical);
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            const auto found = cache_.find(canonical);
            if (found != cache_.end()) {
                LookupResult cached = found->second;
                cached.pdDiagramSvg = diagram.svg;
                cached.pdDiagramError = diagram.error;
                return cached;
            }
        }

        LookupResult result = compute(canonical, cancellation);
        result.pdDiagramSvg = diagram.svg;
        result.pdDiagramError = diagram.error;
        const bool cacheable = !result.homflyWorker.cancelled &&
                               !result.khovanovWorker.cancelled &&
                               (result.homflyWorker.success || result.khovanovWorker.success);
        if (cacheable) {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            cache_[canonical] = result;
        }
        return result;
    }

    std::optional<std::string> lookupNamePd(const std::string& knotName, std::string& error) {
        if (auto crossingError = crossingLimitError(knotName)) {
            error = *crossingError;
            return std::nullopt;
        }
        if (auto pd = sqliteStore_.lookupPd(knotName)) return pd;
        return pdRecords_.lookup(knotName, error);
    }

    std::string nameIndexStatus() const {
        return sqliteStore_.statusMessage() + "\nText fallback: " + pdRecords_.statusMessage() + "\n" +
               nameCanonicalizer_.statusMessage() + "\nInvariant index: " + invariantIndex_.statusMessage();
    }

    int maxCrossing() const {
        return maxCrossing_;
    }

    std::size_t buildSqliteNameDatabase(std::size_t limit) {
        return sqliteStore_.importNamePdText(dataPaths_.namePdFile, limit);
    }

    std::size_t buildPdInvariantIndex(std::size_t limit,
                                      std::size_t workers,
                                      std::size_t batchSize,
                                      int progressSeconds) {
        if (sqliteStore_.hasPdRecords()) {
            return buildSqliteInvariantIndex(limit, workers, batchSize, progressSeconds).written;
        }
        if (!pdRecords_.loaded()) {
            throw std::runtime_error("PD_m_3-16.sorted.txt is required before building the invariant index.");
        }

        std::size_t built = 0;
        std::size_t skipped = 0;
        std::size_t outOfRange = 0;
        std::size_t failed = 0;
        pdRecords_.forEachRecord([&](const PdRecordDatabase::Record& record) -> bool {
            if (limit > 0 && built >= limit) return false;
            if (!shouldBuildInvariantForName(record.name)) {
                ++outOfRange;
                return true;
            }
            if (invariantIndex_.hasName(record.name)) {
                ++skipped;
                return true;
            }
            const std::optional<std::string> canonicalPd = canonicalizePdText(record.pd);
            if (!canonicalPd.has_value()) {
                ++skipped;
                return true;
            }

            std::cerr << "indexing " << record.name << " (" << (built + 1) << " built so far)\n";
            try {
                LookupResult result = computeInvariants(*canonicalPd, nullptr, InvariantPipelineMode::OriginalOnly);
                if (result.homflyWorker.success && result.khovanovWorker.success) {
                    invariantIndex_.append(record.name, *canonicalPd, result.homfly, result.khovanov);
                    ++built;
                } else {
                    ++failed;
                    std::cerr << "warning: could not index " << record.name << ": ";
                    if (!result.homflyWorker.success) {
                        std::cerr << workerFailureMessage("HOMFLY-PT", result.homflyWorker);
                    }
                    if (!result.homflyWorker.success && !result.khovanovWorker.success) std::cerr << "; ";
                    if (!result.khovanovWorker.success) {
                        std::cerr << workerFailureMessage("Khovanov", result.khovanovWorker);
                    }
                    std::cerr << "\n";
                }
            } catch (const std::exception& error) {
                ++failed;
                std::cerr << "warning: could not index " << record.name << ": " << error.what() << "\n";
            }
            return !hki::interrupted();
        });

        std::cerr << "PD_m invariant indexing complete: built " << built
                  << ", skipped " << skipped << ", failed " << failed << ".\n";
        if (outOfRange > 0) {
            std::cerr << "PD_m invariant indexing ignored " << outOfRange
                      << " records outside total crossing <= " << maxCrossing_ << ".\n";
        }
        return built;
    }

private:
    std::filesystem::path executable_;
    DataPaths dataPaths_;
    int timeoutSeconds_ = kMaxComputeTimeoutSeconds;
    int maxCrossing_ = kDefaultMaxCrossing;
    NameCanonicalizer nameCanonicalizer_;
    PdSqliteStore sqliteStore_;
    PdRecordDatabase pdRecords_;
    PdInvariantIndex invariantIndex_;
    std::mutex cacheMutex_;
    std::unordered_map<std::string, LookupResult> cache_;

    enum class IndexWorkStatus {
        Success,
        Skipped,
        Failed,
    };

    struct IndexWorkResult {
        IndexWorkStatus status = IndexWorkStatus::Failed;
        PdSqliteStore::InvariantRecord invariant;
        std::string name;
        std::string error;
    };

    static std::size_t resolveIndexWorkerCount(std::size_t requested) {
        if (requested > 0) return requested;
        const unsigned int hardware = std::thread::hardware_concurrency();
        if (hardware == 0) return 2;
        return std::max<std::size_t>(1, static_cast<std::size_t>(hardware) / 2);
    }

    std::optional<int> crossingNumberForName(const std::string& rawName) const {
        const std::string canonicalName = nameCanonicalizer_.normalize(rawName);
        if (canonicalName.empty()) return std::nullopt;
        return totalKnotCrossings(canonicalName);
    }

    bool shouldBuildInvariantForName(const std::string& rawName) const {
        const std::optional<int> crossings = crossingNumberForName(rawName);
        return crossings.has_value() && *crossings <= maxCrossing_;
    }

    std::optional<std::string> crossingLimitError(const std::string& rawName) const {
        const std::optional<int> crossings = crossingNumberForName(rawName);
        if (!crossings.has_value() || *crossings <= maxCrossing_) return std::nullopt;
        return "knot total crossing number " + std::to_string(*crossings) +
               " exceeds --max-crossing " + std::to_string(maxCrossing_) + ".";
    }

    std::vector<std::string> filterNamesByMaxCrossing(std::vector<std::string> names) const {
        names.erase(std::remove_if(names.begin(), names.end(), [&](const std::string& name) {
            return !shouldBuildInvariantForName(name);
        }), names.end());
        return names;
    }

    std::size_t countEligibleSqliteInvariantRecords(std::size_t limit, std::size_t pageSize) {
        std::size_t count = 0;
        std::string lastName;
        const std::size_t chunkSize = std::max<std::size_t>(1, pageSize);
        while (!hki::interrupted()) {
            std::vector<std::string> names = sqliteStore_.fetchUnindexedNamesAfter(lastName, chunkSize);
            if (names.empty()) break;
            lastName = names.back();
            for (const std::string& name : names) {
                if (!shouldBuildInvariantForName(name)) continue;
                ++count;
                if (limit > 0 && count >= limit) return count;
            }
            if (names.size() < chunkSize) break;
        }
        return count;
    }

    static void printIndexProgress(const IndexBuildStats& stats,
                                   std::size_t totalTarget,
                                   std::size_t workers,
                                   std::chrono::steady_clock::time_point started,
                                   bool final,
                                   std::mutex& logMutex) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsedSeconds =
            std::max(0.001, std::chrono::duration<double>(now - started).count());
        const std::size_t processed = stats.processed.load();
        const std::size_t queued = stats.queued.load();
        const std::size_t pending = queued > processed ? queued - processed : 0;
        const double rate = static_cast<double>(processed) / elapsedSeconds;
        const std::size_t remaining = totalTarget > processed ? totalTarget - processed : 0;
        const std::uint64_t etaSeconds = final ? 0 : (rate > 0.0
            ? static_cast<std::uint64_t>(std::ceil(static_cast<double>(remaining) / rate))
            : 0);
        const double percent = totalTarget > 0
            ? (100.0 * static_cast<double>(std::min(processed, totalTarget)) / static_cast<double>(totalTarget))
            : 100.0;

        std::ostringstream line;
        line << (final ? "PD_m index final: " : "PD_m index progress: ")
             << processed << "/" << totalTarget
             << " (" << std::fixed << std::setprecision(2) << percent << "%)"
             << ", written " << stats.written.load()
             << ", succeeded " << stats.succeeded.load()
             << ", skipped " << stats.skipped.load()
             << ", failed " << stats.failed.load()
             << ", queued " << pending
             << ", workers " << workers
             << ", rate " << formatRate(rate) << "/s"
             << ", elapsed " << formatDurationHms(static_cast<std::uint64_t>(elapsedSeconds))
             << ", ETA " << formatDurationHms(etaSeconds);

        std::lock_guard<std::mutex> lock(logMutex);
        std::cerr << line.str() << "\n";
    }

    IndexBuildResult buildSqliteInvariantIndex(std::size_t limit,
                                               std::size_t requestedWorkers,
                                               std::size_t requestedBatchSize,
                                               int progressSeconds) {
        const std::size_t workers = resolveIndexWorkerCount(requestedWorkers);
        const std::size_t batchSize = std::max<std::size_t>(1, requestedBatchSize);
        const int progressIntervalSeconds = std::max(1, progressSeconds);
        const std::size_t fetchChunk = std::max<std::size_t>(128, workers * 8);
        const std::size_t recordFetchChunk = std::max<std::size_t>(4096, fetchChunk);
        const std::size_t queueCapacity = std::max<std::size_t>(fetchChunk * 2, workers * 16);

        sqliteStore_.beginInvariantBulkBuild();
        bool bulkBuildOpen = true;

        auto finishBulkBuild = [&] {
            if (bulkBuildOpen) {
                sqliteStore_.finishInvariantBulkBuild();
                bulkBuildOpen = false;
            }
        };

        try {
            std::cerr << "Scanning SQLite PD_m records for unindexed knots with total crossing <= "
                      << maxCrossing_ << ".\n";
            const std::size_t totalTarget = countEligibleSqliteInvariantRecords(limit, recordFetchChunk);
            IndexBuildResult summary;
            summary.totalTarget = totalTarget;
            if (totalTarget == 0) {
                std::cerr << "SQLite invariant indexing complete: no eligible unindexed records remain.\n";
                finishBulkBuild();
                return summary;
            }

            std::cerr << "SQLite invariant indexing started: " << totalTarget
                      << " eligible records (total crossing <= " << maxCrossing_
                      << "), " << workers << " compute workers, batch size "
                      << batchSize << ", progress interval " << progressIntervalSeconds << " seconds.\n";

            BlockingQueue<PdSqliteStore::Record> inputQueue(queueCapacity);
            BlockingQueue<IndexWorkResult> outputQueue(queueCapacity);
            IndexBuildStats stats;
            std::atomic_bool stopRequested{false};
            std::atomic_bool reporterDone{false};
            std::atomic_bool monitorDone{false};
            std::mutex logMutex;
            std::mutex errorMutex;
            std::exception_ptr firstError;
            auto cancellation = std::make_shared<hki::CancellationToken>();
            const auto started = std::chrono::steady_clock::now();

            auto requestStop = [&] {
                const bool alreadyStopping = stopRequested.exchange(true);
                cancellation->cancel();
                inputQueue.close();
                outputQueue.close();
                reporterDone.store(true);
                return !alreadyStopping;
            };

            auto setError = [&](std::exception_ptr error) {
                {
                    std::lock_guard<std::mutex> lock(errorMutex);
                    if (!firstError) firstError = error;
                }
                requestStop();
            };

            std::thread interruptMonitor([&] {
                while (!monitorDone.load()) {
                    if (hki::interrupted()) {
                        if (requestStop()) {
                            std::lock_guard<std::mutex> lock(logMutex);
                            std::cerr << "Interrupt requested; stopping PD_m invariant indexing after the current flush.\n";
                        }
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });

            std::thread producer([&] {
                try {
                    std::string lastName;
                    std::size_t submitted = 0;
                    bool keepProducing = true;
                    std::vector<std::string> eligibleNames;
                    eligibleNames.reserve(fetchChunk);

                    auto submitBufferedNames = [&]() -> bool {
                        if (eligibleNames.empty()) return true;
                        std::vector<PdSqliteStore::Record> records = sqliteStore_.fetchRecordsByNames(eligibleNames);
                        eligibleNames.clear();
                        for (PdSqliteStore::Record& record : records) {
                            if (stopRequested.load() || hki::interrupted() || submitted >= totalTarget) return false;
                            if (!inputQueue.push(std::move(record), stopRequested)) return false;
                            ++submitted;
                            ++stats.queued;
                        }
                        return true;
                    };

                    while (keepProducing && !stopRequested.load() && !hki::interrupted() && submitted < totalTarget) {
                        std::vector<std::string> names =
                            sqliteStore_.fetchUnindexedNamesAfter(lastName, recordFetchChunk);
                        if (names.empty()) break;
                        lastName = names.back();
                        for (const std::string& name : names) {
                            if (stopRequested.load() || hki::interrupted() || submitted >= totalTarget) break;
                            if (submitted + eligibleNames.size() >= totalTarget) break;
                            if (!shouldBuildInvariantForName(name)) continue;
                            eligibleNames.push_back(name);
                            if (eligibleNames.size() >= fetchChunk && !submitBufferedNames()) {
                                keepProducing = false;
                                break;
                            }
                        }
                        if (names.size() < recordFetchChunk) break;
                    }
                    if (keepProducing) submitBufferedNames();
                } catch (...) {
                    setError(std::current_exception());
                }
                inputQueue.close();
            });

            std::vector<std::thread> computeThreads;
            computeThreads.reserve(workers);
            for (std::size_t workerIndex = 0; workerIndex < workers; ++workerIndex) {
                computeThreads.emplace_back([&] {
                    PdSqliteStore::Record record;
                    while (inputQueue.pop(record, stopRequested)) {
                        if (stopRequested.load() || hki::interrupted()) break;
                        IndexWorkResult work;
                        work.name = record.name;
                        try {
                            const std::optional<std::string> canonicalPd = canonicalizePdText(record.pd);
                            if (!canonicalPd.has_value()) {
                                work.status = IndexWorkStatus::Skipped;
                                work.error = "invalid PD code";
                            } else {
                                LookupResult result = computeInvariants(
                                    *canonicalPd,
                                    cancellation,
                                    InvariantPipelineMode::OriginalOnly);
                                if (result.homflyWorker.success && result.khovanovWorker.success) {
                                    work.status = IndexWorkStatus::Success;
                                    work.invariant = PdSqliteStore::InvariantRecord{
                                        record.name,
                                        *canonicalPd,
                                        result.homfly,
                                        result.khovanov,
                                    };
                                } else {
                                    work.status = IndexWorkStatus::Failed;
                                    std::ostringstream error;
                                    if (!result.homflyWorker.success) {
                                        error << workerFailureMessage("HOMFLY-PT", result.homflyWorker);
                                    }
                                    if (!result.homflyWorker.success && !result.khovanovWorker.success) error << "; ";
                                    if (!result.khovanovWorker.success) {
                                        error << workerFailureMessage("Khovanov", result.khovanovWorker);
                                    }
                                    work.error = error.str();
                                }
                            }
                        } catch (const std::exception& error) {
                            if (stopRequested.load() || hki::interrupted()) break;
                            work.status = IndexWorkStatus::Failed;
                            work.error = error.what();
                        }
                        if (!outputQueue.push(std::move(work), stopRequested)) break;
                    }
                });
            }

            std::thread writer([&] {
                std::vector<PdSqliteStore::InvariantRecord> batch;
                batch.reserve(batchSize);
                std::size_t failureLogs = 0;

                auto flush = [&] {
                    if (batch.empty()) return;
                    const std::size_t written = sqliteStore_.appendInvariantBatch(batch);
                    stats.written.fetch_add(written);
                    batch.clear();
                };

                try {
                    IndexWorkResult work;
                    while (outputQueue.pop(work, stopRequested)) {
                        if (work.status == IndexWorkStatus::Success) {
                            ++stats.succeeded;
                            batch.push_back(std::move(work.invariant));
                            if (batch.size() >= batchSize) flush();
                        } else if (work.status == IndexWorkStatus::Skipped) {
                            ++stats.skipped;
                        } else {
                            ++stats.failed;
                            if (failureLogs < 20) {
                                std::lock_guard<std::mutex> lock(logMutex);
                                std::cerr << "warning: could not index " << work.name << ": "
                                          << work.error << "\n";
                            } else if (failureLogs == 20) {
                                std::lock_guard<std::mutex> lock(logMutex);
                                std::cerr << "warning: suppressing further per-record indexing failures.\n";
                            }
                            ++failureLogs;
                        }
                        ++stats.processed;
                    }
                    flush();
                } catch (...) {
                    setError(std::current_exception());
                }
            });

            std::thread reporter([&] {
                while (!reporterDone.load() && !stopRequested.load()) {
                    for (int i = 0; i < progressIntervalSeconds && !reporterDone.load() && !stopRequested.load(); ++i) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    if (!reporterDone.load() && !stopRequested.load()) {
                        printIndexProgress(stats, totalTarget, workers, started, false, logMutex);
                    }
                }
            });

            producer.join();
            for (std::thread& thread : computeThreads) thread.join();
            outputQueue.close();
            writer.join();
            monitorDone.store(true);
            interruptMonitor.join();
            reporterDone.store(true);
            reporter.join();

            if (firstError) std::rethrow_exception(firstError);
            if (hki::interrupted()) {
                std::cerr << "SQLite invariant indexing interrupted.\n";
            }

            finishBulkBuild();
            printIndexProgress(stats, totalTarget, workers, started, true, logMutex);

            summary.written = stats.written.load();
            summary.skipped = stats.skipped.load();
            summary.failed = stats.failed.load();
            std::cerr << "SQLite invariant indexing complete: built " << summary.written
                      << ", skipped " << summary.skipped
                      << ", failed " << summary.failed << ".\n";
            return summary;
        } catch (...) {
            finishBulkBuild();
            throw;
        }
    }

    enum class InvariantKind {
        Homfly,
        Khovanov,
    };

    enum class InvariantPipelineMode {
        OriginalOnly,
        SimplifyRetry,
    };

    struct RunningAttempt {
        InvariantKind kind;
        std::string source;
        std::unique_ptr<hki::WorkerProcess> process;
    };

    struct SelectedInvariant {
        hki::WorkerResult result;
        std::string source;
        bool attempted = false;
    };

    static const char* workerNameFor(InvariantKind kind) {
        return kind == InvariantKind::Homfly ? "homfly" : "khovanov";
    }

    static SelectedInvariant& selectedFor(SelectedInvariant& homfly,
                                          SelectedInvariant& khovanov,
                                          InvariantKind kind) {
        return kind == InvariantKind::Homfly ? homfly : khovanov;
    }

    static bool hasRunningSource(const std::vector<RunningAttempt>& running,
                                 InvariantKind kind,
                                 const std::string& source) {
        return std::any_of(running.begin(), running.end(), [kind, &source](const RunningAttempt& attempt) {
            return attempt.kind == kind && attempt.source == source;
        });
    }

    static void recordAttempt(SelectedInvariant& selected,
                              const std::string& source,
                              const hki::WorkerResult& workerResult) {
        const bool hadAttempt = selected.attempted;
        selected.attempted = true;
        if (workerResult.success && !selected.result.success) {
            selected.result = workerResult;
            selected.source = source;
        } else if (!selected.result.success && !workerResult.cancelled) {
            selected.result = workerResult;
            selected.source = source;
        } else if (!selected.result.success && !hadAttempt) {
            selected.result = workerResult;
            selected.source = source;
        }
    }

    static void cancelRunningKind(std::vector<RunningAttempt>& running, InvariantKind kind) {
        for (RunningAttempt& attempt : running) {
            if (attempt.kind == kind) cancelWorkerProcess(*attempt.process);
        }
    }

    static void eraseCancelledAttempts(std::vector<RunningAttempt>& running) {
        running.erase(
            std::remove_if(running.begin(), running.end(), [](const RunningAttempt& attempt) {
                return attempt.process->result().cancelled;
            }),
            running.end());
    }

    LookupResult computeInvariants(const std::string& canonicalPd,
                                   const std::shared_ptr<hki::CancellationToken>& cancellation,
                                   InvariantPipelineMode mode) {
        const auto deadline = timeoutSeconds_ > 0
            ? std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds_)
            : std::chrono::steady_clock::time_point::max();

        LookupResult result;
        result.canonicalPd = canonicalPd;

        SelectedInvariant homfly;
        SelectedInvariant khovanov;
        std::vector<RunningAttempt> running;

        auto startInvariant = [&](InvariantKind kind, const std::string& source, const std::string& pdText) {
            SelectedInvariant& selected = selectedFor(homfly, khovanov, kind);
            if (selected.result.success || hasRunningSource(running, kind, source)) return;
            running.push_back(RunningAttempt{
                kind,
                source,
                hki::startWorkerProcess(executable_, workerNameFor(kind), pdText),
            });
        };

        startInvariant(InvariantKind::Homfly, "original", canonicalPd);
        startInvariant(InvariantKind::Khovanov, "original", canonicalPd);

        const bool allowSimplifyRetry = mode == InvariantPipelineMode::SimplifyRetry;
        std::unique_ptr<hki::WorkerProcess> simplify =
            allowSimplifyRetry ? hki::startWorkerProcess(executable_, "simplify", canonicalPd) : nullptr;
        hki::WorkerResult simplifyResult;
        std::string simplifiedPd;
        bool simplifyFinished = !allowSimplifyRetry;
        bool simplifiedAttemptsStarted = false;

        auto maybeStartSimplifiedAttempts = [&]() {
            if (simplifiedAttemptsStarted || simplifiedPd.empty() || simplifiedPd == canonicalPd) return;
            simplifiedAttemptsStarted = true;
            result.canonicalPd = simplifiedPd;
            if (!homfly.result.success) startInvariant(InvariantKind::Homfly, "simplified", simplifiedPd);
            if (!khovanov.result.success) startInvariant(InvariantKind::Khovanov, "simplified", simplifiedPd);
        };

        auto cancelAll = [&]() {
            if (simplify && !simplifyFinished) {
                hki::cancelWorkerProcess(*simplify);
                simplifyResult = hki::finishWorkerProcess(*simplify);
                simplifyFinished = true;
            }
            for (RunningAttempt& attempt : running) {
                hki::cancelWorkerProcess(*attempt.process);
                hki::WorkerResult workerResult = hki::finishWorkerProcess(*attempt.process);
                recordAttempt(selectedFor(homfly, khovanov, attempt.kind), attempt.source, workerResult);
            }
            running.clear();
        };

        while (true) {
            if (cancellation && cancellation->cancelled()) {
                cancelAll();
                break;
            }
            if (hki::interrupted()) {
                cancelAll();
                homfly.result.interrupted = true;
                khovanov.result.interrupted = true;
                break;
            }

            if (simplify && !simplifyFinished && hki::pollWorkerProcess(*simplify, deadline)) {
                simplifyResult = hki::finishWorkerProcess(*simplify);
                simplifyFinished = true;
                if (simplifyResult.success) {
                    simplifiedPd = simplifyResult.output;
                    maybeStartSimplifiedAttempts();
                }
            }

            for (std::size_t i = 0; i < running.size();) {
                RunningAttempt& attempt = running[i];
                if (!hki::pollWorkerProcess(*attempt.process, deadline)) {
                    ++i;
                    continue;
                }

                hki::WorkerResult workerResult = hki::finishWorkerProcess(*attempt.process);
                SelectedInvariant& selected = selectedFor(homfly, khovanov, attempt.kind);
                const bool alreadySelected = selected.result.success;
                recordAttempt(selected, attempt.source, workerResult);
                if (workerResult.success && !alreadySelected) {
                    cancelRunningKind(running, attempt.kind);
                }
                running.erase(running.begin() + static_cast<std::ptrdiff_t>(i));
                eraseCancelledAttempts(running);
            }

            if (homfly.result.success && khovanov.result.success && simplify && !simplifyFinished) {
                hki::cancelWorkerProcess(*simplify);
                simplifyResult = hki::finishWorkerProcess(*simplify);
                simplifyFinished = true;
            }

            if (simplifyFinished) maybeStartSimplifiedAttempts();
            if (running.empty() && simplifyFinished) break;

            if (std::chrono::steady_clock::now() >= deadline) {
                if (simplify && !simplifyFinished) {
                    hki::pollWorkerProcess(*simplify, deadline);
                    simplifyResult = hki::finishWorkerProcess(*simplify);
                    simplifyFinished = true;
                }
                for (RunningAttempt& attempt : running) {
                    hki::pollWorkerProcess(*attempt.process, deadline);
                    hki::WorkerResult workerResult = hki::finishWorkerProcess(*attempt.process);
                    recordAttempt(selectedFor(homfly, khovanov, attempt.kind), attempt.source, workerResult);
                }
                running.clear();
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        result.homflyWorker = homfly.result;
        result.khovanovWorker = khovanov.result;

        if (hki::interrupted() ||
            (result.homflyWorker.interrupted && !result.homflyWorker.cancelled) ||
            (result.khovanovWorker.interrupted && !result.khovanovWorker.cancelled)) {
            throw std::runtime_error("computation interrupted.");
        }

        if (result.homflyWorker.success) {
            result.homfly = result.homflyWorker.output;
        }
        if (result.khovanovWorker.success) {
            result.khovanov = result.khovanovWorker.output;
        }
        return result;
    }

    LookupResult compute(const std::string& canonicalPd,
                         const std::shared_ptr<hki::CancellationToken>& cancellation) {
        LookupResult result = computeInvariants(canonicalPd, cancellation, InvariantPipelineMode::SimplifyRetry);

        std::optional<std::string> homfly;
        std::optional<std::string> khovanov;
        if (!result.homfly.empty()) homfly = result.homfly;
        if (!result.khovanov.empty()) khovanov = result.khovanov;

        result.candidates = filterNamesByMaxCrossing(sqliteStore_.lookupInvariants(homfly, khovanov));
        if (result.candidates.empty()) {
            result.candidates = filterNamesByMaxCrossing(invariantIndex_.lookup(homfly, khovanov));
        }
        if (result.candidates.empty()) {
            result.candidates = filterNamesByMaxCrossing(sqliteStore_.lookupByCanonicalPd(result.canonicalPd));
            if (result.candidates.empty() && result.canonicalPd != canonicalPd) {
                result.candidates = filterNamesByMaxCrossing(sqliteStore_.lookupByCanonicalPd(canonicalPd));
            }
        }
        if (result.candidates.empty()) {
            result.candidates = filterNamesByMaxCrossing(pdRecords_.lookupByCanonicalPd(result.canonicalPd));
            if (result.candidates.empty() && result.canonicalPd != canonicalPd) {
                result.candidates = filterNamesByMaxCrossing(pdRecords_.lookupByCanonicalPd(canonicalPd));
            }
        }
        if (result.candidates.empty() && !sqliteStore_.hasInvariantRecords() && !invariantIndex_.loaded()) {
            result.candidateError =
                "PD_m invariant index is not built. Generate PD_m_3-16.sqlite with --build-sqlite, "
                "then run --build-pd-index.";
        }
        return result;
    }
};

class SocketRuntime {
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup failed");
#else
        std::signal(SIGPIPE, SIG_IGN);
#endif
    }

    ~SocketRuntime() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

void closeSocket(Socket socket) {
    if (socket == kInvalidSocket) return;
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

bool sendAll(Socket socket, const std::string& value) {
    const char* data = value.data();
    std::size_t remaining = value.size();
    while (remaining > 0) {
#ifdef _WIN32
        const int chunk = remaining > static_cast<std::size_t>(INT_MAX) ? INT_MAX : static_cast<int>(remaining);
        const int sent = send(socket, data, chunk, 0);
#else
        const ssize_t sent = send(socket, data, remaining, 0);
#endif
        if (sent <= 0) return false;
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

bool sendWebSocketText(Socket socket, const std::string& payload) {
    std::string frame;
    frame.reserve(payload.size() + 10);
    frame.push_back(static_cast<char>(0x81));

    const std::uint64_t size = static_cast<std::uint64_t>(payload.size());
    if (size <= 125) {
        frame.push_back(static_cast<char>(size));
    } else if (size <= 0xffff) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((size >> 8) & 0xff));
        frame.push_back(static_cast<char>(size & 0xff));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<char>((size >> shift) & 0xff));
        }
    }

    frame.append(payload);
    return sendAll(socket, frame);
}

std::optional<Request> readRequest(Socket socket) {
    std::string data;
    std::array<char, 4096> buffer{};
    std::size_t headerEnd = std::string::npos;

    while ((headerEnd = data.find("\r\n\r\n")) == std::string::npos) {
        if (data.size() > kMaxHeaderBytes) throw std::runtime_error("request headers are too large");
#ifdef _WIN32
        const int n = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        const ssize_t n = recv(socket, buffer.data(), buffer.size(), 0);
#endif
        if (n <= 0) return std::nullopt;
        data.append(buffer.data(), static_cast<std::size_t>(n));
    }
    headerEnd += 4;

    std::istringstream headers(data.substr(0, headerEnd));
    std::string requestLine;
    if (!std::getline(headers, requestLine)) return std::nullopt;
    if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();

    Request request;
    {
        std::istringstream line(requestLine);
        std::string version;
        line >> request.method >> request.target >> version;
        if (request.method.empty() || request.target.empty()) throw std::runtime_error("bad request line");
    }

    std::string line;
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        const std::size_t sep = line.find(':');
        if (sep == std::string::npos) continue;
        request.headers[lowerCopy(trim(line.substr(0, sep)))] = trim(line.substr(sep + 1));
    }

    const std::string::size_type query = request.target.find('?');
    request.path = urlDecode(request.target.substr(0, query));

    std::size_t contentLength = 0;
    const auto foundLength = request.headers.find("content-length");
    if (foundLength != request.headers.end()) {
        contentLength = static_cast<std::size_t>(std::stoul(foundLength->second));
        if (contentLength > kMaxBodyBytes) throw std::runtime_error("request body is too large");
    }

    while (data.size() < headerEnd + contentLength) {
#ifdef _WIN32
        const int n = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        const ssize_t n = recv(socket, buffer.data(), buffer.size(), 0);
#endif
        if (n <= 0) throw std::runtime_error("connection closed while reading request body");
        data.append(buffer.data(), static_cast<std::size_t>(n));
    }
    request.body = data.substr(headerEnd, contentLength);
    return request;
}

std::string mimeType(const std::filesystem::path& path) {
    const std::string ext = lowerCopy(path.extension().string());
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".js") return "text/javascript; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".map") return "application/json; charset=utf-8";
    return "application/octet-stream";
}

std::optional<std::filesystem::path> safeWebPath(const std::filesystem::path& root, const std::string& relativeUrl) {
    std::filesystem::path relative;
    std::stringstream parts(relativeUrl);
    std::string part;
    while (std::getline(parts, part, '/')) {
        part = urlDecode(part);
        if (part.empty() || part == ".") continue;
        if (part == ".." || part.find('\0') != std::string::npos) return std::nullopt;
        relative /= part;
    }
    return root / relative;
}

Response serveFile(const std::filesystem::path& root, const std::string& relativeUrl) {
    const auto path = safeWebPath(root, relativeUrl);
    if (!path.has_value() || !existsPath(*path) || isDirectory(*path)) {
        return makeText(404, "Not Found", "not found", "text/plain; charset=utf-8");
    }
    return makeText(200, "OK", readWholeFile(*path), mimeType(*path));
}

std::string decodeApiPayload(const std::string& path, const std::string& prefix) {
    if (!startsWith(path, prefix)) throw std::runtime_error("internal API routing error");
    const std::string encoded = urlDecode(path.substr(prefix.size()));
    return base64Decode(encoded);
}

std::string normalizePdInput(const std::string& raw) {
    std::string value = raw;
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c);
    }), value.end());
    return value;
}

std::vector<cki::link_pd_code::Point3> parseCoordinateRows(const std::string& raw) {
    std::string text = raw;
    for (char& c : text) {
        if (c == '[' || c == ']' || c == ';' || c == ',') c = ' ';
    }

    std::istringstream input(text);
    std::vector<double> values;
    double v = 0.0;
    while (input >> v) values.push_back(v);
    if (values.size() % 3 == 1 && values.front() >= 3.0) {
        const double count = values.front();
        const std::size_t pointCount = (values.size() - 1) / 3;
        if (std::abs(count - static_cast<double>(pointCount)) < 1e-9) {
            values.erase(values.begin());
        }
    }
    if (values.size() < 9 || values.size() % 3 != 0) {
        throw std::runtime_error("3D coordinate list must contain at least three x y z rows.");
    }

    std::vector<cki::link_pd_code::Point3> points;
    points.reserve(values.size() / 3);
    for (std::size_t i = 0; i < values.size(); i += 3) {
        points.push_back(cki::link_pd_code::Point3{values[i], values[i + 1], values[i + 2]});
    }
    return points;
}

class Application {
public:
    Application(std::filesystem::path webRoot, KnotEngine& engine, TaskManager& tasks)
        : webRoot_(std::move(webRoot)), engine_(engine), tasks_(tasks) {}

    Response handle(const Request& request) {
        const ClientSession session = clientSessionFromRequest(request);
        Response response;
        try {
            if (request.method == "GET" && request.path == "/") {
                response = serveFile(webRoot_, "index.html");
            } else if (request.method == "GET" && request.path == "/tasks.html") {
                response = serveFile(webRoot_, "tasks.html");
            } else if (request.method == "GET" && startsWith(request.path, "/static/")) {
                response = serveFile(webRoot_, request.path.substr(1));
            } else if (request.method == "GET" && startsWith(request.path, "/img/")) {
                response = serveFile(webRoot_ / "static" / "img", request.path.substr(5));
            } else if (startsWith(request.path, "/api/")) {
                response = handleApi(request, session.id);
            } else {
                response = makeText(404, "Not Found", "not found", "text/plain; charset=utf-8");
            }
        } catch (const std::exception& error) {
            response = makeText(500, "Internal Server Error", error.what(), "text/plain; charset=utf-8");
        }
        attachClientCookie(response, session);
        return response;
    }

    bool isWebSocketRequest(const Request& request) const {
        if (request.method != "GET" || request.path != "/ws/tasks") return false;
        const auto foundUpgrade = request.headers.find("upgrade");
        if (foundUpgrade == request.headers.end() || lowerCopy(foundUpgrade->second) != "websocket") return false;
        const auto foundConnection = request.headers.find("connection");
        if (foundConnection == request.headers.end()) return false;
        return lowerCopy(foundConnection->second).find("upgrade") != std::string::npos;
    }

    void handleWebSocket(const Request& request, Socket socket) {
        const ClientSession session = clientSessionFromRequest(request);
        const auto foundKey = request.headers.find("sec-websocket-key");
        if (foundKey == request.headers.end() || trim(foundKey->second).empty()) {
            sendAll(socket, makeText(400, "Bad Request", "missing WebSocket key", "text/plain; charset=utf-8").serialize());
            return;
        }

        std::ostringstream handshake;
        handshake << "HTTP/1.1 101 Switching Protocols\r\n"
                  << "Upgrade: websocket\r\n"
                  << "Connection: Upgrade\r\n"
                  << "Sec-WebSocket-Accept: " << webSocketAcceptKey(trim(foundKey->second)) << "\r\n";
        if (session.needsCookie) {
            handshake << "Set-Cookie: " << clientCookieHeaderValue(session) << "\r\n";
        }
        handshake << "\r\n";
        if (!sendAll(socket, handshake.str())) return;

        while (!hki::interrupted()) {
            if (!sendWebSocketText(socket, tasks_.toJson(session.id))) break;
            for (int i = 0; i < 10 && !hki::interrupted(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

private:
    std::filesystem::path webRoot_;
    KnotEngine& engine_;
    TaskManager& tasks_;

    Response handleApi(const Request& request, const std::string& clientId) {
        if (request.method == "GET" && request.path == "/api/tasks") {
            return makeText(200, "OK", tasks_.toJson(clientId), "application/json; charset=utf-8");
        }

        if (request.method == "POST" && startsWith(request.path, "/api/tasks/") && endsWith(request.path, "/cancel")) {
            const std::string idText = request.path.substr(std::string("/api/tasks/").size(),
                                                           request.path.size() - std::string("/api/tasks/").size() - std::string("/cancel").size());
            std::size_t used = 0;
            const auto id = static_cast<std::uint64_t>(std::stoull(idText, &used, 10));
            if (used != idText.size()) return makeJsonError("bad task id.");
            std::string message;
            return tasks_.cancel(id, message) ? makeJsonSuccess(message) : makeJsonError(message);
        }

        if (request.method == "GET" && startsWith(request.path, "/api/index_pd_code/")) {
            const std::string rawPd = decodeApiPayload(request.path, "/api/index_pd_code/");
            return runLookupTask(clientId, "PD Notation", rawPd, [&] {
                return normalizePdInput(rawPd);
            });
        }

        if (request.method == "GET" && startsWith(request.path, "/api/index_knot_name/")) {
            const std::string knotName = decodeApiPayload(request.path, "/api/index_knot_name/");
            return runLookupTask(clientId, "Knot Name", knotName, [&] {
                std::string error;
                const auto pd = engine_.lookupNamePd(knotName, error);
                if (!pd.has_value()) throw std::runtime_error(error);
                return normalizePdInput(*pd);
            });
        }

        if (request.method == "POST" && request.path == "/api/index_coord_3d") {
            const auto coordText = extractJsonString(request.body, "coord_3d");
            if (!coordText.has_value()) return makeJsonError("coord_3d JSON string is required.");
            return runLookupTask(clientId, "3D Coordinates", *coordText, [&] {
                const auto points = parseCoordinateRows(*coordText);
                const cki::link_pd_code::PDCode pd = cki::link_pd_code::computePDCode(points);
                return normalizePdInput(cki::link_pd_code::formatPDCode(pd));
            });
        }

        if (request.method == "GET" && startsWith(request.path, "/api/knot_name2pd_code/")) {
            const std::string knotName = decodeApiPayload(request.path, "/api/knot_name2pd_code/");
            std::string error;
            const auto pd = engine_.lookupNamePd(knotName, error);
            return pd.has_value() ? makeJsonSuccess(*pd) : makeJsonError(error);
        }

        if (request.method == "GET" && startsWith(request.path, "/api/pd_code2homflypt/")) {
            const std::string pd = normalizePdInput(decodeApiPayload(request.path, "/api/pd_code2homflypt/"));
            const LookupResult result = engine_.lookup(pd);
            if (!result.homflyWorker.success) return makeJsonError(workerFailureMessage("HOMFLY-PT", result.homflyWorker));
            return makeJsonSuccess(result.homfly);
        }

        if (request.method == "GET" && startsWith(request.path, "/api/pd_code2khovanov/")) {
            const std::string pd = normalizePdInput(decodeApiPayload(request.path, "/api/pd_code2khovanov/"));
            const LookupResult result = engine_.lookup(pd);
            if (!result.khovanovWorker.success) return makeJsonError(workerFailureMessage("Khovanov", result.khovanovWorker));
            return makeJsonSuccess(result.khovanov);
        }

        if (request.method == "GET" && startsWith(request.path, "/api/pd_code2knot_name/")) {
            const std::string pd = normalizePdInput(decodeApiPayload(request.path, "/api/pd_code2knot_name/"));
            const LookupResult result = engine_.lookup(pd);
            if (!result.candidateError.empty()) return makeJsonError(result.candidateError);
            return makeJsonSuccess(join(result.candidates, "; "));
        }

        if (request.method == "POST" && request.path == "/api/coord_3d2pd_code") {
            const auto coordText = extractJsonString(request.body, "coord_3d");
            if (!coordText.has_value()) return makeJsonError("coord_3d JSON string is required.");
            const auto points = parseCoordinateRows(*coordText);
            const cki::link_pd_code::PDCode pd = cki::link_pd_code::computePDCode(points);
            return makeJsonSuccess(cki::link_pd_code::formatPDCode(pd));
        }

        if (request.method == "GET" && request.path == "/api/last_build_info") {
            std::ostringstream info;
            info << "Pure C++ knot-indexer-lab server\n"
                 << "Invariant timeout: " << kMaxComputeTimeoutSeconds << " seconds\n"
                 << "Maximum total crossing number: " << engine_.maxCrossing() << "\n"
                 << "PD_m data: " << engine_.nameIndexStatus() << "\n";
            return makeText(200, "OK", info.str(), "text/plain; charset=utf-8");
        }

        return makeText(404, "Not Found", "not found", "text/plain; charset=utf-8");
    }

    template <typename ResolvePd>
    Response runLookupTask(const std::string& clientId, const std::string& inputType, const std::string& input, ResolvePd resolvePd) {
        auto task = tasks_.create(clientId, inputType, input);
        try {
            const std::string pd = resolvePd();
            const PdDiagram diagram = buildPdDiagram(pd);
            tasks_.setCanonicalPd(task, pd, diagram.svg, diagram.error);
            const LookupResult result = engine_.lookup(pd, task->cancellation);
            tasks_.complete(task, result);
            return makeLookupJson(task, result);
        } catch (const std::exception& error) {
            tasks_.fail(task, error.what());
            return makeTaskFailureJson(task, error.what());
        }
    }

    Response makeLookupJson(const TaskManager::TaskPtr& task, const LookupResult& result) const {
        const bool cancelled = result.homflyWorker.cancelled || result.khovanovWorker.cancelled;
        const bool useful = result.homflyWorker.success || result.khovanovWorker.success || !result.candidates.empty();
        const std::string status = (!cancelled && useful) ? "success" : "error";
        std::string message;
        if (cancelled) {
            message = "task was cancelled.";
        } else if (!useful) {
            message = "no invariant computation completed successfully.";
        } else if (!result.candidateError.empty()) {
            message = result.candidateError;
        }

        std::ostringstream out;
        out << "{"
            << "\"status\":\"" << status << "\","
            << "\"message\":\"" << jsonEscape(message) << "\","
            << "\"task_id\":" << (task ? task->id : 0) << ","
            << "\"pd_code\":\"" << jsonEscape(result.canonicalPd) << "\","
            << "\"pd_diagram_svg\":\"" << jsonEscape(result.pdDiagramSvg) << "\","
            << "\"pd_diagram_error\":\"" << jsonEscape(result.pdDiagramError) << "\","
            << "\"knot_name\":\"" << jsonEscape(join(result.candidates, "; ")) << "\","
            << "\"homfly_status\":\"" << jsonEscape(workerStatusText(result.homflyWorker)) << "\","
            << "\"homflypt_polynomial\":\"" << jsonEscape(result.homfly) << "\","
            << "\"homfly_error\":\"" << jsonEscape(result.homflyWorker.success ? "" : workerFailureMessage("HOMFLY-PT", result.homflyWorker)) << "\","
            << "\"khovanov_status\":\"" << jsonEscape(workerStatusText(result.khovanovWorker)) << "\","
            << "\"khovanov_homology\":\"" << jsonEscape(result.khovanov) << "\","
            << "\"khovanov_error\":\"" << jsonEscape(result.khovanovWorker.success ? "" : workerFailureMessage("Khovanov", result.khovanovWorker)) << "\""
            << "}";
        return makeText(200, "OK", out.str(), "application/json; charset=utf-8");
    }

    Response makeTaskFailureJson(const TaskManager::TaskPtr& task, const std::string& error) const {
        std::ostringstream out;
        out << "{"
            << "\"status\":\"error\","
            << "\"message\":\"" << jsonEscape(error) << "\","
            << "\"task_id\":" << (task ? task->id : 0) << ","
            << "\"pd_code\":\"" << jsonEscape(task ? task->canonicalPd : "") << "\","
            << "\"pd_diagram_svg\":\"" << jsonEscape(task ? task->pdDiagramSvg : "") << "\","
            << "\"pd_diagram_error\":\"" << jsonEscape(task ? task->pdDiagramError : "") << "\""
            << "}";
        return makeText(200, "OK", out.str(), "application/json; charset=utf-8");
    }
};

class HttpServer {
public:
    HttpServer(std::string host, int port, Application& app)
        : host_(std::move(host)), port_(port), app_(app) {}

    void run() {
        SocketRuntime runtime;
        Socket server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server == kInvalidSocket) throw std::runtime_error("cannot create server socket");

        int yes = 1;
#ifdef _WIN32
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<unsigned short>(port_));
        if (inet_pton(AF_INET, host_.c_str(), &address.sin_addr) != 1) {
            closeSocket(server);
            throw std::runtime_error("invalid IPv4 host: " + host_);
        }

        if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            closeSocket(server);
            throw std::runtime_error("cannot bind to " + host_ + ":" + std::to_string(port_));
        }
        if (listen(server, 64) != 0) {
            closeSocket(server);
            throw std::runtime_error("cannot listen on server socket");
        }

        const std::string displayHost = host_ == "0.0.0.0" ? "127.0.0.1" : host_;
        std::cerr << "knot-indexer-lab listening on http://" << displayHost << ":" << port_ << "\n";
        while (!hki::interrupted()) {
            sockaddr_in clientAddress{};
#ifdef _WIN32
            int clientLength = sizeof(clientAddress);
#else
            socklen_t clientLength = sizeof(clientAddress);
#endif
            Socket client = accept(server, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
            if (client == kInvalidSocket) {
                if (hki::interrupted()) break;
                continue;
            }
            std::thread(&HttpServer::handleClient, this, client).detach();
        }
        closeSocket(server);
    }

private:
    std::string host_;
    int port_ = kDefaultPort;
    Application& app_;

    void handleClient(Socket client) {
        try {
            const auto request = readRequest(client);
            if (request.has_value()) {
                if (app_.isWebSocketRequest(*request)) {
                    app_.handleWebSocket(*request, client);
                } else {
                    const Response response = app_.handle(*request);
                    sendAll(client, response.serialize());
                }
            }
        } catch (const std::exception& error) {
            const Response response = makeText(400, "Bad Request", error.what(), "text/plain; charset=utf-8");
            sendAll(client, response.serialize());
        }
        closeSocket(client);
    }
};

void usage(std::ostream& out) {
    out << "Usage: knot_indexer_lab_server [OPTIONS]\n\n"
        << "Options:\n"
        << "  --host ADDRESS       IPv4 address to bind. Default: 0.0.0.0\n"
        << "  --port PORT          TCP port. Default: 5000\n"
        << "  --data-folder PATH   Folder containing name-pd/PD_m_3-16.sorted.txt and knotname-reg/.\n"
        << "  --web-root PATH      Folder containing index.html and static/.\n"
        << "  --timeout SEC        Worker timeout, capped at 1200 seconds. Default: 1200\n"
        << "  --build-sqlite       Import PD_m_3-16.sorted.txt into PD_m_3-16.sqlite, then exit.\n"
        << "  --build-pd-index     Generate invariant records in SQLite or TSV fallback, then exit.\n"
        << "  --max-crossing N     Maximum total crossing number. Default: 14, max: 16\n"
        << "  --index-limit N      Limit newly imported or indexed records in build modes.\n"
        << "  --index-workers N    Parallel PD_m invariant build workers. Default: half of CPU cores.\n"
        << "  --index-batch-size N SQLite invariant rows per write transaction. Default: 256\n"
        << "  --index-progress-seconds N  Progress/ETA refresh interval. Default: 5\n"
        << "  --help, -h           Show this help text.\n";
}

int parsePositiveInt(const std::string& value, const std::string& name) {
    std::size_t used = 0;
    int parsed = 0;
    try {
        parsed = std::stoi(value, &used, 10);
    } catch (const std::exception&) {
        throw std::runtime_error(name + " must be an integer");
    }
    if (used != value.size() || parsed <= 0) throw std::runtime_error(name + " must be positive");
    return parsed;
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
        } else if (arg == "--host") {
            options.host = needValue("--host").text;
        } else if (arg == "--port") {
            options.port = parsePositiveInt(needValue("--port").text, "--port");
        } else if (arg == "--data-folder") {
            options.dataFolder = needValue("--data-folder").path;
        } else if (arg == "--web-root") {
            options.webRoot = needValue("--web-root").path;
        } else if (arg == "--timeout") {
            options.timeoutSeconds = parsePositiveInt(needValue("--timeout").text, "--timeout");
            if (options.timeoutSeconds > kMaxComputeTimeoutSeconds) {
                throw std::runtime_error("--timeout must not exceed 1200 seconds");
            }
        } else if (arg == "--build-sqlite") {
            options.buildSqlite = true;
        } else if (arg == "--build-pd-index") {
            options.buildPdIndex = true;
        } else if (arg == "--max-crossing") {
            options.maxCrossing = parsePositiveInt(needValue("--max-crossing").text, "--max-crossing");
            if (options.maxCrossing > kHardMaxCrossing) {
                throw std::runtime_error("--max-crossing must not exceed 16");
            }
        } else if (arg == "--index-limit") {
            options.indexLimit = static_cast<std::size_t>(parsePositiveInt(needValue("--index-limit").text, "--index-limit"));
        } else if (arg == "--index-workers") {
            options.indexWorkers = static_cast<std::size_t>(parsePositiveInt(needValue("--index-workers").text, "--index-workers"));
        } else if (arg == "--index-batch-size") {
            options.indexBatchSize = static_cast<std::size_t>(parsePositiveInt(needValue("--index-batch-size").text, "--index-batch-size"));
        } else if (arg == "--index-progress-seconds") {
            options.indexProgressSeconds = parsePositiveInt(needValue("--index-progress-seconds").text, "--index-progress-seconds");
        } else if (arg == "--worker") {
            return options;
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }
    return options;
}

}  // namespace
}  // namespace lab

int main(int argc, char** argv) {
    try {
        const std::vector<cki::platform::ProgramArg> args = cki::platform::programArguments(argc, argv);
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i].text == "--worker") {
                hki::installInterruptHandlers();
                return lab::workerMain(args);
            }
        }

        hki::installInterruptHandlers();
        const lab::Options options = lab::parseOptions(args);
        const std::filesystem::path executable = lab::currentExecutablePath(args.empty() ? std::filesystem::path() : args[0].path);
        const lab::DataPaths dataPaths = lab::resolveDataFolder(executable, options.dataFolder);
        lab::KnotEngine engine(executable, dataPaths, options.timeoutSeconds, options.maxCrossing, !options.buildSqlite);
        if (options.buildSqlite) {
            engine.buildSqliteNameDatabase(options.indexLimit);
        }
        if (options.buildPdIndex) {
            engine.buildPdInvariantIndex(options.indexLimit,
                                         options.indexWorkers,
                                         options.indexBatchSize,
                                         options.indexProgressSeconds);
            return 0;
        }
        if (options.buildSqlite) return 0;

        const std::filesystem::path webRoot = lab::resolveWebRoot(executable, options.webRoot);
        lab::TaskManager tasks;
        lab::Application app(webRoot, engine, tasks);
        lab::HttpServer server(options.host, options.port, app);
        server.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << "\n";
        return 1;
    }
}
