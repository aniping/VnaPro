import {
  isCircularFormat,
  type DisplayFormat,
  type DisplayScale,
} from '../domain/presentation'

export interface ReferencePositionMove {
  name: string
  referencePosition: number
  displayRevision: string
}

export interface ReferencePositionGeometryInput {
  name: string
  displayRevision: string
  startClientY: number
  startPosition: number
  plotHeightCSS: number
}

export interface ReferencePositionGeometry {
  readonly startPosition: number
  readonly positionAt: (clientY: number) => number | null
  readonly commandAt: (clientY: number) => ReferencePositionMove | null
}

/** 冻结抓取点和实际 CSS 绘图高度，保留手指与参考三角之间的初始偏移。 */
export function createReferencePositionGeometry(
  input: ReferencePositionGeometryInput,
): ReferencePositionGeometry | null {
  const { name, displayRevision, startClientY, startPosition, plotHeightCSS } =
    input
  if (
    !name ||
    !/^[0-9]+$/.test(displayRevision) ||
    ![startClientY, startPosition, plotHeightCSS].every(Number.isFinite) ||
    startPosition < 0 ||
    startPosition > 10 ||
    plotHeightCSS <= 0
  )
    return null
  const positionAt = (clientY: number) =>
    Number.isFinite(clientY)
      ? Math.max(
          0,
          Math.min(
            10,
            startPosition - (10 * (clientY - startClientY)) / plotHeightCSS,
          ),
        )
      : null
  return Object.freeze({
    startPosition,
    positionAt,
    commandAt(clientY: number) {
      const referencePosition = positionAt(clientY)
      return referencePosition === null
        ? null
        : { name, referencePosition, displayRevision }
    },
  })
}

/** 只平移临时坐标范围，参考值与每格跨度保持不变，不修改权威显示设置。 */
export function previewReferenceScale(
  scale: DisplayScale,
  format: DisplayFormat,
  referencePosition: number,
): DisplayScale | null {
  const oldPosition = scale.referencePosition ?? 8
  if (
    isCircularFormat(format) ||
    ![scale.min, scale.max, oldPosition, referencePosition].every(
      Number.isFinite,
    ) ||
    scale.min >= scale.max ||
    oldPosition < 0 ||
    oldPosition > 10 ||
    referencePosition < 0 ||
    referencePosition > 10 ||
    (format === 'logMagnitude' && scale.min <= 0)
  )
    return null
  const minimum = format === 'logMagnitude' ? Math.log10(scale.min) : scale.min
  const maximum = format === 'logMagnitude' ? Math.log10(scale.max) : scale.max
  const shift = ((oldPosition - referencePosition) * (maximum - minimum)) / 10
  const min =
    format === 'logMagnitude' ? 10 ** (minimum + shift) : minimum + shift
  const max =
    format === 'logMagnitude' ? 10 ** (maximum + shift) : maximum + shift
  return ![min, max].every(Number.isFinite) ||
    min >= max ||
    (format === 'logMagnitude' && min <= 0)
    ? null
    : { ...scale, min, max, referencePosition }
}

interface ReferencePositionDragHooks {
  requestFrame: (callback: () => void) => number
  cancelFrame: (id: number) => void
  preview: (position: number | null, committing: boolean) => void
}

/** 手势只保存标量坐标；不复制或遍历采集帧，正式结果来自一次命令读回。 */
export class ReferencePositionDragController {
  private pointer: number | null = null
  private originY = 0
  private latestY = 0
  private geometry: ReferencePositionGeometry | null = null
  private frameRequest: number | null = null
  private moved = false
  private committing = false
  private generation = 0

  constructor(private readonly hooks: ReferencePositionDragHooks) {}

  get dragging(): boolean {
    return this.pointer !== null
  }
  get pending(): boolean {
    return this.committing
  }

  begin(
    pointer: number,
    clientY: number,
    geometry: ReferencePositionGeometry,
  ): boolean {
    if (this.dragging || this.pending || !Number.isFinite(clientY)) return false
    this.pointer = pointer
    this.originY = clientY
    this.latestY = clientY
    this.geometry = geometry
    this.moved = false
    this.generation += 1
    return true
  }

  move(pointer: number, clientY: number): void {
    if (pointer !== this.pointer || !Number.isFinite(clientY)) return
    this.latestY = clientY
    this.moved ||= Math.abs(clientY - this.originY) >= 3
    if (!this.moved || this.frameRequest !== null) return
    const generation = this.generation
    this.frameRequest = this.hooks.requestFrame(() => {
      if (generation !== this.generation) return
      this.frameRequest = null
      if (this.dragging)
        this.hooks.preview(this.geometry!.positionAt(this.latestY), false)
    })
  }

  async finish(
    pointer: number,
    clientY: number,
    commit: (move: ReferencePositionMove) => Promise<boolean>,
    select: () => void,
  ): Promise<void> {
    if (pointer !== this.pointer) return
    if (!Number.isFinite(clientY)) {
      this.cancel()
      return
    }
    this.move(pointer, clientY)
    this.pointer = null
    this.cancelFrame()
    if (!this.moved) {
      select()
      return
    }
    const command = this.geometry!.commandAt(this.latestY)
    if (
      !command ||
      Math.abs(command.referencePosition - this.geometry!.startPosition) <=
        1e-10
    ) {
      this.hooks.preview(null, false)
      return
    }
    this.committing = true
    this.hooks.preview(command.referencePosition, true)
    try {
      await commit(command)
    } finally {
      this.committing = false
      this.hooks.preview(null, false)
    }
  }

  cancel(): void {
    this.pointer = null
    this.generation += 1
    this.cancelFrame()
    // 已提交的后端命令无法撤回；归还 pending 锁必须等待原 Promise 完成。
    this.hooks.preview(null, this.committing)
  }

  private cancelFrame(): void {
    if (this.frameRequest === null) return
    this.hooks.cancelFrame(this.frameRequest)
    this.frameRequest = null
  }
}
