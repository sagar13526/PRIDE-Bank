#pragma once
#include "concurrency/std_thread_compat.h"
#include <atomic>
#include <memory>
#include <thread>

namespace httplib { class Server; }

namespace api {

class DashboardAPI {
public:
    static DashboardAPI& getInstance();

    // Binds the port and serves in a background thread. Returns false if the
    // port could not be bound.
    bool startServer(int port);
    void stopServer();  // stops the listener and joins the thread

private:
    DashboardAPI() = default;
    ~DashboardAPI();
    DashboardAPI(const DashboardAPI&) = delete;
    DashboardAPI& operator=(const DashboardAPI&) = delete;

    std::atomic<bool> running_{false};
    std::unique_ptr<httplib::Server> server_;
    std::unique_ptr<std::thread> server_thread_;
};

}  // namespace api
