export interface AxisTypography {
  fontSize: number
  lineHeight: number
}

/** 仅按绘图区像素派生字形；小窗有字号下限，大窗不放大，用户字号仍由后端保存。 */
export function axisTypography(
  width: number,
  height: number,
  fontScale = 100,
): AxisTypography {
  const fit = Math.min(Math.max(0, width) / 320, Math.max(0, height) / 250)
  const base = Math.max(9, Math.min(11, 11 * fit))
  const fontSize = (base * fontScale) / 100
  return { fontSize, lineHeight: Math.ceil(fontSize * 1.25) }
}
