#pragma once

// Minimal SQLite 3 C API. Used only when libsqlite3-dev is not installed.
// Signatures match the public SQLite C API (https://sqlite.org/c3ref/intro.html).

#include <cstdint>

extern "C" {

struct sqlite3;
struct sqlite3_stmt;

constexpr int SQLITE_OK = 0;
constexpr int SQLITE_ERROR = 1;
constexpr int SQLITE_BUSY = 5;
constexpr int SQLITE_LOCKED = 6;
constexpr int SQLITE_MISUSE = 21;
constexpr int SQLITE_ROW = 100;
constexpr int SQLITE_DONE = 101;
constexpr int SQLITE_NULL = 5;
constexpr int SQLITE_TRANSIENT = -1;

int sqlite3_open(const char* filename, sqlite3** ppDb);
int sqlite3_close(sqlite3*);
int sqlite3_exec(sqlite3*, const char* sql, int (*callback)(void*, int, char**, char**),
                 void*, char** errmsg);
void sqlite3_free(void*);
int sqlite3_prepare_v2(sqlite3*, const char* zSql, int nByte, sqlite3_stmt** ppStmt,
                       const char** pzTail);
int sqlite3_bind_int(sqlite3_stmt*, int, int);
int sqlite3_bind_int64(sqlite3_stmt*, int, std::int64_t);
int sqlite3_bind_text(sqlite3_stmt*, int, const char*, int n, void (*)(void*));
int sqlite3_bind_null(sqlite3_stmt*, int);
int sqlite3_bind_parameter_index(sqlite3_stmt*, const char* zName);
int sqlite3_step(sqlite3_stmt*);
int sqlite3_reset(sqlite3_stmt*);
int sqlite3_finalize(sqlite3_stmt*);
int sqlite3_column_type(sqlite3_stmt*, int iCol);
int sqlite3_column_int(sqlite3_stmt*, int iCol);
std::int64_t sqlite3_column_int64(sqlite3_stmt*, int iCol);
const unsigned char* sqlite3_column_text(sqlite3_stmt*, int iCol);
int sqlite3_column_bytes(sqlite3_stmt*, int iCol);
const char* sqlite3_errmsg(sqlite3*);
std::int64_t sqlite3_last_insert_rowid(sqlite3*);
int sqlite3_changes(sqlite3*);

}  // extern "C"
