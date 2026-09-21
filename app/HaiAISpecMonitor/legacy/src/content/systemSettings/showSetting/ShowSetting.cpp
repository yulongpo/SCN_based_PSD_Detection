#include "ShowSetting.h"
#include "widgets/HQCComboBox.h"
#include "widgets/HQToolButton.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include "comm/FontManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QButtonGroup>
#include <QRadioButton>
#include <QLabel>
#include <QPushButton>
#include <QDialog>
#include "HQColorWidget.h"

class DashedLine : public QWidget
{
public:
    explicit DashedLine(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(1);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(QColor(163, 162, 164, 80), 1, Qt::DashLine);
        p.setPen(pen);
        p.drawLine(0, 0, width(), 0);
    }
};

ShowSetting::ShowSetting(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    initUI();
}

void ShowSetting::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const qreal w = width();
    const qreal h = height();

    // 圆角半径 16px、边框宽度 1px（设计稿 2x 基准）
    const qreal radius  = DPR_REAL(16.0 * scale, dpr);
    const qreal borderW = DPR_REAL(1.0 * scale, dpr);
    const qreal halfBW  = borderW * 0.5;

    // 绘制圆角边框（颜色从主题配置读取）
    QPainterPath borderPath;
    borderPath.addRoundedRect(
        QRectF(halfBW, halfBW,
               w - borderW, h - borderW),
        radius - halfBW, radius - halfBW);

    painter.setPen(QPen(ThemeManager::instance().color("CollMonitor.borderColor"), borderW));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(borderPath);
}

void ShowSetting::initUI()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int padInt    = DPR_INT(20 * scale, dpr, 10);
    const int spacing   = DPR_INT(8 * scale, dpr, 4);
    const int titleFs   = DPR_INT(15 * scale, dpr, 11);
    const int labelFs   = DPR_INT(13 * scale, dpr, 9);
    const int comboW    = DPR_INT(160 * scale, dpr, 120);
    const int comboH    = DPR_INT(32 * scale, dpr, 22);
    const int pickerSz  = DPR_INT(32 * scale, dpr, 24);

    const QColor titleColor(0xDE, 0xFF, 0xFF);   // #DEEFFF

    // 主布局无边距，分隔线可占满整宽
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ============================================================
    // 第一部分：主题模式
    // ============================================================
    auto *section1 = new QVBoxLayout;
    section1->setContentsMargins(padInt, spacing, padInt, spacing);
    section1->setSpacing(spacing);

    m_themeTitle = new QLabel(QStringLiteral("主题模式"), this);
    m_themeTitle->setFont(FontManager::instance().font(titleFs, QFont::Bold));
    m_themeTitle->setStyleSheet(QString("color: %1; padding: 0; margin: 0;").arg(titleColor.name()));
    m_themeTitle->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
    section1->addWidget(m_themeTitle);

    // 三个主题单选框
    m_themeGroup = new QButtonGroup(this);
    m_themeGroup->setExclusive(true);

    auto *radioWidget = new QWidget(this);
    radioWidget->setFixedHeight(DPR_INT(19 * scale, dpr, 16));
    auto *themeLayout = new QHBoxLayout(radioWidget);
    themeLayout->setContentsMargins(0, 0, 0, 0);
    themeLayout->setSpacing(DPR_INT(16 * scale, dpr, 8));

    const QStringList themeModes = {
        QStringLiteral("浅色"),
        QStringLiteral("深色"),
        QStringLiteral("跟随系统")
    };
    for (int i = 0; i < themeModes.size(); ++i) {
        auto *radio = new QRadioButton(themeModes.at(i), this);
        radio->setFont(FontManager::instance().font(labelFs, QFont::Normal));
        radio->setStyleSheet(QStringLiteral("QRadioButton { color: white; padding: 0; margin: 0; spacing: 2px; }"));
        radio->setFixedHeight(DPR_INT(18 * scale, dpr, 14));
        m_themeGroup->addButton(radio, i);
        themeLayout->addWidget(radio);
    }
    themeLayout->addStretch();
    section1->addWidget(radioWidget);

    // 默认选中"深色"
    m_themeGroup->button(1)->setChecked(true);

    mainLayout->addLayout(section1);

    // 虚线分隔（满宽）
    mainLayout->addWidget(createSeparator());

    // ============================================================
    // 第二部分：频道/时频/标记配色
    // ============================================================
    auto *section2 = new QVBoxLayout;
    section2->setContentsMargins(padInt, spacing, padInt, spacing);
    section2->setSpacing(spacing);

    m_colorTitle = new QLabel(QStringLiteral("频道/时频/标记配色"), this);
    m_colorTitle->setFont(FontManager::instance().font(titleFs, QFont::Bold));
    m_colorTitle->setStyleSheet(QString("color: %1;").arg(titleColor.name()));
    m_colorTitle->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
    section2->addWidget(m_colorTitle);

    // ---- 频谱曲线颜色 ----
    {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(DPR_INT(8 * scale, dpr, 4));

        auto *label = new QLabel(QStringLiteral("频谱曲线颜色:"), this);
        label->setFont(FontManager::instance().font(labelFs, QFont::Normal));
        label->setStyleSheet(QStringLiteral("color: white;"));
        label->setFixedWidth(DPR_INT(120 * scale, dpr, 90));
        label->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
        row->addWidget(label);

        m_spectrumCombo = new HQCComboBox(this);
        m_spectrumCombo->setFixedSize(comboW, comboH);
        m_spectrumCombo->addItems(QStringList()
            << QStringLiteral("默认配色")
            << QStringLiteral("经典绿")
            << QStringLiteral("橙色")
            << QStringLiteral("自定义"));
        m_spectrumCombo->setCurrentIndex(0);
        row->addWidget(m_spectrumCombo);

        m_spectrumPicker = createColorPickerBtn();
        m_spectrumPicker->setFixedSize(pickerSz, pickerSz);
        row->addWidget(m_spectrumPicker);

        row->addStretch();
        section2->addLayout(row);
    }

    // ---- 瀑布图色图 ----
    {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(DPR_INT(8 * scale, dpr, 4));

        auto *label = new QLabel(QStringLiteral("瀑布图色图:"), this);
        label->setFont(FontManager::instance().font(labelFs, QFont::Normal));
        label->setStyleSheet(QStringLiteral("color: white;"));
        label->setFixedWidth(DPR_INT(120 * scale, dpr, 90));
        label->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
        row->addWidget(label);

        m_waterfallCombo = new HQCComboBox(this);
        m_waterfallCombo->setFixedSize(comboW, comboH);
        m_waterfallCombo->addItems(QStringList()
            << QStringLiteral("热力图")
            << QStringLiteral("冷色调")
            << QStringLiteral("灰度")
            << QStringLiteral("彩虹图"));
        m_waterfallCombo->setCurrentIndex(0);
        row->addWidget(m_waterfallCombo);

        row->addStretch();
        section2->addLayout(row);
    }

    // ---- 标记颜色 ----
    {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(DPR_INT(8 * scale, dpr, 4));

        auto *label = new QLabel(QStringLiteral("标记颜色:"), this);
        label->setFont(FontManager::instance().font(labelFs, QFont::Normal));
        label->setStyleSheet(QStringLiteral("color: white;"));
        label->setFixedWidth(DPR_INT(120 * scale, dpr, 90));
        label->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
        row->addWidget(label);

        m_markCombo = new HQCComboBox(this);
        m_markCombo->setFixedSize(comboW, comboH);
        m_markCombo->addItems(QStringList()
            << QStringLiteral("红色")
            << QStringLiteral("黄色")
            << QStringLiteral("蓝色")
            << QStringLiteral("白色")
            << QStringLiteral("自定义"));
        m_markCombo->setCurrentIndex(0);
        row->addWidget(m_markCombo);

        m_markPicker = createColorPickerBtn();
        m_markPicker->setFixedSize(pickerSz, pickerSz);
        row->addWidget(m_markPicker);

        row->addStretch();
        section2->addLayout(row);
    }

    mainLayout->addLayout(section2);

    // 虚线分隔（满宽）
    mainLayout->addWidget(createSeparator());

    // ============================================================
    // 第三部分：恢复默认
    // ============================================================
    auto *section3 = new QVBoxLayout;
    section3->setContentsMargins(padInt, spacing, padInt, padInt);
    section3->setSpacing(spacing);

    m_resetBtn = new HQToolButton(this);
    m_resetBtn->setText(QStringLiteral("恢复默认"));
    m_resetBtn->setHQIcon(QIcon(QStringLiteral(":/button/default.png")), 14, 14);
    m_resetBtn->setFixedSize(DPR_INT(95 * scale, dpr, 0), DPR_INT(27 * scale, dpr, 0));
    m_resetBtn->setHQRadius(8);
    m_resetBtn->setTextColor(QColor(Qt::white));
    section3->addWidget(m_resetBtn);

    mainLayout->addLayout(section3);
    mainLayout->addStretch();
}

QWidget* ShowSetting::createSeparator()
{
    return new DashedLine(this);
}

QPushButton* ShowSetting::createColorPickerBtn()
{
    auto *btn = new QPushButton(this);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(QStringLiteral("取色"));
    btn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  border: 1px solid rgba(163, 162, 164, 80);"
        "  border-radius: 8px;"
        "  background: transparent;"
        "}"
        "QPushButton:hover {"
        "  border: 1px solid #DEEFFF;"
        "}"
    ));

    // 默认白色 12x12 色块
    QPixmap pix(12, 12);
    pix.fill(Qt::white);
    btn->setIcon(QIcon(pix));
    btn->setIconSize(QSize(12, 12));

    // 点击弹出自定义取色板（仅在未打开时响应）
    connect(btn, &QPushButton::clicked, this, [btn, this]() {
        // 防重入：弹窗已打开时忽略点击
        if (btn->property("_hqPickerOpen").toBool()) return;
        btn->setProperty("_hqPickerOpen", true);

        // 从按钮图标获取当前颜色作为初始值
        QPixmap btnPix = btn->icon().pixmap(12, 12);
        QColor initColor = btnPix.isNull() ? Qt::white : btnPix.toImage().pixelColor(0, 0);

        // Popup 弹窗承载取色板（点击非取色板区域自动消失）
        QDialog dlg(this, Qt::Popup);
        dlg.setAttribute(Qt::WA_TranslucentBackground);
        dlg.setStyleSheet(QStringLiteral("background: transparent;"));

        auto *picker = new HQColorWidget(&dlg);
        picker->setCurrentColor(initColor);
        dlg.setFixedSize(picker->size());

        // 拖拽时实时更新按钮色块
        QObject::connect(picker, &HQColorWidget::colorChanged, btn, [btn](const QColor &c) {
            QPixmap px(12, 12);
            px.fill(c);
            btn->setIcon(QIcon(px));
            btn->setIconSize(QSize(12, 12));
        });

        // 定位到按钮右侧
        dlg.move(btn->mapToGlobal(QPoint(btn->width() + 4, 0)));

        dlg.exec();
        btn->setProperty("_hqPickerOpen", false);
    });

    return btn;
}
