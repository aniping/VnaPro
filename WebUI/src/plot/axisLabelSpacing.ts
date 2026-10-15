export interface AxisLabelPlacement {
  division: number
  top: number
  reference: boolean
}

/** 只过滤像素标签，不改变十格网格、刻度值或后端参考范围。 */
export function spacedAxisLabels<T extends AxisLabelPlacement>(
  labels: readonly T[],
  viewportHeight: number,
  labelHeight: number,
  divisions = 10,
): T[] {
  if (
    !Number.isFinite(viewportHeight) ||
    viewportHeight <= 0 ||
    !Number.isFinite(labelHeight) ||
    labelHeight <= 0
  )
    return []
  function priority(label: T) {
    return (
      Number(label.reference) * 2 +
      Number(label.division === 0 || label.division === divisions)
    )
  }
  function bounds(label: T) {
    // 边缘文字不居中；此处必须与绘图区的边缘内缩和行高一致。
    const top =
      label.division === 0
        ? 3
        : label.division === divisions
          ? viewportHeight - 3 - labelHeight
          : label.top - labelHeight / 2
    return { top, bottom: top + labelHeight }
  }
  // 字框之间保留 2px 留白；空间不足时才跳项，不改变 Scale/Div 的十格含义。
  const minimumGap = 2
  const selected: T[] = []
  const ordered = [...labels].sort(
    (a, b) => priority(b) - priority(a) || a.division - b.division,
  )
  for (const label of ordered) {
    if (!Number.isFinite(label.top)) continue
    const box = bounds(label)
    if (
      selected.every((other) => {
        const occupied = bounds(other)
        return (
          box.top >= occupied.bottom + minimumGap ||
          occupied.top >= box.bottom + minimumGap
        )
      })
    )
      selected.push(label)
  }
  return selected.sort((a, b) => a.division - b.division)
}
