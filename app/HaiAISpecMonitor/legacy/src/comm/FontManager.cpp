#include "FontManager.h"

// ============================================================
// 单例
// ============================================================

FontManager &FontManager::instance()
{
    static FontManager s_instance;
    return s_instance;
}

FontManager::FontManager()
    : QObject(nullptr)
{
    // 字体族优先级栈：从最优先到系统默认回退
    // Qt 会按顺序尝试，使用系统中第一个可用的字体
    m_familyStack = QStringList()
        << QStringLiteral("SourceHanSansSC-Medium")   // 思源黑体 Medium（首选）
        << QStringLiteral("思源黑体 Medium")           // 中文回退
        << QStringLiteral("思源黑体 Normal")           // 中文回退 Normal 字重
        << QStringLiteral("思源黑体")                  // 通用思源黑体
        << QStringLiteral("sans-serif");              // 系统默认无衬线字体
}

// ============================================================
// 字体创建
// ============================================================

QFont FontManager::font(int pixelSize, int weight) const
{
    // Qt 5.12 中 setFamily 支持逗号分隔的字体族列表，按顺序回退
    QFont f;
    f.setFamily(m_familyStack.join(','));
    f.setPixelSize(pixelSize);
    f.setWeight(weight);
    f.setStyle(QFont::StyleNormal);
    return f;
}
