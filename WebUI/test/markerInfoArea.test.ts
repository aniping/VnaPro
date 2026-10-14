import assert from 'node:assert/strict'
import test from 'node:test'
import {
  MARKER_INFO_AREAS,
  markerInfoAreaAt,
} from '../src/domain/markerInfoArea'

test('marker info drag selects all nine predefined regions in the actual diagram bounds', () => {
  const bounds = { left: 120, top: 45, width: 600, height: 300 }
  for (let row = 0; row < 3; row++) {
    for (let column = 0; column < 3; column++) {
      assert.equal(
        markerInfoAreaAt(
          {
            x: bounds.left + (bounds.width * (column + 0.5)) / 3,
            y: bounds.top + (bounds.height * (row + 0.5)) / 3,
          },
          bounds,
        ),
        MARKER_INFO_AREAS[row * 3 + column],
      )
    }
  }
  const scaledBounds = { left: 21, top: 87, width: 90, height: 150 }
  assert.equal(markerInfoAreaAt({ x: 96, y: 162 }, scaledBounds), 'centerRight')
})

test('marker info drop snaps boundaries and clamps nearby title or footer coordinates', () => {
  const bounds = { left: 20, top: 30, width: 300, height: 90 }
  assert.equal(markerInfoAreaAt({ x: 120, y: 60 }, bounds), 'center')
  assert.equal(markerInfoAreaAt({ x: 220, y: 90 }, bounds), 'bottomRight')
  assert.equal(markerInfoAreaAt({ x: 10, y: 0 }, bounds), 'topLeft')
  assert.equal(markerInfoAreaAt({ x: 325, y: 200 }, bounds), 'bottomRight')
})

test('marker info drop rejects detached or invalid geometry without inventing a position', () => {
  const bounds = { left: 20, top: 30, width: 300, height: 90 }
  for (const point of [
    { x: NaN, y: 40 },
    { x: 30, y: Infinity },
  ])
    assert.equal(markerInfoAreaAt(point, bounds), null)
  for (const changed of [
    { width: 0 },
    { height: -1 },
    { left: Infinity },
    { top: NaN },
  ])
    assert.equal(
      markerInfoAreaAt({ x: 30, y: 40 }, { ...bounds, ...changed }),
      null,
    )
  const domRectLike = Object.create({
    left: 0,
    top: 0,
    width: Infinity,
    height: 100,
  }) as typeof bounds
  assert.equal(markerInfoAreaAt({ x: 30, y: 40 }, domRectLike), null)
})
