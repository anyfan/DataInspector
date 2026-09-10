#include "signalmodel.h"

#include <algorithm>

SignalModel::SignalModel(QObject *parent) : QAbstractListModel(parent) {}

int SignalModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visibleRows.size();
}

QVariant SignalModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleRows.size())
        return {};
    const int sourceRow = m_visibleRows.at(index.row());
    if (role == Qt::DisplayRole || role == NameRole) return m_names.at(sourceRow);
    if (role == IndexRole) return sourceRow;
    if (role == CheckedRole) {
        return m_activePlot >= 0 && m_activePlot < m_plotRows.size()
            && m_plotRows.at(m_activePlot).contains(sourceRow);
    }
    if (role == ColorRole) return m_colors.value(sourceRow, QColor("#4ea1ff"));
    if (role == GroupRole) return m_groups.value(sourceRow);
    return {};
}

QHash<int, QByteArray> SignalModel::roleNames() const
{
    return {{NameRole, "signalName"}, {IndexRole, "signalIndex"},
            {CheckedRole, "signalChecked"}, {ColorRole, "signalColor"},
            {GroupRole, "groupName"}};
}

void SignalModel::setNames(const QStringList &names, const QVector<QColor> &colors)
{
    setNames(names, {}, colors);
}

void SignalModel::setNames(const QStringList &names, const QStringList &groups,
                           const QVector<QColor> &colors)
{
    beginResetModel();
    m_names = names;
    m_groups = groups;
    m_groups.resize(names.size());
    m_colors = colors;
    if (m_colors.size() < names.size()) m_colors.resize(names.size());
    m_visibleRows.clear();
    for (int row = 0; row < m_names.size(); ++row) {
        if (m_filter.isEmpty() || m_names.at(row).contains(m_filter, Qt::CaseInsensitive)
            || m_groups.value(row).contains(m_filter, Qt::CaseInsensitive)) {
            m_visibleRows.append(row);
        }
    }
    for (QSet<int> &rows : m_plotRows) rows.clear();
    for (QSet<int> &rows : m_hiddenPlotRows) rows.clear();
    endResetModel();
    emit checkedCountChanged();
}

void SignalModel::setFilter(const QString &text)
{
    const QString normalized = text.trimmed();
    if (m_filter == normalized) return;
    m_filter = normalized;
    beginResetModel();
    m_visibleRows.clear();
    for (int row = 0; row < m_names.size(); ++row) {
        if (m_filter.isEmpty() || m_names.at(row).contains(m_filter, Qt::CaseInsensitive)
            || m_groups.value(row).contains(m_filter, Qt::CaseInsensitive)) {
            m_visibleRows.append(row);
        }
    }
    endResetModel();
}

void SignalModel::setAllChecked(bool checked)
{
    if (m_activePlot < 0 || m_activePlot >= m_plotRows.size()) return;
    m_plotRows[m_activePlot].clear();
    m_hiddenPlotRows[m_activePlot].clear();
    if (checked) {
        for (int row = 0; row < m_names.size(); ++row)
            m_plotRows[m_activePlot].insert(row);
    }
    if (!m_visibleRows.isEmpty())
        emit dataChanged(index(0), index(m_visibleRows.size() - 1), {CheckedRole});
    emit checkedCountChanged();
}

int SignalModel::checkedCount() const
{
    return m_activePlot >= 0 && m_activePlot < m_plotRows.size()
        ? m_plotRows.at(m_activePlot).size() : 0;
}

void SignalModel::setChecked(int row, bool checked)
{
    setPlotChecked(m_activePlot, row, checked);
}

void SignalModel::setPlotCount(int count)
{
    const int normalized = qMax(0, count);
    if (m_plotCount == normalized) return;
    if (m_plotRows.size() < normalized) {
        m_plotRows.resize(normalized);
        m_hiddenPlotRows.resize(normalized);
    }
    m_plotCount = normalized;
    if (normalized == 0) m_activePlot = -1;
    else if (m_activePlot < 0 || m_activePlot >= normalized) m_activePlot = 0;
    if (!m_visibleRows.isEmpty())
        emit dataChanged(index(0), index(m_visibleRows.size() - 1), {CheckedRole});
    emit checkedCountChanged();
}

void SignalModel::setActivePlot(int plotIndex)
{
    if (plotIndex < 0 || plotIndex >= m_plotCount || m_activePlot == plotIndex)
        return;
    m_activePlot = plotIndex;
    if (!m_visibleRows.isEmpty())
        emit dataChanged(index(0), index(m_visibleRows.size() - 1), {CheckedRole});
    emit checkedCountChanged();
}

void SignalModel::setPlotChecked(int plotIndex, int row, bool checked)
{
    if (plotIndex < 0 || plotIndex >= m_plotCount
        || row < 0 || row >= m_names.size()) return;
    const bool wasChecked = m_plotRows.at(plotIndex).contains(row);
    if (wasChecked == checked) return;
    if (checked) {
        m_plotRows[plotIndex].insert(row);
        m_hiddenPlotRows[plotIndex].remove(row);
    } else {
        m_plotRows[plotIndex].remove(row);
        m_hiddenPlotRows[plotIndex].remove(row);
    }
    if (plotIndex == m_activePlot) {
        const int visibleRow = m_visibleRows.indexOf(row);
        if (visibleRow >= 0)
            emit dataChanged(index(visibleRow), index(visibleRow), {CheckedRole});
        emit checkedCountChanged();
    }
}

QVector<int> SignalModel::plotRows(int plotIndex) const
{
    if (plotIndex < 0 || plotIndex >= m_plotCount) return {};
    QVector<int> rows(m_plotRows.at(plotIndex).begin(), m_plotRows.at(plotIndex).end());
    std::sort(rows.begin(), rows.end());
    return rows;
}

QVector<int> SignalModel::visiblePlotRows(int plotIndex) const
{
    QVector<int> rows = plotRows(plotIndex);
    if (plotIndex < 0 || plotIndex >= m_hiddenPlotRows.size()) return {};
    rows.erase(std::remove_if(rows.begin(), rows.end(), [this, plotIndex](int row) {
        return m_hiddenPlotRows.at(plotIndex).contains(row);
    }), rows.end());
    return rows;
}

bool SignalModel::plotSignalVisible(int plotIndex, int row) const
{
    return plotIndex >= 0 && plotIndex < m_plotCount
        && m_plotRows.at(plotIndex).contains(row)
        && !m_hiddenPlotRows.at(plotIndex).contains(row);
}

void SignalModel::setPlotSignalVisible(int plotIndex, int row, bool visible)
{
    if (plotIndex < 0 || plotIndex >= m_plotCount
        || !m_plotRows.at(plotIndex).contains(row)) return;
    if (visible) m_hiddenPlotRows[plotIndex].remove(row);
    else m_hiddenPlotRows[plotIndex].insert(row);
}
