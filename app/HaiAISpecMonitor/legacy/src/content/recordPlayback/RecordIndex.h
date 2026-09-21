#pragma once

#include <QWidget>
#include <QStringList>
#include <QVector>
#include "radioai/icd/SignalData.hpp"

class QVBoxLayout;
class RecordFileList;
class HQDetailInfo;
class HQDetailList;

/**
 * @brief 录制回放面板控件
 *
 * 包含列表页和详情页，支持滑动切换动画。
 * 列表页显示录制文件列表，详情页显示选中文件的详细信息和信号列表。
 */
class RecordIndex : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造录制回放面板
     * @param parent 父控件
     */
    explicit RecordIndex(QWidget *parent = nullptr);

    /** @brief 返回内容区最小宽度（逻辑像素） */
    int minimumContentWidth() const;

signals:
    /**
     * @brief 文件信号明细查询请求
     * @param req 请求信息
     */
    void signalDetailPerFileQueryReq(const SignalDetailPerFileQueryReq& req);

    /**
     * @brief 频谱文件删除请求
     * @param req 删除信息
     */
    void spectrumFileDelReq(const SpectrumFileDelReq& req);

    /**
     * @brief 信号删除请求
     * @param req 删除信息
     */
    void signalDelReq(const SignalDetailPerFileDelReq& req);

    /**
     * @brief 请求刷新文件列表
     */
    void requestFileListRefresh();

    /**
     * @brief 信号列表
     * @param resp 信号列表
     */
    void dialogSignalDetail(const SignalDetailPerFileQueryResp& resp);

public slots:
    /**
     * @brief 频谱文件信息
     * @param info 频谱文件信息
     */
    void spectrumFileInfo(const SpectrumFileInfo& info) const;

    /**
     * @brief 频谱文件信息列表
     * @param infos 频谱文件信息列表
     */
    void spectrumFileInfos(const SpectrumFileInfos& infos) const;

    /**
     * @brief 文件信号明细查询回复
     * @param resp 回复结果
     */
    void signalDetailPerFileQueryResp(const SignalDetailPerFileQueryResp& resp);

    /**
     * @brief 频谱文件删除回复
     * @param resp 删除回复
     */
    void spectrumFileDelResp(const SpectrumFileDelResp& resp) const;

    /**
     * @brief 信号删除回复
     * @param resp 删除回复
     */
    void signalDelResp(const SignalDetailPerFileDelResp& resp) const;

    /** @brief 导出文件列表到CSV（信号列表页底部导出按钮） */
    void onExportFileList();
    /** @brief 导出当前详情页信号到CSV（详情页面底部导出按钮） */
    void onExportDetail();

    /**
     * @brief 信号列表查询
     * @param fileId 文件ID
     */
    void dialogSignalDetailSearch(int64_t fileId);

    /** @brief 打开回放界面播放这次实时的数据 */
    void openRecordDialog();

    /**
     * @brief 设置频谱数据存储路径
     * @param signalPath[in] 频谱数据存储路径
     */
    void setSignalPath(const QString& signalPath);

protected:
    /**
     * @brief 事件过滤器，监听页面容器大小变化以同步页面尺寸
     */
    bool eventFilter(QObject *obj, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    /** @brief 初始化 UI 组件 */
    void initUI();
    /** @brief 初始化信号与槽连接 */
    void initConnects();
    /**
     * @brief 滑动切换到指定页面
     * @param index 页面索引：0-列表页，1-详情页
     * @param rowData 传递给详情页的数据
     */
    void slideToPage(int index, const QStringList &rowData = QStringList());

    /** @brief 更新详情页数据 */
    void updateDetailData(const QStringList &rowData);

    /** @brief 发送删除请求（根据 file_id 列表） */
    void doDeleteFiles(const QVector<qint64>& fileIds);

    /** @brief 发送信号删除请求 */
    void doDeleteSignals(qint64 fileId, const QVector<qint64>& signalIds);

    /** @brief 将 SpectrumFileInfo 转为行数据 QStringList */
    QStringList fileInfoToRowData(const SpectrumFileInfo& info, int index) const;

    /** @brief 格式化 epoch 毫秒为时间字符串 */
    static QString epochToTimeStr(int64_t epochMs);

    /** @brief 格式化时长为可读字符串 */
    static QString formatDuration(int64_t seconds);

    QWidget         *m_pageContainer   = nullptr;  ///< 页面容器，用于滑动切换动画

    // ---- 页面 0：列表页 ----
    RecordFileList  *m_recordFileList  = nullptr;

    // ---- 页面 1：详情页 ----
    QWidget         *m_detailPage      = nullptr;  ///< 详情页面容器
    HQDetailInfo    *m_detailInfo      = nullptr;  ///< 文件详情
    HQDetailList    *m_detailList      = nullptr;  ///< 信号列表

    int              m_currentPageIndex = 0;       ///< 当前页面索引：0-列表页，1-详情页
    int              m_pendingIndex     = -1;       ///< 动画期间待切换到的页面索引，-1 表示无待切换
    bool             m_animating        = false;    ///< 动画进行中标志

    qint64           m_currentFileId    = 0;        ///< 当前详情页的文件ID
};
