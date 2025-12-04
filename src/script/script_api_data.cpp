#include "scriptapi.h"
#include "script_internal.h" // 引入辅助函数
#include "mainwindow.h"
#include "datamanager.h"
#include "signalbrowser.h"
#include "types.h"

#include <QFileInfo>
#include <QEventLoop>
#include <QRegularExpression>

bool ScriptAPI::load_file(std::string path, bool overwrite)
{
    if (!m_mainWin)
        return false;

    QString qPath = QString::fromStdString(path);
    QString cleanPath = QFileInfo(qPath).absoluteFilePath();
    QString fileName = QFileInfo(cleanPath).fileName();

    bool exists = runOnMain(m_mainWin, [&]()
                            { return m_mainWin->m_fileDataMap.contains(fileName); });

    if (overwrite && exists)
    {
        runOnMainVoid(m_mainWin, [&]()
                      { m_mainWin->removeFile(fileName); });
        log("Overwriting file: " + fileName.toStdString());
    }

    QEventLoop loop;
    bool success = false;
    QString errorMessage;
    DataManager *dm = m_mainWin->getDataManager();

    auto connSuccess = QObject::connect(m_mainWin, &MainWindow::dataProcessingFinished, [&](const QString &filePath)
                                        {
        if (QFileInfo(filePath).absoluteFilePath() == cleanPath) {
            success = true;
            loop.quit();
        } });

    auto connFail = QObject::connect(dm, &DataManager::loadFailed, [&](const QString &filePath, const QString &err)
                                     {
        if (QFileInfo(filePath).absoluteFilePath() == cleanPath) {
            success = false;
            errorMessage = err;
            loop.quit();
        } });

    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->loadFile(qPath); });

    loop.exec();

    QObject::disconnect(connSuccess);
    QObject::disconnect(connFail);

    if (!success)
        log("File load failed: " + errorMessage.toStdString());

    return success;
}

bool ScriptAPI::remove_file(std::string filename)
{
    if (!m_mainWin)
        return false;
    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->removeFile(QString::fromStdString(filename)); });
    return true;
}

std::vector<std::string> ScriptAPI::get_loaded_files()
{
    if (!m_mainWin)
        return {};
    return runOnMain(m_mainWin, [&]()
                     {
        std::vector<std::string> files;
        for (const QString &key : m_mainWin->m_fileDataMap.keys()) {
            files.push_back(key.toStdString());
        }
        return files; });
}

std::map<std::string, std::vector<std::string>> ScriptAPI::get_file_info(std::string filename)
{
    if (!m_mainWin)
        return {};
    return runOnMain(m_mainWin, [&]()
                     {
        std::map<std::string, std::vector<std::string>> info;
        QString qName = QString::fromStdString(filename);
        
        if (m_mainWin->m_fileDataMap.contains(qName)) {
            const FileData &data = m_mainWin->m_fileDataMap[qName];
            for (const SignalTable &table : data.tables) {
                std::vector<std::string> signal_list;
                for (const QString &header : table.headers) {
                    signal_list.push_back(header.toStdString());
                }
                info[table.name.toStdString()] = signal_list;
            }
        }
        return info; });
}

std::string ScriptAPI::find_id(std::string name)
{
    if (!m_mainWin)
        return "";
    return runOnMain(m_mainWin, [&]() -> std::string
                     {
        if (!m_mainWin->m_signalBrowser) return "";
        QStandardItem *item = m_mainWin->m_signalBrowser->findItemByName(QString::fromStdString(name));
        if (item)
            return item->data(TreeItemRoles::UniqueIdRole).toString().toStdString();
        return ""; });
}

std::vector<double> ScriptAPI::get_data(std::string id)
{
    if (!m_mainWin)
        return {};
    return runOnMain(m_mainWin, [&]()
                     {
        SignalLocation loc = m_mainWin->getSignalDataFromID(QString::fromStdString(id));
        if (loc.table && loc.signalIndex >= 0 && loc.signalIndex < loc.table->valueData.size())
        {
            const QVector<double> &qvec = loc.table->valueData[loc.signalIndex];
            return std::vector<double>(qvec.begin(), qvec.end());
        }
        return std::vector<double>(); });
}

std::vector<double> ScriptAPI::get_time_data(std::string id)
{
    if (!m_mainWin)
        return {};
    return runOnMain(m_mainWin, [&]()
                     {
        SignalLocation loc = m_mainWin->getSignalDataFromID(QString::fromStdString(id));
        if (loc.table)
        {
            const QVector<double> &qvec = loc.table->timeData;
            return std::vector<double>(qvec.begin(), qvec.end());
        }
        return std::vector<double>(); });
}

std::vector<std::string> ScriptAPI::get_all_signal_ids()
{
    if (!m_mainWin)
        return {};
    return runOnMain(m_mainWin, [&]()
                     {
        std::vector<std::string> result;
        if (m_mainWin->m_signalBrowser)
        {
            QStringList ids = m_mainWin->m_signalBrowser->getAllSignalIDs();
            for (const QString &s : ids)
                result.push_back(s.toStdString());
        }
        return result; });
}

std::string ScriptAPI::get_signal_name(std::string id)
{
    if (!m_mainWin)
        return "";
    return runOnMain(m_mainWin, [&]()
                     {
        if (!m_mainWin->m_signalBrowser) return std::string("");
        return m_mainWin->m_signalBrowser->getSignalName(QString::fromStdString(id)).toStdString(); });
}

// 辅助函数：将 py list 转 QStringList
static QStringList pyListToQStringList(const py::list &list)
{
    QStringList result;
    for (auto item : list)
    {
        result.append(QString::fromStdString(py::str(item)));
    }
    return result;
}

bool ScriptAPI::load_parsed_data(std::string filename, py::dict data_dict)
{
    if (!m_mainWin)
        return false;

    FileData fileData;
    fileData.filePath = QString::fromStdString(filename);

    py::list keys = data_dict.attr("keys")();
    QRegularExpression pVarRegex("^p(\\d+)$");

    std::map<int, std::string> validTables;

    for (auto key : keys)
    {
        std::string keyStr = py::str(key);
        QString qKey = QString::fromStdString(keyStr);
        auto match = pVarRegex.match(qKey);
        if (match.hasMatch())
        {
            validTables[match.captured(1).toInt()] = keyStr;
        }
    }

    if (validTables.empty())
    {
        log("Error: No valid data tables found in dictionary.");
        return false;
    }

    for (auto const &pair : validTables)
    {
        auto idx = pair.first;
        auto pKey = pair.second;

        SignalTable table;
        table.name = QString::fromStdString(pKey);

        if (!data_dict.contains(pKey))
            continue;
        py::list rows = data_dict[pKey.c_str()].cast<py::list>();

        size_t rowCount = rows.size();
        if (rowCount == 0)
            continue;

        py::list firstRow = rows[0].cast<py::list>();
        size_t colCount = firstRow.size();
        if (colCount < 2)
            continue;

        size_t valueColCount = colCount - 1;

        std::string titleKey = pKey + "_title";
        std::string title2Key = pKey + "_title2";

        QStringList titles, titles2;
        if (data_dict.contains(titleKey.c_str()))
            titles = pyListToQStringList(data_dict[titleKey.c_str()].cast<py::list>());
        if (data_dict.contains(title2Key.c_str()))
            titles2 = pyListToQStringList(data_dict[title2Key.c_str()].cast<py::list>());

        if (!titles.isEmpty() && titles.size() == colCount)
            titles.removeFirst();
        if (!titles2.isEmpty() && titles2.size() == colCount)
            titles2.removeFirst();

        for (size_t i = 0; i < valueColCount; ++i)
        {
            QString header;
            if (i < titles2.size())
                header += titles2[i];
            if (i < titles.size())
            {
                if (!header.isEmpty())
                    header += " ";
                header += titles[i];
            }
            if (header.isEmpty())
                header = QString("Signal %1").arg(i + 1);
            table.headers.append(header);
        }

        table.timeData.reserve(rowCount);
        table.valueData.resize(valueColCount);
        for (auto &vec : table.valueData)
            vec.reserve(rowCount);

        try
        {
            for (auto rowItem : rows)
            {
                py::list row = rowItem.cast<py::list>();
                double t = row[0].cast<double>();
                table.timeData.append(t);

                for (size_t c = 0; c < valueColCount; ++c)
                {
                    double v = row[c + 1].cast<double>();
                    table.valueData[c].append(v);
                }
            }
        }
        catch (const std::exception &e)
        {
            log(std::string("Error parsing data row: ") + e.what());
            continue;
        }
        fileData.tables.append(table);
    }

    if (fileData.tables.isEmpty())
        return false;

    m_mainWin->getDataManager()->importExternalData(fileData);
    return true;
}
