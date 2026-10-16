#pragma once
#include <QObject>
#include <QJsonObject>
#include <QTcpServer>
#include <QThreadPool>
#include <QHash>
#include <QTimer>
#include <atomic>
#include <memory>

class AppController;
class QTcpSocket;

// Opt-in loopback JSON service. All controller access stays on the GUI thread.
class AutomationServer final : public QObject
{
    Q_OBJECT
public:
    explicit AutomationServer(AppController *controller, QObject *parent = nullptr);
    ~AutomationServer() override;
    bool start(const QString &connectionFile, QString *error);
private:
    void acceptConnections();
    void dispatch(QTcpSocket *socket, const QJsonObject &request);
    void dispatchOperation(QTcpSocket *socket, const QJsonObject &request);
    bool dispatchExtended(QTcpSocket *socket, const QString &command, const QJsonObject &params);
    void reply(QTcpSocket *socket, const QJsonObject &response);
    void send(QTcpSocket *socket, const QJsonObject &response);
    void pollJobs();
    void completeJob(const QString &id, const QJsonObject &result, const QString &error = {}, bool cancelled = false);
    QJsonObject jobValue(const QString &id) const;
    QString generation() const;
    struct Job {
        QJsonObject data, initial, result;
        QString watch, fingerprint, requestId;
        std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
    };
    QHash<QString, Job> m_jobs;
    QStringList m_jobOrder;
    QHash<QString, QString> m_requests;
    QHash<QString, QJsonObject> m_plans;
    QString m_activeJob;
    QTimer m_jobTimer;
    AppController *m_controller;
    QTcpServer m_server;
    QThreadPool m_workers;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    QString m_token, m_connectionFile, m_epoch;
    int m_connections = 0;
    bool m_analyzing = false;
};
