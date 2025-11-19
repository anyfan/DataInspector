#ifndef REPLAYMANAGER_H
#define REPLAYMANAGER_H

#include <QObject>
// #include <QCPRange> 
#include "cursormanager.h"
#include "qcustomplot.h"

// 向前声明
class QDockWidget;
class QWidget;
class QPushButton;
class QSlider;
class QLabel;
class QDoubleSpinBox;
class QTimer;
class QAction;
class QMainWindow;
class QStyle;

/**
 * @brief 重放管理器
 * * 负责创建和管理重放停靠栏、控件和定时器逻辑
 */
class ReplayManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param replayAction 指向 "重放" 菜单/工具栏动作
     * @param cursorManager 指向已创建的游标管理器
     * @param parentWindow 指向主窗口 (用于获取 style() 和作为父级)
     */
    explicit ReplayManager(QAction *replayAction,
                           CursorManager *cursorManager,
                           QMainWindow *parentWindow);
    ~ReplayManager();

    /**
     * @brief 获取此管理器创建的停靠栏
     */
    QDockWidget *getDockWidget() const;

public slots:
    /**
     * @brief [槽] 更新此管理器所知的全局数据范围
     * @param range 全局时间范围
     * @param minStep 最小时间步长
     */
    void updateDataRange(const QCPRange &range, double minStep);

    /**
     * @brief [槽] 响应游标位置变化
     * @param key 游标 1 的新 X 轴坐标
     * @param cursorIndex 发生变化的游标 (1 或 2)
     */
    void onCursorKeyChanged(double key, int cursorIndex);

private slots:
    /**
     * @brief 响应 "重放" 动作的切换
     */
    void onReplayActionToggled(bool checked);

    //  内部重放控制槽 
    void onPlayPauseClicked();
    void onStepForwardClicked();
    void onStepBackwardClicked();
    void onReplayTimerTimeout();
    void onTimeSliderChanged(int value);

private:
    /**
     * @brief 创建重放UI控件
     */
    void createReplayDock(QMainWindow *parentWindow);

    /**
     * @brief 更新重放控件的状态 (滑块范围、标签)
     */
    void updateReplayControls();

    //  依赖 
    CursorManager *m_cursorManager; // (不拥有)

    //  UI 控件 (拥有) 
    QDockWidget *m_replayDock;
    QWidget *m_replayWidget;
    QPushButton *m_playPauseButton;
    QPushButton *m_stepForwardButton;
    QPushButton *m_stepBackwardButton;
    QDoubleSpinBox *m_speedSpinBox;
    QSlider *m_timeSlider;
    QLabel *m_currentTimeLabel;
    QTimer *m_replayTimer;

    //  内部状态 
    QCPRange m_globalTimeRange;
    double m_minTimeStep;
    double m_cursorKey1; // 本地缓存的游标 1 位置
};

#endif // REPLAYMANAGER_H