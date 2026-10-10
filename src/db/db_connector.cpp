#include "db/db_connector.h"
#include <cctype>
#include <cstdlib>
#include <iostream>

namespace db {

namespace {

std::once_flag g_libInitFlag;

std::string envOr(const char* key, const char* def) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : std::string(def);
}

// MySQL client calls from a thread other than the one that initialized the
// library need mysql_thread_init()/mysql_thread_end().
struct ThreadGuard {
    ThreadGuard() { mysql_thread_init(); }
    ~ThreadGuard() { mysql_thread_end(); }
};
void ensureThreadInit(const std::thread::id& initThread) {
    if (std::this_thread::get_id() == initThread) return;
    thread_local ThreadGuard guard;
    (void)guard;
}

bool validDbName(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    for (char c : n)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    return true;
}

bool prevWordIsLimitOrOffset(const std::string& s) {
    size_t e = s.size();
    while (e > 0 && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    size_t b = e;
    while (b > 0 && std::isalpha(static_cast<unsigned char>(s[b - 1]))) --b;
    std::string w = s.substr(b, e - b);
    for (auto& c : w) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return w == "LIMIT" || w == "OFFSET";
}

bool allDigits(const std::string& s) {
    if (s.empty() || s.size() > 18) return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

}  // namespace

DBConfig DBConfig::fromEnv() {
    DBConfig c;
    c.host = envOr("PRIDEBANK_HOST", c.host.c_str());
    c.port = static_cast<unsigned int>(std::atoi(envOr("PRIDEBANK", "3306").c_str()));
    c.user = envOr("PRIDEBANK_DB_USER", c.user.c_str());
    c.password = envOr("PRIDEBANK_DB_PASSWORD", "");
    c.database = envOr("PRIDEBANK_DB_NAME", c.database.c_str());
    return c;
}

DBConnector& DBConnector::getInstance() {
    static DBConnector instance;
    return instance;
}

DBConnector::~DBConnector() {
    close();
}

bool DBConnector::connectLocked(bool selectDatabase) {
    conn_ = mysql_init(nullptr);
    if (!conn_) { last_error_ = "mysql_init failed"; return false; }

    unsigned int timeoutSec = 5;
    mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &timeoutSec);
    mysql_options(conn_, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    if (!mysql_real_connect(conn_, cfg_.host.c_str(), cfg_.user.c_str(), cfg_.password.c_str(),
                            selectDatabase ? cfg_.database.c_str() : nullptr, cfg_.port,
                            nullptr, CLIENT_MULTI_STATEMENTS)) {
        last_error_ = mysql_error(conn_);
        mysql_close(conn_);
        conn_ = nullptr;
        return false;
    }
    return true;
}

void DBConnector::closeLocked() {
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }
}

bool DBConnector::reconnectLocked() {
    closeLocked();
    std::cerr << "[DBConnector] Connection lost, reconnecting..." << std::endl;
    return connectLocked(true);
}

bool DBConnector::init(const DBConfig& cfg) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::call_once(g_libInitFlag, []() { mysql_library_init(0, nullptr, nullptr); });

    closeLocked();
    cfg_ = cfg;
    init_thread_ = std::this_thread::get_id();

    if (!validDbName(cfg_.database)) {
        last_error_ = "Invalid database name (use letters, digits, underscore): " + cfg_.database;
        std::cerr << "[DBConnector] " << last_error_ << std::endl;
        return false;
    }
    if (!connectLocked(false)) {
        std::cerr << "[DBConnector] Cannot connect to MySQL at " << cfg_.host << ":" << cfg_.port
                  << " as '" << cfg_.user << "': " << last_error_ << std::endl;
        return false;
    }

    // Use the database; create it first only if it does not exist yet.
    if (mysql_select_db(conn_, cfg_.database.c_str()) != 0) {
        std::string create = "CREATE DATABASE IF NOT EXISTS `" + cfg_.database + "` CHARACTER SET utf8mb4";
        if (mysql_query(conn_, create.c_str()) != 0 ||
            mysql_select_db(conn_, cfg_.database.c_str()) != 0) {
            last_error_ = mysql_error(conn_);
            std::cerr << "[DBConnector] Cannot use database '" << cfg_.database << "': " << last_error_ << std::endl;
            closeLocked();
            return false;
        }
    }
    return true;
}

void DBConnector::close() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    closeLocked();
}

std::string DBConnector::lastError() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    return last_error_;
}

bool DBConnector::bindLocked(const std::string& sql, const std::vector<std::string>& params, std::string& out) {
    out.clear();
    out.reserve(sql.size() + params.size() * 16);
    size_t next = 0;
    char quote = 0;

    for (size_t i = 0; i < sql.size(); ++i) {
        char ch = sql[i];
        if (quote) {  // inside a quoted literal or identifier: copy verbatim
            out += ch;
            if (ch == '\\' && quote != '`' && i + 1 < sql.size()) out += sql[++i];
            else if (ch == quote) {
                if (i + 1 < sql.size() && sql[i + 1] == quote) out += sql[++i];  // doubled quote
                else quote = 0;
            }
            continue;
        }
        if (ch == '\'' || ch == '"' || ch == '`') { quote = ch; out += ch; continue; }
        if (ch != '?') { out += ch; continue; }

        if (next >= params.size()) { last_error_ = "Too few parameters for query"; return false; }
        const std::string& p = params[next++];
        if (allDigits(p) && prevWordIsLimitOrOffset(out)) {
            out += p;
        } else {
            std::string esc(p.size() * 2 + 1, '\0');
            unsigned long n = mysql_real_escape_string(conn_, &esc[0], p.data(), static_cast<unsigned long>(p.size()));
            esc.resize(n);
            out += "'" + esc + "'";
        }
    }
    if (next != params.size()) { last_error_ = "Too many parameters for query"; return false; }
    return true;
}

bool DBConnector::runLocked(const std::string& sql, std::vector<Row>* rows) {
    ensureThreadInit(init_thread_);

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!conn_) { last_error_ = "Not connected"; return false; }

        if (mysql_real_query(conn_, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0) {
            unsigned int err = mysql_errno(conn_);
            last_error_ = mysql_error(conn_);
            if ((err == 2006 || err == 2013) && attempt == 0 && reconnectLocked()) continue;  // server gone / lost
            std::cerr << "[DBConnector] SQL error " << err << ": " << last_error_
                      << " | Query: " << sql.substr(0, 200) << std::endl;
            return false;
        }

        bool ok = true, firstSet = true;
        for (;;) {  // walk every result set (multi-statement scripts)
            MYSQL_RES* res = mysql_store_result(conn_);
            if (res) {
                if (rows && firstSet) {
                    unsigned int nf = mysql_num_fields(res);
                    MYSQL_FIELD* fields = mysql_fetch_fields(res);
                    while (MYSQL_ROW r = mysql_fetch_row(res)) {
                        unsigned long* len = mysql_fetch_lengths(res);
                        Row row;
                        for (unsigned int i = 0; i < nf; ++i)
                            row[fields[i].name] = r[i] ? std::string(r[i], len[i]) : std::string();
                        rows->push_back(std::move(row));
                    }
                }
                mysql_free_result(res);
                firstSet = false;
            } else if (mysql_field_count(conn_) != 0) {
                last_error_ = mysql_error(conn_);
                std::cerr << "[DBConnector] Result fetch error: " << last_error_ << std::endl;
                ok = false;
            }

            int st = mysql_next_result(conn_);
            if (st == 0) continue;  // another result set follows
            if (st > 0) {           // a later statement in the script failed
                last_error_ = mysql_error(conn_);
                std::cerr << "[DBConnector] SQL error in script: " << last_error_ << std::endl;
                ok = false;
            }
            break;
        }
        return ok;
    }
    return false;
}

bool DBConnector::execute(const std::string& sql) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    return runLocked(sql, nullptr);
}

bool DBConnector::executeParameterized(const std::string& sql, const std::vector<std::string>& params) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string bound;
    if (!conn_ || !bindLocked(sql, params, bound)) {
        if (conn_) std::cerr << "[DBConnector] " << last_error_ << " | Query: " << sql.substr(0, 200) << std::endl;
        return false;
    }
    return runLocked(bound, nullptr);
}

std::vector<std::map<std::string, std::string>> DBConnector::query(const std::string& sql) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<Row> rows;
    runLocked(sql, &rows);
    return rows;
}

std::vector<std::map<std::string, std::string>> DBConnector::queryParameterized(
    const std::string& sql, const std::vector<std::string>& params) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<Row> rows;
    std::string bound;
    if (!conn_ || !bindLocked(sql, params, bound)) {
        if (conn_) std::cerr << "[DBConnector] " << last_error_ << " | Query: " << sql.substr(0, 200) << std::endl;
        return rows;
    }
    runLocked(bound, &rows);
    return rows;
}

} // namespace db
