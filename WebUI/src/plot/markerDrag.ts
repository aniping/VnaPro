export interface MarkerMove {
  name: string
  frequencyHz?: number
  sampleIndex?: number
  step?: number
  discrete?: boolean
}

export interface DragPoint {
  x: number
  y: number
}

export interface MarkerPreviewPoint extends DragPoint {
  sampleIndex: number
}

export interface MarkerDragGeometryInput {
  name: string
  points: readonly MarkerPreviewPoint[]
  smith: boolean
  bounds: { left: number; top: number; width: number; height: number }
  area: { left: number; top: number; width: number; height: number }
  size: { width: number; height: number }
  scale: DragPoint
  offset: DragPoint
  range: { min: number; max: number }
}

/** 冻结手势的绘图几何；后续采集快照不改变当前鼠标选中的原始采样索引。 */
export function createMarkerDragGeometry(input: MarkerDragGeometryInput) {
  const name = input.name
  const smith = input.smith
  const points = input.points.map((point) => ({ ...point }))
  const bounds = {
    left: input.bounds.left,
    top: input.bounds.top,
    width: input.bounds.width,
    height: input.bounds.height,
  }
  const area = { ...input.area }
  const size = { ...input.size }
  const scale = { ...input.scale }
  const offset = { ...input.offset }
  const range = { ...input.range }
  const requested = (point: DragPoint): DragPoint => ({
    x: ((point.x - offset.x - bounds.left) * size.width) / bounds.width,
    y: ((point.y - offset.y - bounds.top) * size.height) / bounds.height,
  })
  function project(point: DragPoint): DragPoint & { sampleIndex?: number } {
    const target = requested(point)
    let closest: DragPoint & { sampleIndex?: number } = target
    let distance = Infinity
    for (const candidate of points) {
      const nextDistance = smith
        ? Math.hypot(
            (candidate.x - target.x) * scale.x,
            (candidate.y - target.y) * scale.y,
          )
        : Math.abs(candidate.x - target.x)
      if (nextDistance < distance) {
        closest = candidate
        distance = nextDistance
      }
    }
    return {
      x: Math.max(area.left, Math.min(area.left + area.width, closest.x)),
      y: Math.max(area.top, Math.min(area.top + area.height, closest.y)),
      sampleIndex: closest.sampleIndex,
    }
  }
  function command(point: DragPoint): MarkerMove | null {
    if (smith) {
      const sampleIndex = project(point).sampleIndex
      return sampleIndex === undefined
        ? null
        : { name, sampleIndex, discrete: true }
    }
    const fraction = Math.max(
      0,
      Math.min(1, (requested(point).x - area.left) / area.width),
    )
    // 预览只吸附抽取后的投影；正式频点由后端按完整帧选择。
    return {
      name,
      frequencyHz: range.min + fraction * (range.max - range.min),
      discrete: true,
    }
  }
  return { project, command }
}

interface DragHooks {
  requestFrame: (callback: () => void) => number
  cancelFrame: (id: number) => void
  preview: (point: DragPoint | null, committing: boolean) => void
}

/** 手势只保存暂时的像素坐标；正式标记与读数由提交后的后端快照决定。 */
export class MarkerDragController {
  private pointer: number | null = null
  private origin: DragPoint = { x: 0, y: 0 }
  private latest: DragPoint = { x: 0, y: 0 }
  private project: (point: DragPoint) => DragPoint = (point) => point
  private frameRequest: number | null = null
  private moved = false
  private committing = false
  private generation = 0

  constructor(private readonly hooks: DragHooks) {}

  get dragging(): boolean {
    return this.pointer !== null
  }

  get pending(): boolean {
    return this.committing
  }

  begin(
    pointer: number,
    point: DragPoint,
    project: (point: DragPoint) => DragPoint,
  ): boolean {
    if (this.dragging || this.committing) return false
    this.pointer = pointer
    this.origin = point
    this.latest = point
    this.project = project
    this.moved = false
    this.generation += 1
    return true
  }

  move(pointer: number, point: DragPoint): void {
    if (pointer !== this.pointer) return
    this.latest = point
    this.moved ||=
      Math.hypot(point.x - this.origin.x, point.y - this.origin.y) >= 3
    if (!this.moved || this.frameRequest !== null) return
    // 高刷新率指针事件合并为每屏幕帧一次反馈，不触发仪器命令或迹线重绘。
    this.frameRequest = this.hooks.requestFrame(() => {
      this.frameRequest = null
      if (this.dragging) this.hooks.preview(this.project(this.latest), false)
    })
  }

  async finish(
    pointer: number,
    point: DragPoint,
    commit: (point: DragPoint) => Promise<boolean>,
    select: () => void,
  ): Promise<void> {
    if (pointer !== this.pointer) return
    this.move(pointer, point)
    this.pointer = null
    this.cancelFrame()
    if (!this.moved) {
      select()
      return
    }
    const generation = this.generation
    this.committing = true
    this.hooks.preview(this.project(this.latest), true)
    try {
      // 包含未赶上最后一帧的释放坐标，并等待读回，避免先跳回旧位置再跳到新位置。
      await commit(this.latest)
    } finally {
      if (generation === this.generation) {
        this.committing = false
        this.hooks.preview(null, false)
      }
    }
  }

  cancel(): void {
    this.pointer = null
    this.committing = false
    this.generation += 1
    this.cancelFrame()
    this.hooks.preview(null, false)
  }

  private cancelFrame(): void {
    if (this.frameRequest === null) return
    this.hooks.cancelFrame(this.frameRequest)
    this.frameRequest = null
  }
}
