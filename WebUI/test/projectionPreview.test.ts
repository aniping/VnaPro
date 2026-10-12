import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeProjection } from '../src/api/displayDecoders'
import { projection } from './displayFixtures'

function smithProjection() {
  return {
    ...projection([0.2, 1, null], 'smith', [0.3, 0, null]),
    impedanceRealValues: [7, null, null],
    impedanceImagValues: [-4, null, null],
    impedanceStates: ['finite', 'open', 'invalid'],
  }
}

test('Smith point impedance remains the supplied C++ projection without local conversion', () => {
  const result = decodeProjection(smithProjection(), 3)
  // 数值刻意不同于 Γ 换算值，客户端只解码，不复算阻抗。
  assert.deepEqual(Reflect.get(result, 'impedanceRealValues'), [7, null, null])
  assert.deepEqual(Reflect.get(result, 'impedanceImagValues'), [-4, null, null])
  assert.deepEqual(Reflect.get(result, 'impedanceStates'), [
    'finite',
    'open',
    'invalid',
  ])
})

test('older frame responses without point impedance remain readable', () => {
  const body = smithProjection()
  const {
    impedanceRealValues,
    impedanceImagValues,
    impedanceStates,
    ...older
  } = body
  const result = decodeProjection(older, 3)
  assert.deepEqual(Reflect.get(result, 'impedanceRealValues'), [])
  assert.deepEqual(Reflect.get(result, 'impedanceImagValues'), [])
  assert.deepEqual(Reflect.get(result, 'impedanceStates'), [])
})

test('point impedance rejects partial fields and wrong array lengths', () => {
  const { impedanceImagValues, ...partial } = smithProjection()
  assert.throws(() => decodeProjection(partial, 3))
  assert.throws(() =>
    decodeProjection({ ...smithProjection(), impedanceRealValues: [7] }, 3),
  )
})

test('point impedance validates finite values and special states', () => {
  assert.throws(() =>
    decodeProjection(
      {
        ...smithProjection(),
        impedanceStates: ['finite', 'finite', 'invalid'],
      },
      3,
    ),
  )
  assert.throws(() =>
    decodeProjection(
      { ...smithProjection(), impedanceImagValues: [Infinity, null, null] },
      3,
    ),
  )
  assert.throws(() =>
    decodeProjection(
      {
        ...smithProjection(),
        impedanceStates: ['finite', 'unknown', 'invalid'],
      },
      3,
    ),
  )
})

test('Cartesian projections keep point impedance arrays empty', () => {
  const result = decodeProjection(
    {
      ...projection([-13, -14]),
      impedanceRealValues: [],
      impedanceImagValues: [],
      impedanceStates: [],
    },
    2,
  )
  assert.deepEqual(Reflect.get(result, 'impedanceStates'), [])
  assert.throws(() =>
    decodeProjection({ ...smithProjection(), format: 'db' }, 3),
  )
})
