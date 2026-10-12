import assert from 'node:assert/strict'
import test from 'node:test'
import { InstrumentApi } from '../src/api/instrumentApi'
import type { MeasurementCandidate } from '../src/api/types'

test('Meas updates the named active trace with one request and keeps its identity', async () => {
  const requests: {
    url: string
    method: string
    body: Record<string, unknown>
  }[] = []
  const api = new InstrumentApi({
    storage: null,
    fetch: async (input, init) => {
      requests.push({
        url: String(input),
        method: init?.method ?? '',
        body: JSON.parse(String(init?.body)),
      })
      return new Response(
        JSON.stringify({ operationId: 'update-1', state: 'pending' }),
        { status: 202 },
      )
    },
  })
  const candidate: MeasurementCandidate = {
    name: '迹线 1',
    kind: 'sParameter',
    receiverKind: 'b',
    receiverPort: 2,
    sourcePort: 2,
  }
  const update = Reflect.get(api, 'updateMeasurement')
  assert.equal(
    typeof update,
    'function',
    'Meas needs an atomic update, not delete followed by add',
  )
  assert.deepEqual(await update.call(api, candidate, '9'), {
    operationId: 'update-1',
    state: 'pending',
  })
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

test('Add Trace requests backend cloning and naming with both catalog and display revisions', async () => {
  let request: { url: string; body: Record<string, unknown> } | undefined
  const api = new InstrumentApi({
    storage: null,
    fetch: async (input, init) => {
      request = { url: String(input), body: JSON.parse(String(init?.body)) }
      return new Response(
        JSON.stringify({ operationId: 'clone-1', state: 'pending' }),
        { status: 202 },
      )
    },
  })
  await api.cloneMeasurement('Trc3', '9', '18446744073709551615')
  assert.equal(request!.url, '/api/v1/measurement-actions/clone')
  assert.equal(request!.body.sourceName, 'Trc3')
  assert.equal(request!.body.expectedRevision, '9')
  assert.equal(request!.body.expectedDisplayRevision, '18446744073709551615')
  assert.equal(request!.body.name, undefined)
  assert.equal(request!.body.kind, undefined)
})

test('parameter updates send canonical names and do not derive receiver/source control fields', async () => {
  let body: Record<string, unknown> | undefined
  const api = new InstrumentApi({
    storage: null,
    fetch: async (_input, init) => {
      body = JSON.parse(String(init?.body))
      return new Response(
        JSON.stringify({ operationId: 'update-1', state: 'pending' }),
        { status: 202 },
      )
    },
  })
  await api.updateMeasurement({ name: 'Trc1', parameter: 'S12' }, '4')
  assert.equal(body!.parameter, 'S12')
  assert.equal(body!.receiverPort, undefined)
  assert.equal(body!.sourcePort, undefined)
})
