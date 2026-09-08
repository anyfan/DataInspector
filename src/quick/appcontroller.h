#pragma once
#include <QObject>
#include <QVector>
#include <QThread>
#include <QColor>
#include <QSet>
#include <QVariantList>
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
    Q_PROPERTY(int plotStateRevision READ plotStateRevision NOTIFY plotBindingsChanged)
    Q_PROPERTY(int legendMode READ legendMode WRITE setLegendMode NOTIFY legendModeChanged)
public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;
    SignalModel *signalModel() const { return m_signals; }
    QString status() const { return m_status; }
    QString currentFile() const { return m_currentFile; }
    bool loading() const { return m_loading; }
    int plotRows() const { return m_plotRows; }
    int plotColumns() const { return m_plotColumns; }
    int plotStateRevision() const { return m_plotStateRevision; }
    int legendMode() const { return m_legendMode; }
    Q_INVOKABLE bool loadCsv(const QString &filePath);
    Q_INVOKABLE void selectSignal(int row);
    Q_INVOKABLE void toggleSignal(int row);
    Q_INVOKABLE void filterSignals(const QString &text);
    Q_INVOKABLE void setAllSignalsChecked(bool checked);
    Q_INVOKABLE int checkedSignalCount() const { return m_signals->checkedCount(); }
    Q_INVOKABLE void togglePlotSignal(int plotIndex, int row);
    Q_INVOKABLE bool plotSignalEnabled(int plotIndex, int row) const;
    Q_INVOKABLE QVariantList plotSignalRows(int plotIndex) const;
    Q_INVOKABLE QColor signalColor(int row) const { return m_signalColors.value(row, QColor("#4ea1ff")); }
    Q_INVOKABLE void attachPlot(QObject *plot, int index = 0);
    Q_INVOKABLE void setLayout(int rows, int columns);
    Q_INVOKABLE void clear();
    void setLegendMode(int mode);
signals:
    void statusChanged();
    void currentFileChanged();
    void loadingChanged();
    void layoutChanged();
    void plotBindingsChanged();
    void legendModeChanged();
private:
    void onLoadFinished(const QString &path,
                        const QVector<LoadedTable> &tables,
                        int skipped, const QString &error);
    void setStatus(const QString &status);
    void refreshPlot(int index, bool fitY = true);
    SignalModel *m_signals;
    QVector<PlotItem *> m_plots;
    int m_plotRows = 1;
    int m_plotColumns = 1;
    QString m_status = QStringLiteral("打开 CSV 或 TXT 文件开始查看");
    QString m_currentFile;
    QVector<LoadedTable> m_tables;
    QVector<QPair<int, int>> m_signalLocations;
    QVector<bool> m_enabled;
    QVector<QColor> m_signalColors;
    QVector<QSet<int>> m_plotSignals;
    QThread *m_loadThread = nullptr;
    QObject *m_loader = nullptr;
    bool m_loading = false;
    bool m_syncingRanges = false;
    bool m_syncingCursors = false;
    int m_plotStateRevision = 0;
    int m_legendMode = 0;
};

Q_DECLARE_METATYPE(QVector<QVector<double>>)
Q_DECLARE_METATYPE(LoadedTable)
Q_DECLARE_METATYPE(QVector<LoadedTable>)
