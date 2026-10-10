import type { DisplayScale } from '../domain/presentation'
import { normalizedX, normalizedY, referenceLevel } from './traceMath'

export interface PlotRect { left: number; top: number; width: number; height: number }
export interface MarkerPlacement {
  left: number
  top: number
  width: number
  height: number
  tipX: number
  tipY: number
  orientation: 'up' | 'down'
  glyphLeft: number
  glyphTop: number
  glyphPath: string
  labelTop: number
}

/** Keep the touch target inside the viewport while the arrow tip stays on its true sample. */
export function markerPlacement(point: { x: number; y: number }, rect: PlotRect): MarkerPlacement {
  const width = Math.min(32, rect.width)
  const height = Math.min(32, rect.height)
  const clamp = (value: number, minimum: number, maximum: number) => Math.max(minimum, Math.min(maximum, value))
  const orientation = point.y - rect.top < 25 ? 'up' : 'down'
  const left = clamp(point.x - width / 2, rect.left, rect.left + rect.width - width)
  const top = clamp(orientation === 'up' ? point.y : point.y - height, rect.top, rect.top + rect.height - height)
  const tipX = point.x - left
  const tipY = point.y - top
  const glyphLeft = clamp(tipX - 5, 0, width - 10)
  const glyphTop = orientation === 'up' ? tipY : tipY - 9
  const arrowX = tipX - glyphLeft
  return { left, top, width, height, tipX, tipY, orientation, glyphLeft, glyphTop,
    glyphPath: orientation === 'up' ? `M0 9H10L${arrowX} 0Z` : `M0 0H10L${arrowX} 9Z`,
    labelTop: orientation === 'up' ? tipY + 9 : tipY - 25 }
}
export interface CanvasCurve {
  frequencyHz: readonly number[]
  values: readonly number[]
  scale: DisplayScale
  color: string
  dashed: boolean
  active: boolean
}
export interface CanvasReference {
  scale: DisplayScale
  color: string
  active: boolean
}
export interface CanvasDiagram {
  width: number
  height: number
  rect: PlotRect
  xMin: number
  xMax: number
  references: readonly CanvasReference[]
  curves: readonly CanvasCurve[]
}

export function plotRect(width: number, height: number): PlotRect {
  return { left: 1, top: 1, width: Math.max(1, width - 2), height: Math.max(1, height - 2) }
}

export interface RasterGeometry {
  bitmapWidth: number
  bitmapHeight: number
  density: number
  cssScaleX: number
  cssScaleY: number
  physicalScaleX: number
  physicalScaleY: number
  originX: number
  originY: number
  rasterOriginX: number
  rasterOriginY: number
  offsetX: number
  offsetY: number
  canvasScaleX: number
  canvasScaleY: number
  canvasOffsetX: number
  canvasOffsetY: number
}

export interface ScreenBounds { width: number; height: number; left: number; top: number }

/** Cover the original plot bounds with whole device pixels without changing its sample projection. */
export function rasterGeometry(
  logical: { width: number; height: number },
  bounds: ScreenBounds,
  devicePixelRatio: number,
): RasterGeometry {
  const positive = (value: number, fallback: number) => Number.isFinite(value) && value > 0 ? value : fallback
  const width = positive(logical.width, 1)
  const height = positive(logical.height, 1)
  const cssWidth = positive(bounds.width, width)
  const cssHeight = positive(bounds.height, height)
  const density = positive(devicePixelRatio, 1)
  const cssScaleX = cssWidth / width
  const cssScaleY = cssHeight / height
  const physicalScaleX = cssScaleX * density
  const physicalScaleY = cssScaleY * density
  const originX = (Number.isFinite(bounds.left) ? bounds.left : 0) * density
  const originY = (Number.isFinite(bounds.top) ? bounds.top : 0) * density
  const rasterOriginX = Math.floor(originX)
  const rasterOriginY = Math.floor(originY)
  const bitmapWidth = Math.max(1, Math.ceil(originX + cssWidth * density) - rasterOriginX)
  const bitmapHeight = Math.max(1, Math.ceil(originY + cssHeight * density) - rasterOriginY)
  const offsetX = originX - rasterOriginX
  const offsetY = originY - rasterOriginY
  return { bitmapWidth, bitmapHeight, density, cssScaleX, cssScaleY, physicalScaleX, physicalScaleY,
    originX, originY, rasterOriginX, rasterOriginY, offsetX, offsetY,
    canvasScaleX: bitmapWidth / (cssWidth * density), canvasScaleY: bitmapHeight / (cssHeight * density),
    canvasOffsetX: -offsetX / physicalScaleX, canvasOffsetY: -offsetY / physicalScaleY }
}

/** Align axis-aligned stroke edges in global physical pixels, including fractional canvas origins. */
export function snapPhysicalStroke(position: number, physicalScale: number, origin: number, strokeWidth = 1): number {
  const halfWidth = strokeWidth / 2
  return (Math.round(origin + position * physicalScale - halfWidth) + halfWidth - origin) / physicalScale
}

/** Bounds belong to the plot parent, never the canvas after applying its compensation transform. */
export function paintDiagram(canvas: HTMLCanvasElement, diagram: CanvasDiagram, bounds: ScreenBounds): void {
  const raster = rasterGeometry(diagram, bounds, window.devicePixelRatio)
  // A transform avoids fractional CSS layout rounding and gives each bitmap texel one device pixel.
  // The parent clips the subpixel overhang; drawing offsets keep samples aligned with DOM markers.
  const transform = `matrix(${raster.canvasScaleX},0,0,${raster.canvasScaleY},${raster.canvasOffsetX},${raster.canvasOffsetY})`
  if (canvas.style.transform !== transform) canvas.style.transform = transform
  // Assigning either dimension resets the entire context, even if its value did not change.
  if (canvas.width !== raster.bitmapWidth) canvas.width = raster.bitmapWidth
  if (canvas.height !== raster.bitmapHeight) canvas.height = raster.bitmapHeight
  const context = canvas.getContext('2d')
  if (!context) return
  context.setTransform(1, 0, 0, 1, 0, 0)
  context.clearRect(0, 0, raster.bitmapWidth, raster.bitmapHeight)
  context.globalAlpha = 1
  context.globalCompositeOperation = 'source-over'
  context.lineCap = 'butt'
  context.lineJoin = 'miter'
  context.setLineDash([])
  context.fillStyle = '#000000'
  context.fillRect(0, 0, raster.bitmapWidth, raster.bitmapHeight)
  context.setTransform(raster.physicalScaleX, 0, 0, raster.physicalScaleY, raster.offsetX, raster.offsetY)
  const { left, top, width, height } = diagram.rect
  const alignX = (x: number) => snapPhysicalStroke(x, raster.physicalScaleX, raster.originX)
  const alignY = (y: number) => snapPhysicalStroke(y, raster.physicalScaleY, raster.originY)
  const xStart = alignX(left)
  const xEnd = alignX(left + width)
  const yStart = alignY(top)
  const yEnd = alignY(top + height)
  // Grid and reference lines occupy one physical pixel. Horizontal/vertical widths differ under scaling.
  context.lineWidth = 1 / raster.physicalScaleX
  context.strokeStyle = '#4b5f6c'
  context.beginPath()
  for (let division = 0; division <= 10; ++division) {
    const x = alignX(left + width * division / 10)
    context.moveTo(x, yStart)
    context.lineTo(x, yEnd)
  }
  context.stroke()
  context.lineWidth = 1 / raster.physicalScaleY
  context.beginPath()
  for (let division = 0; division <= 10; ++division) {
    const y = alignY(top + height * division / 10)
    context.moveTo(xStart, y)
    context.lineTo(xEnd, y)
  }
  context.stroke()
  context.strokeStyle = '#526973'
  context.lineWidth = 1 / raster.physicalScaleX
  context.beginPath()
  for (const x of [xStart, xEnd]) {
    context.moveTo(x, yStart)
    context.lineTo(x, yEnd)
  }
  context.stroke()
  context.lineWidth = 1 / raster.physicalScaleY
  context.beginPath()
  for (const y of [yStart, yEnd]) {
    context.moveTo(xStart, y)
    context.lineTo(xEnd, y)
  }
  context.stroke()

  // Every visible trace keeps its own reference position and color. Paint active last.
  const references = [...diagram.references].sort((first, second) => Number(first.active) - Number(second.active))
  for (const reference of references) {
    const referenceY = top + height * normalizedY(referenceLevel(reference.scale), reference.scale)
    context.strokeStyle = reference.active ? '#dbe2e6' : reference.color
    context.lineWidth = 1 / raster.physicalScaleY
    context.setLineDash([3 / raster.cssScaleX, 3 / raster.cssScaleX])
    context.beginPath()
    context.moveTo(xStart, alignY(referenceY))
    context.lineTo(xEnd, alignY(referenceY))
    context.stroke()
    context.setLineDash([])
    context.fillStyle = reference.color
    context.beginPath()
    context.moveTo(left + width - 8, referenceY)
    context.lineTo(left + width, referenceY - 4)
    context.lineTo(left + width, referenceY + 4)
    context.closePath()
    context.fill()
  }

  context.save()
  // Paint curves in final screen CSS coordinates so a 1px stroke stays 1px at any instrument scale.
  // True sample projection is not snapped or filtered; the bitmap is composited at 1:1 device pixels.
  const screenX = (x: number) => x * raster.cssScaleX
  const screenY = (y: number) => y * raster.cssScaleY
  context.setTransform(raster.density, 0, 0, raster.density, raster.offsetX, raster.offsetY)
  context.beginPath()
  context.rect(screenX(left), screenY(top), screenX(width), screenY(height))
  context.clip()
  for (const curve of diagram.curves) {
    context.strokeStyle = curve.color
    context.lineWidth = 1
    context.setLineDash(curve.dashed ? [5, 3] : [])
    context.beginPath()
    let connected = false
    for (let index = 0; index < curve.frequencyHz.length; ++index) {
      const value = curve.values[index]
      const frequency = curve.frequencyHz[index]
      if (value === undefined || frequency === undefined || !Number.isFinite(value) || !Number.isFinite(frequency)) {
        connected = false
        continue
      }
      const x = screenX(left + normalizedX(frequency, diagram.xMin, diagram.xMax) * width)
      const y = screenY(top + normalizedY(value, curve.scale) * height)
      if (connected) context.lineTo(x, y)
      else context.moveTo(x, y)
      connected = true
    }
    context.stroke()
  }
  context.restore()
}
