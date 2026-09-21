#include "HQMessageBox.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsDropShadowEffect>
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"

HQMessageBox* HQMessageBox::getInstance()
{
    static HQMessageBox* instance = new HQMessageBox();
    return instance;
}

void HQMessageBox::setTitle(const QString& title)
{
    m_titleLabel->setText(title);
}

void HQMessageBox::setMsgInfo(const QString& msg)
{
    m_msgLabel->setText(msg);
}

HQMessageBox::HQMessageBox(QWidget *parent) : QDialog(parent), m_isDragging(false)
{
    // 无边框、对话框属性、置顶显示
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog | Qt::WindowStaysOnTopHint);
    // 背景透明（为了绘制圆角和阴影）
    setAttribute(Qt::WA_TranslucentBackground);
    
    initUI();
    initStyle();
}

HQMessageBox::~HQMessageBox()
{
}

void HQMessageBox::initUI()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();
    resize(DPR_INT(350 * scale, dpr, 0), DPR_INT(200 * scale, dpr, 0));

    // 主布局，留出 10px 边距给阴影特效
    int margin = DPR_INT(10 * scale, dpr, 0);
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(margin, margin, margin, margin);

    // 核心背景容器
    QWidget *container = new QWidget(this);
    container->setObjectName("MsgContainer");
    mainLayout->addWidget(container);

    QVBoxLayout *containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    // ================= 1. 标题栏区域 =================
    m_headerWidget = new QWidget(container); 
    m_headerWidget->setObjectName("MsgHeader");
    m_headerWidget->setFixedHeight(DPR_INT(36 * scale, dpr, 0)); // 固定标题栏高度

    QHBoxLayout *headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(DPR_INT(15 * scale, dpr, 0), 0, DPR_INT(15 * scale, dpr, 0), 0);

    m_titleLabel = new QLabel(QStringLiteral("提示"), m_headerWidget);
    m_closeBtn = new QPushButton(QStringLiteral("✕"), m_headerWidget);
    m_closeBtn->setObjectName("CloseBtn");
    m_closeBtn->setFixedSize(DPR_INT(24 * scale, dpr, 0), DPR_INT(24 * scale, dpr, 0));
    
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_closeBtn);
    
    containerLayout->addWidget(m_headerWidget); // 加入容器

    // ================= 2. 提示内容区域 =================
    m_msgLabel = new QLabel("", container);
    m_msgLabel->setObjectName("MsgContent");
    m_msgLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop); // 文本居中
    m_msgLabel->setWordWrap(true);
    m_msgLabel->setContentsMargins(margin, margin, margin, margin);
    
    containerLayout->addWidget(m_msgLabel, 1); // 1表示铺满剩余所有空间，非常关键！

    // ================= 3. 底部按钮区域 =================
    QWidget *footerWidget = new QWidget(container);
    footerWidget->setFixedHeight(DPR_INT(50 * scale, dpr, 0));
    
    QHBoxLayout *footerLayout = new QHBoxLayout(footerWidget);
    footerLayout->setContentsMargins(DPR_INT(15 * scale, dpr, 0), 0, DPR_INT(20 * scale, dpr, 0), DPR_INT(15 * scale, dpr, 0));
    footerLayout->setSpacing(DPR_INT(15 * scale, dpr, 0));

    m_yesBtn = new QPushButton(u8"确定", footerWidget);
    m_yesBtn->setObjectName("BtnYes");
    m_yesBtn->setFixedSize(DPR_INT(80 * scale, dpr, 0), DPR_INT(28 * scale, dpr, 0)); // 限制按钮大小，更加精致
    
    m_noBtn = new QPushButton(u8"取消", footerWidget);
    m_noBtn->setObjectName("BtnNo");
    m_noBtn->setFixedSize(DPR_INT(80 * scale, dpr, 0), DPR_INT(28 * scale, dpr, 0));

    footerLayout->addStretch(); // 把按钮挤到右边
    footerLayout->addWidget(m_yesBtn);
    footerLayout->addWidget(m_noBtn);
    
    containerLayout->addWidget(footerWidget); // 加入容器

    // ================= 4. 阴影特效 =================
    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setOffset(0, 2);
    shadow->setColor(QColor(0, 0, 0, 180));
    shadow->setBlurRadius(12);
    container->setGraphicsEffect(shadow);

    // ================= 5. 事件绑定 =================
    connect(m_closeBtn, &QPushButton::clicked, this, &HQMessageBox::reject);
    connect(m_noBtn, &QPushButton::clicked, this, &HQMessageBox::reject);
    connect(m_yesBtn, &QPushButton::clicked, this, &HQMessageBox::accept);
}

void HQMessageBox::initStyle()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();
    // 全局暗黑科技风 QSS
    this->setStyleSheet(QString(u8R"(
        #MsgContainer {
            background-color: #090E17;
            border: 1px solid #1E293B;
            border-radius: %1px;
        }
        #MsgHeader {
            background-color: rgba(255, 255, 255, 0.03);
            border-bottom: 1px solid #1E293B;
            border-top-left-radius: %1px;
            border-top-right-radius: %1px;
        }
        #MsgHeader QLabel {
            color: #94A3B8;
            font-size: %2px;
            font-family: "Microsoft YaHei", "微软雅黑";
        }
        #CloseBtn {
            background: transparent;
            border: none;
            color: #94A3B8;
            font-size: %3px;
            font-family: "Microsoft YaHei";
        }
        #CloseBtn:hover {
            color: #EF4444;
        }
        #MsgContent {
            color: #E2E8F0;
            font-size: %2px;
            font-family: "Microsoft YaHei", "微软雅黑";
        }
        /* 按钮全局基础样式 */
        QPushButton {
            font-family: "Microsoft YaHei", "微软雅黑";
            font-size: %2px;
            border-radius: %1px;
            background-color: transparent;
            outline: none;
        }
        /* 确定按钮：科技蓝 */
        #BtnYes {
            border: 2px solid #0078D7;
            color: #0078D7;
        }
        #BtnYes:hover { background-color: rgba(0, 120, 215, 38); }
        #BtnYes:pressed { background-color: rgba(0, 120, 215, 76); }

        /* 取消按钮：暗灰 -> 悬浮警示红 */
        #BtnNo {
            border: 2px solid #475569;
            color: #94A3B8;
        }
        #BtnNo:hover {
            border: 2px solid #EF4444;
            color: #EF4444;
            background-color: rgba(239, 68, 68, 30);
        }
        #BtnNo:pressed { background-color: rgba(239, 68, 68, 76); }
    )").arg(DPR_INT(6 * scale, dpr, 0)).arg(DPR_INT(13 * scale, dpr, 0)).arg(DPR_INT(18 * scale, dpr, 0)));
}

// ================= 【严格限制的标题栏拖拽逻辑】 =================
void HQMessageBox::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // 将全局坐标映射到标题栏坐标系中
        QPoint posInHeader = m_headerWidget->mapFromGlobal(event->globalPos());
        
        // 判断鼠标是不是准确点在了 36px 高度的标题栏里
        if (m_headerWidget->rect().contains(posInHeader)) {
            m_isDragging = true;
            m_dragPosition = event->globalPos() - frameGeometry().topLeft();
            event->accept();
            return;
        }
    }
    QDialog::mousePressEvent(event);
}

void HQMessageBox::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPos() - m_dragPosition);
        event->accept();
        return;
    }
    QDialog::mouseMoveEvent(event);
}

void HQMessageBox::mouseReleaseEvent(QMouseEvent *event)
{
    m_isDragging = false;
    QDialog::mouseReleaseEvent(event);
}
