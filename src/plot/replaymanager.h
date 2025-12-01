#ifndef REPLAYMANAGER_H
#define REPLAYMANAGER_H

#include <QObject>
#include <QDockWidget>
#include "qcustomplot.h"

class ReplayManager : public QObject
{
    Q_OBJECT

public:
    explicit ReplayManager(QAction *replayAction,
                           QMainWindow *parentWindow);
    ~ReplayManager();

    QDockWidget *getDockWidget() const;

public slots:
    void updateDataRange(const QCPRange &range, double minStep);
    void onCursorKeyChanged(double key, int cursorIndex);

signals:
    void replayTimeChanged(double key, int cursorIndex);

private slots:
    void onReplayActionToggled(bool checked);
    void onPlayPauseClicked();
    void onStepClicked();
    void onReplayTimerTimeout();
    void onTimeSliderChanged(int value);

private:
    void createReplayDock(QMainWindow *parentWindow);
    void updateReplayControls();

    QDockWidget *m_replayDock;
    QWidget *m_replayWidget;
    QPushButton *m_playPauseButton;
    QPushButton *m_stepForwardButton;
    QPushButton *m_stepBackwardButton;
    QDoubleSpinBox *m_speedSpinBox;
    QSlider *m_timeSlider;
    QLabel *m_currentTimeLabel;
    QTimer *m_replayTimer;

    QCPRange m_globalTimeRange;
    double m_minTimeStep;
    double m_cursorKey1;
};

#endif // REPLAYMANAGER_H