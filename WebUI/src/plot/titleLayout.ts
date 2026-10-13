export interface TraceTitleLayout {
  columns: number
  compact: boolean
}

export interface TraceTitleField {
  kind: string
  text: string
  font: string
  paddingLeft: number
  paddingRight: number
}

export interface MeasuredTraceTitleLayout extends TraceTitleLayout {
  slotWidth: number
  nameWidth: number
  chipWidth: number
  formatWidth: number
  sections: TraceTitleSections
}

export interface TraceTitleSections {
  format: boolean
  scale: boolean
  reference: boolean
  state: boolean
}

const TITLE_COLUMN_GAP = 4

/** 完整字段优先减少列数，整个图窗仍放不下一条标题时才整项隐藏。 */
export function measuredTraceTitleLayout(
  availableWidth: number,
  titles: readonly (readonly TraceTitleField[])[],
  measure: (text: string, font: string) => number,
): MeasuredTraceTitleLayout {
  const widths = titles.map((fields) =>
    fields.map((field) => ({
      kind: field.kind,
      width:
        measure(field.text, field.font) +
        field.paddingLeft +
        field.paddingRight,
    })),
  )
  const widestField = (kind: string) =>
    Math.ceil(
      Math.max(
        0,
        ...widths.flatMap((fields) =>
          fields
            .filter((field) => field.kind === kind)
            .map((field) => field.width),
        ),
      ),
    )
  const nameWidth = widestField('trace-number')
  const chipWidth = widestField('measurement-chip')
  const formatWidth = widestField('trace-format')
  const hiddenFields = new Set<string>()
  const widestTitle = () =>
    Math.ceil(
      Math.max(
        1,
        ...widths.map((fields) =>
          fields
            .filter((field) => !hiddenFields.has(field.kind))
            .reduce(
              (sum, field) =>
                sum +
                (field.kind === 'trace-number'
                  ? nameWidth
                  : field.kind === 'measurement-chip'
                    ? chipWidth
                    : field.kind === 'trace-format'
                      ? formatWidth
                      : field.width),
              0,
            ),
        ),
      ),
    )
  const intrinsic = widestTitle()
  const sections: TraceTitleSections = {
    format: true,
    scale: true,
    reference: true,
    state: true,
  }
  // 手册只规定隐藏整个字段；以下取舍顺序是本项目的显示规则。
  for (const [section, kind] of [
    ['reference', 'trace-reference'],
    ['scale', 'trace-scale'],
    ['state', 'trace-state'],
    ['format', 'trace-format'],
  ] as const) {
    if (widestTitle() <= availableWidth) break
    hiddenFields.add(kind)
    sections[section] = false
  }
  const geometry = traceTitleLayout(availableWidth, widestTitle())
  const columns = Math.min(Math.max(1, titles.length), geometry.columns)
  return {
    columns,
    // 空状态使用整行，真实标题只占自然槽宽，余宽留空。
    slotWidth: titles.length ? widestTitle() : Math.max(0, availableWidth),
    compact: availableWidth < intrinsic,
    nameWidth,
    chipWidth,
    formatWidth,
    sections,
  }
}

/** 列数来自实际字体宽度和可用绘图区，不绑定三列、四列或迹线数量。 */
export function traceTitleLayout(
  availableWidth: number,
  intrinsicWidth: number,
  gap = TITLE_COLUMN_GAP,
): TraceTitleLayout {
  const available = Math.max(0, availableWidth)
  const intrinsic = Math.max(1, intrinsicWidth)
  return {
    columns: Math.max(1, Math.floor((available + gap) / (intrinsic + gap))),
    compact: available < intrinsic,
  }
}
