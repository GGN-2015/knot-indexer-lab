#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace hki {

class NameNormalizer {
public:
    NameNormalizer() = default;
    explicit NameNormalizer(const std::filesystem::path& dataDir);

    std::string normalize(const std::string& knotName) const;

private:
    std::unordered_map<std::string, std::string> name1ToName2_;
    std::unordered_map<std::string, bool> amphichiral_;
    std::unordered_map<std::string, bool> needMirror_;

    bool isAmphichiralPrime(const std::string& rawPrimeName) const;
    std::string regularizePrime(const std::string& knotName) const;
    std::string simplifyPrime(const std::string& knotName) const;
};

using InvariantMap = std::unordered_map<std::string, std::vector<std::string>>;

InvariantMap loadInvariantMap(const std::filesystem::path& file, const NameNormalizer& normalizer);
std::vector<std::string> lookupInvariant(const InvariantMap& map, const std::string& invariant);

} // namespace hki

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace hki {

namespace {

std::string trim(const std::string& value) {
    const char* ws = " \t\r\n";
    size_t first = value.find_first_not_of(ws);
    if (first == std::string::npos) return "";
    size_t last = value.find_last_not_of(ws);
    return value.substr(first, last - first + 1);
}

std::vector<std::string> splitComma(const std::string& value) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= value.size()) {
        size_t pos = value.find(',', start);
        std::string part = trim(value.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (!part.empty()) parts.push_back(part);
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return parts;
}

std::string normalizeK(const std::string& value) {
    std::string out = value;
    for (char& c : out) {
        if (c == 'k') c = 'K';
    }
    return out;
}

std::string lowerCopy(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool isPrimeNameFormat(const std::string& knotName) {
    std::string s = lowerCopy(knotName);
    size_t pos = 0;
    if (pos < s.size() && s[pos] == 'm') ++pos;
    if (pos >= s.size() || s[pos] != 'k') return false;
    ++pos;
    size_t digitStart = pos;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
    if (pos == digitStart) return false;
    if (pos >= s.size() || (s[pos] != 'a' && s[pos] != 'n')) return false;
    ++pos;
    size_t finalDigits = pos;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
    return pos > finalDigits && pos == s.size();
}

std::string toggleMirror(const std::string& knotName) {
    if (!knotName.empty() && knotName[0] == 'm') return knotName.substr(1);
    return "m" + knotName;
}

void loadOneColumnSet(const std::filesystem::path& file, std::unordered_map<std::string, bool>& target) {
    std::ifstream in(file);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) target[normalizeK(line)] = true;
    }
}

} // namespace

inline NameNormalizer::NameNormalizer(const std::filesystem::path& dataDir) {
    std::ifstream pairs(dataDir / "name_pair.txt");
    std::string line;
    while (std::getline(pairs, line)) {
        line = trim(line);
        if (line.empty()) continue;
        std::istringstream in(line);
        std::string name2;
        std::string name1;
        if (in >> name2 >> name1) name1ToName2_[normalizeK(name1)] = normalizeK(name2);
    }
    loadOneColumnSet(dataDir / "amphichiral_list.txt", amphichiral_);
    loadOneColumnSet(dataDir / "need_mirror.txt", needMirror_);
}

inline bool NameNormalizer::isAmphichiralPrime(const std::string& rawPrimeName) const {
    std::string raw = normalizeK(rawPrimeName);
    auto mapped = name1ToName2_.find(raw);
    if (mapped == name1ToName2_.end()) return false;
    return amphichiral_.find(mapped->second) != amphichiral_.end();
}

inline std::string NameNormalizer::regularizePrime(const std::string& knotName) const {
    if (!isPrimeNameFormat(knotName)) return normalizeK(knotName);
    std::string normalized = normalizeK(knotName);
    std::string base = normalized[0] == 'm' ? normalized.substr(1) : normalized;
    if (needMirror_.find(base) != needMirror_.end()) return toggleMirror(normalized);
    return normalized;
}

inline std::string NameNormalizer::simplifyPrime(const std::string& knotName) const {
    if (!isPrimeNameFormat(knotName)) return normalizeK(knotName);
    std::string normalized = normalizeK(knotName);
    std::string raw = normalized[0] == 'm' ? normalized.substr(1) : normalized;
    if (isAmphichiralPrime(raw)) return raw;
    return normalized;
}

inline std::string NameNormalizer::normalize(const std::string& knotName) const {
    std::vector<std::string> parts = splitComma(knotName);
    if (parts.empty()) return "";
    for (std::string& part : parts) {
        part = simplifyPrime(regularizePrime(part));
    }
    std::sort(parts.begin(), parts.end());
    std::ostringstream out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out << ",";
        out << parts[i];
    }
    return out.str();
}

inline InvariantMap loadInvariantMap(const std::filesystem::path& file, const NameNormalizer& normalizer) {
    std::ifstream in(file);
    if (!in) throw std::runtime_error("cannot open invariant database: " + file.string());

    InvariantMap map;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line.front() != '[' || line.back() != ']') {
            throw std::runtime_error("bad invariant database line: " + line);
        }
        size_t sep = line.find('|');
        if (sep == std::string::npos) throw std::runtime_error("bad invariant database line: " + line);
        std::string invariant = line.substr(1, sep - 1);
        std::string knotName = normalizer.normalize(line.substr(sep + 1, line.size() - sep - 2));
        if (knotName.empty()) continue;
        std::vector<std::string>& names = map[invariant];
        if (std::find(names.begin(), names.end(), knotName) == names.end()) names.push_back(knotName);
    }
    return map;
}

inline std::vector<std::string> lookupInvariant(const InvariantMap& map, const std::string& invariant) {
    auto found = map.find(trim(invariant));
    if (found == map.end()) return {};
    return found->second;
}

} // namespace hki
