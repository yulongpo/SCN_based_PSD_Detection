#include "ThemeManager.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

// ============================================================
// 单例
// ============================================================

ThemeManager &ThemeManager::instance()
{
    static ThemeManager s_instance;
    return s_instance;
}

ThemeManager::ThemeManager()
    : QObject(nullptr)
    , m_nightMode(true)
{
    loadConfig();
}

// ============================================================
// 配置加载
// ============================================================

bool ThemeManager::loadConfig()
{
    QFile file(":/theme_config.json");
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        qWarning(u8"ThemeManager: 无法打开配置文件 :/theme_config.json");
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning(u8"ThemeManager: JSON 解析错误: %s", qPrintable(parseError.errorString()));
        return false;
    }

    if (!doc.isObject()) {
        qWarning(u8"ThemeManager: JSON 根节点不是对象");
        return false;
    }

    m_config = doc.object();
    return true;
}

// ============================================================
// 主题切换
// ============================================================

void ThemeManager::setNightMode(bool night)
{
    if (m_nightMode == night)
        return;

    m_nightMode = night;
    emit themeChanged(night);
}

// ============================================================
// 值查询 — QColor
// ============================================================

QColor ThemeManager::color(const QString &dotPath) const
{
    return color(dotPath, m_nightMode);
}

QColor ThemeManager::color(const QString &dotPath, bool night) const
{
    QJsonValue val = resolveValue(dotPath, night);
    if (val.isUndefined() || val.isNull())
        return QColor();

    return parseColor(val.toString());
}

// ============================================================
// 值查询 — 原始 CSS 字符串
// ============================================================

QString ThemeManager::colorString(const QString &dotPath) const
{
    QJsonValue val = resolveValue(dotPath, m_nightMode);
    if (val.isUndefined() || val.isNull())
        return QString();

    return val.toString();
}

// ============================================================
// 值查询 — 整数
// ============================================================

int ThemeManager::intValue(const QString &dotPath, int defaultValue) const
{
    QJsonValue val = resolveValue(dotPath, m_nightMode);
    if (val.isUndefined() || val.isNull())
        return defaultValue;

    return val.toInt(defaultValue);
}

// ============================================================
// 按点分路径解析（含 dark/light 自动分流）
// ============================================================

QJsonValue ThemeManager::resolveValue(const QString &dotPath, bool night) const
{
    if (m_config.isEmpty())
        return QJsonValue();

    // 用 '.' 分割路径，第一部分为顶层 section 名
    QStringList parts = dotPath.split('.');
    if (parts.isEmpty())
        return QJsonValue();

    const QString section = parts.first();
    if (!m_config.contains(section))
        return QJsonValue();

    QJsonObject sectionObj = m_config[section].toObject();

    // 根据 night 选择对应的主题分支（dark / light）
    const QString themeKey = night ? QStringLiteral("dark") : QStringLiteral("light");
    if (!sectionObj.contains(themeKey) || !sectionObj[themeKey].isObject())
        return QJsonValue();

    // 剩余路径逐层遍历
    QStringList remaining = parts.mid(1);
    if (remaining.isEmpty())
        return QJsonValue();

    QJsonObject themeObj = sectionObj[themeKey].toObject();
    QJsonValue val = themeObj;
    for (const QString &part : remaining) {
        if (!val.isObject())
            return QJsonValue();
        val = val.toObject().value(part);
        if (val.isUndefined() || val.isNull())
            return QJsonValue();
    }
    return val;
}

// ============================================================
// CSS 颜色字符串 → QColor 解析
// ============================================================

QColor ThemeManager::parseColor(const QString &str)
{
    if (str.isEmpty())
        return QColor();

    const QString s = str.trimmed();

    // ---- 1) #RRGGBB / #RGB ----
    if (s.startsWith('#')) {
        return QColor(s);
    }

    // ---- 2) rgba(R, G, B, A) ----
    static const QRegularExpression rgbaRe(
        "rgba\\s*\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\)",
        QRegularExpression::CaseInsensitiveOption);
    auto rgbaMatch = rgbaRe.match(s);
    if (rgbaMatch.hasMatch()) {
        const int r = rgbaMatch.captured(1).toInt();
        const int g = rgbaMatch.captured(2).toInt();
        const int b = rgbaMatch.captured(3).toInt();
        const int a = rgbaMatch.captured(4).toInt();
        return QColor(r, g, b, a);
    }

    // ---- 3) rgb(R, G, B) ----
    static const QRegularExpression rgbRe(
        "rgb\\s*\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\)",
        QRegularExpression::CaseInsensitiveOption);
    auto rgbMatch = rgbRe.match(s);
    if (rgbMatch.hasMatch()) {
        const int r = rgbMatch.captured(1).toInt();
        const int g = rgbMatch.captured(2).toInt();
        const int b = rgbMatch.captured(3).toInt();
        return QColor(r, g, b);
    }

    // ---- 4) 尝试直接让 QColor 解析 ----
    QColor fallback(s);
    if (fallback.isValid())
        return fallback;

    qWarning(u8"ThemeManager: 无法解析颜色字符串 \"%s\"", qPrintable(s));
    return QColor();
}

// ============================================================
// QSS 模板占位符解析
// ============================================================

QString ThemeManager::resolveStyleSheet(const QString &templateText) const
{
    if (templateText.isEmpty())
        return QString();

    QString result = templateText;

    // 匹配 {{section.key}} 或 {{section.nested.key}} 等点分路径占位符
    static const QRegularExpression placeholderRe(
        "\\{\\{([a-zA-Z0-9_.]+)\\}\\}");

    int pos = 0;
    QRegularExpressionMatch match;

    // 使用迭代替换：从前往后逐个匹配，每次替换后调整搜索位置
    while ((match = placeholderRe.match(result, pos)).hasMatch()) {
        const QString key     = match.captured(1);       // "window.backgroundColor"
        const QString rawRef  = match.captured(0);       // "{{window.backgroundColor}}"
        const QString value   = colorString(key);

        if (value.isEmpty()) {
            qWarning(u8"ThemeManager::resolveStyleSheet: 无法解析占位符 \"%s\"，将保留原样",
                     qPrintable(rawRef));
            pos = match.capturedEnd();
        } else {
            result.replace(match.capturedStart(), match.capturedLength(), value);
            // 替换后，从替换内容的末尾继续搜索，避免重复匹配替换结果中的内容
            pos = match.capturedStart() + value.length();
        }
    }

    return result;
}
