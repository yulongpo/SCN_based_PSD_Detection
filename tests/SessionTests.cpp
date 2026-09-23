#include "../application/MonitoringSession.h"
#include "../algorithm/detector/IScnBackend.h"
#include "../source/SourceFactory.h"
#include "../source/FileSource/FileSource.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " CHECK " #expression); } while(false)
namespace
{
std::atomic<unsigned> inferenceCalls{0};
class TestBackend final : public scn::algorithm::IScnBackend
{
public:
    bool initialize(const scn::algorithm::DetectorConfig&, std::string&) override { return true; }
    bool infer(const std::vector<float>&, scn::algorithm::ScnModelOutput& output, std::string&) override
    {
        // Deliberately slower than the 1ms FILE producer to exercise real backpressure.
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ++inferenceCalls;
        output.heatmap.assign(8192, 0); output.bandwidth.assign(8192, 0); output.offset.assign(8192, 0);
        return true;
    }
    std::string modelInfo() const override { return "Qt session test-only backend"; }
};
template<class Predicate> bool waitFor(Predicate ready, int timeoutMs = 5000)
{
    if (ready()) return true;
    QEventLoop loop;
    QTimer poll, timeout;
    poll.setInterval(1); timeout.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(); timeout.start(timeoutMs); loop.exec();
    return ready();
}
void pumpFor(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
struct Fixture
{
    std::filesystem::path directory, file;
    explicit Fixture(unsigned frames)
    {
        directory = std::filesystem::temp_directory_path() / ("scn_session_" +
            std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        CHECK(std::filesystem::create_directory(directory));
        file = directory / "test_Fc=1000_Bw=512_Rbw=1_Reflevel=-20_SpectrumLen=512.dat";
        std::ofstream stream(file, std::ios::binary);
        const std::vector<float> powers(512, -100);
        for (unsigned i = 0; i < frames; ++i)
            stream.write(reinterpret_cast<const char*>(powers.data()), powers.size() * sizeof(float));
        CHECK(stream.good());
    }
    ~Fixture() { std::error_code error; std::filesystem::remove_all(directory, error); }
    scn::source::SourceConfig config(bool loop) const
    {
        scn::source::SourceConfig c; c.kind = scn::algorithm::SourceKind::File;
        c.filePath = file.u8string(); c.frameRateHz = 1000; c.loopFile = loop; return c;
    }
};
void backpressureAndEof()
{
    Fixture fixture(12);
    scn::application::MonitoringSession session;
    scn::algorithm::DisplaySnapshotPtr latest;
    QString state, error;
    QObject::connect(&session, &scn::application::MonitoringSession::snapshotReady,
        &session, [&](const auto& snapshot) { latest = snapshot; });
    QObject::connect(&session, &scn::application::MonitoringSession::stateChanged,
        &session, [&](const auto& value) { state = value; });
    QObject::connect(&session, &scn::application::MonitoringSession::errorOccurred,
        &session, [&](const auto& value) { error = value; });
    inferenceCalls = 0;
    session.configureDetection({}); session.configure(fixture.config(false)); session.start();
    CHECK(waitFor([&] { return state == QStringLiteral("Stopped (file completed)") &&
        latest && latest->detection.sequence == 12; }));
    CHECK(error.isEmpty() && latest->droppedFrames == 0 && inferenceCalls == 24);
    CHECK(latest->detection.accumulatedFrames == 12 && latest->detection.diagnostics.completedCount == 12);
    CHECK(latest->frame.timestampNs == 11000000 && latest->detection.timestampNs == 11000000);
}
void pauseAcrossFileLoops()
{
    Fixture fixture(1);
    scn::application::MonitoringSession session;
    scn::algorithm::DisplaySnapshotPtr latest;
    QString state;
    QObject::connect(&session, &scn::application::MonitoringSession::snapshotReady,
        &session, [&](const auto& snapshot) { latest = snapshot; });
    QObject::connect(&session, &scn::application::MonitoringSession::stateChanged,
        &session, [&](const auto& value) { state = value; });
    session.configureDetection({}); session.configure(fixture.config(true)); session.start();
    CHECK(waitFor([&] { return latest && state == QStringLiteral("Running"); }));
    for (int i = 0; i < 4; ++i) {
        session.pause();
        CHECK(waitFor([&] { return state == QStringLiteral("Paused"); }));
        pumpFor(80); // Permit the at-most-one in-flight frame and its publication to settle.
        CHECK(latest);
        const auto epoch = latest->detection.generation;
        pumpFor(80);
        CHECK(state == QStringLiteral("Paused") && latest->detection.generation == epoch);
        session.resume();
        CHECK(waitFor([&] { return state == QStringLiteral("Running") &&
            latest && latest->detection.generation > epoch; }));
    }
    session.stop();
    CHECK(waitFor([&] { return state == QStringLiteral("Stopped"); }));
    const auto epoch = latest->detection.generation;
    pumpFor(80);
    CHECK(latest->detection.generation == epoch);
}
}
// The CPU integration executable overrides only the TensorRT backend. FILE
// source construction uses the production factory, which is safe and avoids
// duplicating the production createSource symbol in this test binary.
namespace scn::algorithm
{
std::unique_ptr<IScnBackend> createTensorRtScnBackend() { return std::make_unique<TestBackend>(); }
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        backpressureAndEof(); pauseAcrossFileLoops();
        std::cout << "Qt FILE session: passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
