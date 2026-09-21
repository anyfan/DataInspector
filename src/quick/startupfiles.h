#pragma once
#include <QString>
#include <QStringList>

// File arguments Explorer hands to the process when data files are dropped onto
// DataInspector.exe, opened with it, or double-clicked through an association.
struct StartupFiles
{
    QStringList dataFiles;  // Supported data files, in command-line order.
    QString sessionFile;    // First session argument, when one was passed.
    QStringList rejected;   // Existing files whose suffix is not supported.

    bool isEmpty() const { return dataFiles.isEmpty() && sessionFile.isEmpty(); }
};

// Suffixes accepted as data sources and as session bundles.
const QStringList &startupDataSuffixes();
const QStringList &startupSessionSuffixes();

// Parses argv[1..]: skips Qt/QML switches together with their values, resolves
// relative paths against the working directory, drops duplicates and keeps only
// arguments naming an existing readable file.
StartupFiles parseStartupFiles(const QStringList &arguments);
