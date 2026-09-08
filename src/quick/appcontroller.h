#pragma once
#include <QObject>
#include <QVector>
#include <QThread>
#include "signalmodel.h"
class PlotItem;

struct LoadedTable
{
    QString name;
    QStringList signalNames;
    QVector<double> time;
    QVector<QVector<double>> values;
};

class AppController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(SignalModel *signalModel READ signalModel CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int plotRows READ plotRows NOTIFY layoutChanged)
    Q_PROPERTY(int plotColumns READ plotColumns NOTIFY layoutChanged)
public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;
    SignalModel *signalModel() const { return m_signals; }
    QString status() const { return m_status; }
    QString currentFile() const { return m_currentFile; }
    bool loading() const { return m_loading; }
    int plotRows() const { return m_plotRows; }
    int plotColumns() const { return m_plotColumns; }
    Q_INVOKABLE bool loadCsv(const QString &filePath);
    Q_INVOKABLE void selectSignal(int row);
    Q_INVOKABLE void toggleSignal(int row);
    Q_INVOKABLE void attachPlot(QObject *plot, int index = 0);
    Q_INVOKABLE void setLayout(int rows, int columns);
    Q_INVOKABLE void clear();
signals:
    void statusChanged();
    void currentFileChanged();
    void loadingChanged();
    void layoutChanged();
private:
    void onLoadFinished(const QString &path,
                        const QVector<LoadedTable> &tables,
                        int skipped, const QString &error);
    void setStatus(const QString &status);
    SignalModel *m_signals;
    QVector<PlotItem *> m_plots;
    int m_plotRows = 1;
    int m_plotColumns = 1;
    QString m_status = QStringLiteral("打开 CSV 或 TXT 文件开始查看");
    QString m_currentFile;
    QVector<LoadedTable> m_tables;
    QVector<QPair<int, int>> m_signalLocations;
    QVector<bool> m_enabled;
    QThread *m_loadThread = nullptr;
    QObject *m_loader = nullptr;
    bool m_loading = false;
};

Q_DECLARE_METATYPE(QVector<QVector<double>>)
Q_DECLARE_METATYPE(LoadedTable)
Q_DECLARE_METATYPE(QVector<LoadedTable>)
