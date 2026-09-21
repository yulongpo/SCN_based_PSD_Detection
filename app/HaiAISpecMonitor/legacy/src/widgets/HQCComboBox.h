#pragma once

#include <QList>
#include <QStringList>
#include <QVariant>
#include <QWidget>

class QLabel;
class QPropertyAnimation;
class QVariantAnimation;

/**
 * @brief HQComboBox 的简化版，去掉了"详情"按钮功能，仅保留基础下拉选择
 */
class HQCComboBox : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal arrowRotation READ arrowRotation WRITE setArrowRotation)
    Q_PROPERTY(qreal emphasisProgress READ emphasisProgress WRITE setEmphasisProgress)

public:
    explicit HQCComboBox(QWidget *parent = nullptr);
    ~HQCComboBox() override;

    void addItem(const QString &text, const QVariant &userData = QVariant());
    void addItems(const QStringList &texts);
    void clear();

    int count() const;
    int currentIndex() const { return m_currentIndex; }
    QString currentText() const;
    QVariant currentData() const;

    QString placeholderText() const { return m_placeholderText; }
    void setPlaceholderText(const QString &text);
    void setCurrentIndex(int index);

    QSize sizeHint() const override;
    void setCusFont(int cusFont);

signals:
    void currentIndexChanged(int index);
    void currentTextChanged(const QString &text);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void applyThemeAppearance(bool night);

public:
    struct ItemEntry {
        QString text;
        QVariant data;
    };

private:
    qreal arrowRotation() const { return m_arrowRotation; }
    void setArrowRotation(qreal rotation);

    qreal emphasisProgress() const { return m_emphasisProgress; }
    void setEmphasisProgress(qreal progress);

    QColor themeColor(const QString &key, const QColor &fallback = QColor()) const;
    void animateArrowTo(qreal value);
    void animateEmphasisTo(qreal value);
    void ensurePopup();
    void showPopup();
    void hidePopup();
    void updatePopupGeometry();
    void updateCurrentDisplay();
    void updateFonts();
    void refreshMetrics();
    qreal targetEmphasis() const;
    int scaledPx(int designPx, int min = 0) const;

    QLabel *m_textLabel;
    QWidget *m_popup;
    QList<ItemEntry> m_items;

    QPropertyAnimation *m_arrowAnimation;
    QPropertyAnimation *m_emphasisAnimation;
    QVariantAnimation *m_popupOpenAnimation;
    QVariantAnimation *m_popupCloseAnimation;

    QString m_placeholderText;
    int m_currentIndex;
    qreal m_arrowRotation;
    qreal m_emphasisProgress;
    bool m_hovered;
    bool m_popupVisible;
    bool m_popupAnimating;
    int m_fontSize;
    bool m_cursorOverridden = false;           // 是否已设置全局禁止光标覆盖
};
