#pragma once

#include <QFont>
#include <QObject>
#include <QString>
#include <QStringList>

/**
 * FontManager — 统一字体管理器（单例）
 *
 * 职责：
 *   - 提供全局统一的字体族栈：思源黑体 → 系统回退
 *   - 按像素大小返回配置好的 QFont 对象
 *   - 避免各处硬编码字体名称，方便统一更换
 *
 * 字体族优先级：
 *   SourceHanSansSC-Medium → 思源黑体 Medium → 思源黑体 Normal →
 *   思源黑体 → sans-serif（系统默认）
 *
 * 使用方式：
 *   QFont font = FontManager::instance().font(12);           // 12px 常规
 *   QFont bold = FontManager::instance().font(14, QFont::Bold); // 14px 加粗
 */
class FontManager : public QObject
{
    Q_OBJECT

public:
    /// 返回单例引用
    static FontManager &instance();

    /**
     * 获取指定像素大小的 QFont
     * @param pixelSize  字体像素大小
     * @param weight     字体粗细（默认 QFont::Normal）
     * @return 配置好字体族的 QFont 对象
     */
    QFont font(int pixelSize, int weight = QFont::Normal) const;

    /// 返回当前字体族栈（只读）
    const QStringList &familyStack() const { return m_familyStack; }

private:
    FontManager();
    ~FontManager() override = default;
    FontManager(const FontManager &) = delete;
    FontManager &operator=(const FontManager &) = delete;

    QStringList m_familyStack;  ///< 字体族优先级列表
};
