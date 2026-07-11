#pragma once

#include "database.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef HKI_WITH_SQLITE
#include "sqlite3.h"
#endif

namespace hki {

#ifdef HKI_WITH_SQLITE

class SqliteInvariantDatabase {
public:
    SqliteInvariantDatabase() = default;
    SqliteInvariantDatabase(std::filesystem::path file, const NameNormalizer& normalizer);
    ~SqliteInvariantDatabase();

    SqliteInvariantDatabase(const SqliteInvariantDatabase&) = delete;
    SqliteInvariantDatabase& operator=(const SqliteInvariantDatabase&) = delete;

    bool opened() const { return db_ != nullptr; }
    bool hasInvariantRecords() const { return db_ != nullptr && invariantRecordCount_ > 0; }
    const std::filesystem::path& file() const { return file_; }
    std::string statusMessage() const;

    std::vector<std::string> lookup(const std::optional<std::string>& homfly,
                                    const std::optional<std::string>& khovanov) const;

private:
    std::filesystem::path file_;
    const NameNormalizer* normalizer_ = nullptr;
    sqlite3* db_ = nullptr;
    std::size_t invariantRecordCount_ = 0;
    std::string statusMessage_;

    void close();
    void openReadOnly();
    bool tableExists(const std::string& table) const;
    std::size_t countRows(const std::string& table) const;
    std::vector<std::string> queryNames(const std::string& sql,
                                        const std::vector<std::string>& values) const;
};

namespace sqlite_detail {

inline std::string pathUtf8(const std::filesystem::path& path) {
    return path.u8string();
}

inline bool existsPath(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

inline std::string sqliteError(sqlite3* db) {
    return db ? sqlite3_errmsg(db) : "sqlite handle is null";
}

class Statement {
public:
    Statement(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
            throw std::runtime_error("sqlite prepare failed: " + sqliteError(db_));
        }
    }

    ~Statement() {
        if (stmt_) sqlite3_finalize(stmt_);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bindText(int index, const std::string& value) {
        if (sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
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

    std::string columnText(int index) const {
        const unsigned char* value = sqlite3_column_text(stmt_, index);
        return value ? reinterpret_cast<const char*>(value) : "";
    }

    std::size_t columnSize(int index) const {
        return static_cast<std::size_t>(sqlite3_column_int64(stmt_, index));
    }

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

inline std::vector<std::string> sortedUnique(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

}  // namespace sqlite_detail

inline SqliteInvariantDatabase::SqliteInvariantDatabase(std::filesystem::path file,
                                                        const NameNormalizer& normalizer)
    : file_(std::move(file)), normalizer_(&normalizer) {
    if (file_.empty()) {
        statusMessage_ = "SQLite invariant database was not configured.";
        return;
    }
    if (!sqlite_detail::existsPath(file_)) {
        statusMessage_ = "SQLite invariant database not found at " + file_.string() + ".";
        return;
    }

    try {
        openReadOnly();
        if (!tableExists("invariants")) {
            statusMessage_ = "SQLite database has no invariants table: " + file_.string() + ".";
            close();
            return;
        }
        invariantRecordCount_ = countRows("invariants");
    } catch (const std::exception& error) {
        statusMessage_ = "SQLite database could not be opened at " + file_.string() + ": " + error.what();
        close();
    }
}

inline SqliteInvariantDatabase::~SqliteInvariantDatabase() {
    close();
}

inline std::string SqliteInvariantDatabase::statusMessage() const {
    if (!db_) return statusMessage_;
    return "SQLite invariant database: " + std::to_string(invariantRecordCount_) +
           " records from " + file_.string();
}

inline std::vector<std::string> SqliteInvariantDatabase::lookup(
    const std::optional<std::string>& homfly,
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

inline void SqliteInvariantDatabase::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    invariantRecordCount_ = 0;
}

inline void SqliteInvariantDatabase::openReadOnly() {
    sqlite3* db = nullptr;
    const std::string filename = sqlite_detail::pathUtf8(file_);
    const int rc = sqlite3_open_v2(filename.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        std::string message = db ? sqlite3_errmsg(db) : "unknown sqlite open error";
        if (db) sqlite3_close(db);
        throw std::runtime_error(message);
    }
    db_ = db;
    sqlite3_busy_timeout(db_, 30000);
}

inline bool SqliteInvariantDatabase::tableExists(const std::string& table) const {
    sqlite_detail::Statement stmt(db_, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1");
    stmt.bindText(1, table);
    return stmt.step() == SQLITE_ROW;
}

inline std::size_t SqliteInvariantDatabase::countRows(const std::string& table) const {
    sqlite_detail::Statement stmt(db_, "SELECT COUNT(*) FROM " + table);
    if (stmt.step() != SQLITE_ROW) return 0;
    return stmt.columnSize(0);
}

inline std::vector<std::string> SqliteInvariantDatabase::queryNames(
    const std::string& sql,
    const std::vector<std::string>& values) const {
    sqlite_detail::Statement stmt(db_, sql);
    for (std::size_t i = 0; i < values.size(); ++i) {
        stmt.bindText(static_cast<int>(i + 1), values[i]);
    }

    std::vector<std::string> names;
    while (stmt.step() == SQLITE_ROW) {
        const std::string name = normalizer_ ? normalizer_->normalize(stmt.columnText(0)) : stmt.columnText(0);
        if (!name.empty()) names.push_back(name);
    }
    return sqlite_detail::sortedUnique(std::move(names));
}

#else

class SqliteInvariantDatabase {
public:
    SqliteInvariantDatabase() = default;
    SqliteInvariantDatabase(std::filesystem::path file, const NameNormalizer&)
        : file_(std::move(file)) {}

    bool opened() const { return false; }
    bool hasInvariantRecords() const { return false; }
    const std::filesystem::path& file() const { return file_; }
    std::string statusMessage() const {
        return "SQLite support was not compiled into this executable.";
    }

    std::vector<std::string> lookup(const std::optional<std::string>&,
                                    const std::optional<std::string>&) const {
        return {};
    }

private:
    std::filesystem::path file_;
};

#endif

}  // namespace hki
