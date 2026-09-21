#pragma once

#include "PolicyTypes.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace scn::application::policy
{

class AlarmHistoryStore final
{
public:
    explicit AlarmHistoryStore(std::string path = {});
    ~AlarmHistoryStore();

    bool start(std::string& error);
    void enqueue(const std::vector<AlarmEventChange>& changes);
    void stop();
    std::string lastError() const;
    bool loadEvents(std::vector<AlarmEvent>& events, std::string& error) const;

private:
    void run();
    bool initializeDatabase(std::string& error);

    std::string m_path;
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::condition_variable m_space;
    std::deque<AlarmEventChange> m_queue;
    std::string m_error;
    bool m_stopping = false;
    bool m_started = false;
    std::thread m_thread;
};

} // namespace scn::application::policy
