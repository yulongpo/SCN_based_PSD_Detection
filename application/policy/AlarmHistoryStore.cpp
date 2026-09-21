#include "AlarmHistoryStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QStringList>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <utility>

namespace scn::application::policy
{

AlarmHistoryStore::AlarmHistoryStore(std::string path) : m_path(std::move(path)) {}

AlarmHistoryStore::~AlarmHistoryStore() { stop(); }

bool AlarmHistoryStore::initializeDatabase(std::string& error)
{
    const auto connectionName = QStringLiteral("scn_alarm_history_%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(QString::fromStdString(m_path));
    if (!db.open()) { error = db.lastError().text().toStdString(); return false; }
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS alarm_events ("
        "event_id TEXT PRIMARY KEY, generation INTEGER, segment INTEGER, signal_id INTEGER,"
        "source_type INTEGER, display_id TEXT, representative_signal_id INTEGER, original_signal_ids TEXT,"
        "current_level INTEGER, highest_level INTEGER, state INTEGER, acknowledged INTEGER,"
        "first_hit_ns INTEGER, last_hit_ns INTEGER, triggered_ns INTEGER, ended_ns INTEGER,"
        "end_reason TEXT, source_name TEXT, policy_version INTEGER, matched_rule_ids TEXT, matched_whitelist_ids TEXT, start_frequency_hz REAL,"
        "end_frequency_hz REAL, bandwidth_hz REAL, signal_level_dbm REAL, cnr_db REAL,"
        "confidence REAL, acknowledgement_note TEXT, acknowledged_at_ms INTEGER, updated_wall_ms INTEGER)"))) {
        error = query.lastError().text().toStdString(); return false;
    }
    const QStringList migrations = {
        QStringLiteral("ALTER TABLE alarm_events ADD COLUMN start_frequency_hz REAL"),
        QStringLiteral("ALTER TABLE alarm_events ADD COLUMN end_frequency_hz REAL"),
        QStringLiteral("ALTER TABLE alarm_events ADD COLUMN bandwidth_hz REAL"),
        QStringLiteral("ALTER TABLE alarm_events ADD COLUMN signal_level_dbm REAL"),
        QStringLiteral("ALTER TABLE alarm_events ADD COLUMN cnr_db REAL"),
        QStringLiteral("ALTER TABLE alarm_events ADD COLUMN confidence REAL"),
        QStringLiteral("ALTER TABLE alarm_events ADD COLUMN acknowledgement_note TEXT"),
        QStringLiteral("ALTER TABLE alarm_events ADD COLUMN acknowledged_at_ms INTEGER")};
    const QStringList extendedMigrations = migrations + QStringList{
        QStringLiteral("ALTER TABLE alarm_events ADD COLUMN policy_version INTEGER"),
        QStringLiteral("ALTER TABLE alarm_events ADD COLUMN matched_whitelist_ids TEXT"),
        QStringLiteral("ALTER TABLE alarm_events ADD COLUMN source_type INTEGER"),
        QStringLiteral("ALTER TABLE alarm_events ADD COLUMN display_id TEXT"),
        QStringLiteral("ALTER TABLE alarm_events ADD COLUMN representative_signal_id INTEGER"),
        QStringLiteral("ALTER TABLE alarm_events ADD COLUMN original_signal_ids TEXT")};
    for (const auto& migration : extendedMigrations) query.exec(migration);
    return true;
}

bool AlarmHistoryStore::start(std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_started) return true;
    if (m_path.empty()) { error = "告警历史路径为空。"; return false; }
    // CPU-only pipeline tests can construct SessionPipeline without a Qt event
    // application. Product sessions always have QCoreApplication and enable SQLite.
    if (!QCoreApplication::instance()) return true;
    QDir().mkpath(QFileInfo(QString::fromStdString(m_path)).absolutePath());
    m_stopping = false; m_started = true;
    m_thread = std::thread([this] { run(); });
    return true;
}

void AlarmHistoryStore::enqueue(const std::vector<AlarmEventChange>& changes)
{
    if (changes.empty()) return;
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_started || m_stopping) return;
    constexpr std::size_t maxQueue = 4096;
    for (const auto& change : changes) {
        m_space.wait(lock, [this] { return m_stopping || m_queue.size() < maxQueue; });
        if (m_stopping) return;
        m_queue.push_back(change);
    }
    lock.unlock();
    m_changed.notify_one();
}

void AlarmHistoryStore::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_started) return;
        m_stopping = true;
    }
    m_changed.notify_one();
    m_space.notify_all();
    if (m_thread.joinable()) m_thread.join();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_started = false;
}

std::string AlarmHistoryStore::lastError() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_error;
}

void AlarmHistoryStore::run()
{
    std::string error;
    if (!initializeDatabase(error)) {
        std::lock_guard<std::mutex> lock(m_mutex); m_error = std::move(error);
    }
    const auto connectionName = QStringLiteral("scn_alarm_history_%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    auto db = QSqlDatabase::database(connectionName);
    while (true) {
        AlarmEventChange change;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_changed.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
            if (m_queue.empty() && m_stopping) break;
            change = std::move(m_queue.front()); m_queue.pop_front();
            m_space.notify_one();
        }
        if (!db.isOpen()) continue;
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "INSERT INTO alarm_events(event_id,generation,segment,source_type,display_id,signal_id,representative_signal_id,original_signal_ids,current_level,highest_level,state,"
            "acknowledged,first_hit_ns,last_hit_ns,triggered_ns,ended_ns,end_reason,source_name,policy_version,matched_rule_ids,matched_whitelist_ids,"
            "start_frequency_hz,end_frequency_hz,bandwidth_hz,signal_level_dbm,cnr_db,confidence,"
            "acknowledgement_note,acknowledged_at_ms,updated_wall_ms) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
            "ON CONFLICT(event_id) DO UPDATE SET current_level=excluded.current_level,"
            "highest_level=excluded.highest_level,state=excluded.state,acknowledged=excluded.acknowledged,"
            "source_type=excluded.source_type,display_id=excluded.display_id,"
            "representative_signal_id=excluded.representative_signal_id,original_signal_ids=excluded.original_signal_ids,"
            "last_hit_ns=excluded.last_hit_ns,ended_ns=excluded.ended_ns,end_reason=excluded.end_reason,"
            "policy_version=excluded.policy_version,matched_rule_ids=excluded.matched_rule_ids,"
            "matched_whitelist_ids=excluded.matched_whitelist_ids,start_frequency_hz=excluded.start_frequency_hz,"
            "end_frequency_hz=excluded.end_frequency_hz,bandwidth_hz=excluded.bandwidth_hz,"
            "signal_level_dbm=excluded.signal_level_dbm,cnr_db=excluded.cnr_db,confidence=excluded.confidence,"
            "acknowledgement_note=excluded.acknowledgement_note,acknowledged_at_ms=excluded.acknowledged_at_ms,"
            "updated_wall_ms=excluded.updated_wall_ms"));
        const auto& event = change.event;
        QStringList ids; for (const auto id : event.matchedRuleIds) ids << QString::number(id);
        QStringList originalIds; for (const auto id : event.originalSignalIds) originalIds << QString::number(id);
        query.addBindValue(QString::fromStdString(event.eventId));
        query.addBindValue(static_cast<qint64>(event.generation));
        query.addBindValue(static_cast<qint64>(event.segment));
        query.addBindValue(static_cast<int>(event.source));
        query.addBindValue(QString::fromStdString(event.displayId));
        query.addBindValue(event.signalId);
        query.addBindValue(event.representativeSignalId);
        query.addBindValue(originalIds.join(QLatin1Char(',')));
        query.addBindValue(static_cast<int>(event.currentLevel));
        query.addBindValue(static_cast<int>(event.highestLevel)); query.addBindValue(static_cast<int>(event.state));
        query.addBindValue(event.acknowledged); query.addBindValue(event.firstHitNs); query.addBindValue(event.lastHitNs);
        query.addBindValue(event.triggeredNs); query.addBindValue(event.endedNs);
        query.addBindValue(QString::fromStdString(event.endReason));
        query.addBindValue(QString::fromStdString(event.sourceName));
        QStringList whitelistIds; for (const auto id : event.matchedWhitelistIds) whitelistIds << QString::number(id);
        query.addBindValue(static_cast<qint64>(event.policyVersion));
        query.addBindValue(ids.join(QLatin1Char(',')));
        query.addBindValue(whitelistIds.join(QLatin1Char(',')));
        query.addBindValue(event.startFrequencyHz); query.addBindValue(event.endFrequencyHz);
        query.addBindValue(event.bandwidthHz); query.addBindValue(event.signalLevelDbm);
        query.addBindValue(event.cnrDb); query.addBindValue(event.confidence);
        query.addBindValue(QString::fromStdString(event.acknowledgementNote));
        query.addBindValue(event.acknowledgedAtMs); query.addBindValue(change.wallClockMs);
        if (!query.exec()) {
            std::lock_guard<std::mutex> lock(m_mutex); m_error = query.lastError().text().toStdString();
        }
    }
    db.close();
    QSqlDatabase::removeDatabase(connectionName);
}

bool AlarmHistoryStore::loadEvents(std::vector<AlarmEvent>& events, std::string& error) const
{
    events.clear();
    if (m_path.empty()) { error = "告警历史路径为空。"; return false; }
    const auto name = QStringLiteral("scn_alarm_history_read_%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(QString::fromStdString(m_path));
    if (!db.open()) { error = db.lastError().text().toStdString(); return false; }
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral(
            "SELECT event_id,generation,segment,source_type,display_id,signal_id,representative_signal_id,original_signal_ids,"
            "current_level,highest_level,state,acknowledged,first_hit_ns,last_hit_ns,triggered_ns,ended_ns,end_reason,source_name,policy_version,matched_rule_ids,matched_whitelist_ids,"
        "start_frequency_hz,end_frequency_hz,bandwidth_hz,signal_level_dbm,cnr_db,confidence,"
        "acknowledgement_note,acknowledged_at_ms FROM alarm_events ORDER BY updated_wall_ms DESC"))) {
        error = query.lastError().text().toStdString();
        db.close(); QSqlDatabase::removeDatabase(name); return false;
    }
    while (query.next()) {
        AlarmEvent event;
        event.eventId = query.value(0).toString().toStdString();
        event.generation = query.value(1).toULongLong();
        event.segment = query.value(2).toULongLong();
        event.source = static_cast<PolicySignalSource>(query.value(3).toInt());
        event.displayId = query.value(4).toString().toStdString();
        event.signalId = query.value(5).toLongLong();
        event.representativeSignalId = query.value(6).toLongLong();
        const auto originalIds = query.value(7).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const auto& id : originalIds) event.originalSignalIds.push_back(id.toLongLong());
        event.currentLevel = static_cast<AlarmLevel>(query.value(8).toInt());
        event.highestLevel = static_cast<AlarmLevel>(query.value(9).toInt());
        event.state = static_cast<AlarmState>(query.value(10).toInt());
        event.acknowledged = query.value(11).toBool();
        event.firstHitNs = query.value(12).toLongLong();
        event.lastHitNs = query.value(13).toLongLong();
        event.triggeredNs = query.value(14).toLongLong();
        event.endedNs = query.value(15).toLongLong();
        event.endReason = query.value(16).toString().toStdString();
        event.sourceName = query.value(17).toString().toStdString();
        event.policyVersion = query.value(18).toULongLong();
        const auto ids = query.value(19).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const auto& id : ids) event.matchedRuleIds.push_back(id.toLongLong());
        const auto whitelistIds = query.value(20).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const auto& id : whitelistIds) event.matchedWhitelistIds.push_back(id.toLongLong());
        event.startFrequencyHz = query.value(21).toDouble();
        event.endFrequencyHz = query.value(22).toDouble();
        event.bandwidthHz = query.value(23).toDouble();
        event.signalLevelDbm = query.value(24).toFloat();
        event.cnrDb = query.value(25).toFloat();
        event.confidence = query.value(26).toFloat();
        event.acknowledgementNote = query.value(27).toString().toStdString();
        event.acknowledgedAtMs = query.value(28).toLongLong();
        events.push_back(std::move(event));
    }
    db.close();
    QSqlDatabase::removeDatabase(name);
    return true;
}

} // namespace scn::application::policy
