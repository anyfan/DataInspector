#pragma once
#include <QQuickItem>
#include <QMutex>
#include <QTimer>
#include "render/statetimeline.h"

class StateTimelineItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(int rowHeight READ rowHeight CONSTANT)
    Q_PROPERTY(int headerHeight READ headerHeight CONSTANT)
    Q_PROPERTY(double minimum READ minimum WRITE setMinimum NOTIFY viewportChanged)
    Q_PROPERTY(double maximum READ maximum WRITE setMaximum NOTIFY viewportChanged)
    Q_PROPERTY(QVariantList labels READ labels NOTIFY frameChanged)
    Q_PROPERTY(QStringList rowNames READ rowNames NOTIFY sourcesChanged)
    Q_PROPERTY(QString error READ error NOTIFY frameChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY frameChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameChanged)
public:
    static constexpr int RowHeight = 32, HeaderHeight = 0;
    int rowHeight() const { return RowHeight; }
    int headerHeight() const { return HeaderHeight; }
    explicit StateTimelineItem(QQuickItem *parent = nullptr);
    ~StateTimelineItem() override;
    void setSources(const StateTimelineConfig &config, const PlotSeriesSnapshot &snapshot);
    double minimum() const { return m_minimum; }
    double maximum() const { return m_maximum; }
    void setMinimum(double value);
    void setMaximum(double value);
    QVariantList labels() const;
    QStringList rowNames() const;
    QString error() const;
    bool hasFrame() const;
    bool pending() const { return bool(m_job) || m_debounce.isActive(); }
    Q_INVOKABLE QStringList cursorValues(double time) const;
    Q_INVOKABLE QString inspect(int row, double time) const;
    Q_INVOKABLE QVariant transition(int row, double time, int direction) const;
signals:
    void sourcesChanged();
    void viewportChanged();
    void frameChanged();
protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
private:
    struct Cell { QRectF rect; QColor color; };
    struct Frame { QVector<Cell> cells; QVariantList labels; QStringList names; QString error; double minimum = 0, maximum = 1, width = 1; };
    struct Job {
        std::atomic_bool cancelled{false}, done{false};
        StateTimelineConfig config; PlotSeriesSnapshot sources;
        std::shared_ptr<const StateTimelineData> data;
        std::shared_ptr<const Frame> frame;
        double minimum = 0, maximum = 1; QSizeF size;
    };
    void request();
    void start();
    void finish();
    double m_minimum = 0, m_maximum = 1;
    // Current projection is published under the same lock as the immutable frame.
    double m_displayMinimum = 0, m_displayMaximum = 1, m_displayWidth = 1;
    StateTimelineConfig m_config;
    PlotSeriesSnapshot m_sources;
    std::shared_ptr<const StateTimelineData> m_data;
    std::shared_ptr<const Frame> m_frame;
    std::shared_ptr<Job> m_job;
    QTimer m_poll, m_debounce;
    mutable QMutex m_mutex;
};
