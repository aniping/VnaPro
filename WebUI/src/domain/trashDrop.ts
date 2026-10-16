export const TRASH_DRAG_TYPE = 'application/x-vnapro-trash'

export type TrashIntent =
  | { kind: 'trace'; name: string; revision: string; displayRevision: string }
  | { kind: 'marker' | 'markers'; name: string; markerId?: number; displayRevision: string }
  | { kind: 'diagram'; diagram: number; revision: string; displayRevision: string }

function revision(value: unknown): value is string {
  return typeof value === 'string' && /^(0|[1-9][0-9]{0,19})$/.test(value) &&
    (value.length < 20 || value <= '18446744073709551615')
}

function name(value: unknown): value is string {
  return typeof value === 'string' && value.length > 0 && value.length <= 128 &&
    value !== '.' && value !== '..' && !/[\u0000-\u001f/\\]/.test(value)
}

function decode(value: unknown): TrashIntent | null {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return null
  const item = value as Record<string, unknown>
  if (!revision(item.displayRevision)) return null
  if (item.kind === 'diagram') {
    if (!Number.isInteger(item.diagram) || Number(item.diagram) < 1 || Number(item.diagram) > 128 ||
      !revision(item.revision) ||
      Object.keys(item).some((key) => !['kind', 'diagram', 'revision', 'displayRevision'].includes(key))) return null
    return { kind: 'diagram', diagram: Number(item.diagram), revision: item.revision, displayRevision: item.displayRevision }
  }
  if (!name(item.name)) return null
  if (item.kind === 'trace' && revision(item.revision) &&
    Object.keys(item).every((key) => ['kind', 'name', 'revision', 'displayRevision'].includes(key)))
    return { kind: 'trace', name: item.name, revision: item.revision, displayRevision: item.displayRevision }
  if (item.kind === 'markers' &&
    Object.keys(item).every((key) => ['kind', 'name', 'displayRevision'].includes(key)))
    return { kind: 'markers', name: item.name, displayRevision: item.displayRevision }
  if (item.kind === 'marker' && Number.isInteger(item.markerId) &&
    Number(item.markerId) >= 1 && Number(item.markerId) <= 10 &&
    Object.keys(item).every((key) => ['kind', 'name', 'markerId', 'displayRevision'].includes(key)))
    return { kind: 'marker', name: item.name, markerId: Number(item.markerId), displayRevision: item.displayRevision }
  return null
}

/** 删除令牌独立于移动和创建；写入即冻结来源身份，不选择或删除任何对象。 */
export function beginTrashDrag(event: DragEvent, intent: TrashIntent | null, enabled: boolean): boolean {
  const source = enabled ? decode(intent) : null
  if (!source || !event.dataTransfer) return false
  event.dataTransfer.setData(TRASH_DRAG_TYPE, JSON.stringify(source))
  event.dataTransfer.effectAllowed = 'move'
  return true
}

export function isTrashDrag(event: DragEvent): boolean {
  const types = event.dataTransfer?.types
  return Boolean(types?.includes(TRASH_DRAG_TYPE) && !types.includes('application/x-vnapro-new-trace'))
}

export function trashDropIntent(event: DragEvent): TrashIntent | null {
  if (!isTrashDrag(event)) return null
  try {
    const payload = event.dataTransfer!.getData(TRASH_DRAG_TYPE)
    return payload.length <= 1024 ? decode(JSON.parse(payload)) : null
  } catch {
    return null
  }
}

/** 指针捕获中的 Marker 使用同一可用落点，坐标始终为屏幕 CSS 像素。 */
export function hitTrashTarget(clientX: number, clientY: number): boolean {
  if (![clientX, clientY].every(Number.isFinite) || typeof document === 'undefined') return false
  const target = document.querySelector<HTMLElement>('[data-testid="trash-target"]')
  if (!target || target.getAttribute('data-trash-enabled') !== 'true') return false
  const bounds = target.getBoundingClientRect()
  return bounds.width > 0 && bounds.height > 0 &&
    clientX >= bounds.left && clientX <= bounds.right && clientY >= bounds.top && clientY <= bounds.bottom
}
