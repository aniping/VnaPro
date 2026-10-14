/** ZNB 的 Marker info 定位由横向与纵向各三个预定义位置组成。 */
export const MARKER_INFO_AREAS = [
  'topLeft',
  'topCenter',
  'topRight',
  'centerLeft',
  'center',
  'centerRight',
  'bottomLeft',
  'bottomCenter',
  'bottomRight',
] as const

export type MarkerInfoArea = (typeof MARKER_INFO_AREAS)[number]

/** 这里只把释放坐标映射到九宫格，不改变标记的频率、数值或正式显示快照。 */
export function markerInfoAreaAt(
  point: { x: number; y: number },
  bounds: { left: number; top: number; width: number; height: number },
): MarkerInfoArea | null {
  if (
    ![
      point.x,
      point.y,
      bounds.left,
      bounds.top,
      bounds.width,
      bounds.height,
    ].every(Number.isFinite) ||
    bounds.width <= 0 ||
    bounds.height <= 0
  )
    return null
  const column = Math.max(
    0,
    Math.min(2, Math.floor((3 * (point.x - bounds.left)) / bounds.width)),
  )
  const row = Math.max(
    0,
    Math.min(2, Math.floor((3 * (point.y - bounds.top)) / bounds.height)),
  )
  return MARKER_INFO_AREAS[row * 3 + column]!
}
