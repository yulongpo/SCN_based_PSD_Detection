#include "HQSpectrum.h"
#include "../plot/qcustomplot.h"
#include "../tfwaterfall/HQTfwaterfall.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include <QPainter>
#include <QVariantAnimation>
#include <QMenu>
#include <QAction>

HQSpectrum::HQSpectrum(QWidget *parent)
    : SpecPlotBase(parent)
{
    // 谱线颜色从主题配置读取
    const QColor lineColor = ThemeManager::instance().color("CollMonitor.spectrum.lineColor");
    m_linePen = QPen(lineColor.isValid() ? lineColor : QColor(21, 207, 255), 1.5);
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    // 设置背景透明
    setBackground(Qt::transparent);

    // 轴矩形背景渐变色：顶部不透明深蓝 → 底部全透明
    m_bgGradient.setCoordinateMode(QGradient::ObjectBoundingMode);
    m_bgGradient.setStart(0, 0);                          // 顶部
    m_bgGradient.setFinalStop(0, 1);                      // 底部
    m_bgGradient.setColorAt(0.0, QColor(7, 33, 63)); // 顶部实色
    m_bgGradient.setColorAt(1.0, QColor(6, 12, 26));   // 底部透明
    axisRect()->setBackground(QBrush(m_bgGradient));

    // 网格/轴/刻度颜色从主题配置读取
    const QColor gridColor = ThemeManager::instance().color("CollMonitor.spectrum.gridColor");
    const QColor axisColor = gridColor.isValid() ? gridColor : QColor(26, 48, 75);

    // 网格线：虚线
    QPen gridPen(axisColor);
    gridPen.setStyle(Qt::DashLine);
    xAxis->grid()->setPen(gridPen);
    yAxis->grid()->setPen(gridPen);
    xAxis->grid()->setZeroLinePen(gridPen);
    yAxis->grid()->setZeroLinePen(gridPen);
    xAxis->grid()->setVisible(true);
    yAxis->grid()->setVisible(true);
    xAxis->grid()->setSubGridVisible(false);
    yAxis->grid()->setSubGridVisible(false);

    // 轴基线（一整条实线）
    xAxis->setBasePen(QPen(axisColor));
    yAxis->setBasePen(QPen(axisColor));

    // 固定左侧边距（与时频图保持一致）
    const int leftMargin = DPR_INT(85 * scale, dpr, 0);
    // 顶部留白：为标记框文字（12pt ≈ 18px + 3px 间隙）预留空间
    const int topMargin  = DPR_INT(2 * scale, dpr, 0);
    // 让底部能够有更多的拖动空间
    const int bottomMargin  = DPR_INT(30 * scale, dpr, 0);
    axisRect()->setAutoMargins(QCP::msRight);
    axisRect()->setMargins(QMargins(leftMargin, topMargin, 0, bottomMargin));

    // 限制 Y 轴刻度格式
    yAxis->setNumberFormat("f");
    yAxis->setNumberPrecision(1);

    // X 轴标签
    //setXLabel(QStringLiteral("频率 (Hz)"));

    // Y 轴标签（父类统一管理：固定视口位置 + 旋转 90°）
    setupFixedYLabel(QStringLiteral("功率 (dB)"));

    // 设置刻度文字和轴标签颜色（从主题配置读取）
    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &HQSpectrum::applyThemeColors);

    // ---- 创建谱线 ----
    m_graph = addGraph(xAxis, yAxis);
    m_graph->setPen(m_linePen);
    m_graph->setLineStyle(QCPGraph::lsLine);
    m_graph->setAntialiased(false);  // 4096 点 ≈ 5点/px，抗锯齿反致重叠模糊

    // ---- 创建最大保持谱线 ----
    m_graphMaxHold = addGraph(xAxis, yAxis);
    // 最大保持颜色从主题配置读取
    const QColor maxHoldColor = ThemeManager::instance().color("CollMonitor.spectrum.maxHoldColor");
    m_maxHoldPen = QPen(maxHoldColor.isValid() ? maxHoldColor : QColor(182, 13, 173), 1, Qt::SolidLine);
    m_graphMaxHold->setPen(m_maxHoldPen);
    m_graphMaxHold->setLineStyle(QCPGraph::lsLine);
    m_graphMaxHold->setAntialiased(false);
    m_graphMaxHold->setVisible(false);  // 默认隐藏，由按钮控制

    // ---- 创建平均谱线 ----
    m_graphAvg = addGraph(xAxis, yAxis);
    const QColor avgColor = ThemeManager::instance().color("CollMonitor.spectrum.avgColor");
    m_avgPen = QPen(avgColor.isValid() ? avgColor : QColor(8, 196, 33), 1, Qt::SolidLine);
    m_graphAvg->setPen(m_avgPen);
    m_graphAvg->setLineStyle(QCPGraph::lsLine);
    m_graphAvg->setAntialiased(false);
    m_graphAvg->setVisible(false);  // 默认隐藏，由按钮控制

    // ---- 创建瀑布图最大谱线（灰色） ----
    m_graphTfMaxHold = addGraph(xAxis, yAxis);
    m_tfMaxHoldPen = QPen(QColor(211, 211, 211, 51), 1, Qt::SolidLine);
    m_graphTfMaxHold->setPen(m_tfMaxHoldPen);
    m_graphTfMaxHold->setLineStyle(QCPGraph::lsLine);
    m_graphTfMaxHold->setAntialiased(false);

    // 将谱线移到独立缓存层，重绘时只刷新曲线（避免重绘坐标轴/网格/背景）
    {
        addLayer(QStringLiteral("graphLayer"));
        QCPLayer *gLayer = layer(QStringLiteral("graphLayer"));
        gLayer->setMode(QCPLayer::lmBuffered);
        addLayer(QStringLiteral("maxGraphLayer"));// 其他的最大谱等图层
        QCPLayer *gMaxLayer = layer(QStringLiteral("maxGraphLayer"));
        gMaxLayer->setMode(QCPLayer::lmBuffered);
        m_graph->setLayer(gLayer);
        m_graphMaxHold->setLayer(gMaxLayer);
        m_graphAvg->setLayer(gMaxLayer);
        m_graphTfMaxHold->setLayer(gMaxLayer);
    }
    setPlottingHint(QCP::phCacheLabels, true);      // 缓存刻度标签位图
    setPlottingHint(QCP::phFastPolylines, true);    // 快速折线绘制

    // X 轴范围变化时从完整数据副本重绘（解决缩小后数据丢失问题）
    connect(xAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, &HQSpectrum::onVisRangeChanged);

    // 右上角悬浮工具栏（显隐由 updateToolBarVisibility 按鼠标像素位置判断）
    setupToolButtons();

    connect(m_btnMaxHold, &QPushButton::toggled, this, &HQSpectrum::onBtnMaxHoldToggled);
    connect(m_btnAvg, &QPushButton::toggled, this, &HQSpectrum::onBtnAvgToggled);
    connect(m_btnLabel, &QPushButton::toggled, this, &HQSpectrum::onBtnLabelToggled);
    connect(m_btnGrid, &QPushButton::toggled, this, &HQSpectrum::onBtnGridToggled);
    // 网格按钮默认选中
    m_btnGrid->setChecked(true);
    m_btnLabel->setChecked(true);

    // 设置合理的默认范围
    setXRange(0, 1000.0);
    setYRange(-120.0, 20);
    setXRangeLimit(0, 1000.0);
    m_fullFreqLower = 0.0;
    m_fullFreqUpper = 1000.0;
    updateVisBinRange();

    // 预计算默认频率值到图内部数据容器
    QVector<QCPGraphData> defaultBins(m_fftlen);
    for (int i = 0; i < m_fftlen; ++i)
        defaultBins[i] = QCPGraphData(m_freqStart + i * m_freqPerBin, -200.0);
    m_graph->data()->set(defaultBins);
    m_graphMaxHold->data()->set(defaultBins);
    m_graphAvg->data()->set(defaultBins);
    m_graphTfMaxHold->data()->set(defaultBins);

    // ---- 预分配标记框内存（避免 addObj 时频繁申请/释放） ----
    m_marks.reserve(HQ_SPECTRUM_MARK_CAPACITY);
    m_marks.resize(HQ_SPECTRUM_MARK_CAPACITY);
    m_markCount = 0;

    // ---- 创建右键还原手势的画笔光标 ----
    {
        const int pixSize = DPR_INT(24 * scale, dpr, 0);
        QPixmap brushPix(pixSize, pixSize);
        brushPix.fill(Qt::transparent);

        QPainter p(&brushPix);
        p.setRenderHint(QPainter::Antialiasing, true);
        // 笔杆：右上 → 左下的白色粗线
        p.setPen(QPen(QColor(235, 246, 255), DPR_REAL(3.0*scale,dpr), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(DPR_INT(20*scale,dpr,0), DPR_INT(4*scale,dpr,0)), QPointF(DPR_INT(7*scale,dpr,0), DPR_INT(17*scale,dpr,0)));
        // 笔尖：指向左下方的青蓝色小三角
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(21, 207, 255));
        QPolygonF tip;
        tip << QPointF(DPR_INT(7*scale,dpr,0), DPR_INT(17*scale,dpr,0)) << QPointF(DPR_INT(3*scale,dpr,0), DPR_INT(21*scale,dpr,0))
            << QPointF(DPR_INT(2*scale,dpr,0), DPR_INT(18*scale,dpr,0)) << QPointF(DPR_INT(6*scale,dpr,0), DPR_INT(15*scale,dpr,0));
        p.drawPolygon(tip);
        p.end();

        // 热点对准笔尖尖端（物理像素坐标，无 dpr 换算歧义）
        m_brushCursor = QCursor(brushPix, DPR_INT(3*scale,dpr,0), DPR_INT(20*scale,dpr,0));
    }
}

void HQSpectrum::post(const float *spectrum)
{
    if (!spectrum) return;

    // 保存完整频谱副本（缩放时从此重绘，避免数据丢失）
    if (m_lastSpectrum.size() != m_fftlen) m_lastSpectrum.resize(m_fftlen);
    memcpy(m_lastSpectrum.data(), spectrum, m_fftlen * sizeof(float));

    renderSpectrum();
    layer(QStringLiteral("graphLayer"))->replot();
}

void HQSpectrum::onMaxHoldAvgUpdated(const std::vector<float> &maxHold,
                                     const std::vector<float> &avgData,
                                     const std::vector<float> &TfMaxHold)
{
    // Worker 完成重算后回调 —— 数据已通过信号参数直接送达，无需再查询 waterfall
    if (maxHold.empty() || avgData.empty()) return;

    m_maxHold.resize(m_fftlen);
    m_avgData.resize(m_fftlen);
    m_TfMaxHold.resize(m_fftlen);
    memcpy(m_maxHold.data(),  maxHold.data(),  m_fftlen * sizeof(float));
    memcpy(m_avgData.data(),   avgData.data(),   m_fftlen * sizeof(float));
    memcpy(m_TfMaxHold.data(), TfMaxHold.data(), m_fftlen * sizeof(float));

    maxRenderSpectrum();
    layer(QStringLiteral("maxGraphLayer"))->replot();
}

void HQSpectrum::renderSpectrum()
{
    if (!m_graph || m_lastSpectrum.isEmpty()) return;
    if (m_visBinCount <= 0) return;

    // 直接修改图内部数据容器的 Y 值（避免 setData 拷贝和中间缓冲）
    QCPGraphDataContainer::iterator itMain = m_graph->data()->begin();
    for (int i = 0; i < m_visBinCount; ++i) {
        const int binIdx = m_visBinStart + i;
        itMain[binIdx].value = static_cast<double>(m_lastSpectrum[binIdx]);
    }
}

void HQSpectrum::maxRenderSpectrum()
{
    if (m_graphMaxHold && m_maxHold.size() == m_fftlen) {
        QCPGraphDataContainer::iterator itMax = m_graphMaxHold->data()->begin();
        for (int i = 0; i < m_visBinCount; ++i)
            itMax[m_visBinStart + i].value = m_maxHold[m_visBinStart + i];
    }

    if (m_graphAvg && m_avgData.size() == m_fftlen) {
        QCPGraphDataContainer::iterator itAvg = m_graphAvg->data()->begin();
        for (int i = 0; i < m_visBinCount; ++i)
            itAvg[m_visBinStart + i].value = m_avgData[m_visBinStart + i];
    }

    if (m_graphTfMaxHold && m_TfMaxHold.size() == m_fftlen) {
        QCPGraphDataContainer::iterator itTf = m_graphTfMaxHold->data()->begin();
        for (int i = 0; i < m_visBinCount; ++i)
            itTf[m_visBinStart + i].value = m_TfMaxHold[m_visBinStart + i];
    }
}

void HQSpectrum::updateVisBinRange()
{
    const QCPRange visRange = xAxis->range();
    m_visBinStart = qBound(0,
                           static_cast<int>((visRange.lower - m_freqStart) / m_freqPerBin),
                           m_fftlen - 1);
    const int visBinEnd = qBound(m_visBinStart + 1,
                                 static_cast<int>((visRange.upper - m_freqStart) / m_freqPerBin) + 1,
                                 m_fftlen);
    m_visBinCount = visBinEnd - m_visBinStart;
}

void HQSpectrum::onVisRangeChanged(const QCPRange & /*newRange*/)
{
    updateVisBinRange();
    renderSpectrum();
    maxRenderSpectrum();
    replot(QCustomPlot::rpQueuedReplot);  // 完整重绘（移动/缩放时坐标轴/网格需刷新）
}

void HQSpectrum::post(const QVector<float> &spectrum)
{
    post(spectrum.constData());
}

void HQSpectrum::setResolution(int fftlen, const QCPRange &freqRange)
{
    // 记录完整频率范围（用于从右向左拖动时还原最大视图）
    m_fullFreqLower = freqRange.lower;
    m_fullFreqUpper = freqRange.upper;

    m_fftlen    = fftlen;
    m_freqRange = freqRange.size();
    m_freqStart = freqRange.lower;
    m_freqPerBin = m_freqRange / m_fftlen;

    m_maxHold.resize(fftlen);
    m_maxHold.fill(-200.0);
    m_avgData.resize(fftlen);
    m_avgData.fill(0.0);

    // 预计算所有频率值到图内部数据容器（分辨率固定后 key 不变，每次只更新 value）
    QVector<QCPGraphData> allBins(fftlen);
    for (int i = 0; i < fftlen; ++i)
        allBins[i] = QCPGraphData(m_freqStart + i * m_freqPerBin, -200.0);
    m_graph->data()->set(allBins);
    m_graphMaxHold->data()->set(allBins);
    m_graphAvg->data()->set(allBins);
    m_graphTfMaxHold->data()->set(allBins);

    setXRange(freqRange.lower, freqRange.upper);
    updateVisBinRange();
}

void HQSpectrum::clearData()
{
    m_lastSpectrum.clear();
    m_maxHold.clear();
    m_avgData.clear();
    m_TfMaxHold.clear();
    m_markCount = 0;
    m_offlineMarkActive = false;

    if (m_graph) {
        QCPGraphDataContainer::iterator it = m_graph->data()->begin();
        const int count = m_graph->data()->size();
        for (int i = 0; i < count; ++i)
            it[i].value = -200.0;
        replot();
    }
    if (m_graphMaxHold) {
        QCPGraphDataContainer::iterator it = m_graphMaxHold->data()->begin();
        const int count = m_graphMaxHold->data()->size();
        for (int i = 0; i < count; ++i)
            it[i].value = -200.0;
    }
    if (m_graphAvg) {
        QCPGraphDataContainer::iterator it = m_graphAvg->data()->begin();
        const int count = m_graphAvg->data()->size();
        for (int i = 0; i < count; ++i)
            it[i].value = -200.0;
    }
    if (m_graphTfMaxHold) {
        QCPGraphDataContainer::iterator it = m_graphTfMaxHold->data()->begin();
        const int count = m_graphTfMaxHold->data()->size();
        for (int i = 0; i < count; ++i)
            it[i].value = -200.0;
    }
    renderSpectrum();
    maxRenderSpectrum();
    replot(QCustomPlot::rpQueuedReplot);  // 完整重绘（移动/缩放时坐标轴/网格需刷新）
}

void HQSpectrum::addObj(HQSigMF::DetectionObject* obj, double opacity)
{
    HQMarkStyle style = HQMarkStyle::Normal;
    if (obj->alarm_level == 1) {
        style = HQMarkStyle::Warning;
    } else if (obj->alarm_level == 2) {
        style = HQMarkStyle::Error;
    }

    // 容量不足时扩容为当前2倍（避免频繁扩容）
    if (m_markCount >= m_marks.size()) {
        const int newSize = qMax(m_marks.size() * 2, HQ_SPECTRUM_MARK_CAPACITY);
        m_marks.resize(newSize);
    }

    // 直接写入预分配的槽位（无堆分配，仅赋值已有对象的成员）
    m_marks[m_markCount].setId(obj->id);
    m_marks[m_markCount].setFreqStart(obj->f_start_hz);
    m_marks[m_markCount].setFreqStop(obj->f_end_hz);
    m_marks[m_markCount].setStyle(style);
    m_marks[m_markCount].setOpacity(opacity);
    m_marks[m_markCount].setCfText(QString::number((obj->f_start_hz + obj->f_end_hz) / 2000000.0, 'f', 6) + " MHz");
    m_marks[m_markCount].setBwText(QString::number((obj->f_end_hz - obj->f_start_hz) / 1000.0, 'f', 3) + " kHz");
    m_markCount++;
    update();
}

void HQSpectrum::clearMarks()
{
    for (int i = 0; i < m_markCount; ++i) {
        m_marks[i].setSelected(false);
    }
    m_offlineMark.setSelected(false);
    m_offlineMarkActive = false;
    m_markCount = 0;
    update();
}

void HQSpectrum::hideMaxAndAvgBtn()
{
    m_btnMaxHold->hide();
    m_btnAvg->hide();
    m_btnClear->hide();
}

void HQSpectrum::setOperatorEnabled(bool enable)
{
    enable = true;
    // 利用 QWidget::setEnabled 阻止所有鼠标/滚轮事件送达控件，
    // 从而禁用频谱图的拖拽缩放、单击标记、频率范围选择等所有交互操作。
    // 工具栏按钮作为子控件，也会自动跟随禁用。
    // 视图内容（谱线、标记框）保持可见，仅交互被冻结。
    QWidget::setEnabled(enable);
}

void HQSpectrum::setTracerVisible(bool visible)
{
    m_tracerEnabled = visible;
    m_tracerVisible = visible && axisRect()->rect().contains(m_tracerPos);
    update();
}

// ========================================================================
// 通用缩放动画：从当前 X 轴范围平滑过渡到目标频率范围
// 在列表行单击定位（onRowSelectedForMark）和频谱图标记框点击（selectMarkAt）时均可触发
// ========================================================================
void HQSpectrum::animateZoomToFreqRange(double centerFreq, double bandwidth)
{
    // 记录动画起始的 X 轴范围
    const double startLower = xAxis->range().lower;
    const double startUpper = xAxis->range().upper;

    // 计算目标范围：以信号中心频率为中心，宽度为信号带宽的 10 倍（最小 1 Hz）
    const double viewWidth = qMax(bandwidth * 10.0, 1.0);
    const double halfView  = viewWidth / 2.0;
    const double endLower  = centerFreq - halfView;
    const double endUpper  = centerFreq + halfView;

    // 如果目标范围与当前范围几乎一致（差值 < 0.1 Hz），跳过动画避免不必要的重绘
    if (qAbs(startLower - endLower) < 0.1 && qAbs(startUpper - endUpper) < 0.1) {
        return;
    }

    // 创建 500ms 的 OutCubic 缓出动画，从当前范围过渡到目标范围
    auto *anim = new QVariantAnimation(this);
    anim->setDuration(500);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);

    connect(anim, &QVariantAnimation::valueChanged, this,
            [this, startLower, startUpper, endLower, endUpper](const QVariant &val) {
        const double t = val.toDouble();
        xAxis->setRange(
            startLower + (endLower - startLower) * t,
            startUpper + (endUpper - startUpper) * t);
    });

    // 动画结束后自动销毁
    connect(anim, &QVariantAnimation::finished, anim, &QObject::deleteLater);
    anim->start();
}

void HQSpectrum::animateRestoreFullRange()
{
    // 目标范围：完整频率范围（m_fullFreqLower ~ m_fullFreqUpper）
    const double targetLower = m_fullFreqLower;
    const double targetUpper = m_fullFreqUpper;
    const double startLower  = xAxis->range().lower;
    const double startUpper  = xAxis->range().upper;

    // 已处于完整范围时无需动画
    if (qAbs(startLower - targetLower) < 0.1 && qAbs(startUpper - targetUpper) < 0.1) {
        return;
    }

    // 停止上一次可能还在运行的动画
    if (m_dragAnim) {
        m_dragAnim->stop();
        m_dragAnim->deleteLater();
        m_dragAnim = nullptr;
    }

    m_dragAnim = new QVariantAnimation(this);
    m_dragAnim->setDuration(300);
    m_dragAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_dragAnim->setStartValue(0.0);
    m_dragAnim->setEndValue(1.0);

    connect(m_dragAnim, &QVariantAnimation::valueChanged, this,
            [this, startLower, startUpper, targetLower, targetUpper](const QVariant &val) {
        const double t = val.toDouble();
        xAxis->setRange(
            startLower + (targetLower - startLower) * t,
            startUpper + (targetUpper - startUpper) * t);
    });

    connect(m_dragAnim, &QVariantAnimation::finished, this, [this]() {
        m_dragAnim->deleteLater();
        m_dragAnim = nullptr;
    });

    m_dragAnim->start();
}

void HQSpectrum::onRowSelectedForMark(int64_t id, qint64 freqStart, qint64 freqStop, int alarmLevel)
{
    // ========================================================================
    // 步骤1：将 alarmLevel 转换为 HQMarkStyle
    // ========================================================================
    HQMarkStyle style = HQMarkStyle::Normal;
    if (alarmLevel == 1) {
        style = HQMarkStyle::Warning;
    } else if (alarmLevel == 2) {
        style = HQMarkStyle::Error;
    }

    // ========================================================================
    // 步骤2：在 m_marks 中查找匹配 ID
    // ========================================================================
    int foundIdx = -1;
    for (int i = 0; i < m_markCount; ++i) {
        if (m_marks[i].id() == id) {
            foundIdx = i;
            break;
        }
    }

    // ========================================================================
    // 步骤3：如果找到匹配的 ID，高亮该标记、取消其他高亮；停用离线标记
    // ========================================================================
    if (foundIdx >= 0) {
        // 取消所有标记的高亮
        for (int i = 0; i < m_markCount; ++i) {
            m_marks[i].setSelected(false);
        }
        // 高亮匹配的标记
        m_marks[foundIdx].setSelected(true);

        // 停用离线标记框
        m_offlineMarkActive = false;
    }
    // ========================================================================
    // 步骤4：如果未找到，将数据赋值给 m_offlineMark 并以 OffLine 灰色高亮
    // ========================================================================
    else {
        // 取消所有活动标记的高亮
        for (int i = 0; i < m_markCount; ++i) {
            m_marks[i].setSelected(false);
        }

        // 组装并激活离线标记框（使用 OffLine 灰色样式）
        m_offlineMark.setId(id);
        m_offlineMark.setFreqStart(freqStart);
        m_offlineMark.setFreqStop(freqStop);
        m_offlineMark.setStyle(HQMarkStyle::OffLine);
        m_offlineMark.setCfText(QString::number((freqStart + freqStop) / 2000000, 'f', 1) + " MHz");
        m_offlineMark.setBwText(QString::number((freqStop - freqStart) / 1000, 'f', 0) + " kHz");
        m_offlineMark.setSelected(true);   // 高亮显示
        m_offlineMarkActive = true;         // 激活绘制
    }

    // ========================================================================
    // 步骤5：缩放动画 — 从当前 X 轴范围平滑过渡到高亮标记中心 + 10 倍宽度
    // ========================================================================
    if (foundIdx > 0)
    {
        animateZoomToFreqRange((m_marks[foundIdx].freqStart() + m_marks[foundIdx].freqStop()) / 2.0, m_marks[foundIdx].freqStop() - m_marks[foundIdx].freqStart());
    }
    else
    {
        const double centerFreq = (freqStart + freqStop) / 2.0;
        const double bandwidth  = freqStop - freqStart;
        animateZoomToFreqRange(centerFreq, bandwidth);
    }

    replot();
}

void HQSpectrum::applyThemeColors()
{
    const QColor tickLabelColor = ThemeManager::instance().color(
        QStringLiteral("CollMonitor.tfwaterfall.tickLabelColor"));
    const QColor axisLabelColor = ThemeManager::instance().color(
        QStringLiteral("CollMonitor.tfwaterfall.axisLabelColor"));
    const QColor lineColor = ThemeManager::instance().color(
        QStringLiteral("CollMonitor.spectrum.lineColor"));
    const QColor gridColor = ThemeManager::instance().color(
        QStringLiteral("CollMonitor.spectrum.gridColor"));
    const QColor maxHoldColor = ThemeManager::instance().color(
        QStringLiteral("CollMonitor.spectrum.maxHoldColor"));
    const QColor avgColor = ThemeManager::instance().color(
        QStringLiteral("CollMonitor.spectrum.avgColor"));

    if (tickLabelColor.isValid()) {
        setTickLabelColor(tickLabelColor);
    }
    if (axisLabelColor.isValid()) {
        setLabelColor(axisLabelColor);
    }
    if (lineColor.isValid() && m_graph) {
        m_linePen.setColor(lineColor);
        m_graph->setPen(m_linePen);
    }
    if (maxHoldColor.isValid() && m_graphMaxHold) {
        m_maxHoldPen.setColor(maxHoldColor);
        m_graphMaxHold->setPen(m_maxHoldPen);
    }
    if (avgColor.isValid() && m_graphAvg) {
        m_avgPen.setColor(avgColor);
        m_graphAvg->setPen(m_avgPen);
    }
    if (gridColor.isValid()) {
        QPen gridPen(gridColor);
        gridPen.setStyle(Qt::DashLine);
        xAxis->grid()->setPen(gridPen);
        yAxis->grid()->setPen(gridPen);
        xAxis->grid()->setZeroLinePen(gridPen);
        yAxis->grid()->setZeroLinePen(gridPen);
        xAxis->setBasePen(QPen(gridColor));
        yAxis->setBasePen(QPen(gridColor));
        xAxis->setTickPen(QPen(gridColor));
        yAxis->setTickPen(QPen(gridColor));
        replot();
    }
}

// ============================================================================
// 右上角悬浮工具栏
// ============================================================================

void HQSpectrum::setupToolButtons()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int btnW = DPR_INT(80 * scale, dpr, 60);
    const int btnH = DPR_INT(27 * scale, dpr, 22);

    m_buttonBar = new QFrame(this);
    m_buttonBar->setFixedHeight(btnH);
    m_toolBarHeight = btnH; // 记录按钮栏高度，供像素判断按钮栏显隐使用
    m_buttonBar->hide();
    m_buttonBar->setStyleSheet("QFrame { background: transparent; }");

    auto *layout = new QHBoxLayout(m_buttonBar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(DPR_INT(2 * scale, dpr, 1));

    const QString labels[] = {
        QStringLiteral("最大保持"),
        QStringLiteral("平均"),
        QStringLiteral("标签"),
        QStringLiteral("网格"),
        QStringLiteral("清理")
    };
    QPushButton **btnPtrs[] = { &m_btnMaxHold, &m_btnAvg, &m_btnLabel, &m_btnGrid, &m_btnClear };

    for (int i = 0; i < 5; ++i) {
        auto *btn = new QPushButton(labels[i], m_buttonBar);
        btn->setFixedSize(btnW, btnH);
        btn->setCursor(Qt::PointingHandCursor);
        *btnPtrs[i] = btn;

        if (i == 4) {
            // "清理"按钮——点击后清理前 2 个按钮的选中状态
            btn->setCheckable(false);
            connect(btn, &QPushButton::clicked, this, [this]() {
                if (m_btnMaxHold) m_btnMaxHold->setChecked(false);
                if (m_btnAvg)     m_btnAvg->setChecked(false);
            });
        } else {
            btn->setCheckable(true);
            connect(btn, &QPushButton::toggled, this, [this, i](bool) {
                updateToolButtonStyle(i);
            });
        }

        layout->addWidget(btn);
        m_toolBtns.append(btn);

        // 立即应用初始未选中样式（避免短暂白底）
        updateToolButtonStyle(i);
    }
}

void HQSpectrum::updateToolButtonStyle(int index)
{
    if (index < 0 || index >= m_toolBtns.size()) return;

    QPushButton *btn = m_toolBtns.at(index);
    const bool checked = btn->isChecked();

    const QString bgOn    = QStringLiteral("rgb(10, 140, 254)");
    const QString bgOff   = QStringLiteral("rgb(7, 31, 61)");
    const QString bg      = checked ? bgOn : bgOff;

    // 边框色用网格颜色
    const QColor gridColor = ThemeManager::instance().color(
        QStringLiteral("CollMonitor.spectrum.gridColor"));
    const QString border = gridColor.isValid()
        ? QString("rgb(%1,%2,%3)").arg(gridColor.red()).arg(gridColor.green()).arg(gridColor.blue())
        : QStringLiteral("rgb(26,48,75)");

    // 无 hover/pressed 变化，只有 checked/!checked 切换
    btn->setStyleSheet(QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: #D6ECFF;"
        "  border: 1px solid %4;"
        "  border-radius: 6px;"
        "  font-size: 12px;"
        "}"
        "QPushButton:checked  { background-color: %2; }"
        "QPushButton:!checked { background-color: %3; }"
        "QPushButton:hover    { }"
        "QPushButton:pressed  { }"
    ).arg(bg, bgOn, bgOff, border));
}

void HQSpectrum::updateToolBarVisibility()
{
    if (!m_buttonBar) return;

    // 按钮栏目标位置：轴矩形右上角
    QCPAxisRect *ar = axisRect();
    const int barW = m_buttonBar->sizeHint().width();
    const int gap  = DPR_INT(6 * ScreenScale::instance().scale(),
                             ScreenScale::instance().dpr(), 4);
    const int barX = static_cast<int>(ar->right()) - barW - gap;
    const int barY = static_cast<int>(ar->top()) + gap;

    // 显示区域 = m_buttonBar 控件范围，高度方向扩展为按钮高度的三倍
    const QRect showRect(barX, barY, barW, m_toolBarHeight * 3);

    // 鼠标位于显示区域内时显示按钮栏，否则隐藏
    if (showRect.contains(m_lastMousePos)) {
        m_buttonBar->move(barX, barY);
        m_buttonBar->show();
        m_buttonBar->raise();
    } else {
        m_buttonBar->hide();
    }
}

void HQSpectrum::onBtnMaxHoldToggled(bool checked)
{
    if (m_graphMaxHold) {
        m_graphMaxHold->setVisible(checked);
        replot();
    }
}

void HQSpectrum::onBtnAvgToggled(bool checked)
{
    if (m_graphAvg) {
        m_graphAvg->setVisible(checked);
        replot();
    }
}

void HQSpectrum::onBtnLabelToggled(bool checked)
{
    // 标记框显隐跟随标签按钮（仅控制频谱图自身，时频图标记由时频图右上角按钮独立控制）
    m_marksVisible = checked;
    this->replot();
}

bool HQSpectrum::spectrumValueAt(double freq, double *value) const
{
    if (!value || m_lastSpectrum.isEmpty() || m_fftlen <= 0 || m_freqPerBin <= 0.0) {
        return false;
    }

    int index = qRound((freq - m_freqStart) / m_freqPerBin);
    index = qBound(0, index, qMin(m_fftlen, m_lastSpectrum.size()) - 1);
    *value = static_cast<double>(m_lastSpectrum.at(index));
    return true;
}

void HQSpectrum::drawTracer(QPainter *painter) const
{
    if (!painter || !m_tracerEnabled || !m_tracerVisible || m_isDragging || m_rightDragging) {
        return;
    }

    const QRect axisRectPx = axisRect()->rect();
    if (!axisRectPx.contains(m_tracerPos)) {
        return;
    }

    const double freq = xAxis->pixelToCoord(m_tracerPos.x());
    double level = yAxis->pixelToCoord(m_tracerPos.y());
    const bool hasSpectrumValue = spectrumValueAt(freq, &level);
    const int tracerX = qBound(axisRectPx.left(), m_tracerPos.x(), axisRectPx.right());
    const int tracerY = qBound(axisRectPx.top(),
                               static_cast<int>(qRound(yAxis->coordToPixel(level))),
                               axisRectPx.bottom());

    painter->save();
    painter->setClipRect(axisRectPx);
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(QColor(255, 210, 80, 180), 1, Qt::DashLine));
    painter->drawLine(tracerX, axisRectPx.top(), tracerX, axisRectPx.bottom());
    //painter->drawLine(axisRectPx.left(), tracerY, axisRectPx.right(), tracerY);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(255, 210, 80, 220));
    painter->drawEllipse(QPoint(tracerX, tracerY), 3, 3);
    painter->restore();

    const QString text = QStringLiteral("%1 MHz  %2 dB%3")
        .arg(freq / 1000000.0, 0, 'f', 6)
        .arg(level, 0, 'f', 1)
        .arg(hasSpectrumValue ? QString() : QString());
        //.arg(hasSpectrumValue ? QString() : QStringLiteral(" (mouse)"));
    const QFontMetrics fm(painter->font());
    const QSize textSize = fm.size(Qt::TextSingleLine, text) + QSize(12, 8);
    int labelX = tracerX + 0;
    int labelY = tracerY - textSize.height() - 8;
    if (labelX + textSize.width() > axisRectPx.right()) {
        labelX = tracerX - textSize.width() - 0;
    }
    if (labelY < axisRectPx.top()) {
        labelY = tracerY + 8;
    }

    labelY = axisRect()->margins().top();
    const QRect labelRect(QPoint(labelX, labelY), textSize);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(QColor(255, 210, 80, 230), 1));
    painter->setBrush(QColor(5, 18, 36, 220));
    //painter->drawRoundedRect(labelRect, 4, 4);
    painter->setPen(QColor(235, 246, 255));
    painter->drawText(labelRect.adjusted(6, 4, -6, -4),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      text);
    painter->restore();
}

void HQSpectrum::paintEvent(QPaintEvent *event)
{
    // 先让 QCustomPlot 渲染正常内容（谱线、网格、轴等）
    QCustomPlot::paintEvent(event);

    {
        QPainter painter(this);
        drawTracer(&painter);
    }

    // 再在顶部绘制标记框（仅绘制有效的 m_markCount 个，不遍历预分配的全部槽位）
    if ((m_markCount > 0 || m_offlineMarkActive) && m_marksVisible) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QCPAxisRect *ar = axisRect();

        // 绘制活动标记框（来自检测对象的标记）
        for (int i = 0; i < m_markCount; ++i) {
            m_marks[i].draw(&painter, ar, xAxis, yAxis);
        }

        // 绘制离线标记框（列表手动选择但当前帧未检测到的信号）
        if (m_offlineMarkActive) {
            m_offlineMark.draw(&painter, ar, xAxis, yAxis);
        }

        // 鼠标悬浮在活动标记上时，绘制 CF/BW 提示文字
#if 0
        if (m_hoveredMarkIndex >= 0 && m_hoveredMarkIndex < m_markCount) {
            const Quadrant quadrant = getQuadrant(QPointF(m_lastMousePos));
            m_marks[m_hoveredMarkIndex].drawTooltip(&painter, QPointF(m_lastMousePos), quadrant);
        } else if (m_hoverOffline && m_offlineMarkActive) {
            // 鼠标悬浮在离线标记上时，显示 tooltip
            const Quadrant quadrant = getQuadrant(QPointF(m_lastMousePos));
            m_offlineMark.drawTooltip(&painter, QPointF(m_lastMousePos), quadrant);
        }
#endif
    }

    // ---- 绘制拖动选择矩形（半透明灰色框） ----
    if (m_isDragging) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false); // 矩形不需要抗锯齿

        // 取起止像素坐标的左右边界
        const int x1 = qMin(m_dragStartPos.x(), m_dragCurrentPos.x());
        const int x2 = qMax(m_dragStartPos.x(), m_dragCurrentPos.x());

        // 限制选择矩形在轴矩形范围内
        const QRect &axisRectPx = axisRect()->rect();
        const int selLeft  = qMax(axisRectPx.left(), x1);
        const int selRight = qMin(axisRectPx.right(), x2);

        if (selRight > selLeft) {
            const QRect selRect(selLeft, axisRectPx.top(),
                                selRight - selLeft, axisRectPx.height());

            // 半透明灰色填充（80/255 透明度 ≈ 31% 不透明）
            painter.fillRect(selRect, QColor(128, 128, 128, 80));
            // 边框略微深色
            painter.setPen(QPen(QColor(128, 128, 128, 180), 1));
            painter.drawRect(selRect);
        }
    }

    // ---- 绘制右键还原手势轨迹线（沿鼠标实际移动路径） ----
    if (m_rightDragging && m_rightTrajectory.size() >= 2) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // 轨迹线限定在轴矩形范围内绘制
        const QRect &axisRectPx = axisRect()->rect();
        painter.setClipRect(axisRectPx);

        // 轨迹线：连接起点到当前位置的鼠标移动轨迹点，圆角圆头衔接
        QPen trajPen(QColor(255, 210, 80, 200), 2.0, Qt::SolidLine);
        trajPen.setCapStyle(Qt::RoundCap);
        trajPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(trajPen);
        painter.drawPolyline(m_rightTrajectory.constData(), m_rightTrajectory.size());

        // 末端绘制一个小圆点，表示画笔当前位置
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 210, 80, 240));
        painter.drawEllipse(m_rightDragCurrentPos, 3, 3);
    }
}

void HQSpectrum::mousePressEvent(QMouseEvent *event)
{
    SpecPlotBase::mousePressEvent(event);

    if (event->button() == Qt::LeftButton) {
        if (m_marksVisible) selectMarkAt(event->pos());

        // ---- 检测是否点击到标记框 ----
        bool hitMark = false;
        for (int i = m_markCount - 1; i >= 0 && m_marksVisible; --i) {
            if (m_marks[i].containsPoint(event->pos(), axisRect(), xAxis)) {
                hitMark = true;
                break;
            }
        }
        if (!hitMark && m_offlineMarkActive) {
            hitMark = m_offlineMark.containsPoint(event->pos(), axisRect(), xAxis);
        }

        // ---- 在数据区空白处按下左键时，记录拖动起始位置 ----
        if (!hitMark && getAxisZone(event->localPos()) == AxisZone::AtCenter) {
            m_dragStartPos   = event->pos();
            m_dragCurrentPos = event->pos();
            m_mousePressed   = true;
            m_isDragging     = false;
            m_pressTimer.start();           // 记录按下时刻：拖动立即激活，松开须距上次移动超过 200ms 且位置稳定才执行缩放
        }
    }

    // ---- 在数据区空白处按下右键时，记录还原手势起始位置 ----
    // 与左键拖动一致：移出按下点死区即激活手势，松开时须超过 200ms 才在目标位置弹出 还原/取消 菜单
    if (event->button() == Qt::RightButton) {
        if (getAxisZone(event->localPos()) == AxisZone::AtCenter) {
            m_rightPressed        = true;
            m_rightDragging       = false;
            m_rightDragStartPos   = event->pos();
            m_rightDragCurrentPos = event->pos();
            // 轨迹从起点开始记录，随鼠标移动不断追加
            m_rightTrajectory.clear();
            m_rightTrajectory.append(event->pos());
            m_pressTimer.start();           // 记录按下时刻：手势立即激活，松开须距上次移动超过 200ms 且位置稳定才弹出菜单
        }
    }
}

void HQSpectrum::mouseMoveEvent(QMouseEvent *event)
{
    // 先调用基类（处理光标、轴区域/象限信号发射等）
    SpecPlotBase::mouseMoveEvent(event);

    m_tracerPos = event->pos();
    const bool nextTracerVisible = m_tracerEnabled
        && !m_isDragging
        && !m_rightDragging
        && getAxisZone(event->localPos()) == AxisZone::AtCenter;
    if (m_tracerVisible != nextTracerVisible || m_tracerVisible) {
        m_tracerVisible = nextTracerVisible;
        update();
    }

    // ---- 拖动选择频率范围 ----
    if (m_mousePressed && (event->buttons() & Qt::LeftButton)) {
        // 确保仍在数据区内
        if (getAxisZone(event->localPos()) == AxisZone::AtCenter) {
            m_dragCurrentPos = event->pos();
            m_pressTimer.restart();         // 刷新最近移动时刻：松开须距上次移动超过 200ms 才执行缩放

            // 指针移出按下点 5px 死区即立即激活拖动（不等待长按），
            // 快速点击/划动由松开时的 200ms 时长检查拦截
            if (!m_isDragging) {
                const int dragDist = (event->pos() - m_dragStartPos).manhattanLength();
                if (dragDist > 5) {
                    m_isDragging = true;
                    setCursor(Qt::CrossCursor);
                }
            }

            // 拖动中持续重绘以更新选择矩形
            if (m_isDragging) {
                update();
            }
        }
    }

    // ---- 右键拖动：还原手势 ----
    if (m_rightPressed && (event->buttons() & Qt::RightButton)) {
        // 确保仍在数据区内
        if (getAxisZone(event->localPos()) == AxisZone::AtCenter) {
            m_rightDragCurrentPos = event->pos();
            m_pressTimer.restart();         // 刷新最近移动时刻：松开须距上次移动超过 200ms 才弹出菜单

            // 记录轨迹点：相邻点间距 ≥2px 才追加，避免同像素重复点
            if (m_rightTrajectory.isEmpty() ||
                (event->pos() - m_rightTrajectory.last()).manhattanLength() >= 2) {
                m_rightTrajectory.append(event->pos());
            }

            // 指针移出按下点 5px 死区即立即激活手势（不等待长按），
            // 快速划动由松开时的 200ms 时长检查拦截
            if (!m_rightDragging) {
                const int dragDist = (event->pos() - m_rightDragStartPos).manhattanLength();
                if (dragDist > 5) {
                    m_rightDragging = true;
                    m_tracerVisible = false;    // 手势期间隐藏游标
                }
            }

            // 手势进行中：保持画笔光标，并持续重绘以更新轨迹线
            if (m_rightDragging) {
                setCursor(m_brushCursor);   // 画笔光标
                update();
            }
        }
    }
}

void HQSpectrum::leaveEvent(QEvent *event)
{
    if (m_tracerVisible) {
        m_tracerVisible = false;
        update();
    }
    // 鼠标离开控件时隐藏按钮栏
    if (m_buttonBar) {
        m_buttonBar->hide();
    }
    SpecPlotBase::leaveEvent(event);
}

void HQSpectrum::mouseReleaseEvent(QMouseEvent *event)
{
    // ---- 拖动选择结束，根据方向执行频率缩放 ----
    if (event->button() == Qt::LeftButton && m_isDragging) {
        // 触发条件：松开位置与上一次移动位置相差不大（稳定松开），
        // 且距上次移动超过 200ms（排除快速划动/误触）
        const int moveDist = (event->pos() - m_dragCurrentPos).manhattanLength();
        if (moveDist > 5 || m_pressTimer.elapsed() < 200) {
            m_mousePressed = false;
            m_isDragging   = false;
            update();   // 清除选择矩形
            SpecPlotBase::mouseReleaseEvent(event);
            return;
        }

        m_mousePressed = false;
        m_isDragging   = false;

        // 计算目标频率范围
        const double freqStart = xAxis->pixelToCoord(m_dragStartPos.x());
        const double freqEnd   = xAxis->pixelToCoord(event->pos().x());

        double targetLower, targetUpper;
        bool   validZoom = false;

        targetLower = qMax(m_fullFreqLower, qMin(freqStart, freqEnd));
        targetUpper = qMin(m_fullFreqUpper, qMax(freqStart, freqEnd));

        // 最小范围保护：避免拖动距离过小时视图崩塌
        if (targetUpper - targetLower >= 1.0) {
            validZoom = true;
        }

        // 清除选择矩形
        update();

        if (!validZoom) {
            SpecPlotBase::mouseReleaseEvent(event);
            return;
        }

        // ---- 300ms 缓出动画：从当前 X 轴范围平滑过渡到目标范围 ----
        const double startLower = xAxis->range().lower;
        const double startUpper = xAxis->range().upper;

        // 如果当前范围与目标范围几乎一致，跳过动画
        if (qAbs(startLower - targetLower) < 0.1 && qAbs(startUpper - targetUpper) < 0.1) {
            SpecPlotBase::mouseReleaseEvent(event);
            return;
        }

        // 停止上一次可能还在运行的动画
        if (m_dragAnim) {
            m_dragAnim->stop();
            m_dragAnim->deleteLater();
            m_dragAnim = nullptr;
        }

        m_dragAnim = new QVariantAnimation(this);
        m_dragAnim->setDuration(300);
        m_dragAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_dragAnim->setStartValue(0.0);
        m_dragAnim->setEndValue(1.0);

        connect(m_dragAnim, &QVariantAnimation::valueChanged, this,
                [this, startLower, startUpper, targetLower, targetUpper](const QVariant &val) {
            const double t = val.toDouble();
            xAxis->setRange(
                startLower + (targetLower - startLower) * t,
                startUpper + (targetUpper - startUpper) * t);
        });

        connect(m_dragAnim, &QVariantAnimation::finished, this, [this]() {
            m_dragAnim->deleteLater();
            m_dragAnim = nullptr;
        });

        m_dragAnim->start();
    }

    // ---- 右键释放：还原手势在目标位置弹出 还原/取消 菜单 ----
    if (event->button() == Qt::RightButton && m_rightDragging) {
        // 触发条件：松开位置与上一次移动位置相差不大（稳定松开），
        // 且距上次移动超过 200ms（排除快速划动/误触）
        const int moveDist = (event->pos() - m_rightDragCurrentPos).manhattanLength();
        if (moveDist > 5 || m_pressTimer.elapsed() < 200) {
            m_rightPressed  = false;
            m_rightDragging = false;
            m_rightTrajectory.clear();
            update();
            SpecPlotBase::mouseReleaseEvent(event);
            return;
        }

        m_rightPressed  = false;
        m_rightDragging = false;

        // 清除轨迹线（光标由下方基类 mouseReleaseEvent 恢复）
        m_rightTrajectory.clear();
        update();

        // 在目标位置（松开处）弹出菜单
        QMenu menu(this);
        QAction *actRestore = menu.addAction(QStringLiteral("还原"));
        menu.addAction(QStringLiteral("取消"));
        // 与暗色主题匹配的菜单样式
        menu.setStyleSheet(
            "QMenu { background-color: rgb(7,31,61); color: #D6ECFF;"
            "  border: 1px solid rgb(26,48,75); border-radius: 4px; padding: 4px; }"
            "QMenu::item { padding: 4px 24px; border-radius: 3px; }"
            "QMenu::item:selected { background-color: rgb(10,140,254); color: #FFFFFF; }");

        QAction *chosen = menu.exec(event->globalPos());
        if (chosen == actRestore) {
            // 还原到完整频率范围（m_fullFreqLower ~ m_fullFreqUpper）
            animateRestoreFullRange();
        }
    }

    // 重置拖动状态
    m_mousePressed = false;
    m_isDragging   = false;
    m_rightPressed  = false;
    m_rightDragging = false;

    // 调用基类（恢复 iRangeDrag 等）
    SpecPlotBase::mouseReleaseEvent(event);
}

void HQSpectrum::selectMarkAt(const QPoint &pos)
{
    int hitIdx = -1;
    bool hitOffline = false;

    // 先检测活动标记框（从上层向下遍历，上层即后添加的优先响应）
    for (int i = m_markCount - 1; i >= 0; --i) {
        if (m_marks[i].containsPoint(pos, axisRect(), xAxis)) {
            hitIdx = i;
            // 停用离线标记框
            m_offlineMarkActive = false;
            break;
        }
    }

    // 未命中活动标记时，检测是否点击了离线标记框
    if (hitIdx < 0 && m_offlineMarkActive) {
        hitOffline = m_offlineMark.containsPoint(pos, axisRect(), xAxis);
    }

    bool changed = false;

    // 更新活动标记框的选中状态
    for (int i = 0; i < m_markCount; ++i) {
        const bool sel = (i == hitIdx);
        if (m_marks[i].isSelected() != sel) {
            m_marks[i].setSelected(sel);
            changed = true;
        }
    }

    // 更新离线标记框的选中状态（点击离线标记时保持高亮，点击其他地方取消高亮）
    if (m_offlineMarkActive) {
        const bool offlineSel = hitOffline;
        if (m_offlineMark.isSelected() != offlineSel) {
            m_offlineMark.setSelected(offlineSel);
            changed = true;
        }
    }

    // 命中标记框（活动或离线）时，发送信号供列表反向定位同步，并触发缩放动画
    if (hitIdx >= 0) {
        emit markClicked(m_marks[hitIdx].id());
        // 触发缩放动画：以命中信号为中心、带宽×10 为视图宽度
        const double centerFreq = (m_marks[hitIdx].freqStart() + m_marks[hitIdx].freqStop()) / 2.0;
        const double bandwidth  = m_marks[hitIdx].freqStop() - m_marks[hitIdx].freqStart();
        animateZoomToFreqRange(centerFreq, bandwidth);
    } else if (hitOffline) {
        emit markClicked(m_offlineMark.id());
        // 触发缩放动画：以离线信号为中心、带宽×10 为视图宽度
        const double centerFreq = (m_offlineMark.freqStart() + m_offlineMark.freqStop()) / 2.0;
        const double bandwidth  = m_offlineMark.freqStop() - m_offlineMark.freqStart();
        animateZoomToFreqRange(centerFreq, bandwidth);
    }

    // 通知另一张图同步选中状态（命中活动/离线标记时发对应 id，否则 -1 全部取消）
    int64_t selId = -1;
    if (hitIdx >= 0) {
        selId = m_marks[hitIdx].id();
    } else if (hitOffline) {
        selId = m_offlineMark.id();
    }
    emit markSelectionChanged(selId);

    if (changed) replot();
}

void HQSpectrum::onExternalMarkSelected(int64_t id)
{
    // 在 m_marks 中查找匹配 ID
    int foundIdx = -1;
    for (int i = 0; i < m_markCount; ++i) {
        if (m_marks[i].id() == id) {
            foundIdx = i;
            break;
        }
    }

    bool changed = false;

    // 更新活动标记框的选中状态：仅命中者高亮，其余（含 -1）取消
    for (int i = 0; i < m_markCount; ++i) {
        const bool sel = (i == foundIdx);
        if (m_marks[i].isSelected() != sel) {
            m_marks[i].setSelected(sel);
            changed = true;
        }
    }

    // 更新离线标记框：仅当 id 命中离线标记时保持高亮
    if (m_offlineMarkActive) {
        const bool offlineSel = (m_offlineMark.id() == id);
        if (m_offlineMark.isSelected() != offlineSel) {
            m_offlineMark.setSelected(offlineSel);
            changed = true;
        }
    }

    if (foundIdx >= 0) animateZoomToFreqRange((m_marks[foundIdx].freqStart() + m_marks[foundIdx].freqStop()) / 2.0, m_marks[foundIdx].freqStop() - m_marks[foundIdx].freqStart());
    if (changed) replot();
}

void HQSpectrum::updateHoverCursor(AxisZone zone, const QPoint &mousePos)
{
    // 记录鼠标位置（供 paintEvent 中 tooltip 定位使用）
    m_lastMousePos = mousePos;

    // 每次鼠标移动时按像素位置判断按钮栏显隐（替代原象限信号）
    updateToolBarVisibility();

    // 先检测是否在标记框上（无论什么区域）
    int hoveredIdx = -1;
    bool hoverOffline = false;
    if (m_marksVisible) {
        // 先检测活动标记框
        for (int i = m_markCount - 1; i >= 0; --i) {
            if (m_marks[i].containsPoint(mousePos, axisRect(), xAxis)) {
                hoveredIdx = i;
                break;
            }
        }
        // 未命中活动标记时，检测离线标记框
        if (hoveredIdx < 0 && m_offlineMarkActive) {
            hoverOffline = m_offlineMark.containsPoint(mousePos, axisRect(), xAxis);
        }
    }

    // 悬浮标记变化时触发重绘，同一标记内移动也需重绘（tooltip 跟随鼠标）
    const bool hoverChanged = (hoveredIdx != m_hoveredMarkIndex) || (hoverOffline != m_hoverOffline);
    if (hoverChanged) {
        m_hoveredMarkIndex = hoveredIdx;
        m_hoverOffline = hoverOffline;
        replot();
    } else if (hoveredIdx >= 0 || hoverOffline) {
        // 鼠标在同一标记内移动：重绘以更新 tooltip 位置
        replot();
    }

    if (hoveredIdx >= 0 || hoverOffline) {
        setCursor(Qt::PointingHandCursor);
        return;
    }

    // 不在标记框上，使用父类默认光标（中心区十字，拖拽区手型，外部箭头）
    SpecPlotBase::updateHoverCursor(zone, mousePos);
}

void HQSpectrum::onBtnGridToggled(bool checked)
{
    const QColor gridColor = ThemeManager::instance().color("CollMonitor.spectrum.gridColor");
    const QColor axisColor = gridColor.isValid() ? gridColor : QColor(26, 48, 75);

    // 刻度线（不隐藏刻度标签文字）
    xAxis->setTickPen(checked ? QPen(axisColor) : Qt::NoPen);
    yAxis->setTickPen(checked ? QPen(axisColor) : Qt::NoPen);
    xAxis->setBasePen(checked ? QPen(axisColor) : Qt::NoPen);
    yAxis->setBasePen(checked ? QPen(axisColor) : Qt::NoPen);
    // 网格线
    xAxis->grid()->setVisible(checked);
    yAxis->grid()->setVisible(checked);
    // 轴矩形背景
    // axisRect()->setBackground(checked ? QBrush(m_bgGradient) : Qt::transparent);

    replot();
}
