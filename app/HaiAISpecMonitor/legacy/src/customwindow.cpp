#include "customwindow.h"
#include "controller/Controller.h"
#include "content/ContentPanel.h"
#include "tittle/customtitlebar.h"
#include "widgets/ToastWidget.h"
#include "comm/ThemeManager.h"
#include "widgets/HQMessageBox.h"

#include <QEvent>
#include <QFile>
#include <QApplication>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QScreen>
#include <QShowEvent>
#include <QStyle>

#include "comm/ScreenScale.h"
#include "comm/CommonMacros.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#endif


QString loadStyleSheetText(const QString &resourcePath)
{
    QFile styleFile(resourcePath);
    if (!styleFile.open(QFile::ReadOnly | QFile::Text)) {
        return QString();
    }

    return QString::fromUtf8(styleFile.readAll());
}

CustomWindow::CustomWindow(QWidget *parent)
    : QWidget(parent)
    , m_titleBar(nullptr)
    , m_contentPanel(nullptr)
    , m_layout(nullptr)
    , m_isMaximized(false)
    , m_isNightMode(true)
    , m_windowBackgroundColor(ThemeManager::instance().color("window.backgroundColor"))
    , m_inactiveBorderColor(ThemeManager::instance().color("window.inactiveBorderColor"))
    , m_controller(nullptr)
{
    // 默认隐藏
    this->hide();
    qRegisterMetaType<haiq::GuiResponseData>("haiq::GuiResponseData");
    qRegisterMetaType<SignalAlarmRules>("SignalAlarmRules");
    qRegisterMetaType<SignalAlarmRuleOpResp>("SignalAlarmRuleOpResp");
    qRegisterMetaType<SpectrumFileInfo>("SpectrumFileInfo");
    qRegisterMetaType<SpectrumFileInfos>("SpectrumFileInfos");
    qRegisterMetaType<SignalDetailPerFileQueryResp>("SignalDetailPerFileQueryResp");
    qRegisterMetaType<SpectrumFileDelResp>("SpectrumFileDelResp");
    qRegisterMetaType<SignalDetailPerFileDelResp>("SignalDetailPerFileDelResp");
    qRegisterMetaType<SignalWhitelists>("SignalWhitelists");
    qRegisterMetaType<SignalWhitelistOpResp>("SignalWhitelistOpResp");

    setObjectName("customWindow");
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(true);

    ToastWidget::instance(this); // ToastWidget 需要先初始化，否则无法正常显示

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_titleBar = new CustomTitleBar(this);
    m_layout->addWidget(m_titleBar);

    // 内容区使用 ContentPanel，包含侧边栏导航 + QSplitter + 右侧内容区。
    // 左侧 SideBar 宽度固定 104px（按 dpr 缩放），右侧为中央内容区。
    m_contentPanel = new ContentPanel(this);
    m_layout->addWidget(m_contentPanel, 1);

    connect(m_titleBar, &CustomTitleBar::minimizeClicked, this, &QWidget::showMinimized);
    connect(m_titleBar, &CustomTitleBar::maximizeRestoreClicked, this, [this]() {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
    });
    connect(m_titleBar, &CustomTitleBar::closeClicked, this, [this]()
    {
        HQMessageBox* dialog = HQMessageBox::getInstance();
        dialog->setTitle(QStringLiteral("退出确认"));
        dialog->setMsgInfo(QStringLiteral("确定要退出“智能频谱监测仪”吗？"));
        if (dialog->exec() == QDialog::Accepted)
        {
            this->close();
        }
    });
    // 连接菜单
    connect(m_titleBar, &CustomTitleBar::collectClicked, m_contentPanel, &ContentPanel::collectClicked);
    connect(m_titleBar, &CustomTitleBar::playbackClicked, m_contentPanel, &ContentPanel::playbackClicked);
    connect(m_titleBar, &CustomTitleBar::settingClicked, m_contentPanel, &ContentPanel::systemSettingClicked);
    // 连接通信信号
    QObject::connect(this, &CustomWindow::signalRespToGui, this, &CustomWindow::slotRespToGui, Qt::QueuedConnection);
    QObject::connect(this, &CustomWindow::signalAlarmRules, m_contentPanel, &ContentPanel::setSignalAlarmRules, Qt::QueuedConnection);
    QObject::connect(this, &CustomWindow::signalWhiteLists, m_contentPanel, &ContentPanel::setSignalWhiteLists, Qt::QueuedConnection);
    QObject::connect(m_contentPanel, &ContentPanel::signalRequest, this, &CustomWindow::slotRequest, Qt::UniqueConnection);
    QObject::connect(m_contentPanel, &ContentPanel::alarmRuleOpReq, this, &CustomWindow::alarmRuleOpReq, Qt::UniqueConnection);
    QObject::connect(m_contentPanel, &ContentPanel::whiteListsOpReq, this, &CustomWindow::whiteListsOpReq, Qt::UniqueConnection);
    QObject::connect(this, &CustomWindow::signalAlarmRuleOpResp, m_contentPanel, &ContentPanel::alarmRuleOpResp, Qt::QueuedConnection);
    QObject::connect(this, &CustomWindow::signalWhiteListsOpResp, m_contentPanel, &ContentPanel::whiteListsOpResp, Qt::QueuedConnection);
    QObject::connect(this, &CustomWindow::signalSpectrumFileInfo, m_contentPanel, &ContentPanel::spectrumFileInfo, Qt::QueuedConnection);
    QObject::connect(this, &CustomWindow::signalSpectrumFileInfos, m_contentPanel, &ContentPanel::spectrumFileInfos, Qt::QueuedConnection);
    QObject::connect(this, &CustomWindow::signalSignalDetailPerFileQueryResp, m_contentPanel, &ContentPanel::signalDetailPerFileQueryResp, Qt::QueuedConnection);
    QObject::connect(this, &CustomWindow::signalSpectrumFileDelResp, m_contentPanel, &ContentPanel::spectrumFileDelResp, Qt::QueuedConnection);
    QObject::connect(this, &CustomWindow::signalSignalDetailPerFileDelResp, m_contentPanel, &ContentPanel::signalDelResp, Qt::QueuedConnection);
    QObject::connect(m_contentPanel, &ContentPanel::spectrumFileDelReq, this, &CustomWindow::spectrumFileDelReq, Qt::UniqueConnection);
    QObject::connect(m_contentPanel, &ContentPanel::signalDelReq, this, &CustomWindow::signalDelReq, Qt::UniqueConnection);
    QObject::connect(m_contentPanel, &ContentPanel::signalDetailPerFileQueryReq, this, &CustomWindow::signalDetailPerFileQueryReq, Qt::UniqueConnection);
    QObject::connect(m_contentPanel, &ContentPanel::requestFileListRefresh, this, [this]() {
        if (m_controller == nullptr) return;
        haiq::GuiRequestData requestData;
        requestData._cmdType = haiq::CMDType::CMD_REFRESH_FILE_LIST;
        m_controller->pushCMD(requestData);
    }, Qt::UniqueConnection);

    setWindowTitle(QStringLiteral("智能频谱监测仪"));
    QScreen *screen = QApplication::primaryScreen();
    int w = m_contentPanel->minimumContentWidth();
    if (screen && w > 0) {
        QRect avail = screen->availableGeometry();
        resize(w, avail.height() * 7 / 10);
        move(avail.center() - geometry().center());
    } else {
        resize(1280, 720);
    }

    // 构造函数中不调用showMaximized()，窗口默认隐藏，
    // 等待Controller::showGUI()统一控制显示时机
    loadThemeStyleSheets();
    applyTheme(true);
}

CustomWindow::~CustomWindow()
{
}

int CustomWindow::titleBarHeight() const
{
    return m_titleBar ? m_titleBar->height() : 0;
}

void CustomWindow::setParams(ParamTable& params)
{
    m_contentPanel->setParams(params);
}

void CustomWindow::registerController(Controller* controller)
{
    // 目前不考虑失效问题，控制器生命周期必定比GUI长
    m_controller = controller;
    m_controller->regRespCallBack(std::bind(&CustomWindow::responseCallBack, this, std::placeholders::_1));
}

void CustomWindow::addICDHQSigMF(const HQSigMF& icd)
{
    m_contentPanel->addMFICDData(icd);
}

void CustomWindow::setSignalAlarmRules(const SignalAlarmRules& rules)
{
    // 通过信号转发，这是其他线程来的数据
    emit signalAlarmRules(rules);
}

void CustomWindow::setSignalWhiteLists(const SignalWhitelists& whiteLists)
{
    // 通过信号转发，这是其他线程来的数据
    emit signalWhiteLists(whiteLists);
}

void CustomWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        m_isMaximized = isMaximized();
        m_titleBar->setMaximized(m_isMaximized);
        emit windowStateChanged(windowState());
    } else if (event->type() == QEvent::ActivationChange) {
        updateBorderColor();
    }
    QWidget::changeEvent(event);
}

void CustomWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    // 显式绘制窗口背景，确保整窗底色一致。
    // 仅靠样式表和调色板在无边框窗口下并不总是稳定，因此这里主动填充当前主题背景色。
    QPainter painter(this);
    painter.fillRect(rect(), m_windowBackgroundColor);
}

void CustomWindow::loadThemeStyleSheets()
{
    // 加载 QSS 模板文件，其中颜色值使用 {{section.key}} 占位符。
    // 切主题时通过 ThemeManager::resolveStyleSheet() 将占位符替换为当前主题颜色值，
    // 避免颜色值在 QSS 和 JSON 中双重硬编码。
    // 模板文件只加载一次到内存，后续替换操作在内存中完成，无资源访问抖动。
    m_styleSheetTemplate = loadStyleSheetText(":/style_template.qss");
}

void CustomWindow::applyTheme(bool night)
{
    m_isNightMode = night;
    ThemeManager::instance().setNightMode(night);
    m_windowBackgroundColor = ThemeManager::instance().color("window.backgroundColor");
    m_inactiveBorderColor = ThemeManager::instance().color("window.inactiveBorderColor");

    // 将模板中的 {{section.key}} 占位符替换为当前主题对应的颜色值，
    // ThemeManager::setNightMode() 在上方已调用，此时读取的正是目标主题色。
    if (!m_styleSheetTemplate.isEmpty()) {
        const QString resolvedStyleSheet =
            ThemeManager::instance().resolveStyleSheet(m_styleSheetTemplate);
        setStyleSheet(resolvedStyleSheet);
    }

    QPalette pal = palette();
    pal.setColor(QPalette::Window, m_windowBackgroundColor);
    setPalette(pal);

    if (m_titleBar) {
        m_titleBar->applyThemeAppearance(night);
    }

    if (m_contentPanel) {
        m_contentPanel->update();
    }

    update();

#ifdef Q_OS_WIN
    if (winId()) {
        updateBorderColor();
    }
#endif
}

void CustomWindow::updateBorderColor()
{
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) return;

    COLORREF color;
    if (isActiveWindow()) {
        // Read Windows 10/11 accent color from registry (ABGR format).
        // QPalette::highlight() gives COLOR_HIGHLIGHT, not the accent color.
        DWORD accent = 0xFF0078D4; // default blue fallback
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                L"SOFTWARE\\Microsoft\\Windows\\DWM", 0,
                KEY_READ, &key) == ERROR_SUCCESS) {
            DWORD value = 0, size = sizeof(value);
            if (RegQueryValueExW(key, L"AccentColor", nullptr, nullptr,
                    reinterpret_cast<LPBYTE>(&value),
                    &size) == ERROR_SUCCESS) {
                accent = value;
            }
            RegCloseKey(key);
        }
        // Registry stores 0xAABBGGRR (same byte order as COLORREF + alpha)
        color = RGB(GetRValue(accent), GetGValue(accent), GetBValue(accent));
    } else {
        color = RGB(m_inactiveBorderColor.red(),
                    m_inactiveBorderColor.green(),
                    m_inactiveBorderColor.blue());
    }
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &color, sizeof(color));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE
                 | SWP_NOZORDER | SWP_NOACTIVATE);
#endif
}

void CustomWindow::printLog(haiq::Result result, haiq::CMDType cmdType)
{
    if (result == haiq::Result::Success)
    {
        switch (cmdType)
        {
        case haiq::CMDType::CMD_START:
            LOG_INFO(u8"启动成功");
            ToastWidget::instance()->showInfo(QStringLiteral("启动成功"));
            break;
        case haiq::CMDType::CMD_STOP:
            LOG_INFO(u8"停止监测成功");
            ToastWidget::instance()->showInfo(QStringLiteral("停止监测成功"));
            break;
        case haiq::CMDType::CMD_RESET:
            LOG_INFO(u8"重置成功");
            //ToastWidget::instance()->showInfo("重置成功");
            break;
        case haiq::CMDType::CMD_PARAM:
            LOG_INFO(u8"参数下发成功");
            //ToastWidget::instance()->showInfo("参数下发成功");
            break;
        case haiq::CMDType::CMD_PAUSE:
            LOG_INFO(u8"停止监测成功");
            ToastWidget::instance()->showInfo(QStringLiteral("停止监测成功"));
            break;
        case haiq::CMDType::CMD_RESUME:
            LOG_INFO(u8"启动监测成功");
            ToastWidget::instance()->showInfo(QStringLiteral("启动监测成功"));
            break;
        case haiq::CMDType::CMD_FILE_SIG_SEARCH_REQ:
            LOG_INFO(u8"文件信号查询成功");
            ToastWidget::instance()->showInfo(QStringLiteral("文件信号查询成功"));
            break;
        case haiq::CMDType::CMD_FREQ_FILE_DEL_REQ:
            LOG_INFO(u8"删除频谱文件成功");
            // ToastWidget::instance()->showInfo("删除频谱文件成功");
            break;
        case haiq::CMDType::CMD_RULE_OPER_REQ:
            LOG_INFO(u8"告警规则操作成功");
            ToastWidget::instance()->showInfo(QStringLiteral("告警规则操作成功"));
            break;
        case haiq::CMDType::CMD_REFRESH_FILE_LIST:
            break;
        default:
            LOG_WARN(u8"未知命令响应: {0}", (int)cmdType);
            ToastWidget::instance()->showWarning(QStringLiteral("未知命令响应"));
            break;
        }
    }
    else
    {
        switch (cmdType)
        {
        case haiq::CMDType::CMD_START:
            LOG_ERROR(u8"启动失败");
            ToastWidget::instance()->showError(QStringLiteral("启动失败"));
            break;
        case haiq::CMDType::CMD_STOP:
            LOG_ERROR(u8"停止监测失败");
            ToastWidget::instance()->showError(QStringLiteral("停止监测失败"));
            break;
        case haiq::CMDType::CMD_RESET:
            LOG_INFO(u8"重置失败");
            //ToastWidget::instance()->showError("重置失败");
            break;
        case haiq::CMDType::CMD_PARAM:
            LOG_ERROR(u8"参数下发失败");
            //ToastWidget::instance()->showError("参数下发失败");
            break;
        case haiq::CMDType::CMD_PAUSE:
            LOG_ERROR(u8"停止监测失败");
            ToastWidget::instance()->showError(QStringLiteral("停止监测失败"));
            break;
        case haiq::CMDType::CMD_RESUME:
            LOG_ERROR(u8"启动监测失败");
            ToastWidget::instance()->showError(QStringLiteral("启动监测失败"));
            break;
        case haiq::CMDType::CMD_FILE_SIG_SEARCH_REQ:
            LOG_INFO(u8"文件信号查询失败");
            ToastWidget::instance()->showError(QStringLiteral("文件信号查询失败"));
            break;
        case haiq::CMDType::CMD_FREQ_FILE_DEL_REQ:
            LOG_INFO(u8"删除频谱文件失败");
            // ToastWidget::instance()->showError("删除频谱文件失败");
            break;
        case haiq::CMDType::CMD_RULE_OPER_REQ:
            LOG_INFO(u8"告警规则操作失败");
            ToastWidget::instance()->showError(QStringLiteral("告警规则操作失败"));
            break;
        case haiq::CMDType::CMD_REFRESH_FILE_LIST:
            break;
        default:
            LOG_WARN(u8"未知命令: {0}", (int)cmdType);
            ToastWidget::instance()->showWarning(QStringLiteral("未知命令"));
            break;
        }
    }
}

void CustomWindow::responseCallBack(haiq::GuiResponseData& data)
{
    if (data._cmdType == haiq::CMDType::CMD_RULE_OPER_REQ || data._cmdType == haiq::CMDType::CMD_WHITE_OPER_REQ) return;// 不处理告警规则，这个响应是通过其他方式响应到alarmRuleOpResp
    emit signalRespToGui(data);
}

void CustomWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
#ifdef Q_OS_WIN
    if (winId()) {
        updateBorderColor();
    }
#endif
}

#ifdef Q_OS_WIN

int CustomWindow::getWindowDpi(HWND hwnd)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef UINT (WINAPI *GetDpiForWindow_t)(HWND);
        auto pfn = reinterpret_cast<GetDpiForWindow_t>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (pfn) {
            return static_cast<int>(pfn(hwnd));
        }
    }
    HDC hdc = GetDC(hwnd);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(hwnd, hdc);
    return dpi;
}

int CustomWindow::topResizeHandleHeight(HWND hwnd)
{
    int dpi = getWindowDpi(hwnd);
    return GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi)
         + GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi);
}

bool CustomWindow::isInTitleBar(const LPARAM lParam) const
{
    int physX = GET_X_LPARAM(lParam);
    int physY = GET_Y_LPARAM(lParam);

    // Convert physical screen coords to Qt logical coords
    const qreal dpr = devicePixelRatioF();
    QPoint globalLogical(static_cast<int>(physX / dpr),
                         static_cast<int>(physY / dpr));

    QPoint localPos = m_titleBar->mapFromGlobal(globalLogical);
    if (!m_titleBar->rect().contains(localPos)) {
        return false;
    }

    QWidget *child = m_titleBar->childAt(localPos);
    // 向上遍历：仅交互类控件从拖拽区域排除（按钮、下拉框等），
    // 其余控件（label、分隔线等）保持可拖拽。
    while (child && child != m_titleBar) {
        if (qobject_cast<QPushButton *>(child)
            || qobject_cast<QToolButton *>(child))
            return false;
        child = child->parentWidget();
    }
    return true;
}

#endif

void CustomWindow::slotRequest(haiq::GuiRequestData& requestData)
{
    if (m_controller == nullptr)
    {
        LOG_WARN(u8"未注册控制器");
        ToastWidget::instance()->showInfo(QStringLiteral("未注册控制器"));
        return;
    }
    if (!m_controller->pushCMD(requestData))
    {
        printLog(haiq::Result::Fail, requestData._cmdType);
        haiq::GuiResponseData responseData;
        responseData._result = haiq::Result::Fail;
        responseData._cmdType = requestData._cmdType;
        responseData._uuid = requestData._uuid;
        responseData._reqSrc = requestData._reqSrc; // 回填请求来源，用于区分响应归属
        m_contentPanel->slotResponse(responseData);
    }
}

void CustomWindow::slotRespToGui(haiq::GuiResponseData responseData)
{
    printLog(responseData._result, responseData._cmdType);
    m_contentPanel->slotResponse(responseData);
}

void CustomWindow::alarmRuleOpReq(const SignalAlarmRuleOpReq& request)
{
    haiq::GuiRequestData requestData;
    requestData._cmdType = haiq::CMDType::CMD_RULE_OPER_REQ;
    // 用 request 拷贝构造，避免对象切片导致派生字段（op_type、rule）丢失
    requestData._comData = std::make_shared<SignalAlarmRuleOpReq>(request);
    if (m_controller == nullptr)
    {
        LOG_WARN(u8"未注册控制器");
        ToastWidget::instance()->showInfo(QStringLiteral("未注册控制器"));
        return;
    }
    if (!m_controller->pushCMD(requestData))
    {
        SignalAlarmRuleOpResp resp;
        resp.success = -1;
        m_contentPanel->alarmRuleOpResp(resp);
    }
}

void CustomWindow::whiteListsOpReq(const SignalWhitelistOpReq& request)
{
    haiq::GuiRequestData requestData;
    requestData._cmdType = haiq::CMDType::CMD_WHITE_OPER_REQ;
    // 用 request 拷贝构造，避免对象切片导致派生字段（op_type、item）丢失
    requestData._comData = std::make_shared<SignalWhitelistOpReq>(request);
    if (m_controller == nullptr)
    {
        LOG_WARN(u8"未注册控制器");
        ToastWidget::instance()->showInfo(QStringLiteral("未注册控制器"));
        return;
    }
    if (!m_controller->pushCMD(requestData))
    {
        SignalWhitelistOpResp resp;
        resp.success = -1;
        m_contentPanel->whiteListsOpResp(resp);
    }
}

void CustomWindow::signalDetailPerFileQueryReq(const SignalDetailPerFileQueryReq& req)
{
    haiq::GuiRequestData requestData;
    requestData._cmdType = haiq::CMDType::CMD_FILE_SIG_SEARCH_REQ;
    // 用 req 拷贝构造，避免对象切片导致派生字段丢失
    requestData._comData = std::make_shared<SignalDetailPerFileQueryReq>(req);
    if (m_controller == nullptr)
    {
        LOG_WARN(u8"未注册控制器");
        ToastWidget::instance()->showInfo(QStringLiteral("未注册控制器"));
        return;
    }
    if (!m_controller->pushCMD(requestData))
    {
        SignalDetailPerFileQueryResp resp;
        m_contentPanel->signalDetailPerFileQueryResp(resp);
    }
}

void CustomWindow::spectrumFileDelReq(const SpectrumFileDelReq& req)
{
    haiq::GuiRequestData requestData;
    requestData._cmdType = haiq::CMDType::CMD_FREQ_FILE_DEL_REQ;
    // 用 req 拷贝构造，避免对象切片导致派生字段丢失
    requestData._comData = std::make_shared<SpectrumFileDelReq>(req);
    if (m_controller == nullptr)
    {
        LOG_WARN(u8"未注册控制器");
        ToastWidget::instance()->showInfo(QStringLiteral("未注册控制器"));
        return;
    }
    if (!m_controller->pushCMD(requestData))
    {
        SpectrumFileDelResp resp;
        resp.success = -1;
        m_contentPanel->spectrumFileDelResp(resp);
    }
}

void CustomWindow::alarmRuleOpResp(const SignalAlarmRuleOpResp& response)
{
    emit signalAlarmRuleOpResp(response);
}

void CustomWindow::whiteListsOpResp(const SignalWhitelistOpResp& response)
{
    emit signalWhiteListsOpResp(response);
}

void CustomWindow::spectrumFileInfo(const SpectrumFileInfo& info)
{
    emit signalSpectrumFileInfo(info);
}

void CustomWindow::spectrumFileInfos(const SpectrumFileInfos& infos)
{
    emit signalSpectrumFileInfos(infos);
}

void CustomWindow::signalDetailPerFileQueryResp(const SignalDetailPerFileQueryResp& resp)
{
    // 深拷贝：QueuedConnection 跨线程传递时按值拷贝，默认拷贝构造函数为浅拷贝，
    // 会导致 item/times 指针指向 Controller 的原始内存，Controller 释放后产生悬空指针。
    // !!!记得在使用完成的地方释放!!!
    SignalDetailPerFileQueryResp deepCopy;
    deepCopy.file_id    = resp.file_id;
    deepCopy.signal_num = resp.signal_num;
    deepCopy.item       = nullptr;

    if (resp.signal_num > 0) {
        deepCopy.item = new SignalDetailPerFileQueryResp::SignalItem[resp.signal_num];
        for (int32_t i = 0; i < resp.signal_num; ++i) {
            const auto& src = resp.item[i];
            auto&       dst = deepCopy.item[i];

            // 拷贝基本类型成员
            dst.id           = src.id;
            dst.fc           = src.fc;
            dst.bw           = src.bw;
            dst.carry_type   = src.carry_type;
            dst.alarm_level = src.alarm_level;
            dst.recent_time  = src.recent_time;
            dst.burst_num    = src.burst_num;
            dst.avg_duration = src.avg_duration;

            // 深拷贝 times 数组（长度由 burst_num 决定）
            dst.times = nullptr;
            if (src.burst_num > 0) {
                dst.times = new SignalDetailPerFileQueryResp::TimeRange[src.burst_num];
                for (int32_t j = 0; j < src.burst_num; ++j) {
                    dst.times[j].bgn_time = src.times[j].bgn_time;
                    dst.times[j].end_time = src.times[j].end_time;
                }
            }
        }
    }

    emit signalSignalDetailPerFileQueryResp(deepCopy);
}

void CustomWindow::spectrumFileDelResp(const SpectrumFileDelResp& resp)
{
    emit signalSpectrumFileDelResp(resp);
}

void CustomWindow::signalDelReq(const SignalDetailPerFileDelReq& req)
{
    haiq::GuiRequestData requestData;
    requestData._cmdType = haiq::CMDType::CMD_SIGNAL_DEL_REQ;
    requestData._comData = std::make_shared<SignalDetailPerFileDelReq>(req);
    if (m_controller == nullptr)
    {
        LOG_WARN(u8"未注册控制器");
        ToastWidget::instance()->showInfo(QStringLiteral("未注册控制器"));
        return;
    }
    if (!m_controller->pushCMD(requestData))
    {
        SignalDetailPerFileDelResp resp;
        resp.success = -1;
        m_contentPanel->signalDelResp(resp);
    }
}

void CustomWindow::signalDelResp(const SignalDetailPerFileDelResp& resp)
{
    emit signalSignalDetailPerFileDelResp(resp);
}

bool CustomWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    Q_UNUSED(eventType);
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

#ifdef Q_OS_WIN
    MSG *msg = reinterpret_cast<MSG *>(message);
    HWND hwnd = msg->hwnd;

    switch (msg->message) {

    case WM_NCCALCSIZE: {
        if (msg->wParam != TRUE) break;

        NCCALCSIZE_PARAMS *p = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
        const LONG originalTop = p->rgrc[0].top;

        // Let Windows calculate correct left/right/bottom borders (DWM shadows)
        DefWindowProcW(hwnd, msg->message, msg->wParam, msg->lParam);

        // Restore original top — removes standard caption from non-client area
        p->rgrc[0].top = originalTop;

        // Maximized: keep a small top resize handle so mouse can grab edge
        if (IsZoomed(hwnd)) {
            p->rgrc[0].top += topResizeHandleHeight(hwnd);
        }

        *result = 0;
        return true;
    }

    case WM_NCHITTEST: {
        // Step 1: Default hit test handles standard resize borders
        LRESULT def = DefWindowProcW(hwnd, msg->message, msg->wParam, msg->lParam);
        if (def != HTCLIENT) {
            *result = def;
            return true;
        }

        // Step 2: Top resize handle (non-maximized only)
        if (!IsZoomed(hwnd) && (GetWindowLongW(hwnd, GWL_STYLE) & WS_THICKFRAME)) {
            RECT rw;
            GetWindowRect(hwnd, &rw);
            int y = GET_Y_LPARAM(msg->lParam) - rw.top;
            if (y < topResizeHandleHeight(hwnd)) {
                *result = HTTOP;
                return true;
            }
        }

        // Step 3: Title bar area — HTCAPTION drives drag/double-click/Aero Snap
        if (isInTitleBar(msg->lParam)) {
            *result = HTCAPTION;
            return true;
        }

        *result = HTCLIENT;
        return true;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = reinterpret_cast<MINMAXINFO *>(msg->lParam);
        int minContentW = DPR_INT(800 * scale, dpr, 0); // 安全 fallback
        if (m_contentPanel)
        {
            minContentW = m_contentPanel->minimumContentWidth();
        }
        mmi->ptMinTrackSize.x = minContentW;
        mmi->ptMinTrackSize.y = DPR_INT(600 * scale, dpr, 0);
        *result = 0;
        return true;
    }

    case WM_DPICHANGED: {
        RECT *suggested = reinterpret_cast<RECT *>(msg->lParam);
        SetWindowPos(hwnd, nullptr,
                     suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        *result = 0;
        return true;
    }

    case WM_SETTINGCHANGE: {
        // System accent color or theme changed — refresh border
        updateBorderColor();
        *result = 0;
        return true;
    }

    case WM_ERASEBKGND: {
        // 在无边框窗口下，Windows 仍可能尝试用默认白刷子擦背景。
        // 这里使用当前主题背景色主动填充，避免浅深主题切换时出现闪白。
        RECT rect;
        GetClientRect(hwnd, &rect);
        HBRUSH brush = CreateSolidBrush(RGB(m_windowBackgroundColor.red(),
                                           m_windowBackgroundColor.green(),
                                           m_windowBackgroundColor.blue()));
        FillRect(reinterpret_cast<HDC>(msg->wParam), &rect, brush);
        DeleteObject(brush);
        *result = 1;
        return true;
    }

    default:
        break;
    }
#endif

    return QWidget::nativeEvent(eventType, message, result);
}
