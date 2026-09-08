#include "signalmodel.h"
SignalModel::SignalModel(QObject *parent) : QAbstractListModel(parent) {}
int SignalModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_visibleRows.size(); }
QVariant SignalModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleRows.size()) return {};
    const int sourceRow = m_visibleRows.at(index.row());
    if (role == Qt::DisplayRole || role == NameRole) return m_names.at(sourceRow);
    if (role == IndexRole) return sourceRow;
    if (role == CheckedRole) return m_checked.value(sourceRow, false);
    if (role == ColorRole) return m_colors.value(sourceRow, QColor("#4ea1ff"));
    return {};
}
QHash<int, QByteArray> SignalModel::roleNames() const { return {{NameRole, "name"}, {IndexRole, "signalIndex"}, {CheckedRole, "checked"}, {ColorRole, "color"}}; }
void SignalModel::setNames(const QStringList &names, const QVector<QColor> &colors) { beginResetModel(); m_names = names; m_checked.fill(false, names.size()); m_colors = colors; if (m_colors.size() < names.size()) m_colors.resize(names.size()); m_visibleRows.clear(); for (int i = 0; i < m_names.size(); ++i) if (m_filter.isEmpty() || m_names.at(i).contains(m_filter, Qt::CaseInsensitive)) m_visibleRows.append(i); endResetModel(); emit checkedCountChanged(); }
void SignalModel::setFilter(const QString &text) { const QString normalized = text.trimmed(); if (m_filter == normalized) return; m_filter = normalized; beginResetModel(); m_visibleRows.clear(); for (int i = 0; i < m_names.size(); ++i) if (m_filter.isEmpty() || m_names.at(i).contains(m_filter, Qt::CaseInsensitive)) m_visibleRows.append(i); endResetModel(); }
void SignalModel::setAllChecked(bool checked) { for (int i = 0; i < m_checked.size(); ++i) m_checked[i] = checked; if (!m_visibleRows.isEmpty()) emit dataChanged(index(0), index(m_visibleRows.size() - 1), {CheckedRole}); emit checkedCountChanged(); }
int SignalModel::checkedCount() const { int count = 0; for (bool checked : m_checked) if (checked) ++count; return count; }
void SignalModel::setChecked(int row, bool checked)
{
    if (row < 0 || row >= m_checked.size() || m_checked.at(row) == checked) return;
    m_checked[row] = checked;
    emit checkedCountChanged();
    const int visibleRow = m_visibleRows.indexOf(row);
    if (visibleRow >= 0) emit dataChanged(index(visibleRow), index(visibleRow), {CheckedRole});
}
