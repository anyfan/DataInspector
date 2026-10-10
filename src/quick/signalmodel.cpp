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
    if (role == FileNodeRole) return node.groupNode && node.depth == 0;
    if (role == ExpandedRole)
        return node.groupNode && m_expandedGroups.contains(node.group);
    if (role == DepthRole) return node.depth;
    if (role == GroupRole) return node.group;
    if (node.groupNode) {
        if (role == Qt::DisplayRole || role == NameRole)
            return node.group.section(QLatin1Char('/'), -1);
        if (role == IndexRole) return -1;
        if (role == CheckedRole) return false;
        if (role == ColorRole) return QColor("#4ea1ff");
        if (role == WidthRole) return 2.0;
        if (role == LineStyleRole) return static_cast<int>(Qt::SolidLine);
        return {};
    }
    const int sourceRow = node.sourceRow;
    if (role == Qt::DisplayRole || role == NameRole) return m_names.at(sourceRow);
    if (role == IndexRole) return sourceRow;
    if (role == CheckedRole) {
        if (m_selectionOverride) return m_selectionOverride->contains(sourceRow);
        return m_activePlot >= 0 && m_activePlot < m_plotRows.size()
            && m_plotRows.at(m_activePlot).contains(sourceRow);
    }
    if (role == ColorRole) return m_colors.value(sourceRow, QColor("#4ea1ff"));
    if (role == WidthRole) return m_widths.value(sourceRow, 2.0);
    if (role == LineStyleRole)
        return static_cast<int>(m_lineStyles.value(sourceRow, Qt::SolidLine));
    return {};
}

QHash<int, QByteArray> SignalModel::roleNames() const
{
    return {{NameRole, "signalName"}, {IndexRole, "signalIndex"},
            {CheckedRole, "signalChecked"}, {ColorRole, "signalColor"},
            {GroupRole, "groupName"}, {GroupNodeRole, "groupNode"},
            {FileNodeRole, "fileNode"},
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
    m_originalNames = names;
    m_groups = groups;
    m_groups.resize(names.size());
    m_groupIndexDirty = true;
    m_colors = colors;
    if (m_colors.size() < names.size()) m_colors.resize(names.size());
    m_widths.fill(2.0, names.size());
    m_lineStyles.fill(Qt::SolidLine, names.size());
    rebuildGroupIndex();
    m_expandedGroups = m_groupPrefixes;
    rebuildVisibleNodes();
    for (QSet<int> &rows : m_plotRows) rows.clear();
    m_selectionOverride.reset();
    endResetModel();
    emit checkedCountChanged();
}

void SignalModel::appendNames(const QStringList &names, const QStringList &groups,
                              const QVector<QColor> &colors)
{
    if (names.isEmpty()) return;
    beginResetModel();
    const int oldSize = m_names.size();
    m_names.append(names);
    m_originalNames.append(names);

    QStringList appendedGroups = groups;
    appendedGroups.resize(names.size());
    m_groups.append(appendedGroups);
    m_groupIndexDirty = true;

    m_colors.resize(oldSize + names.size());
    m_widths.resize(oldSize + names.size());
    m_lineStyles.resize(oldSize + names.size());
    for (int index = 0; index < names.size(); ++index) {
        const int row = oldSize + index;
        const QColor color = colors.value(index);
        m_colors[row] = color.isValid() ? color : QColor("#4ea1ff");
        m_widths[row] = 2.0;
        m_lineStyles[row] = Qt::SolidLine;
    }
    const QSet<QString> newGroups(appendedGroups.cbegin(), appendedGroups.cend());
    for (const QString &group : newGroups) {
        QString prefix;
        for (const QString &part : group.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
            prefix = prefix.isEmpty() ? part : prefix + QLatin1Char('/') + part;
            m_expandedGroups.insert(prefix);
        }
    }
    rebuildVisibleNodes();
    endResetModel();
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
    if (group.isEmpty() || !groupExists(group)) return;
    const auto previousNodes = m_visibleNodes;
    const auto previousGroupRows = m_groupRows;
    if (m_expandedGroups.contains(group)) m_expandedGroups.remove(group);
    else m_expandedGroups.insert(group);
    rebuildVisibleNodes();
    const auto nextNodes = m_visibleNodes;
    m_visibleNodes = previousNodes;
    m_groupRows = previousGroupRows;
    const auto sameNode = [](const VisibleNode &a, const VisibleNode &b) {
        return a.groupNode == b.groupNode && a.sourceRow == b.sourceRow
            && a.group == b.group && a.depth == b.depth;
    };
    int first = 0;
    while (first < previousNodes.size() && first < nextNodes.size()
           && sameNode(previousNodes[first], nextNodes[first])) ++first;
    int tail = 0;
    while (tail < previousNodes.size() - first && tail < nextNodes.size() - first
           && sameNode(previousNodes[previousNodes.size() - tail - 1],
                       nextNodes[nextNodes.size() - tail - 1])) ++tail;
    const auto updateGroupRows = [this]() {
        m_groupRows.clear();
        for (int row = 0; row < m_visibleNodes.size(); ++row)
            if (m_visibleNodes[row].groupNode) m_groupRows.insert(m_visibleNodes[row].group, row);
    };
    const int removed = previousNodes.size() - first - tail;
    if (removed > 0) {
        beginRemoveRows({}, first, first + removed - 1);
        m_visibleNodes.remove(first, removed);
        updateGroupRows();
        endRemoveRows();
    }
    const int inserted = nextNodes.size() - first - tail;
    if (inserted > 0) {
        beginInsertRows({}, first, first + inserted - 1);
        m_visibleNodes = nextNodes;
        updateGroupRows();
        endInsertRows();
    }
    const int groupRow = m_groupRows.value(group, -1);
    if (groupRow >= 0) emit dataChanged(index(groupRow), index(groupRow), {ExpandedRole});
}

QVector<int> SignalModel::removeFile(const QString &fileName)
{
    if (fileName.isEmpty()) return {};
    QSet<int> ids;
    for (int row = 0; row < m_names.size(); ++row)
        if (m_groups.value(row).section(QLatin1Char('/'), 0, 0) == fileName) ids.insert(row);
    return removeRowsById(ids);
}

void SignalModel::setSignalGroup(int row, const QString &group)
{
    if (row < 0 || row >= m_groups.size() || m_groups[row] == group) return;
    beginResetModel(); m_groups[row] = group; m_groupIndexDirty = true;
    rebuildVisibleNodes(); endResetModel();
}

QVector<int> SignalModel::removeRowsById(const QSet<int> &ids)
{

    QVector<int> removedRows;
    QVector<int> oldToNew(m_names.size(), -1);
    QStringList names;
    QStringList originalNames;
    QStringList groups;
    QVector<QColor> colors;
    QVector<double> widths;
    QVector<Qt::PenStyle> lineStyles;
    names.reserve(m_names.size());
    groups.reserve(m_groups.size());
    colors.reserve(m_colors.size());
    widths.reserve(m_widths.size());
    lineStyles.reserve(m_lineStyles.size());

    for (int row = 0; row < m_names.size(); ++row) {
        if (ids.contains(row)) {
            removedRows.append(row);
            continue;
        }
        oldToNew[row] = names.size();
        names.append(m_names.at(row));
        originalNames.append(m_originalNames.at(row));
        groups.append(m_groups.value(row));
        colors.append(m_colors.value(row, QColor("#4ea1ff")));
        widths.append(m_widths.value(row, 2.0));
        lineStyles.append(m_lineStyles.value(row, Qt::SolidLine));
    }
    if (removedRows.isEmpty()) return {};

    beginResetModel();
    m_names = std::move(names);
    m_originalNames = std::move(originalNames);
    m_groups = std::move(groups);
    m_groupIndexDirty = true;
    m_colors = std::move(colors);
    m_widths = std::move(widths);
    m_lineStyles = std::move(lineStyles);
    auto remapRows = [&oldToNew](QSet<int> &rows) {
        QSet<int> remapped;
        for (int oldRow : std::as_const(rows)) {
            if (oldRow >= 0 && oldRow < oldToNew.size()
                && oldToNew.at(oldRow) >= 0)
                remapped.insert(oldToNew.at(oldRow));
        }
        rows = std::move(remapped);
    };
    for (QSet<int> &rows : m_plotRows) remapRows(rows);
    for (auto it = m_expandedGroups.begin(); it != m_expandedGroups.end();) {
        if (!groupExists(*it))
            it = m_expandedGroups.erase(it);
        else
            ++it;
    }
    rebuildVisibleNodes();
    endResetModel();
    emit checkedCountChanged();
    return removedRows;
}

int SignalModel::checkedCount() const
{
    if (m_selectionOverride) return m_selectionOverride->size();
    return m_activePlot >= 0 && m_activePlot < m_plotRows.size()
        ? m_plotRows.at(m_activePlot).size() : 0;
}

void SignalModel::setChecked(int row, bool checked)
{
    setPlotChecked(m_activePlot, row, checked);
}
void SignalModel::setSelectionOverride(std::optional<QSet<int>> rows, bool refresh)
{
    if (m_selectionOverride == rows && !refresh) return;
    m_selectionOverride = std::move(rows);
    if (!m_visibleNodes.isEmpty())
        emit dataChanged(index(0), index(m_visibleNodes.size() - 1), {CheckedRole});
    emit checkedCountChanged();
}

void SignalModel::setPlotCount(int count)
{
    const int normalized = qMax(0, count);
    if (m_plotCount == normalized) return;
    if (m_plotRows.size() < normalized) {
        m_plotRows.resize(normalized);
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
    if (checked) m_plotRows[plotIndex].insert(row);
    else m_plotRows[plotIndex].remove(row);
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

QColor SignalModel::signalColor(int row) const
{
    return m_colors.value(row, QColor("#4ea1ff"));
}

double SignalModel::signalWidth(int row) const
{
    return m_widths.value(row, 2.0);
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

bool SignalModel::renameSignal(int row, const QString &name)
{
    const QString normalized = name.trimmed();
    if (row < 0 || row >= m_names.size() || normalized.isEmpty()) return false;
    if (m_names.at(row) == normalized) return true;
    m_names[row] = normalized;
    if (!m_filter.isEmpty()) {
        // The search filter matches on names, so visibility may change.
        beginResetModel();
        rebuildVisibleNodes();
        endResetModel();
        return true;
    }
    const int modelRow = visibleModelRow(row);
    if (modelRow >= 0)
        emit dataChanged(index(modelRow), index(modelRow),
                         {Qt::DisplayRole, NameRole});
    return true;
}

void SignalModel::rebuildGroupIndex()
{
    if (!m_groupIndexDirty) return;
    m_orderedGroups.clear(); m_groupPrefixes.clear();
    m_groupSourceRows.clear(); m_groupAncestors.clear();
    for (int row = 0; row < m_groups.size(); ++row) {
        const QString &group = m_groups[row];
        if (!m_groupSourceRows.contains(group)) {
            QString prefix;
            QStringList ancestors;
            for (const QString &part : group.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
                prefix = prefix.isEmpty() ? part : prefix + QLatin1Char('/') + part;
                ancestors.append(prefix);
                if (!m_groupPrefixes.contains(prefix)) {
                    m_groupPrefixes.insert(prefix);
                    m_orderedGroups.append(prefix);
                }
            }
            m_groupAncestors.insert(group, ancestors);
        }
        m_groupSourceRows[group].append(row);
    }
    m_groupIndexDirty = false;
}

void SignalModel::rebuildVisibleNodes()
{
    rebuildGroupIndex();
    m_visibleNodes.clear();
    m_groupRows.clear();
    QSet<QString> matchingGroups;
    QHash<QString, QVector<int>> matchingRowsByGroup;
    if (m_filter.isEmpty()) matchingRowsByGroup = m_groupSourceRows;
    else {
        for (auto it = m_groupSourceRows.cbegin(); it != m_groupSourceRows.cend(); ++it) {
            QVector<int> matches;
            const bool groupMatches = it.key().contains(m_filter, Qt::CaseInsensitive);
            for (int row : it.value()) {
                if (groupMatches || m_names.at(row).contains(m_filter, Qt::CaseInsensitive)) matches.append(row);
            }
            if (!matches.isEmpty()) {
                matchingRowsByGroup.insert(it.key(), matches);
                for (const QString &prefix : m_groupAncestors.value(it.key())) matchingGroups.insert(prefix);
            }
        }
    }
    for (int row : matchingRowsByGroup.value(QString())) m_visibleNodes.append({false, row, {}, 0});

    for (const QString &group : std::as_const(m_orderedGroups)) {
        const QString parent = group.section(QLatin1Char('/'), 0, -2);
        if (m_filter.isEmpty() && !parent.isEmpty()) {
            QString prefix;
            bool visible = true;
            for (const QString &part : parent.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
                prefix = prefix.isEmpty() ? part : prefix + QLatin1Char('/') + part;
                if (!m_expandedGroups.contains(prefix)) { visible = false; break; }
            }
            if (!visible) continue;
        }
        const bool groupMatches = group.contains(m_filter, Qt::CaseInsensitive);
        if (!m_filter.isEmpty() && !groupMatches && !matchingGroups.contains(group)) continue;
        const int depth = group.count(QLatin1Char('/'));
        m_groupRows.insert(group, m_visibleNodes.size());
        m_visibleNodes.append({true, -1, group, depth});
        const bool showChildren = !m_filter.isEmpty()
            || m_expandedGroups.contains(group);
        if (showChildren)
            for (int row : matchingRowsByGroup.value(group))
                m_visibleNodes.append({false, row, group, depth + 1});
    }
}

bool SignalModel::groupExists(const QString &group) const
{
    return m_groupPrefixes.contains(group);
}

int SignalModel::visibleModelRow(int sourceRow) const
{
    for (int row = 0; row < m_visibleNodes.size(); ++row)
        if (!m_visibleNodes.at(row).groupNode
            && m_visibleNodes.at(row).sourceRow == sourceRow) return row;
    return -1;
}

int SignalModel::revealSignal(int sourceRow)
{
    if (sourceRow < 0 || sourceRow >= m_names.size()) return -1;
    beginResetModel();
    m_filter.clear();
    QString prefix;
    for (const QString &part : m_groups.value(sourceRow).split('/', Qt::SkipEmptyParts)) {
        prefix = prefix.isEmpty() ? part : prefix + '/' + part;
        m_expandedGroups.insert(prefix);
    }
    rebuildVisibleNodes();
    endResetModel();
    return visibleModelRow(sourceRow);
}

QVariantList SignalModel::ancestorPath(int modelRow) const
{
    QVariantList path;
    if (modelRow < 0 || modelRow >= m_visibleNodes.size()) return path;
    const VisibleNode &node = m_visibleNodes.at(modelRow);
    // A group node's own row is not an ancestor; use its parent chain.
    const QString chain = node.groupNode
        ? node.group.section(QLatin1Char('/'), 0, -2)
        : node.group;
    if (chain.isEmpty()) return path;
    QString prefix;
    int depth = 0;
    for (const QString &part : chain.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        prefix = prefix.isEmpty() ? part : prefix + QLatin1Char('/') + part;
        int row = m_groupRows.value(prefix, -1);
        if (row >= modelRow) row = -1;
        path.append(QVariantMap{{QStringLiteral("name"), part},
                                {QStringLiteral("group"), prefix},
                                {QStringLiteral("depth"), depth},
                                {QStringLiteral("row"), row}});
        ++depth;
    }
    return path;
}
