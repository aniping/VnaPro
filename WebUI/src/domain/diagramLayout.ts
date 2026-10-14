/** 图窗相对分割意图；布局结构及归属由仪器服务保存。 */
export type TraceDropPosition = 'center' | 'above' | 'below' | 'left' | 'right'

export interface TraceMoveRequest {
  name: string
  diagram: number
  position: TraceDropPosition
  displayRevision: string
}
