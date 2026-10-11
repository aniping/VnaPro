import assert from 'node:assert/strict'
import test from 'node:test'
import { frequencyText, markerFrequencyText, markerValueText } from '../src/plot/formatting'

test('marker frequencies use aligned engineering units without changing detailed frequency formatting', () => {
  assert.equal(markerFrequencyText(5113350000), '5.113350 GHz')
  assert.equal(markerFrequencyText(4250005000), '4.250005 GHz')
  assert.equal(markerFrequencyText(10000000), '10.000000 MHz')
  assert.equal(markerFrequencyText(1250), '1.250000 kHz')
  assert.equal(markerFrequencyText(50.25), '50.250000 Hz')
  assert.equal(markerFrequencyText(NaN), '—')
  assert.equal(frequencyText(4005123456, 10), '4.005123456 GHz')
  assert.equal(markerValueText(-14.731612, 'db'), '-14.7316')
  assert.equal(markerValueText(-0.000001, 'db'), '0.0000')
  assert.equal(markerValueText(-Infinity, 'db'), '−∞')
  assert.equal(markerValueText(NaN, 'db'), '—')
  assert.equal(markerValueText(0.00000012345678, 'real'), '1.2345678e-7')
})
