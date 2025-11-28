#pragma once

#include <QMainWindow>

enum TreeItemRoles
{
    UniqueIdRole = Qt::UserRole + 1, // "filename/tablename/signalindex"
    IsFileItemRole,
    PenDataRole,
    FileNameRole,
    IsSignalItemRole
};
