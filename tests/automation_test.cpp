#include "automation/automationserver.h"
#include "controller/appcontroller.h"
#include "items/plotitem.h"
#include "startupfiles.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

class AutomationTest final : public QObject
{
    Q_OBJECT
    QJsonObject call(const QJsonObject &connection, const QString &command,
                     const QJsonObject &params = {}, bool authenticated = true)
    {
        QTcpSocket socket;
        socket.connectToHost(QStringLiteral("127.0.0.1"), quint16(connection.value("port").toInt()));
        if (!socket.waitForConnected(2000)) return {};
        const QJsonObject request{{"token", authenticated ? connection.value("token").toString() : "wrong"},
                                  {"command", command}, {"params", params}};
        socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
        QElapsedTimer timer; timer.start();
        while (!socket.canReadLine() && timer.elapsed() < 10000) QTest::qWait(5);
        return QJsonDocument::fromJson(socket.readLine()).object();
    }
    QJsonObject descriptor(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }
private slots:
    void runtimeEnableDisableAndRestart()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto path=dir.filePath("connection.json");
        QJsonObject first;
        {
            AppController controller;
            QVERIFY(!controller.automationEnabled());
            QVERIFY(controller.automationConnectionFile().isEmpty());
            QVERIFY(controller.automationCommand().isEmpty());
            const auto object=controller.addDataObject("aircraft"); QVERIFY(!object.isEmpty());
            const auto objects=controller.dataObjects();
            const bool modified=controller.sessionModified();
            QString error;
            QVERIFY(!controller.startAutomation("relative.json",&error));
            QVERIFY(!controller.automationError().isEmpty());
            QVERIFY(!controller.automationEnabled());
            QVERIFY(controller.startAutomation(path,&error));
            QVERIFY(controller.automationEnabled());
            QVERIFY(controller.automationError().isEmpty());
            first=descriptor(path);
            QVERIFY(call(first,"status")["ok"].toBool());
            const auto generation=call(first,"signals").value("result").toObject().value("generation");
            QVERIFY(controller.enableAutomation()); // Enabling twice preserves the same endpoint.
            QCOMPARE(descriptor(path),first);
            QCOMPARE(controller.dataObjects(),objects);
            QCOMPARE(controller.sessionModified(),modified);
            QVERIFY(controller.automationCommand().contains("--connection"));
            controller.disableAutomation();
            QVERIFY(!controller.automationEnabled());
            QVERIFY(!QFile::exists(path));
            QVERIFY(controller.automationConnectionFile().isEmpty());
            QTcpSocket oldSocket; oldSocket.connectToHost("127.0.0.1",quint16(first["port"].toInt()));
            QVERIFY(!oldSocket.waitForConnected(500));
            QVERIFY(controller.startAutomation(path,&error));
            const auto second=descriptor(path);
            QVERIFY(second["token"]!=first["token"]);
            auto stale=second; stale["token"]=first["token"];
            QCOMPARE(call(stale,"status")["error"].toObject()["code"].toString(),QString("unauthorized"));
            QVERIFY(call(second,"status")["ok"].toBool());
            QVERIFY(call(second,"signals")["result"].toObject()["generation"]!=generation);
            QCOMPARE(controller.dataObjects(),objects);
            QCOMPARE(controller.sessionModified(),modified);
        }
        QVERIFY(!QFile::exists(path));
    }

    void optInAuthAndValidation()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppController controller;
        const QString path = dir.filePath("connection.json");
        {
            AutomationServer server(&controller);
            QString error;
            QVERIFY(!server.start("relative.json", &error));
            QVERIFY2(server.start(path, &error), qPrintable(error));
            const auto conn = descriptor(path);
            QCOMPARE(conn.value("host").toString(), QStringLiteral("127.0.0.1"));
            QCOMPARE(call(conn, "status", {}, false).value("error").toObject().value("code").toString(), QStringLiteral("unauthorized"));
            QVERIFY(call(conn, "help").value("ok").toBool());
            QVERIFY(call(conn, "status").value("ok").toBool());
            QCOMPARE(call(conn, "typo").value("error").toObject().value("code").toString(), QStringLiteral("unknown_command"));
            QVERIFY(!call(conn, "layout", {{"rows", 2}, {"columns", 1.5}}).value("ok").toBool());
            QVERIFY(!call(conn, "layout", {{"rows", 2}, {"columns", 1}, {"typo", 1}}).value("ok").toBool());
            QCOMPARE(controller.plotRows(), 1); // No partial mutation on rejected arguments.
            AutomationServer second(&controller);
            QVERIFY(!second.start(path, &error));
            const auto parsed = parseStartupFiles({"DataInspector.exe", "--automation-file", path});
            QVERIFY(parsed.sessionFile.isEmpty()); // Descriptor .json is never opened as a session.
        }
        QVERIFY(!QFileInfo::exists(path));
    }
    void packetLossCounterRuleUpdate()
    {
        QTemporaryDir dir; QFile csv(dir.filePath("counter.csv"));
        QVERIFY(csv.open(QIODevice::WriteOnly)); csv.write("time,count\n0,254\n1,255\n2,1\n"); csv.close();
        AppController controller; AutomationServer server(&controller); QString error;
        const auto path = dir.filePath("connection.json"); QVERIFY(server.start(path, &error));
        const auto conn = descriptor(path);
        QVERIFY(call(conn, "load", {{"path", csv.fileName()}})["ok"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 10000);
        const auto object = controller.createDataObject("counter", "processing");
        const auto field = controller.addDataObjectField(object);
        QVERIFY(!controller.bindObjectField(object, field, "count", "scalar", 0).isEmpty());
        const auto rule = controller.addObjectRule(object, {{"name", "loss"}, {"operation", "packetloss"},
            {"inputs", QVariantList{field}}, {"samplePeriod", 1}, {"rateMode", "count"}});
        QVERIFY(!rule.isEmpty()); QTRY_VERIFY(!controller.deriving());
        const auto listed = call(conn, "signals")["result"].toObject();
        QJsonObject config{{"name", "loss"}, {"operation", "packetloss"}, {"inputs", QJsonArray{field}},
            {"lossMode", "sequence"}, {"counterStep", 1}, {"expectedStride", 1}, {"counterModulus", 256},
            {"counterRate", 1}, {"counterTolerance", 0}};
        QJsonObject params{{"generation", listed["generation"]}, {"object", object}, {"rule", rule}, {"config", config}};
        QVERIFY(call(conn, "rule_update", params)["ok"].toBool()); QTRY_VERIFY(!controller.deriving());
        const auto current = controller.dataObjects()[0].toMap()["rules"].toList()[0].toMap();
        QCOMPARE(current["lossMode"].toString(), QString("sequence"));
        QCOMPARE(current["outputs"].toList()[0].toMap()["preview"].toString(), QString("0 / 0 / 1"));
        config["lossMode"] = "unknown"; params["config"] = config;
        QVERIFY(!call(conn, "rule_update", params)["ok"].toBool());
        QCOMPARE(controller.dataObjects()[0].toMap()["rules"].toList()[0].toMap()["lossMode"].toString(), QString("sequence"));
    }
    void originalSamplesStatisticsAndSettings()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile csv(dir.filePath(QStringLiteral("数据.csv")));
        QVERIFY(csv.open(QIODevice::WriteOnly));
        csv.write("time,A\n0,1\n1,3\n2,\n3,5\n"); csv.close();
        AppController controller;
        PlotItem plot;
        plot.setWidth(640); plot.setHeight(320);
        controller.attachPlot(&plot);
        AutomationServer server(&controller);
        QString error;
        const auto path = dir.filePath("connection.json");
        QVERIFY(server.start(path, &error));
        const auto conn = descriptor(path);
        QVERIFY(call(conn, "load", {{"path", csv.fileName()}}).value("ok").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 10000);
        QCOMPARE(controller.signalCount(), 1);
        auto listed = call(conn, "signals").value("result").toObject();
        const QString generation = listed.value("generation").toString();
        QCOMPARE(listed.value("signals").toArray().size(), 1);
        QJsonObject params{{"signal", 0}, {"generation", generation}};
        auto stats = call(conn, "statistics", params).value("result").toObject();
        QCOMPARE(stats.value("count").toInt(), 3);
        QCOMPARE(stats.value("missing").toInt(), 1);
        QCOMPARE(stats.value("mean").toDouble(), 3.0);
        QVERIFY(qAbs(stats.value("stddev").toDouble() - std::sqrt(8.0 / 3.0)) < 1e-12);
        QVERIFY(qAbs(stats.value("rms").toDouble() - std::sqrt(35.0 / 3.0)) < 1e-12);
        auto range = params; range["start"] = 1; range["end"] = 2;
        stats = call(conn, "statistics", range).value("result").toObject();
        QCOMPARE(stats.value("mean").toDouble(), 3.0);
        QCOMPARE(stats.value("count").toInt(), 1);
        range["start"] = 10; range["end"] = 20;
        stats = call(conn, "statistics", range).value("result").toObject();
        QCOMPARE(stats.value("count").toInt(), 0); QVERIFY(stats.value("mean").isNull());
        auto paged = params; paged["offset"] = 1; paged["limit"] = 2;
        const auto page = call(conn, "samples", paged).value("result").toObject();
        QCOMPARE(page.value("nextOffset").toInt(), 3);
        QVERIFY(page.value("samples").toArray()[1].toArray()[1].isNull());
        auto nearest = params; nearest["time"] = 0.8;
        QCOMPARE(call(conn, "nearest", nearest).value("result").toObject().value("sample").toArray(), (QJsonArray{1, 3}));
        auto binding = params; binding["plot"] = 0; binding["enabled"] = true;
        QVERIFY(call(conn, "plot_signal", binding).value("ok").toBool());
        QVERIFY(controller.plotSignalEnabled(0, 0));
        QVERIFY(call(conn, "cursor_mode", {{"mode", 2}}).value("ok").toBool());
        QCOMPARE(plot.cursorMode(), 2);
        QVERIFY(call(conn, "layout", {{"rows", 2}, {"columns", 2}}).value("ok").toBool());
        QCOMPARE(controller.plotRows(), 2); QCOMPARE(controller.plotColumns(), 2);
        auto style = params; style["color"] = "#ff123456"; style["width"] = 3; style["style"] = 2;
        QVERIFY(call(conn, "style", style).value("ok").toBool());
        QCOMPARE(controller.signalColor(0), QColor("#123456"));
        QCOMPARE(controller.signalWidth(0), 3.0);
        QCOMPARE(call(conn, "samples", params).value("error").toObject().value("code").toString(), QStringLiteral("stale_generation"));
        params["generation"] = call(conn, "signals").value("result").toObject().value("generation");
        auto offset = params; offset["seconds"] = 10;
        QVERIFY(call(conn, "offset", offset).value("ok").toBool());
        params["generation"] = call(conn, "signals").value("result").toObject().value("generation");
        const auto shifted = call(conn, "samples", params).value("result").toObject().value("samples").toArray();
        QCOMPARE(shifted[0].toArray(), (QJsonArray{10, 1}));
        auto renamed = params; renamed["name"] = QStringLiteral("重命名");
        QVERIFY(call(conn, "rename", renamed).value("ok").toBool());
        QCOMPARE(controller.signalName(0), QStringLiteral("重命名"));
        const auto session = dir.filePath("test.disession");
        QVERIFY(call(conn, "save_session", {{"path", session}}).value("ok").toBool());
        QVERIFY(QFileInfo::exists(session));
        QVERIFY(call(conn, "restore_session", {{"path", session}}).value("ok").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.loading() && !controller.restoringSession(), 10000);
        QCOMPARE(controller.signalName(0), QStringLiteral("重命名"));
        QCOMPARE(controller.signalTimeOffset(0), 10.0);
        QCOMPARE(call(conn, "samples", params).value("error").toObject().value("code").toString(), QStringLiteral("stale_generation"));
    }
    void framing()
    {
        QTemporaryDir dir;
        AppController controller;
        AutomationServer server(&controller);
        QString error;
        const auto path = dir.filePath("connection.json");
        QVERIFY(server.start(path, &error));
        const auto conn = descriptor(path);
        QTcpSocket socket;
        socket.connectToHost("127.0.0.1", quint16(conn.value("port").toInt()));
        QVERIFY(socket.waitForConnected(2000));
        socket.write("{"); socket.flush(); QTest::qWait(15);
        QCOMPARE(socket.bytesAvailable(), 0);
        socket.write("bad json}\n");
        QTRY_VERIFY(socket.canReadLine());
        QCOMPARE(QJsonDocument::fromJson(socket.readLine()).object().value("error").toObject().value("code").toString(), QStringLiteral("invalid_json"));
        QTcpSocket oversized;
        oversized.connectToHost("127.0.0.1", quint16(conn.value("port").toInt()));
        QVERIFY(oversized.waitForConnected(2000));
        oversized.write(QByteArray(65537, 'x'));
        QTRY_VERIFY(oversized.canReadLine());
        QCOMPARE(QJsonDocument::fromJson(oversized.readLine()).object().value("error").toObject().value("code").toString(), QStringLiteral("request_too_large"));
    }
};

QTEST_MAIN(AutomationTest)
#include "automation_test.moc"
