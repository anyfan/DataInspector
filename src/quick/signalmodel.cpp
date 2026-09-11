#include "signalmodel.h"

#include <algorithm>
#include <utility>

SignalModel::SignalModel(QObject *parent) : QAbstractListModel(parent) {}

int SignalModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visibleNodes.size();
}

QVariant SignalModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleNodes.size())
        return {};
    const VisibleNode &node = m_visibleNodes.at(index.row());
    if (role == GroupNodeRole) return node.groupNode;
    if (role == ExpandedRole)
        return node.groupNode && m_expandedGroups.contains(node.group);
    if (role == DepthRole) return node.depth;
    if (role == GroupRole) return node.group;
    if (node.groupNode) {
        if (role == Qt::DisplayRole || role == NameRole) return node.group;
        if (role == IndexRole) return -1;
        if (role == CheckedRole) return false;
        if (role == ColorRole) return QColor("#4ea1ff");
        if (role == WidthRole) return 1.0;
        if (role == LineStyleRole) return static_cast<int>(Qt::SolidLine);
        return {};
    }
    const int sourceRow = node.sourceRow;
    if (role == Qt::DisplayRole || role == NameRole) return m_names.at(sourceRow);
    if (role == IndexRole) return sourceRow;
    if (role == CheckedRole) {
        return m_activePlot >= 0 && m_activePlot < m_plotRows.size()
            && m_plotRows.at(m_activePlot).contains(sourceRow);
    }
    if (role == ColorRole) return m_colors.value(sourceRow, QColor("#4ea1ff"));
    if (role == WidthRole) return m_widths.value(sourceRow, 1.0);
    if (role == LineStyleRole)
        return static_cast<int>(m_lineStyles.value(sourceRow, Qt::SolidLine));
    return {};
}

QHash<int, QByteArray> SignalModel::roleNames() const
{
    return {{NameRole, "signalName"}, {IndexRole, "signalIndex"},
            {CheckedRole, "signalChecked"}, {ColorRole, "signalColor"},
            {GroupRole, "groupName"}, {GroupNodeRole, "groupNode"},
            {ExpandedRole, "groupExpanded"}, {DepthRole, "nodeDepth"},
            {WidthRole, "signalWidth"}, {LineStyleRole, "signalLineStyle"}};
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
    m_widths.fill(1.0, names.size());
    m_lineStyles.fill(Qt::SolidLine, names.size());
    m_expandedGroups.clear();
    for (const QString &group : std::as_const(m_groups))
        if (!group.isEmpty()) m_expandedGroups.insert(group);
    rebuildVisibleNodes();
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
    rebuildVisibleNodes();
    endResetModel();
}

void SignalModel::toggleGroup(const QString &group)
{
    if (group.isEmpty() || !m_groups.contains(group)) return;
    beginResetModel();
    if (m_expandedGroups.contains(group)) m_expandedGroups.remove(group);
    else m_expandedGroups.insert(group);
    rebuildVisibleNodes();
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
    if (!m_visibleNodes.isEmpty())
        emit dataChanged(index(0), index(m_visibleNodes.size() - 1), {CheckedRole});
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
    if (!m_visibleNodes.isEmpty())
        emit dataChanged(index(0), index(m_visibleNodes.size() - 1), {CheckedRole});
    emit checkedCountChanged();
}

void SignalModel::setActivePlot(int plotIndex)
{
    if (plotIndex < 0 || plotIndex >= m_plotCount || m_activePlot == plotIndex)
        return;
    m_activePlot = plotIndex;
    if (!m_visibleNodes.isEmpty())
        emit dataChanged(index(0), index(m_visibleNodes.size() - 1), {CheckedRole});
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
        const int visibleRow = visibleModelRow(row);
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

QColor SignalModel::signalColor(int row) const
{
    return m_colors.value(row, QColor("#4ea1ff"));
}

double SignalModel::signalWidth(int row) const
{
    return m_widths.value(row, 1.0);
}

Qt::PenStyle SignalModel::signalStyle(int row) const
{
    return m_lineStyles.value(row, Qt::SolidLine);
}

void SignalModel::setSignalPen(int row, const QColor &color, double width,
                               Qt::PenStyle style)
{
    if (row < 0 || row >= m_names.size()) return;
    const QColor normalizedColor = color.isValid() ? color : QColor("#4ea1ff");
    const double normalizedWidth = qBound(1.0, width, 20.0);
    const Qt::PenStyle normalizedStyle = style >= Qt::SolidLine
            && style <= Qt::DashDotDotLine ? style : Qt::SolidLine;
    if (m_colors.at(row) == normalizedColor
        && qFuzzyCompare(m_widths.at(row), normalizedWidth)
        && m_lineStyles.at(row) == normalizedStyle) return;
    m_colors[row] = normalizedColor;
    m_widths[row] = normalizedWidth;
    m_lineStyles[row] = normalizedStyle;
    const int modelRow = visibleModelRow(row);
    if (modelRow >= 0)
        emit dataChanged(index(modelRow), index(modelRow),
                         {ColorRole, WidthRole, LineStyleRole});
}

void SignalModel::rebuildVisibleNodes()
{
    m_visibleNodes.clear();
    QStringList orderedGroups;
    for (const QString &group : std::as_const(m_groups))
        if (!group.isEmpty() && !orderedGroups.contains(group)) orderedGroups.append(group);

    for (int row = 0; row < m_names.size(); ++row) {
        if (!m_groups.value(row).isEmpty()) continue;
        if (m_filter.isEmpty()
            || m_names.at(row).contains(m_filter, Qt::CaseInsensitive))
            m_visibleNodes.append({false, row, {}, 0});
    }

    for (const QString &group : std::as_const(orderedGroups)) {
        QVector<int> matchingRows;
        bool hasSignalMatch = false;
        for (int row = 0; row < m_names.size(); ++row) {
            if (m_groups.value(row) != group) continue;
            const bool signalMatches = m_filter.isEmpty()
                || m_names.at(row).contains(m_filter, Qt::CaseInsensitive);
            if (signalMatches) {
                matchingRows.append(row);
                hasSignalMatch = true;
            }
        }
        const bool groupMatches = group.contains(m_filter, Qt::CaseInsensitive);
        if (!m_filter.isEmpty() && !groupMatches && !hasSignalMatch) continue;
        m_visibleNodes.append({true, -1, group, 0});
        const bool showChildren = !m_filter.isEmpty()
            || m_expandedGroups.contains(group);
        if (showChildren)
            for (int row : std::as_const(matchingRows))
                m_visibleNodes.append({false, row, group, 1});
    }
}

int SignalModel::visibleModelRow(int sourceRow) const
{
    for (int row = 0; row < m_visibleNodes.size(); ++row)
        if (!m_visibleNodes.at(row).groupNode
            && m_visibleNodes.at(row).sourceRow == sourceRow) return row;
    return -1;
}
