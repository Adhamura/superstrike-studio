// Private App state shared by app.cpp (logic) and window.cpp (window chrome).
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include "app.hpp"

struct Job {
    std::function<void(Device&)> work;
    std::function<void()> done;
    std::function<void(const std::string&)> fail;
    bool connect = false;  // a connect job runs without a device and opens it
};

struct App::Impl {
    // worker (device lives only on the worker thread)
    std::thread worker;
    std::mutex mu;
    std::condition_variable cv;
    std::deque<Job> jobs;
    std::unique_ptr<Device> dev;
    std::atomic<bool> connecting{false};
    std::atomic<double> last_input{0};
    bool backed_up = false;
    int poll_n = 0;

    // window chrome (window.cpp)
    AdwWindowTitle* title = nullptr;
    AdwBanner* banner = nullptr;
    GtkWidget* save_btn = nullptr;
    GtkWidget* revert_btn = nullptr;
    GtkWidget* savebar = nullptr;
    GtkWidget* savebar_text = nullptr;
    GtkWidget* status_dot = nullptr;
    GtkWidget* status_text = nullptr;
    GtkWidget* battery = nullptr;  // own line under the status, never cut off
    GtkWidget* device_name = nullptr;
    GtkWidget* profile_dd = nullptr;
    GtkWidget* nav = nullptr;
    GtkWidget* stack = nullptr;
    GtkWidget* split = nullptr;
    GtkWidget* content = nullptr;
    std::vector<std::pair<std::string, std::string>> pages;  // name, title
    bool filling = false;
};

void window_build(App& app, GtkApplication* gapp);  // window.cpp
void app_start(App& app);                           // app.cpp: worker, input, polling, connect
void app_stop(App& app);
