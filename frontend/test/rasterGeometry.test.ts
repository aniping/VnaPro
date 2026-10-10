import assert from 'node:assert/strict'
import test from 'node:test'
import { rasterGeometry, snapPhysicalStroke } from '../src/plot/canvasRenderer'

function near(actual: number, expected: number): void {
  assert.ok(Math.abs(actual - expected) < 1e-9, `${actual} != ${expected}`)
}

test('device-pixel canvas bounds composite 1:1 without moving samples at nine DPR/viewport combinations', () => {
  const logical = { width: 846, height: 658 }
  for (const viewport of [{ width: 1280, height: 800 }, { width: 1024, height: 640 }, { width: 1440, height: 900 }]) {
    const scale = Math.min(viewport.width / 1280, viewport.height / 800)
    const bounds = { width: logical.width * scale, height: logical.height * scale, left: 7.25 * scale, top: 63.4 * scale }
    for (const dpr of [1, 1.25, 2]) {
      const raster = rasterGeometry(logical, bounds, dpr)
      for (const axis of [
        { length: logical.width, origin: bounds.left, screenSize: bounds.width, bitmapSize: raster.bitmapWidth,
          cssScale: raster.cssScaleX, physicalScale: raster.physicalScaleX, offset: raster.offsetX,
          canvasScale: raster.canvasScaleX, canvasOffset: raster.canvasOffsetX },
        { length: logical.height, origin: bounds.top, screenSize: bounds.height, bitmapSize: raster.bitmapHeight,
          cssScale: raster.cssScaleY, physicalScale: raster.physicalScaleY, offset: raster.offsetY,
          canvasScale: raster.canvasScaleY, canvasOffset: raster.canvasOffsetY },
      ]) {
        // Apply the element's own CSS matrix, then the instrument's outer scale.
        const canvasOrigin = axis.origin + axis.canvasOffset * axis.cssScale
        const canvasSize = axis.screenSize * axis.canvasScale
        near(canvasOrigin * dpr, Math.floor(axis.origin * dpr))
        near((canvasOrigin + canvasSize) * dpr, Math.ceil((axis.origin + axis.screenSize) * dpr))
        near(canvasSize * dpr, axis.bitmapSize)
        assert.ok(canvasOrigin <= axis.origin + 1e-9 && canvasOrigin + canvasSize >= axis.origin + axis.screenSize - 1e-9)
        assert.ok(axis.origin - canvasOrigin < 1 / dpr)
        assert.ok(canvasOrigin + canvasSize - axis.origin - axis.screenSize < 1 / dpr)
        const texelScreenSize = canvasSize / axis.bitmapSize
        near(texelScreenSize * dpr, 1)
        for (const coordinate of [0, axis.length * 0.27125, axis.length]) {
          // Context translation cancels the expanded canvas origin, preserving the DOM marker projection.
          const bitmapPosition = axis.offset + coordinate * axis.physicalScale
          const screenPosition = canvasOrigin + bitmapPosition * texelScreenSize
          near(screenPosition, axis.origin + coordinate * axis.cssScale)
          near((screenPosition - axis.origin) / axis.cssScale, coordinate)
        }
        // Curves use a DPR-wide stroke in the bitmap, which becomes one screen CSS pixel.
        near(raster.density * texelScreenSize, 1)
      }
    }
  }
  const downsized = rasterGeometry(logical, { width: 676.8, height: 526.4, left: 0, top: 0 }, 1)
  assert.equal(downsized.bitmapWidth, 677)
  assert.equal(downsized.bitmapHeight, 527)
  assert.notEqual(downsized.canvasScaleX, downsized.canvasScaleY, 'each axis compensates its own covering bitmap size')
  const exact = rasterGeometry(logical, { ...logical, left: 0, top: 0 }, 2)
  assert.equal(exact.canvasScaleX, 1)
  assert.equal(exact.canvasScaleY, 1)
  assert.equal(exact.offsetX, 0)
  assert.equal(exact.offsetY, 0)
})

test('one-pixel grid strokes align both bitmap texels and global device pixels at fractional origins', () => {
  for (const scale of [0.8, 1, 1.125]) {
    for (const dpr of [1, 1.25, 2]) {
      for (const origin of [0, 0.25 * dpr, 17.3 * dpr, -0.25 * dpr]) {
        const physicalScale = scale * dpr
        const rasterOrigin = Math.floor(origin)
        const offset = origin - rasterOrigin
        for (let division = 0; division <= 10; ++division) {
          const local = 1 + 844 * division / 10
          const aligned = snapPhysicalStroke(local, physicalScale, origin)
          const bitmapCenter = offset + aligned * physicalScale
          const globalCenter = rasterOrigin + bitmapCenter
          near(bitmapCenter - 0.5, Math.round(bitmapCenter - 0.5))
          near(globalCenter - 0.5, Math.round(globalCenter - 0.5))
          near(globalCenter + 0.5, Math.round(globalCenter + 0.5))
          assert.ok(Math.abs(aligned - local) * physicalScale <= 0.5 + 1e-9)
        }
      }
    }
  }
  assert.equal(snapPhysicalStroke(10, 1, 0.25), 10.25, 'ignoring origin leaves the stroke between screen pixels')
})
