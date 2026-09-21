#pragma once

#include "../../../application/MonitoringSession.h"

namespace scn::app::controller
{

class MainController final
{
public:
    explicit MainController(application::MonitoringSession& session)
        : m_session(session)
    {
    }

    void configure(const source::SourceConfig& config) { m_session.configure(config); }
    void start() { m_session.start(); }
    void pause() { m_session.pause(); }
    void resume() { m_session.resume(); }
    void stop() { m_session.stop(); }

private:
    application::MonitoringSession& m_session;
};

} // namespace scn::app::controller
