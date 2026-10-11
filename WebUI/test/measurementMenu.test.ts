import assert from 'node:assert/strict'
import test from 'node:test'
import { InstrumentApi } from '../src/api/instrumentApi'
import type { MeasurementCandidate } from '../src/api/types'
import { cloneMeasurementCandidate } from '../src/domain/measurements'

test('Meas updates the named active trace with one request and keeps its identity', async () => {
  const requests: { url: string; method: string; body: Record<string, unknown> }[] = []
  const api = new InstrumentApi({
    storage: null,
    fetch: async (input, init) => {
      requests.push({ url: String(input), method: init?.method ?? '', body: JSON.parse(String(init?.body)) })
      return new Response(JSON.stringify({ operationId: 'update-1', state: 'pending' }), { status: 202 })
    },
  })
  const candidate: MeasurementCandidate = {
    name: '迹线 1', kind: 'sParameter', receiverKind: 'b', receiverPort: 2, sourcePort: 2,
  }
  const update = Reflect.get(api, 'updateMeasurement')
  assert.equal(typeof update, 'function', 'Meas needs an atomic update, not delete followed by add')
  assert.deepEqual(await update.call(api, candidate, '9'), { operationId: 'update-1', state: 'pending' })
  assert.equal(requests.length, 1)
  assert.equal(requests[0]!.method, 'PUT')
  assert.equal(requests[0]!.url, '/api/v1/measurements/%E8%BF%B9%E7%BA%BF%201')
  const body = requests[0]!.body
  assert.equal(body.expectedRevision, '9')
  assert.equal(body.name, undefined, 'The path owns the trace name')
  assert.equal(body.kind, 'sParameter')
  assert.equal(body.receiverPort, 2)
  assert.equal(body.sourcePort, 2)
  assert.equal(typeof body.requestId, 'string')
})

test('Add Trace copies the active definition and allocates an unused trace name', () => {
  const active = {
    name: 'Trc1', kind: 'sParameter' as const, receiverKind: 'b' as const,
    receiverPort: 2, sourcePort: 2, parameter: 'S22', unit: '1' as const, calibration: 'uncalibrated' as const,
  }
  const catalog = [active, { ...active, name: 'Trc3' }, { ...active, name: 'custom' }]
  const before = structuredClone(catalog)
  assert.deepEqual(cloneMeasurementCandidate(active, catalog), {
    name: 'Trc2', kind: 'sParameter', receiverKind: 'b', receiverPort: 2, sourcePort: 2,
  })
  assert.deepEqual(catalog, before)
  const full = Array.from({ length: 32 }, (_, index) => ({ ...active, name: `Trc${index + 1}` }))
  assert.equal(cloneMeasurementCandidate(active, full), null)
})
