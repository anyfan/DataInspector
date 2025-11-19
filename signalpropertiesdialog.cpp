#include "signalpropertiesdialog.h"
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QColorDialog>
#include <QVector>
#include <QColor>

static const QVector<QColor> matlabColors = {
    QColor("#0072bd"), QColor("#d95319"), QColor("#edb120"),
    QColor("#7e2f8e"), QColor("#77ac30"), QColor("#4dbeee"),
    QColor("#a2142f"), QColor("#139fff"), QColor("#ff6929"),
    QColor("#b746ff"), QColor("#64d413"), QColor("#ff13a6"),
    QColor("#fe330a"), QColor("#22b573")};

SignalPropertiesDialog::SignalPropertiesDialog(const QPen &initialPen, QWidget *parent)
    : QDialog(parent), m_selectedColor(initialPen.color())
{
    setWindowTitle(tr("Signal Properties"));

    //  1. 创建控件 

    // 颜色按钮
    m_colorButton = new QPushButton(this);
    m_colorButton->setFixedSize(60, 20);
    m_colorButton->setFlat(true);
    m_colorButton->setAutoFillBackground(true);
    updateColorButton(); // 设置初始颜色

    // 宽度选择
    m_widthSpinBox = new QSpinBox(this);
    m_widthSpinBox->setMinimum(1);
    m_widthSpinBox->setMaximum(20);
    m_widthSpinBox->setSuffix(" px");
    // QPen 宽度为 0 是“装饰笔”（总是 1px），我们的最小宽度为 1。
    m_widthSpinBox->setValue(initialPen.width() > 0 ? initialPen.width() : 1);

    // 样式选择
    m_styleComboBox = new QComboBox(this);
    m_styleMap.insert(tr("Solid Line"), Qt::SolidLine);
    m_styleMap.insert(tr("Dash Line"), Qt::DashLine);
    m_styleMap.insert(tr("Dot Line"), Qt::DotLine);
    m_styleMap.insert(tr("Dash Dot Line"), Qt::DashDotLine);
    m_styleMap.insert(tr("Dash Dot Dot Line"), Qt::DashDotDotLine);
    m_styleComboBox->addItems(m_styleMap.keys());
    // 查找与 initialPen.style() 匹配的文本
    m_styleComboBox->setCurrentText(m_styleMap.key(initialPen.style(), tr("Solid Line")));

    // OK 和 Cancel 按钮
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    //  2. 布局 
    QFormLayout *formLayout = new QFormLayout;
    formLayout->addRow(tr("Color:"), m_colorButton);
    formLayout->addRow(tr("Width:"), m_widthSpinBox);
    formLayout->addRow(tr("Style:"), m_styleComboBox);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(m_buttonBox);

    //  3. 连接 
    connect(m_colorButton, &QPushButton::clicked, this, &SignalPropertiesDialog::onColorButtonClicked);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setMinimumWidth(250); // 设置一个合理的最小宽度
}

/**
 * @brief 从控件的当前状态构造并返回一个新的 QPen
 */
QPen SignalPropertiesDialog::getSelectedPen() const
{
    QPen pen;
    pen.setColor(m_selectedColor);
    pen.setWidth(m_widthSpinBox->value());
    pen.setStyle(m_styleMap.value(m_styleComboBox->currentText()));
    return pen;
}

/**
 * @brief 打开颜色对话框并更新按钮
 */
void SignalPropertiesDialog::onColorButtonClicked()
{
    //  使用 QColorDialog 实例并设置自定义颜色 
    QColorDialog dialog(m_selectedColor, this);
    dialog.setWindowTitle(tr("Select Signal Color"));
    dialog.setOptions(QColorDialog::ShowAlphaChannel); // 允许设置透明度

    // 设置14种自定义颜色
    for (int i = 0; i < matlabColors.size() && i < 16; ++i) // QColorDialog 最多支持 16 个自定义颜色
    {
        dialog.setCustomColor(i, matlabColors.at(i));
    }

    // 执行对话框
    if (dialog.exec() == QDialog::Accepted)
    {
        QColor newColor = dialog.selectedColor();
        if (newColor.isValid())
        {
            m_selectedColor = newColor;
            updateColorButton();
        }
    }
}

/**
 * @brief [辅助] 更新颜色按钮的背景色
 */
void SignalPropertiesDialog::updateColorButton()
{
    // 使用样式表来显示颜色预览
    m_colorButton->setStyleSheet(QString("background-color: %1; border: 1px solid #888;").arg(m_selectedColor.name()));
}