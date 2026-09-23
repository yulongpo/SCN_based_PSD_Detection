#include "../application/policy/AlarmHistoryStore.h"
#include "../application/policy/PolicyRepository.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

#define CHECK(value) do { if (!(value)) throw std::runtime_error("CHECK failed: " #value); } while (false)

using namespace scn::application::policy;

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try {
        QTemporaryDir directory;
        CHECK(directory.isValid());

        const auto configPath = directory.filePath(QStringLiteral("policy.json"));
        QFile legacyConfig(configPath);
        CHECK(legacyConfig.open(QIODevice::WriteOnly));
        const QByteArray legacyJson = R"JSON({
            "version": 1,
            "whitelists": [{
                "id": 1, "name": "legacy", "enabled": true,
                "startFrequencyHz": 100.5, "endFrequencyHz": 200.5
            }],
            "alarmRules": [{
                "id": 2, "name": "legacy rule", "enabled": true,
                "startFrequencyHz": 300.5, "endFrequencyHz": 400.5,
                "minBandwidthHz": 1.5, "maxBandwidthHz": 0
            }]
        })JSON";
        CHECK(legacyConfig.write(legacyJson) == legacyJson.size());
        legacyConfig.close();

        PolicyConfig migratedConfig;
        std::string error;
        CHECK(PolicyRepository::load(configPath.toStdString(), migratedConfig, error));
        CHECK(migratedConfig.whitelists.front().startFrequencyHz == 101);
        CHECK(migratedConfig.whitelists.front().endFrequencyHz == 201);
        CHECK(migratedConfig.alarmRules.front().minBandwidthHz == 2);

        // New integer-Hz JSON must round-trip exact int64 values beyond the
        // precision limit of double-backed numeric conversions.
        migratedConfig.whitelists.front().startFrequencyHz = 9007199254740993LL;
        migratedConfig.whitelists.front().endFrequencyHz = 9007199254740995LL;
        CHECK(PolicyRepository::save(configPath.toStdString(), migratedConfig, error));
        PolicyConfig exactConfig;
        CHECK(PolicyRepository::load(configPath.toStdString(), exactConfig, error));
        CHECK(exactConfig.whitelists.front().startFrequencyHz == 9007199254740993LL);
        CHECK(exactConfig.whitelists.front().endFrequencyHz == 9007199254740995LL);

        const auto path = (directory.path() + QStringLiteral("/alarm.sqlite")).toStdString();
        AlarmHistoryStore store(path);
        CHECK(store.start(error));
        AlarmEventChange change;
        change.kind = AlarmEventChange::Kind::Created;
        change.event.eventId = "1-1-1";
        change.event.generation = 1;
        change.event.segment = 1;
        change.event.signalId = 7;
        change.event.currentLevel = AlarmLevel::Critical;
        change.event.highestLevel = AlarmLevel::Critical;
        change.event.state = AlarmState::Active;
        change.event.startFrequencyHz = 100.0;
        change.event.endFrequencyHz = 200.0;
        change.event.rawStartFrequencyHz = 92.0;
        change.event.rawEndFrequencyHz = 207.0;
        change.event.stableStartFrequencyHz = 98.0;
        change.event.stableEndFrequencyHz = 203.0;
        change.event.boundaryState = scn::algorithm::BoundaryState::PendingChange;
        change.event.hasBoundaryMetadata = true;
        change.event.pendingBoundaryCount = 2;
        change.event.requiredBoundaryCount = 3;
        change.event.measurementBranch = scn::algorithm::SpectrumBranch::Maximum;
        change.event.cnrDb = 12.0F;
        change.event.sourceName = "TEST";
        store.enqueue({change});
        store.stop();

        AlarmHistoryStore reader(path);
        std::vector<AlarmEvent> events;
        CHECK(reader.loadEvents(events, error));
        CHECK(events.size() == 1);
        CHECK(events.front().eventId == "1-1-1");
        CHECK(events.front().currentLevel == AlarmLevel::Critical);
        CHECK(events.front().startFrequencyHz == 100.0);
        CHECK(events.front().rawStartFrequencyHz == 92.0);
        CHECK(events.front().stableEndFrequencyHz == 203.0);
        CHECK(events.front().boundaryState == scn::algorithm::BoundaryState::PendingChange);
        CHECK(events.front().hasBoundaryMetadata);
        CHECK(events.front().pendingBoundaryCount == 2 && events.front().requiredBoundaryCount == 3);
        CHECK(events.front().measurementBranch == scn::algorithm::SpectrumBranch::Maximum);
        std::cout << "policy storage tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
