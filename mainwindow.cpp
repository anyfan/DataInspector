#include "mainwindow.h"
#include "qcustomplot.h" // 包含 QCustomPlot 头文件
#include "csvloader.h"   // 包含我们简易的CSV加载器

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QVector>
#include <QStringList>
#include <QMessageBox>
#include <QColor>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_customPlot(nullptr), m_loadCsvAction(nullptr)
{
    // 1. 创建 UI 骨架 (Milestone 1)
    m_customPlot = new QCustomPlot(this);
    setCentralWidget(m_customPlot);

    // 2. 设置绘图控件
    setupPlot();

    // 3. 创建菜单和动作
    createActions();
    createMenus();

    // 4. 设置窗口标题
    setWindowTitle(tr("Data Inspector (Milestone 1)"));
}

MainWindow::~MainWindow()
{
    // m_customPlot 作为 centralWidget 会被 Qt 自动删除
}

void MainWindow::createActions()
{
    // 创建“加载CSV”动作
    m_loadCsvAction = new QAction(tr("&Load CSV..."), this);
    m_loadCsvAction->setShortcut(QKeySequence::Open);

    // 连接信号槽
    connect(m_loadCsvAction, &QAction::triggered, this, &MainWindow::on_actionLoadCsv_triggered);
}

void MainWindow::createMenus()
{
    // 创建“文件”菜单
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_loadCsvAction);
}

void MainWindow::setupPlot()
{
    // 基本的绘图设置
    m_customPlot->xAxis->setLabel(tr("Time (s)"));
    m_customPlot->yAxis->setLabel(tr("Value"));

    // 启用交互：拖动、缩放
    m_customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

    // 显示图例
    m_customPlot->legend->setVisible(true);
}

void MainWindow::on_actionLoadCsv_triggered()
{
    // 1. 打开文件对话框
    QString filePath = QFileDialog::getOpenFileName(this,
                                                    tr("Open CSV File"), "", tr("CSV Files (*.csv *.txt)"));

    if (filePath.isEmpty())
    {
        return; // 用户取消
    }

    // 2. 准备数据容器
    QVector<double> timeVector;            // X 轴数据 (第一列)
    QVector<QVector<double>> valueVectors; // Y 轴数据 (所有其他列)
    QStringList headers;                   // 列标题

    // 3. 调用 CSV 加载器
    bool success = CsvLoader::loadCsv(filePath, timeVector, valueVectors, headers);

    if (!success)
    {
        QMessageBox::warning(this, tr("Error"), tr("Failed to load or parse the CSV file."));
        return;
    }

    if (timeVector.isEmpty() || valueVectors.isEmpty())
    {
        QMessageBox::information(this, tr("Empty File"), tr("The CSV file is empty or has no data columns."));
        return;
    }

    // 4. 加载数据到 QCustomPlot
    m_customPlot->clearGraphs(); // 清除旧数据

    // 假设 headers[0] 是 time, headers[1...] 是 value
    int numValueColumns = valueVectors.count();

    for (int i = 0; i < numValueColumns; ++i)
    {
        QCPGraph *graph = m_customPlot->addGraph();

        // 设置数据
        graph->setData(timeVector, valueVectors[i]);

        // 从 header 设置图例名称
        if (i + 1 < headers.count())
        {
            graph->setName(headers[i + 1]);
        }
        else
        {
            graph->setName(QString("Column %1").arg(i + 1));
        }

        // 设置一个唯一的颜色 (简单的颜色循环)
        // 7-16 是一些比较清晰的 Qt::GlobalColor
        QColor color(Qt::GlobalColor(7 + (i % 10)));
        graph->setPen(QPen(color));
    }

    // 5. 自动缩放轴并重绘
    m_customPlot->rescaleAxes();
    m_customPlot->replot();

    qDebug() << "Successfully loaded and plotted" << filePath;
}