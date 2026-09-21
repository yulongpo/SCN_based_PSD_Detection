#pragma once

#include <QString>

/** Runtime-only presentation settings for the live monitor. */
struct MonitorUiConfig
{
    // Detection marks are opt-in on the waterfall. Missing or invalid configuration
    // deliberately keeps them hidden.
    bool waterfallShowDetectionResults{ false };

    static MonitorUiConfig load(const QString& path);
    static QString defaultPath();
};
