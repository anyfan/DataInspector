#pragma once
#include "plotlodbuilder.h"
#include <QObject>
#include <QTimer>
#include <atomic>

// GUI-thread scheduler. Workers own values only, never QObjects or scene nodes.
class PlotLodScheduler final : public QObject
{
    Q_OBJECT
public:
    explicit PlotLodScheduler(QObject *parent = nullptr);
    ~PlotLodScheduler() override;
    void request(const PlotSeriesSnapshot &snapshot, const LodRequestKey &key);
    void cancel();
    bool pending() const { return bool(m_job); }
    std::shared_ptr<const LodResult> result() const { return m_result; }
signals:
    void ready();
private:
    struct Job {
        std::atomic_bool cancelled{false};
        std::atomic_bool done{false};
        std::shared_ptr<const LodResult> result;
        quint64 revision = 0;
    };
    void start();
    void finish();
    QTimer m_poll;
    PlotSeriesSnapshot m_snapshot;
    std::optional<LodRequestKey> m_key;
    quint64 m_revision = 0;
    std::shared_ptr<Job> m_job;
    std::shared_ptr<const LodResult> m_result;
};
