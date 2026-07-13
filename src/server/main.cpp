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

#include "database.hpp"
#include "homfly_backend.hpp"
#include "khovanov_backend.hpp"
#include "link_pd_code.hpp"
#include "name_pd_lookup.hpp"
#include "pd_code.hpp"
#include "pd_simplify_backend.hpp"
#include "path_utils.hpp"
#include "process_runner.hpp"
#include "resource_control.hpp"
#include "runtime_control.hpp"

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
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <list>
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
constexpr int kMaxClientConnections = 128;

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
    int maxCrossing = kDefaultMaxCrossing;
    std::uint64_t workerMemoryMb = 0;
    std::uint64_t memoryReserveMb = 0;
    std::uint64_t cacheMemoryMb = 64;
    std::filesystem::path dataFolder;
    std::filesystem::path webRoot;
    std::filesystem::path taskHistory;
    bool renderPdSvg = false;
    bool svgInputFromFile = false;
    bool svgInputInline = false;
    bool svgOutputToFile = false;
    std::filesystem::path svgInputPath;
    std::filesystem::path svgOutputPath;
    std::string svgInlinePd;
};

struct DataPaths {
    std::filesystem::path root;
    std::filesystem::path homflyDb;
    std::filesystem::path khovanovDb;
    std::filesystem::path knotNameRegDir;
    std::filesystem::path primePdDb;
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
    const std::filesystem::path homflyDb = folder / "homfly" / "sorted_HOMFLY-PT.txt";
    const std::filesystem::path khovanovDb = folder / "khovanov" / "sorted_khovanov.txt";
    const std::filesystem::path knotNameRegDir = folder / "knotname-reg";
    const std::filesystem::path primePdDb = folder / "name-pd" / "prime_knots_3-11.txt";
    if (!existsPath(homflyDb) || !existsPath(khovanovDb) || !isDirectory(knotNameRegDir) ||
        !existsPath(primePdDb)) {
        return std::nullopt;
    }
    return DataPaths{folder, homflyDb, khovanovDb, knotNameRegDir, primePdDb};
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
    throw std::runtime_error(
        "cannot locate data folder; pass --data-folder. The folder must contain "
        "homfly/sorted_HOMFLY-PT.txt, khovanov/sorted_khovanov.txt, and knotname-reg/.");
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
    } else if (result.resourceExhausted) {
        out << "stopped because the memory limit was reached";
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
    if (result.resourceExhausted) return "resource_exhausted";
    if (result.exitCode != -1) return "failed";
    return "pending";
}

class CancellationToken {
public:
    void cancel() {
        cancelled_.store(true);
    }

    bool cancelled() const {
        return cancelled_.load();
    }

private:
    std::atomic_bool cancelled_{false};
};

struct TaskRecord {
    std::uint64_t id = 0;
    std::string clientId;
    std::string status = "queued";
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
    std::shared_ptr<CancellationToken> cancellation;
};

class TaskHistoryStore {
public:
    struct Page {
        std::vector<std::string> records;
        std::uint64_t nextCursor = 0;
        bool hasMore = false;
    };

    explicit TaskHistoryStore(std::filesystem::path path) : path_(std::move(path)) {
        try {
            if (!path_.parent_path().empty()) std::filesystem::create_directories(path_.parent_path());
            repairAndFindMaximumId();
        } catch (const std::exception& error) {
            enabled_ = false;
            std::cerr << "warning: task history disabled: " << error.what() << "\n";
        }
    }

    std::uint64_t maximumId() const { return maximumId_; }
    const std::filesystem::path& path() const { return path_; }

    bool append(std::uint64_t id, const std::string& clientId, const std::string& json) {
        if (!enabled_) return false;
        try {
            const std::string payload = std::to_string(id) + "\t" + clientId + "\n" + json;
            const std::uint64_t length = static_cast<std::uint64_t>(payload.size());
            if (length > kMaximumRecordBytes) throw std::runtime_error("task history record exceeds 16 MiB");
            std::ofstream output(path_, std::ios::binary | std::ios::app);
            if (!output) throw std::runtime_error("cannot open " + path_.string());
            writeNumber(output, length);
            output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            writeNumber(output, length);
            output.flush();
            if (!output) throw std::runtime_error("cannot append " + path_.string());
            maximumId_ = std::max(maximumId_, id);
            writeLatestClient(clientId, json);
            return true;
        } catch (const std::exception& error) {
            std::cerr << "warning: could not persist task history: " << error.what() << "\n";
            return false;
        }
    }

    Page newest(std::uint64_t beforeCursor, std::size_t limit) const {
        Page page;
        if (!enabled_ || limit == 0 || !existsPath(path_)) return page;
        std::ifstream input(path_, std::ios::binary);
        if (!input) return page;
        input.seekg(0, std::ios::end);
        const std::uint64_t size = static_cast<std::uint64_t>(input.tellg());
        std::uint64_t cursor = beforeCursor == 0 || beforeCursor > size ? size : beforeCursor;
        std::uint64_t pageBytes = 0;
        while (cursor >= frameOverhead() && page.records.size() < limit) {
            std::uint64_t length = 0;
            if (!readNumberAt(input, cursor - sizeof(length), length) ||
                length > kMaximumRecordBytes || length + frameOverhead() > cursor) break;
            const std::uint64_t start = cursor - frameOverhead() - length;
            if (pageBytes + length > kMaximumPageBytes && !page.records.empty()) break;
            std::uint64_t headerLength = 0;
            if (!readNumberAt(input, start, headerLength) || headerLength != length) break;
            std::string payload(static_cast<std::size_t>(length), '\0');
            input.clear();
            input.seekg(static_cast<std::streamoff>(start + sizeof(length)), std::ios::beg);
            input.read(payload.data(), static_cast<std::streamsize>(payload.size()));
            if (!input) break;
            const std::size_t newline = payload.find('\n');
            if (newline == std::string::npos) break;
            page.records.push_back(payload.substr(newline + 1));
            pageBytes += length;
            cursor = start;
        }
        page.nextCursor = cursor;
        page.hasMore = cursor >= frameOverhead();
        return page;
    }

    std::string latestForClient(const std::string& clientId) const {
        if (!enabled_ || clientId.empty()) return "";
        const std::filesystem::path latestPath = latestDirectory() / (clientFileName(clientId) + ".json");
        std::ifstream input(latestPath, std::ios::binary);
        if (!input) return "";
        std::string storedClient;
        if (!std::getline(input, storedClient) || storedClient != clientId) return "";
        std::ostringstream json;
        json << input.rdbuf();
        return json.str();
    }

private:
    static constexpr std::uint64_t kMaximumRecordBytes = 16ULL * 1024ULL * 1024ULL;
    static constexpr std::uint64_t kMaximumPageBytes = 16ULL * 1024ULL * 1024ULL;
    std::filesystem::path path_;
    bool enabled_ = true;
    std::uint64_t maximumId_ = 0;

    static constexpr std::uint64_t frameOverhead() { return 2 * sizeof(std::uint64_t); }

    static void writeNumber(std::ostream& output, std::uint64_t value) {
        output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    static bool readNumberAt(std::istream& input, std::uint64_t offset, std::uint64_t& value) {
        input.clear();
        input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        input.read(reinterpret_cast<char*>(&value), sizeof(value));
        return static_cast<bool>(input);
    }

    static std::uint64_t parseRecordId(const std::string& payload) {
        const std::size_t tab = payload.find('\t');
        if (tab == std::string::npos) return 0;
        try {
            return static_cast<std::uint64_t>(std::stoull(payload.substr(0, tab)));
        } catch (const std::exception&) {
            return 0;
        }
    }

    void repairAndFindMaximumId() {
        if (!existsPath(path_)) return;
        std::ifstream input(path_, std::ios::binary);
        if (!input) throw std::runtime_error("cannot read " + path_.string());
        input.seekg(0, std::ios::end);
        const std::uint64_t size = static_cast<std::uint64_t>(input.tellg());
        std::uint64_t cursor = 0;
        std::uint64_t validEnd = 0;
        while (cursor + frameOverhead() <= size) {
            std::uint64_t length = 0;
            if (!readNumberAt(input, cursor, length) || length > kMaximumRecordBytes ||
                cursor + frameOverhead() + length > size) break;
            std::string payload(static_cast<std::size_t>(length), '\0');
            input.read(payload.data(), static_cast<std::streamsize>(payload.size()));
            std::uint64_t footer = 0;
            input.read(reinterpret_cast<char*>(&footer), sizeof(footer));
            if (!input || footer != length || payload.find('\n') == std::string::npos) break;
            maximumId_ = std::max(maximumId_, parseRecordId(payload));
            cursor += frameOverhead() + length;
            validEnd = cursor;
        }
        input.close();
        if (validEnd != size) {
            std::filesystem::resize_file(path_, validEnd);
            std::cerr << "warning: truncated incomplete task history tail in " << path_.string() << "\n";
        }
    }

    std::filesystem::path latestDirectory() const {
        return path_.parent_path() / (path_.filename().string() + ".clients");
    }

    static std::string clientFileName(const std::string& clientId) {
        std::uint64_t hash = 1469598103934665603ULL;
        for (unsigned char c : clientId) {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
        std::ostringstream out;
        out << std::hex << std::setw(16) << std::setfill('0') << hash;
        return out.str();
    }

    void writeLatestClient(const std::string& clientId, const std::string& json) const {
        if (clientId.empty()) return;
        const std::filesystem::path directory = latestDirectory();
        std::filesystem::create_directories(directory);
        const std::filesystem::path target = directory / (clientFileName(clientId) + ".json");
        const std::filesystem::path temporary = target.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("cannot write latest client task");
            output << clientId << "\n" << json;
        }
        std::error_code ec;
        std::filesystem::remove(target, ec);
        ec.clear();
        std::filesystem::rename(temporary, target, ec);
        if (ec) throw std::runtime_error("cannot publish latest client task: " + ec.message());
    }
};

class TaskManager {
public:
    using TaskPtr = std::shared_ptr<TaskRecord>;

    explicit TaskManager(std::filesystem::path historyPath)
        : history_(std::move(historyPath)), nextId_(history_.maximumId() + 1) {}

    TaskPtr create(std::string clientId, std::string inputType, std::string input) {
        constexpr std::size_t maximumStoredInputBytes = 256 * 1024;
        auto task = std::make_shared<TaskRecord>();
        task->id = nextId_.fetch_add(1, std::memory_order_relaxed);
        task->clientId = std::move(clientId);
        task->inputType = std::move(inputType);
        if (input.size() > maximumStoredInputBytes) {
            task->input = input.substr(0, maximumStoredInputBytes) + "\n...[input truncated by task monitor]";
        } else {
            task->input = std::move(input);
        }
        task->startedAt = localTimestamp();
        task->cancellation = std::make_shared<CancellationToken>();
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
            if (task->status != "running" && task->status != "queued") {
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

    void markRunning(const TaskPtr& task) {
        if (!task) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (task->status == "queued") task->status = "running";
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
        } else if (!result.homflyWorker.success && !result.khovanovWorker.success && result.candidates.empty()) {
            task->status = "failed";
        } else {
            task->status = "completed";
        }
        persistAndRemoveLocked(task);
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
        persistAndRemoveLocked(task);
    }

    void failResourceExhausted(const TaskPtr& task, const std::string& error) {
        if (!task) return;
        std::lock_guard<std::mutex> lock(mutex_);
        task->endedAt = localTimestamp();
        task->error = error;
        task->status = "failed";
        task->homflyStatus = "resource_exhausted";
        task->khovanovStatus = "resource_exhausted";
        task->homflyError = error;
        task->khovanovError = error;
        persistAndRemoveLocked(task);
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
        if (latest) {
            appendTaskJson(out, *latest);
        } else {
            const std::string persisted = history_.latestForClient(clientId);
            if (persisted.empty()) out << "null";
            else out << persisted;
        }
        out << "}";
        return out.str();
    }

    std::string historyJson(std::uint64_t cursor, std::size_t limit) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const TaskHistoryStore::Page page = history_.newest(cursor, std::min<std::size_t>(limit, 200));
        std::ostringstream out;
        out << "{\"status\":\"success\",\"tasks\":[";
        for (std::size_t i = 0; i < page.records.size(); ++i) {
            if (i) out << ",";
            out << page.records[i];
        }
        out << "],\"next_cursor\":" << page.nextCursor
            << ",\"has_more\":" << (page.hasMore ? "true" : "false") << "}";
        return out.str();
    }

    std::string historyStatus() const {
        return cki::platform::displayPath(history_.path());
    }

private:
    TaskHistoryStore history_;
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

    static std::string taskJson(const TaskRecord& task) {
        std::ostringstream out;
        appendTaskJson(out, task);
        return out.str();
    }

    void persistAndRemoveLocked(const TaskPtr& task) {
        history_.append(task->id, task->clientId, taskJson(*task));
        tasks_.erase(std::remove(tasks_.begin(), tasks_.end(), task), tasks_.end());
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

struct SvgPoint {
    double x = 0.0;
    double y = 0.0;
};

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

std::vector<int> diagramMaskDirections(int mask) {
    std::vector<int> dirs;
    for (int bit : {kDiagramTop, kDiagramRight, kDiagramBottom, kDiagramLeft}) {
        if (mask & bit) dirs.push_back(bit);
    }
    return dirs;
}

int diagramOppositeDirection(int direction) {
    switch (direction) {
        case kDiagramTop: return kDiagramBottom;
        case kDiagramRight: return kDiagramLeft;
        case kDiagramBottom: return kDiagramTop;
        case kDiagramLeft: return kDiagramRight;
        default: return 0;
    }
}

int diagramDirectionIndex(int direction) {
    switch (direction) {
        case kDiagramTop: return 0;
        case kDiagramRight: return 1;
        case kDiagramBottom: return 2;
        case kDiagramLeft: return 3;
        default: return -1;
    }
}

std::pair<int, int> diagramStepCell(int row, int col, int direction) {
    switch (direction) {
        case kDiagramTop: return {row - 1, col};
        case kDiagramRight: return {row, col + 1};
        case kDiagramBottom: return {row + 1, col};
        case kDiagramLeft: return {row, col - 1};
        default: return {row, col};
    }
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

SvgPoint diagramPortPoint(double x, double y, double tile, int direction) {
    const double midX = x + tile / 2.0;
    const double midY = y + tile / 2.0;
    switch (direction) {
        case kDiagramTop: return {midX, y};
        case kDiagramRight: return {x + tile, midY};
        case kDiagramBottom: return {midX, y + tile};
        case kDiagramLeft: return {x, midY};
        default: return {midX, midY};
    }
}

int diagramArcSweepFlag(int entryDirection, int exitDirection) {
    const int entryIndex = diagramDirectionIndex(entryDirection);
    const int exitIndex = diagramDirectionIndex(exitDirection);
    if (entryIndex < 0 || exitIndex < 0) return 0;
    return exitIndex == (entryIndex + 1) % 4 ? 0 : 1;
}

void appendSvgCornerArc(std::ostringstream& svg,
                        double x,
                        double y,
                        double tile,
                        int entryDirection,
                        int exitDirection) {
    const SvgPoint start = diagramPortPoint(x, y, tile, entryDirection);
    const SvgPoint end = diagramPortPoint(x, y, tile, exitDirection);
    const double radius = tile / 2.0;
    const int sweep = diagramArcSweepFlag(entryDirection, exitDirection);
    svg << "<path class=\"strand\" d=\"M " << svgNumber(start.x) << " " << svgNumber(start.y)
        << " A " << svgNumber(radius) << " " << svgNumber(radius)
        << " 0 0 " << sweep
        << " " << svgNumber(end.x) << " " << svgNumber(end.y) << "\"/>\n";
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
            appendSvgCornerArc(svg, x, y, tile, kDiagramTop, kDiagramRight);
            return;
        case kDiagramRight | kDiagramBottom:
            appendSvgCornerArc(svg, x, y, tile, kDiagramRight, kDiagramBottom);
            return;
        case kDiagramBottom | kDiagramLeft:
            appendSvgCornerArc(svg, x, y, tile, kDiagramBottom, kDiagramLeft);
            return;
        case kDiagramLeft | kDiagramTop:
            appendSvgCornerArc(svg, x, y, tile, kDiagramLeft, kDiagramTop);
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

bool isTraceableDiagramCell(const IntMatrix& matrix, int row, int col) {
    if (matrix.getPos(row, col) <= 0) return false;
    const int mask = diagramLineMask(matrix, row, col);
    return diagramBitCount4(mask) == 2 &&
           (isDiagramStraightMask(mask) || isDiagramCornerMask(mask));
}

bool sameTraceableDiagramArcNeighbor(const IntMatrix& matrix,
                                     int row,
                                     int col,
                                     int direction) {
    const int value = matrix.getPos(row, col);
    if (value <= 0) return false;
    const auto next = diagramStepCell(row, col, direction);
    if (matrix.getPos(next.first, next.second) != value) return false;
    return isTraceableDiagramCell(matrix, next.first, next.second);
}

int otherTraceDirection(const IntMatrix& matrix, int row, int col, int entryDirection) {
    const std::vector<int> dirs = diagramMaskDirections(diagramLineMask(matrix, row, col));
    if (dirs.size() != 2) return 0;
    if (dirs[0] == entryDirection) return dirs[1];
    if (dirs[1] == entryDirection) return dirs[0];
    return 0;
}

struct DiagramTraceCursor {
    int row = 0;
    int col = 0;
    int entryDirection = 0;
};

DiagramTraceCursor rewindDiagramTraceStart(const IntMatrix& matrix,
                                           int row,
                                           int col,
                                           int entryDirection) {
    DiagramTraceCursor cursor{row, col, entryDirection};
    const int guardLimit = std::max(4, matrix.getRowCnt() * matrix.getColCnt() + 4);
    std::set<std::tuple<int, int, int>> seen;

    for (int guard = 0; guard < guardLimit; ++guard) {
        const auto state = std::make_tuple(cursor.row, cursor.col, cursor.entryDirection);
        if (!seen.insert(state).second) break;
        if (!sameTraceableDiagramArcNeighbor(matrix, cursor.row, cursor.col, cursor.entryDirection)) break;

        const auto prev = diagramStepCell(cursor.row, cursor.col, cursor.entryDirection);
        const int connectedSide = diagramOppositeDirection(cursor.entryDirection);
        const int previousEntry = otherTraceDirection(matrix, prev.first, prev.second, connectedSide);
        if (previousEntry == 0) break;
        cursor = DiagramTraceCursor{prev.first, prev.second, previousEntry};
    }

    return cursor;
}

void appendSvgTraceSegment(std::ostringstream& path,
                           double x,
                           double y,
                           double tile,
                           int entryDirection,
                           int exitDirection) {
    const SvgPoint end = diagramPortPoint(x, y, tile, exitDirection);
    if ((entryDirection == kDiagramTop && exitDirection == kDiagramBottom) ||
        (entryDirection == kDiagramBottom && exitDirection == kDiagramTop) ||
        (entryDirection == kDiagramLeft && exitDirection == kDiagramRight) ||
        (entryDirection == kDiagramRight && exitDirection == kDiagramLeft)) {
        path << " L " << svgNumber(end.x) << " " << svgNumber(end.y);
        return;
    }

    const double radius = tile / 2.0;
    const int sweep = diagramArcSweepFlag(entryDirection, exitDirection);
    path << " A " << svgNumber(radius) << " " << svgNumber(radius)
         << " 0 0 " << sweep
         << " " << svgNumber(end.x) << " " << svgNumber(end.y);
}

void appendSvgRegularTracedPaths(std::ostringstream& svg,
                                 const IntMatrix& matrix,
                                 const DiagramMatrixBounds& bounds,
                                 double padding,
                                 double tile,
                                 std::set<std::pair<int, int>>& tracedCells) {
    for (int row = bounds.minRow; row <= bounds.maxRow; ++row) {
        for (int col = bounds.minCol; col <= bounds.maxCol; ++col) {
            if (!isTraceableDiagramCell(matrix, row, col)) continue;
            if (tracedCells.count({row, col})) continue;

            const std::vector<int> dirs = diagramMaskDirections(diagramLineMask(matrix, row, col));
            if (dirs.size() != 2) continue;
            DiagramTraceCursor cursor = rewindDiagramTraceStart(matrix, row, col, dirs[0]);
            if (tracedCells.count({cursor.row, cursor.col})) {
                cursor = rewindDiagramTraceStart(matrix, row, col, dirs[1]);
            }
            if (tracedCells.count({cursor.row, cursor.col})) continue;

            const double startX = padding + static_cast<double>(cursor.col - bounds.minCol) * tile;
            const double startY = padding + static_cast<double>(cursor.row - bounds.minRow) * tile;
            const SvgPoint start = diagramPortPoint(startX, startY, tile, cursor.entryDirection);

            std::ostringstream path;
            path << "M " << svgNumber(start.x) << " " << svgNumber(start.y);
            bool wroteSegment = false;

            const int guardLimit = std::max(4, matrix.getRowCnt() * matrix.getColCnt() + 4);
            for (int guard = 0; guard < guardLimit; ++guard) {
                if (!isTraceableDiagramCell(matrix, cursor.row, cursor.col)) break;
                if (tracedCells.count({cursor.row, cursor.col})) break;
                tracedCells.insert({cursor.row, cursor.col});

                const int exitDirection = otherTraceDirection(matrix, cursor.row, cursor.col, cursor.entryDirection);
                if (exitDirection == 0) break;

                const double x = padding + static_cast<double>(cursor.col - bounds.minCol) * tile;
                const double y = padding + static_cast<double>(cursor.row - bounds.minRow) * tile;
                appendSvgTraceSegment(path, x, y, tile, cursor.entryDirection, exitDirection);
                wroteSegment = true;

                if (!sameTraceableDiagramArcNeighbor(matrix, cursor.row, cursor.col, exitDirection)) break;
                const auto next = diagramStepCell(cursor.row, cursor.col, exitDirection);
                if (tracedCells.count({next.first, next.second})) break;
                cursor = DiagramTraceCursor{
                    next.first,
                    next.second,
                    diagramOppositeDirection(exitDirection),
                };
            }

            if (wroteSegment) {
                svg << "<path class=\"strand\" d=\"" << path.str() << "\"/>\n";
            }
        }
    }
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

void appendSvgArcLabel(std::ostringstream& svg,
                       int value,
                       double x,
                       double y,
                       const char* anchor) {
    if (value <= 0) return;
    svg << "<text class=\"arc-label\" x=\"" << svgNumber(x)
        << "\" y=\"" << svgNumber(y)
        << "\" text-anchor=\"" << anchor << "\">"
        << value << "</text>\n";
}

void appendSvgCrossingLabels(std::ostringstream& svg,
                             const IntMatrix& matrix,
                             int row,
                             int col,
                             double x,
                             double y,
                             double tile) {
    constexpr double margin = 3.0;
    constexpr double fontBaseline = 8.0;

    appendSvgArcLabel(svg,
                      matrix.getPos(row - 1, col),
                      x + tile - margin,
                      y - margin,
                      "end");
    appendSvgArcLabel(svg,
                      matrix.getPos(row, col + 1),
                      x + tile + margin,
                      y + margin + fontBaseline,
                      "start");
    appendSvgArcLabel(svg,
                      matrix.getPos(row + 1, col),
                      x + margin,
                      y + tile + margin + fontBaseline,
                      "start");
    appendSvgArcLabel(svg,
                      matrix.getPos(row, col - 1),
                      x - margin,
                      y + tile - margin,
                      "end");
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
        << ".arc-label{font-family:Arial,DejaVu Sans,sans-serif;font-size:9px;"
        << "font-weight:700;fill:#dc2626;stroke:white;stroke-width:3px;"
        << "paint-order:stroke fill;stroke-linejoin:round}"
        << "</style>\n";

    std::set<std::pair<int, int>> tracedCells;
    appendSvgRegularTracedPaths(svg, matrix, bounds, padding, tile, tracedCells);

    for (int row = bounds.minRow; row <= bounds.maxRow; ++row) {
        for (int col = bounds.minCol; col <= bounds.maxCol; ++col) {
            const int value = matrix.getPos(row, col);
            if (value <= 0) continue;
            if (tracedCells.count({row, col})) continue;
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

    for (int row = bounds.minRow; row <= bounds.maxRow; ++row) {
        for (int col = bounds.minCol; col <= bounds.maxCol; ++col) {
            const int value = matrix.getPos(row, col);
            if (value != -1 && value != -2) continue;
            const double x = padding + static_cast<double>(col - bounds.minCol) * tile;
            const double y = padding + static_cast<double>(row - bounds.minRow) * tile;
            appendSvgCrossingLabels(svg, matrix, row, col, x, y, tile);
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

class KnotEngine {
public:
    KnotEngine(std::filesystem::path executable,
               DataPaths dataPaths,
               int timeoutSeconds,
               int maxCrossing,
               const AdaptiveMemoryController& memory,
               ComputeQueue& computeQueue,
               std::size_t cacheMemoryBytes)
        : executable_(std::move(executable)),
          dataPaths_(std::move(dataPaths)),
          timeoutSeconds_(timeoutSeconds),
          maxCrossing_(maxCrossing),
          memory_(memory),
          computeQueue_(computeQueue),
          cacheMemoryLimitBytes_(cacheMemoryBytes),
          nameNormalizer_(dataPaths_.knotNameRegDir),
          namePdLookup_(dataPaths_.primePdDb),
          homflyIndex_(hki::loadInvariantMap(dataPaths_.homflyDb, nameNormalizer_)),
          khovanovIndex_(hki::loadInvariantMap(dataPaths_.khovanovDb, nameNormalizer_)) {}

    LookupResult lookup(const std::string& pdText,
                        const std::shared_ptr<CancellationToken>& cancellation = nullptr,
                        const std::function<void()>& onStarted = {}) {
        const hki::PDCode pd = hki::parsePDCode(pdText);
        const std::string canonical = hki::formatPDCodeList(pd);
        if (auto cached = findCached(canonical)) {
            const PdDiagram diagram = buildPdDiagram(canonical);
            cached->pdDiagramSvg = diagram.svg;
            cached->pdDiagramError = diagram.error;
            return *cached;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds_);
        const auto cancelled = [&] {
            return hki::interrupted() || (cancellation && cancellation->cancelled());
        };
        ComputeQueue::Lease lease = computeQueue_.acquire(deadline, cancelled, onStarted);
        if (auto cached = findCached(canonical)) {
            const PdDiagram diagram = buildPdDiagram(canonical);
            cached->pdDiagramSvg = diagram.svg;
            cached->pdDiagramError = diagram.error;
            return *cached;
        }

        LookupResult result = compute(canonical, cancellation, deadline);
        const PdDiagram diagram = buildPdDiagram(result.canonicalPd);
        result.pdDiagramSvg = diagram.svg;
        result.pdDiagramError = diagram.error;
        const bool cacheable = !result.homflyWorker.cancelled &&
                               !result.khovanovWorker.cancelled &&
                               (result.homflyWorker.success || result.khovanovWorker.success);
        if (cacheable) {
            putCached(canonical, result);
        }
        return result;
    }

    std::optional<std::string> lookupNamePd(const std::string& knotName, std::string& error) {
        if (auto crossingError = crossingLimitError(knotName)) {
            error = *crossingError;
            return std::nullopt;
        }
        const std::string canonicalName = nameNormalizer_.normalize(normalizeNameSyntax(knotName));
        return namePdLookup_.lookup(canonicalName, error);
    }

    std::string nameIndexStatus() const {
        return "Invariant data source: text files " +
               cki::platform::displayPath(dataPaths_.homflyDb) + " and " +
               cki::platform::displayPath(dataPaths_.khovanovDb) +
               "\nName normalization data: " + cki::platform::displayPath(dataPaths_.knotNameRegDir) +
               "\nName-to-PD data: " + namePdLookup_.statusMessage();
    }

    int maxCrossing() const {
        return maxCrossing_;
    }

private:
    std::filesystem::path executable_;
    DataPaths dataPaths_;
    int timeoutSeconds_ = kMaxComputeTimeoutSeconds;
    int maxCrossing_ = kDefaultMaxCrossing;
    const AdaptiveMemoryController& memory_;
    ComputeQueue& computeQueue_;
    std::size_t cacheMemoryLimitBytes_ = 0;
    hki::NameNormalizer nameNormalizer_;
    NamePdLookup namePdLookup_;
    hki::InvariantMap homflyIndex_;
    hki::InvariantMap khovanovIndex_;
    std::mutex cacheMutex_;
    struct CacheEntry {
        LookupResult result;
        std::size_t bytes = 0;
        std::list<std::string>::iterator lru;
    };
    std::unordered_map<std::string, CacheEntry> cache_;
    std::list<std::string> cacheLru_;
    std::size_t cacheBytes_ = 0;

    static std::size_t resultBytes(const std::string& key, const LookupResult& result) {
        std::size_t bytes = key.size() + result.canonicalPd.size() + result.homfly.size() +
            result.khovanov.size() + result.candidateError.size() + result.homflyWorker.output.size() +
            result.homflyWorker.error.size() + result.khovanovWorker.output.size() +
            result.khovanovWorker.error.size();
        for (const std::string& candidate : result.candidates) bytes += candidate.size();
        return bytes + sizeof(CacheEntry);
    }

    std::optional<LookupResult> findCached(const std::string& canonical) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        const auto found = cache_.find(canonical);
        if (found == cache_.end()) return std::nullopt;
        cacheLru_.splice(cacheLru_.begin(), cacheLru_, found->second.lru);
        return found->second.result;
    }

    void putCached(const std::string& canonical, const LookupResult& source) {
        if (cacheMemoryLimitBytes_ == 0) return;
        LookupResult result = source;
        result.pdDiagramSvg.clear();
        result.pdDiagramError.clear();
        const std::size_t bytes = resultBytes(canonical, result);
        if (bytes > cacheMemoryLimitBytes_) return;

        std::lock_guard<std::mutex> lock(cacheMutex_);
        const auto existing = cache_.find(canonical);
        if (existing != cache_.end()) {
            cacheBytes_ -= existing->second.bytes;
            cacheLru_.erase(existing->second.lru);
            cache_.erase(existing);
        }
        cacheLru_.push_front(canonical);
        cache_.emplace(canonical, CacheEntry{std::move(result), bytes, cacheLru_.begin()});
        cacheBytes_ += bytes;
        while (cacheBytes_ > cacheMemoryLimitBytes_ && !cacheLru_.empty()) {
            const std::string key = cacheLru_.back();
            const auto found = cache_.find(key);
            if (found != cache_.end()) {
                cacheBytes_ -= found->second.bytes;
                cache_.erase(found);
            }
            cacheLru_.pop_back();
        }
    }

    std::optional<int> crossingNumberForName(const std::string& rawName) const {
        const std::string canonicalName = nameNormalizer_.normalize(rawName);
        if (canonicalName.empty()) return std::nullopt;
        return totalKnotCrossings(canonicalName);
    }

    bool shouldKeepCandidateName(const std::string& rawName) const {
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
            return !shouldKeepCandidateName(name);
        }), names.end());
        return names;
    }

    enum class InvariantKind {
        Homfly,
        Khovanov,
    };

    enum class InvariantPipelineMode {
        OriginalOnly,
        SimplifyRetry,
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

    hki::WorkerResult runLimitedWorker(const std::string& workerName,
                                       const std::string& pdText,
                                       std::chrono::steady_clock::time_point deadline,
                                       const std::shared_ptr<CancellationToken>& cancellation) {
        const auto cancelled = [&] {
            return hki::interrupted() || (cancellation && cancellation->cancelled());
        };
        hki::WorkerResult result;
        std::uint64_t memoryLimit = 0;
        try {
            memoryLimit = memory_.waitForWorkerLimit(deadline, cancelled);
        } catch (const ComputeCancelledError&) {
            result.cancelled = !hki::interrupted();
            result.interrupted = hki::interrupted();
            result.exitCode = result.interrupted ? 130 : 125;
            return result;
        } catch (const ResourceExhaustedError& error) {
            result.resourceExhausted = true;
            result.exitCode = 126;
            result.error = error.what();
            return result;
        }

        hki::WorkerLimits limits;
        limits.memoryBytes = memoryLimit;
        limits.linuxOomScoreAdjust = 750;
        std::unique_ptr<hki::WorkerProcess> process;
        try {
            process = hki::startWorkerProcess(executable_, workerName, pdText, limits);
        } catch (const std::exception& error) {
            result.resourceExhausted = true;
            result.exitCode = 126;
            result.error = error.what();
            return result;
        }
        while (!hki::pollWorkerProcess(*process, deadline)) {
            if (cancellation && cancellation->cancelled()) {
                hki::cancelWorkerProcess(*process);
                break;
            }
            if (memory_.emergencyPressure()) {
                hki::exhaustWorkerProcess(*process, "worker stopped to preserve the server memory reserve");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return hki::finishWorkerProcess(*process);
    }

    LookupResult computeInvariants(const std::string& canonicalPd,
                                   const std::shared_ptr<CancellationToken>& cancellation,
                                   InvariantPipelineMode mode,
                                   std::chrono::steady_clock::time_point deadline) {
        LookupResult result;
        result.canonicalPd = canonicalPd;

        std::vector<std::pair<std::string, std::string>> sources;
        if (mode == InvariantPipelineMode::SimplifyRetry) {
            const hki::WorkerResult simplified = runLimitedWorker("simplify", canonicalPd, deadline, cancellation);
            if (simplified.success && !simplified.output.empty() && simplified.output != canonicalPd) {
                result.canonicalPd = simplified.output;
                sources.push_back({"simplified", simplified.output});
            }
            if (simplified.cancelled || simplified.interrupted) {
                result.homflyWorker = simplified;
                result.khovanovWorker = simplified;
                return result;
            }
        }
        sources.push_back({"original", canonicalPd});

        auto computeOne = [&](InvariantKind kind) {
            SelectedInvariant selected;
            for (const auto& source : sources) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    selected.result.timedOut = true;
                    selected.result.exitCode = 124;
                    break;
                }
                const hki::WorkerResult attempt =
                    runLimitedWorker(workerNameFor(kind), source.second, deadline, cancellation);
                recordAttempt(selected, source.first, attempt);
                if (attempt.success || attempt.cancelled || attempt.interrupted || attempt.resourceExhausted) break;
            }
            return selected.result;
        };

        result.homflyWorker = computeOne(InvariantKind::Homfly);
        if (result.homflyWorker.cancelled || result.homflyWorker.interrupted) {
            result.khovanovWorker = result.homflyWorker;
        } else {
            result.khovanovWorker = computeOne(InvariantKind::Khovanov);
        }

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
                         const std::shared_ptr<CancellationToken>& cancellation,
                         std::chrono::steady_clock::time_point deadline) {
        LookupResult result = computeInvariants(
            canonicalPd, cancellation, InvariantPipelineMode::SimplifyRetry, deadline);

        std::optional<std::string> homfly;
        std::optional<std::string> khovanov;
        if (!result.homfly.empty()) homfly = result.homfly;
        if (!result.khovanov.empty()) khovanov = result.khovanov;

        const std::vector<std::string> khovanovNames =
            khovanov.has_value() ? hki::lookupInvariant(khovanovIndex_, *khovanov) : std::vector<std::string>{};
        const std::vector<std::string> homflyNames =
            homfly.has_value() ? hki::lookupInvariant(homflyIndex_, *homfly) : std::vector<std::string>{};

        result.candidates = filterNamesByMaxCrossing(
            mergeCandidates(result.khovanovWorker, khovanovNames, result.homflyWorker, homflyNames));
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
    while (input >> v) {
        if (!std::isfinite(v)) throw std::runtime_error("3D coordinates must be finite numbers.");
        values.push_back(v);
    }
    if (!input.eof()) {
        throw std::runtime_error("3D coordinate list contains a non-numeric value.");
    }
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
                response = makeText(302, "Found", "", "text/plain; charset=utf-8");
                response.headers["Location"] = "/";
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

        if (request.method == "GET" && startsWith(request.path, "/api/tasks/history/")) {
            const std::string cursorText = request.path.substr(std::string("/api/tasks/history/").size());
            std::size_t used = 0;
            const std::uint64_t cursor = static_cast<std::uint64_t>(std::stoull(cursorText, &used, 10));
            if (used != cursorText.size()) return makeJsonError("bad task history cursor.");
            return makeText(200, "OK", tasks_.historyJson(cursor, 100), "application/json; charset=utf-8");
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
            try {
                const auto points = parseCoordinateRows(*coordText);
                const cki::link_pd_code::PDCode pd = cki::link_pd_code::computePDCode(points);
                return makeJsonSuccess(cki::link_pd_code::formatPDCode(pd));
            } catch (const std::exception& error) {
                return makeJsonError(error.what());
            }
        }

        if (request.method == "GET" && request.path == "/api/last_build_info") {
            std::ostringstream info;
            info << "Pure C++ knot-indexer-lab server\n"
                 << "Invariant timeout: " << kMaxComputeTimeoutSeconds << " seconds\n"
                 << "Maximum total crossing number: " << engine_.maxCrossing() << "\n"
                 << "Invariant data: " << engine_.nameIndexStatus() << "\n";
            return makeText(200, "OK", info.str(), "text/plain; charset=utf-8");
        }

        return makeText(404, "Not Found", "not found", "text/plain; charset=utf-8");
    }

    template <typename ResolvePd>
    Response runLookupTask(const std::string& clientId, const std::string& inputType, const std::string& input, ResolvePd resolvePd) {
        auto task = tasks_.create(clientId, inputType, input);
        try {
            const std::string pd = resolvePd();
            tasks_.setCanonicalPd(task, pd, "", "");
            const LookupResult result = engine_.lookup(pd, task->cancellation, [&] {
                tasks_.markRunning(task);
            });
            tasks_.complete(task, result);
            return makeLookupJson(task, result);
        } catch (const ResourceExhaustedError& error) {
            tasks_.failResourceExhausted(task, error.what());
            return makeTaskFailureJson(task, error.what());
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
            if (activeClients_.fetch_add(1, std::memory_order_relaxed) >= kMaxClientConnections) {
                activeClients_.fetch_sub(1, std::memory_order_relaxed);
                const Response response = makeText(503, "Service Unavailable", "server connection limit reached", "text/plain; charset=utf-8");
                sendAll(client, response.serialize());
                closeSocket(client);
                continue;
            }
            try {
                std::thread(&HttpServer::handleClient, this, client).detach();
            } catch (...) {
                activeClients_.fetch_sub(1, std::memory_order_relaxed);
                closeSocket(client);
                throw;
            }
        }
        closeSocket(server);
    }

private:
    std::string host_;
    int port_ = kDefaultPort;
    Application& app_;
    std::atomic_int activeClients_{0};

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
        activeClients_.fetch_sub(1, std::memory_order_relaxed);
    }
};

void usage(std::ostream& out) {
    out << "Usage: knot_indexer_lab_server [OPTIONS]\n\n"
        << "SVG generation:\n"
        << "  knot_indexer_lab_server --render-pd-svg --input code.txt --output diagram.svg\n"
        << "  knot_indexer_lab_server --render-pd-svg --pd \"[[4,2,5,1],...]\"\n\n"
        << "Options:\n"
        << "  --host ADDRESS       IPv4 address to bind. Default: 0.0.0.0\n"
        << "  --port PORT          TCP port. Default: 5000\n"
        << "  --data-folder PATH   Folder containing homfly/, khovanov/, knotname-reg/, and name-pd/.\n"
        << "  --web-root PATH      Folder containing index.html and static/.\n"
        << "  --timeout SEC        Worker timeout, capped at 1200 seconds. Default: 1200\n"
        << "  --max-crossing N     Maximum total crossing number. Default: 14, max: 16\n"
        << "  --worker-memory-mb N Maximum memory per compute worker. Default: adaptive (cap 4096)\n"
        << "  --memory-reserve-mb N Memory kept free for the server and OS. Default: adaptive\n"
        << "  --cache-memory-mb N  In-memory result cache limit. Default: 64, 0 disables\n"
        << "  --task-history PATH  Persistent completed-task history file.\n"
        << "  --render-pd-svg      Render a PD code as an SVG and exit.\n"
        << "  --pd TEXT            Inline PD code for --render-pd-svg.\n"
        << "  --input PATH         PD-code input file for --render-pd-svg.\n"
        << "  --output PATH        SVG output file for --render-pd-svg. Default: stdout\n"
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

std::uint64_t parseNonNegativeMb(const std::string& value, const std::string& name) {
    std::size_t used = 0;
    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(value, &used, 10);
    } catch (const std::exception&) {
        throw std::runtime_error(name + " must be a non-negative integer");
    }
    if (used != value.size() || parsed > 1024ULL * 1024ULL) {
        throw std::runtime_error(name + " must be between 0 and 1048576 MiB");
    }
    return static_cast<std::uint64_t>(parsed);
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
        } else if (arg == "--max-crossing") {
            options.maxCrossing = parsePositiveInt(needValue("--max-crossing").text, "--max-crossing");
            if (options.maxCrossing > kHardMaxCrossing) {
                throw std::runtime_error("--max-crossing must not exceed 16");
            }
        } else if (arg == "--worker-memory-mb") {
            options.workerMemoryMb = parseNonNegativeMb(needValue("--worker-memory-mb").text, "--worker-memory-mb");
        } else if (arg == "--memory-reserve-mb") {
            options.memoryReserveMb = parseNonNegativeMb(needValue("--memory-reserve-mb").text, "--memory-reserve-mb");
        } else if (arg == "--cache-memory-mb") {
            options.cacheMemoryMb = parseNonNegativeMb(needValue("--cache-memory-mb").text, "--cache-memory-mb");
        } else if (arg == "--task-history") {
            options.taskHistory = needValue("--task-history").path;
        } else if (arg == "--render-pd-svg") {
            options.renderPdSvg = true;
        } else if (arg == "--pd") {
            options.svgInlinePd = needValue("--pd").text;
            options.svgInputInline = true;
        } else if (arg == "--input") {
            options.svgInputPath = needValue("--input").path;
            options.svgInputFromFile = true;
        } else if (arg == "--output") {
            options.svgOutputPath = needValue("--output").path;
            options.svgOutputToFile = true;
        } else if (arg == "--worker") {
            return options;
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }
    if (options.renderPdSvg) {
        if (options.svgInputFromFile == options.svgInputInline) {
            throw std::runtime_error("--render-pd-svg needs exactly one of --input or --pd");
        }
    } else if (options.svgInputFromFile || options.svgInputInline || options.svgOutputToFile) {
        throw std::runtime_error("--pd, --input, and --output are only valid with --render-pd-svg");
    }
    return options;
}

int renderPdSvgCli(const Options& options) {
    const std::string pdText = options.svgInputInline ? options.svgInlinePd : readWholeFile(options.svgInputPath);
    const PdDiagram diagram = buildPdDiagram(pdText);
    if (!diagram.error.empty()) {
        throw std::runtime_error("cannot render PD SVG: " + diagram.error);
    }

    if (options.svgOutputToFile) {
        writeWholeFile(options.svgOutputPath, diagram.svg + "\n");
    } else {
        std::cout << diagram.svg << "\n";
    }
    return 0;
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
        if (options.renderPdSvg) {
            return lab::renderPdSvgCli(options);
        }

        const std::filesystem::path executable = lab::currentExecutablePath(args.empty() ? std::filesystem::path() : args[0].path);
        const lab::DataPaths dataPaths = lab::resolveDataFolder(executable, options.dataFolder);
        const lab::AdaptiveMemoryController memory(
            options.workerMemoryMb * lab::kMemoryMiB,
            options.memoryReserveMb * lab::kMemoryMiB);
        std::cerr << lab::configureServerOomProtection() << "\n";
        lab::ComputeQueue computeQueue(memory);
        lab::KnotEngine engine(
            executable,
            dataPaths,
            options.timeoutSeconds,
            options.maxCrossing,
            memory,
            computeQueue,
            static_cast<std::size_t>(options.cacheMemoryMb * lab::kMemoryMiB));
        std::cerr << memory.status() << "\n";

        const std::filesystem::path webRoot = lab::resolveWebRoot(executable, options.webRoot);
        const std::filesystem::path historyPath = options.taskHistory.empty()
            ? executable.parent_path() / "state" / "tasks.history"
            : lab::absolutePath(options.taskHistory);
        lab::TaskManager tasks(historyPath);
        std::cerr << "Task history: " << tasks.historyStatus() << "\n";
        lab::Application app(webRoot, engine, tasks);
        lab::HttpServer server(options.host, options.port, app);
        server.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << "\n";
        return 1;
    }
}
