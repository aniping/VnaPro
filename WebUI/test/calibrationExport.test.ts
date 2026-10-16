import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeCalibrationExport } from '../src/api/calibrationDecoders'

test('calibration export accepts schema 2 without rewriting fractional dBm metadata', () => {
  const document = {
    schema: 2,
    id: '18446744073709551615',
    powerDbm: [-120, -12.375, 100],
    onePortTerms: [[0, 0, 0, 0, 1, 0]],
  }
  assert.equal(decodeCalibrationExport(document), document)
  assert.deepEqual(document.powerDbm, [-120, -12.375, 100])
})

test('calibration export rejects stale or unsupported server formats', () => {
  for (const schema of [1, 0, 4, '2', null]) {
    assert.throws(() => decodeCalibrationExport({ schema }), /schema/)
  }
  assert.throws(() => decodeCalibrationExport([]), /calibration data/)
})
