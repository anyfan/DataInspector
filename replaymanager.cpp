#include "replaymanager.h"
#include "cursormanager.h" // 确保包含完整的定义
#include <QDockWidget>
#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QTimer>
#include <QAction>
#include <QMainWindow>
#include <QStyle>
#include <QHBoxLayout>
#include <QSignalBlocker>

ReplayManager::ReplayManager(QAction *replayAction,
                             CursorManager *cursorManager,
                             QMainWindow *parentWindow)
    : QObject(parentWindow),
      m_cursorManager(cursorManager),
      m_replayDock(nullptr),
      m_replayWidget(nullptr),
      m_playPauseButton(nullptr),
      m_stepForwardButton(nullptr),
      m_stepBackwardButton(nullptr),
      m_speedSpinBox(nullptr),
      m_timeSlider(nullptr),
      m_currentTimeLabel(nullptr),
      m_replayTimer(nullptr),
      m_minTimeStep(0.01),
      m_cursorKey1(0)
{
    // 1. 创建 UI
    createReplayDock(parentWindow);

    // 2. 创建定时器
    m_replayTimer = new QTimer(this);
    connect(m_replayTimer, &QTimer::timeout, this, &ReplayManager::onReplayTimerTimeout);

    // 3. 连接外部动作
    if (replayAction)
    {
        connect(replayAction, &QAction::toggled, this, &ReplayManager::onReplayActionToggled);
    }
}

ReplayManager::~ReplayManager()
{
    // QObject 的父子关系会自动处理 m_replayDock 及其子控件的删除
}

QDockWidget *ReplayManager::getDockWidget() const
{
    return m_replayDock;
}

/**
 * @brief 创建底部重放停靠栏 (从 MainWindow 移来)
 */
void ReplayManager::createReplayDock(QMainWindow *parentWindow)
{
    QStyle *style = parentWindow->style(); // 从父窗口获取样式

    m_replayDock = new QDockWidget(tr("重放控制"), parentWindow);
    m_replayWidget = new QWidget(m_replayDock);

    QHBoxLayout *layout = new QHBoxLayout(m_replayWidget);

    m_stepBackwardButton = new QPushButton(style->standardIcon(QStyle::SP_MediaSeekBackward), "", m_replayWidget);
    m_playPauseButton = new QPushButton(style->standardIcon(QStyle::SP_MediaPlay), "", m_replayWidget);
    m_stepForwardButton = new QPushButton(style->standardIcon(QStyle::SP_MediaSeekForward), "", m_replayWidget);

    m_currentTimeLabel = new QLabel(tr("Time: 0.0"), m_replayWidget);
    m_timeSlider = new QSlider(Qt::Horizontal, m_replayWidget);
    m_timeSlider->setMinimum(0);
    m_timeSlider->setMaximum(10000); // 10000 步的分辨率

    QLabel *speedLabel = new QLabel(tr("Speed:"), m_replayWidget);
    m_speedSpinBox = new QDoubleSpinBox(m_replayWidget);
    m_speedSpinBox->setMinimum(0.1);
    m_speedSpinBox->setMaximum(100.0);
    m_speedSpinBox->setValue(1.0);
    m_speedSpinBox->setSuffix("x");

    layout->addWidget(m_stepBackwardButton);
    layout->addWidget(m_playPauseButton);
    layout->addWidget(m_stepForwardButton);
    layout->addWidget(m_currentTimeLabel);
    layout->addWidget(m_timeSlider, 1); // 1 = stretch factor
    layout->addWidget(speedLabel);
    layout->addWidget(m_speedSpinBox);

    m_replayDock->setWidget(m_replayWidget);
    m_replayDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);

    // 默认隐藏
    m_replayDock->hide();

    // 连接信号 (连接到 *this* 的槽)
    connect(m_playPauseButton, &QPushButton::clicked, this, &ReplayManager::onPlayPauseClicked);
    connect(m_stepForwardButton, &QPushButton::clicked, this, &ReplayManager::onStepForwardClicked);
    connect(m_stepBackwardButton, &QPushButton::clicked, this, &ReplayManager::onStepBackwardClicked);
    connect(m_timeSlider, &QSlider::valueChanged, this, &ReplayManager::onTimeSliderChanged);
}

/**
 * @brief [槽] 更新数据范围
 */
void ReplayManager::updateDataRange(const QCPRange &range, double minStep)
{
    m_globalTimeRange = range;
    m_minTimeStep = (minStep > 0) ? minStep : 0.01;
    updateReplayControls();
}

/**
 * @brief [槽] 响应游标位置变化，更新本地状态
 */
void ReplayManager::onCursorKeyChanged(double key, int cursorIndex)
{
    if (cursorIndex == 1)
    {
        m_cursorKey1 = key;
        updateReplayControls(); // 更新滑块和标签
    }
}

/**
 * @brief 响应重放按钮切换
 */
void ReplayManager::onReplayActionToggled(bool checked)
{
    m_replayDock->setVisible(checked);

    if (checked && m_cursorManager->getMode() == CursorManager::NoCursor)
    {
        // 如果没有游标，自动启用单游标
        // (我们不再需要访问 m_cursorSingleAction)
        m_cursorManager->setMode(CursorManager::SingleCursor);
    }
}

/**
 * @brief 更新重放控件 (滑块和标签)
 */
void ReplayManager::updateReplayControls()
{
    if (m_globalTimeRange.size() <= 0)
        return;

    // 更新标签
    m_currentTimeLabel->setText(tr("Time: %1").arg(m_cursorKey1, 0, 'f', 4));

    // 更新滑块
    double relativePos = (m_cursorKey1 - m_globalTimeRange.lower) / m_globalTimeRange.size();
    {
        QSignalBlocker blocker(m_timeSlider); // 阻止触发 onTimeSliderChanged
        m_timeSlider->setValue(relativePos * m_timeSlider->maximum());
    }
}

// --- 播放逻辑 (从 MainWindow 移来) ---

void ReplayManager::onPlayPauseClicked()
{
    if (m_replayTimer->isActive())
    {
        m_replayTimer->stop();
        m_playPauseButton->setIcon(m_playPauseButton->style()->standardIcon(QStyle::SP_MediaPlay));
    }
    else
    {
        m_replayTimer->setInterval(33); // 约 30fps
        m_replayTimer->start();
        m_playPauseButton->setIcon(m_playPauseButton->style()->standardIcon(QStyle::SP_MediaPause));
    }
}

void ReplayManager::onReplayTimerTimeout()
{
    double speed = m_speedSpinBox->value();
    double timeStep = (m_replayTimer->interval() / 1000.0) * speed;

    if (timeStep <= 0 || speed <= 0)
        return;

    double newKey = m_cursorKey1 + timeStep; // 使用本地 m_cursorKey1

    if (newKey > m_globalTimeRange.upper)
    {
        newKey = m_globalTimeRange.lower; // 循环
    }

    // 命令 CursorManager 更新
    m_cursorManager->updateCursors(newKey, 1);
}

void ReplayManager::onStepForwardClicked()
{
    if (m_replayTimer->isActive())
        return;

    double timeStep = m_minTimeStep;
    double newKey = m_cursorKey1 + timeStep;
    m_cursorManager->updateCursors(newKey, 1);
}

void ReplayManager::onStepBackwardClicked()
{
    if (m_replayTimer->isActive())
        return;

    double timeStep = m_minTimeStep;
    double newKey = m_cursorKey1 - timeStep;
    m_cursorManager->updateCursors(newKey, 1);
}

void ReplayManager::onTimeSliderChanged(int value)
{
    if (m_replayTimer->isActive())
        return; // 播放时，定时器优先

    if (m_globalTimeRange.size() <= 0)
        return;

    double relativePos = (double)value / m_timeSlider->maximum();
    double newKey = m_globalTimeRange.lower + relativePos * m_globalTimeRange.size();

    m_cursorManager->updateCursors(newKey, 1);
}