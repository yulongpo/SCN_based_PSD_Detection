#include "../application/policy/AlarmHistoryStore.h"

#include <QCoreApplication>
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
        const auto path = (directory.path() + QStringLiteral("/alarm.sqlite")).toStdString();
        AlarmHistoryStore store(path);
        std::string error;
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
        std::cout << "policy storage tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
