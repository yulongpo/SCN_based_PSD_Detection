#pragma once

#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QString>

/**
 * ThemeManager — 统一主题颜色管理器（单例）
 *
 * 职责：
 *   - 从 res/qss/theme_config.json 加载所有颜色配置
 *   - 根据当前暗色/亮色主题自动返回对应颜色值
 *   - 提供 color() / colorString() / intValue() 等便捷查询接口
 *
 * JSON 路径规则：
 *   对于随主题变化的颜色（如窗口背景色），在配置中用 dark/light 子对象分组：
 *       window.dark.backgroundColor
 *       window.light.backgroundColor
 *   此时调用 color("window.backgroundColor") 会根据当前主题自动选择暗色或亮色值。
 *
 *   对于不随主题变化的颜色（如 Toast 图标色），直接放在对应小节下：
 *       toast.info.accentColor
 *   此时调用 color("toast.info.accentColor") 直接返回该值。
 *
 * 使用方式：
 *   auto &tm = ThemeManager::instance();
 *   QColor bg = tm.color("window.backgroundColor");
 *   QString border = tm.colorString("historyTable.borderColor");
 *   int alpha = tm.intValue("historyTable.alternateRowAlpha");
 */
class ThemeManager : public QObject
{
    Q_OBJECT

public:
    /// 返回单例引用
    static ThemeManager &instance();

    /// 设置当前主题（true=暗色 / false=亮色），发射 themeChanged 信号
    void setNightMode(bool night);
    bool isNightMode() const { return m_nightMode; }

    /**
     * 按点分路径查询颜色值，自动根据当前主题解析 dark/light 分支。
     * 示例: "window.backgroundColor", "themeSwitch.trackTop",
     *       "toast.info.accentColor", "sidebar.navButtonActiveColor"
     */
    QColor color(const QString &dotPath) const;

    /**
     * 按点分路径查询颜色值，使用指定的 night 参数覆盖当前主题。
     * 适用于需要同时读取暗/亮两套颜色值进行对比或过渡的场景。
     */
    QColor color(const QString &dotPath, bool night) const;

    /**
     * 返回颜色的原始 CSS 字符串（不解析为 QColor），
     * 适用于需要直接嵌入 QSS 字符串的场景。
     */
    QString colorString(const QString &dotPath) const;

    /**
     * 返回配置中的整数值，路径规则同 color()。
     * 示例: "historyTable.alternateRowAlpha"
     */
    int intValue(const QString &dotPath, int defaultValue = 0) const;

    /**
     * 解析 QSS 模板字符串，将 {{section.key}} 占位符替换为当前主题对应的颜色值。
     *
     * 占位符映射到 theme_config.json 中的颜色值（通过 colorString() 查询），
     * 不随主题变化的属性（如 border: none、padding 等）保留原样。
     *
     * 示例:
     *   模板: "background-color: {{window.backgroundColor}};"
     *   输出: "background-color: #060610;"
     *
     * 若某个占位符无法解析，会输出 qWarning 并保留原占位符文本。
     */
    QString resolveStyleSheet(const QString &templateText) const;

signals:
    /// 主题切换时发射（true=暗色, false=亮色）
    void themeChanged(bool night);

private:
    ThemeManager();
    ~ThemeManager() override = default;
    ThemeManager(const ThemeManager &) = delete;
    ThemeManager &operator=(const ThemeManager &) = delete;

    /// 加载 theme_config.json 并解析到 m_config
    bool loadConfig();

    /// 将 CSS 颜色字符串（#RRGGBB / #RGB / rgb() / rgba()）转为 QColor
    static QColor parseColor(const QString &str);

    /**
     * 按点分路径逐层遍历 JSON 对象并返回值。
     * night 参数显式指定解析时使用的暗色/亮色分支。
     */
    QJsonValue resolveValue(const QString &dotPath, bool night) const;

    QJsonObject m_config;   ///< 完整的配置文件 JSON 对象
    bool m_nightMode;       ///< 当前是否为暗色主题
};
