#ifndef SIGNALPROPERTIESDIALOG_H
#define SIGNALPROPERTIESDIALOG_H

#include <QDialog>
#include <QPen>
#include <QMap>

// 向前声明
class QComboBox;
class QSpinBox;
class QPushButton;
class QDialogButtonBox;

/**
 * @brief 一个自定义对话框，用于编辑信号的 QPen 属性 (颜色、宽度、样式)
 */
class SignalPropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param initialPen 当前信号的 QPen，用于初始化控件
     * @param parent 父窗口
     */
    explicit SignalPropertiesDialog(const QPen &initialPen, QWidget *parent = nullptr);

    /**
     * @brief 获取用户选择的新 QPen
     */
    QPen getSelectedPen() const;

private slots:
    /**
     * @brief 当颜色选择按钮被点击时
     */
    void onColorButtonClicked();

private:
    /**
     * @brief [辅助] 根据 m_selectedColor 更新颜色按钮的外观
     */
    void updateColorButton();

    QPushButton *m_colorButton;
    QSpinBox *m_widthSpinBox;
    QComboBox *m_styleComboBox;
    QDialogButtonBox *m_buttonBox;

    QColor m_selectedColor;
    // 用于在 ComboBox 文本和 Qt::PenStyle 枚举之间映射
    QMap<QString, Qt::PenStyle> m_styleMap;
};

#endif // SIGNALPROPERTIESDIALOG_H