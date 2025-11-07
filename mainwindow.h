#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

// 向前声明 QCustomPlot 类
class QCustomPlot;
class QAction;

/**
 * @brief 主窗口类，作为 Milstone 1 的 UI 骨架
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    /**
     * @brief 响应“加载CSV”菜单动作的槽函数
     */
    void on_actionLoadCsv_triggered();

private:
    /**
     * @brief 创建所有 QAction
     */
    void createActions();

    /**
     * @brief 创建菜单栏
     */
    void createMenus();

    /**
     * @brief 初始化 QCustomPlot 的基本设置
     */
    void setupPlot();

    // UI 控件
    QCustomPlot *m_customPlot; // 绘图控件 (来自 4.1 节)

    // 菜单动作
    QAction *m_loadCsvAction;
};

#endif // MAINWINDOW_H