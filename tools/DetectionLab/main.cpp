#include "../../algorithm/DetectionEngine/DetectionEngine.h"
#include "../../application/SourceManager.h"

#include <iostream>

int main()
{
    scn::application::SourceManager sources;
    scn::source::SourceConfig config;
    std::string error;
    if (!sources.configure(config, error) || !sources.start()) {
        std::cerr << "DetectionLab: " << error << '\n';
        return 1;
    }

    scn::algorithm::DetectionEngine engine;
    if (!engine.initialize({})) {
        std::cerr << "DetectionLab: DetectionEngine initialization failed\n";
        return 1;
    }

    for (int i = 0; i < 10; ++i) {
        scn::algorithm::SpectrumFrame frame;
        if (!sources.read(frame)) break;
        const auto result = engine.process(frame);
        std::cout << "frame=" << frame.sequence
                  << " bins=" << frame.powerDb.size()
                  << " detections=" << result.detections.size() << '\n';
    }
    return 0;
}
