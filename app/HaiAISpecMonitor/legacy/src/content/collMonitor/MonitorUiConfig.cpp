#include "MonitorUiConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

MonitorUiConfig MonitorUiConfig::load(const QString& path)
{
    MonitorUiConfig config;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return config;

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return config;

    const QJsonValue waterfallValue = document.object().value(
        QStringLiteral("waterfall"));
    if (!waterfallValue.isObject()) return config;

    const QJsonValue showValue = waterfallValue.toObject().value(
        QStringLiteral("show_detection_results"));
    if (showValue.isBool())
        config.waterfallShowDetectionResults = showValue.toBool();
    return config;
}

QString MonitorUiConfig::defaultPath()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString installedPath = QDir::cleanPath(appDir.absoluteFilePath(
        QStringLiteral("config/HaiAISpecMonitor/UI.json")));
    if (QFile::exists(installedPath)) return installedPath;

    // Development binaries live in bin/<Config>, while shared runtime
    // configuration lives in bin/config/HaiAISpecMonitor.
    return QDir::cleanPath(appDir.absoluteFilePath(
        QStringLiteral("../config/HaiAISpecMonitor/UI.json")));
}
