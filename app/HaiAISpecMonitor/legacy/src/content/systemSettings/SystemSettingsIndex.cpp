#include "SystemSettingsIndex.h"
#include "showSetting/ShowSetting.h"
#include "storageSetting/StorageSetting.h"
#include "alertSetting/AlertSetting.h"
#include "pushSetting/PushSetting.h"
#include "logSetting/LogSetting.h"
#include "helpSetting/HelpSetting.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/FontManager.h"
#include "comm/ThemeManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>

SettingsNavButton::SettingsNavButton(const QString &text, QWidget *parent)
    : QToolButton(parent)
{
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SettingsNavButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const qreal w = width();
    const qreal h = height();

    // 圆角半径 12px（设计稿 2x 基准）
    const qreal radius = DPR_REAL(12.0 * scale, dpr);

    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, w, h), radius, radius);

    if (isChecked()) {
        // 选中状态：使用 primary_bg.png 作为背景
        QPixmap bgPix(":/button/primary_bg.png");
        if (!bgPix.isNull()) {
            bgPix.setDevicePixelRatio(dpr);
            QPixmap scaled = bgPix.scaled(QSize(static_cast<int>(w * dpr), static_cast<int>(h * dpr)),
                                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            scaled.setDevicePixelRatio(dpr);
            painter.save();
            painter.setClipPath(path);
            painter.drawPixmap(0, 0, scaled);
            painter.restore();
        } else {
            painter.fillPath(path, QColor("#0A8CFE"));
        }
    } else {
        // 未选中状态：深色背景 + 边框线
        painter.fillPath(path, QColor(24, 27, 37));
        const qreal borderW = DPR_REAL(1.0 * scale, dpr);
        painter.setPen(QPen(QColor(60, 65, 80), borderW));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }

    // 绘制文字
    QFont font = FontManager::instance().font(DPR_INT(14 * scale, dpr, 10), QFont::Bold);
    painter.setFont(font);
    painter.setPen(isChecked() ? QColor(222, 239, 255) : QColor(163, 162, 164));
    painter.drawText(rect(), Qt::AlignCenter, text());
}


SystemSettingsIndex::SystemSettingsIndex(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    initUI();
}

int SystemSettingsIndex::minimumContentWidth() const
{
    const auto &ss = ScreenScale::instance();
    return static_cast<int>(DPR_REAL(600.0 * ss.scale(), ss.dpr()));
}

void SystemSettingsIndex::setSignalAlarmRules(SignalAlarmRules rules)
{
    m_alertPage->setSignalAlarmRules(rules);
}

void SystemSettingsIndex::setSignalWhiteLists(SignalWhitelists whiteLists)
{
    m_alertPage->setSignalWhiteLists(whiteLists);
}

void SystemSettingsIndex::setSignalRepo(const std::map<std::string, std::string>& params)
{
    m_storagePage->setSignalRepo(params);
}

void SystemSettingsIndex::alarmRuleOpResp(const SignalAlarmRuleOpResp& response)
{
    m_alertPage->alarmRuleOpResp(response);
}

void SystemSettingsIndex::whiteListsOpResp(const SignalWhitelistOpResp& response)
{
    m_alertPage->whiteListsOpResp(response);
}

void SystemSettingsIndex::slotResponse(haiq::GuiResponseData& responseData)
{
    m_storagePage->slotResponse(responseData);
}

void SystemSettingsIndex::slotListDbRecord(bool flag)
{
    if (m_storagePage) {
        m_storagePage->slotListDbRecord(flag);
    }
}

void SystemSettingsIndex::initUI()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    // 按钮宽 82px、高 32px、间距 8px（设计稿 2x 基准）
    const int btnWidth   = DPR_INT(82 * scale, dpr, 60);
    const int btnHeight  = DPR_INT(32 * scale, dpr, 24);
    const int btnSpacing = DPR_INT(8 * scale, dpr, 4);
    const int padInt     = DPR_INT(12 * scale, dpr, 6);

    // 整体垂直布局：上部按钮栏 + 下部内容区
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(padInt, padInt, padInt, padInt);
    mainLayout->setSpacing(DPR_INT(8 * scale, dpr, 4));

    // ---- 顶部按钮导航栏 ----
    m_navWidget = new QWidget(this);
    m_navWidget->setAttribute(Qt::WA_StyledBackground, true);

    auto *navLayout = new QHBoxLayout(m_navWidget);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(btnSpacing);

    // 创建按钮组（互斥选择）
    auto *btnGroup = new QButtonGroup(this);
    btnGroup->setExclusive(true);

    // 6 个导航按钮
    const QStringList btnTexts = {
        QStringLiteral("显示"),         // 显示
        QStringLiteral("存储"),         // 存储
        QStringLiteral("规则"),         // 规则
        QStringLiteral("推送"),         // 推送
        QStringLiteral("日志"),         // 日志
        QStringLiteral("帮助")          // 帮助
    };

    for (int i = 0; i < btnTexts.size(); ++i) {
        auto *btn = createNavBtn(btnTexts.at(i), btnGroup, i);
        btn->setFixedSize(btnWidth, btnHeight);
        navLayout->addWidget(btn);
    }
    navLayout->addStretch();

    // ---- 下方内容堆栈 ----
    m_contentStack = new QStackedWidget(this);

    // 页面 0：显示设置
    m_showPage = new ShowSetting(m_contentStack);
    m_contentStack->addWidget(m_showPage);

    // 页面 1：存储策略
    m_storagePage = new StorageSetting(m_contentStack);
    m_contentStack->addWidget(m_storagePage);

    // 页面 2：告警规则
    m_alertPage = new AlertSetting(m_contentStack);
    m_contentStack->addWidget(m_alertPage);

    // 页面 3：推送
    m_pushPage = new PushSetting(m_contentStack);
    m_contentStack->addWidget(m_pushPage);

    // 页面 4：日志
    m_logPage = new LogSetting(m_contentStack);
    m_contentStack->addWidget(m_logPage);

    // 页面 5：帮助
    m_helpPage = new HelpSetting(m_contentStack);
    m_contentStack->addWidget(m_helpPage);

    // 默认选中第一个按钮（显示）
    btnGroup->button(0)->setChecked(true);
    m_contentStack->setCurrentIndex(0);

    // 按钮点击切换页面
    connect(btnGroup, QOverload<int>::of(&QButtonGroup::buttonClicked),
            m_contentStack, &QStackedWidget::setCurrentIndex);
    // 通信连接
    connect(m_alertPage, &AlertSetting::alarmRuleOpReq, this, &SystemSettingsIndex::alarmRuleOpReq);
    connect(m_alertPage, &AlertSetting::whiteListsOpReq, this, &SystemSettingsIndex::whiteListsOpReq);
    // 存储策略参数下发请求转发
    connect(m_storagePage, &StorageSetting::signalRequest, this, &SystemSettingsIndex::signalRequest);
    // 存储路径下发成功转发（供上层同步更新回放页面存储路径）
    connect(m_storagePage, &StorageSetting::signalPathChanged, this, &SystemSettingsIndex::signalPathChanged);

    mainLayout->addWidget(m_navWidget);
    // 内容堆栈占用导航栏以下的全部剩余空间（拉伸因子 1）
    mainLayout->addWidget(m_contentStack, 1);
}

SettingsNavButton* SystemSettingsIndex::createNavBtn(const QString &text, QButtonGroup *group, int id)
{
    auto *btn = new SettingsNavButton(text, this);
    group->addButton(btn, id);
    return btn;
}
