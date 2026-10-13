export interface TraceTitleLayout {
  columns: number
  compact: boolean
}

/** 列数来自实际字体宽度和可用绘图区，不绑定三列、四列或迹线数量。 */
export function traceTitleLayout(
  availableWidth: number,
  intrinsicWidth: number,
  gap = 4,
): TraceTitleLayout {
  const available = Math.max(0, availableWidth)
  const intrinsic = Math.max(1, intrinsicWidth)
  return {
    columns: Math.max(1, Math.floor((available + gap) / (intrinsic + gap))),
    compact: available < intrinsic,
  }
}
