import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeStatus } from '../src/api/decoders'
import { ApiError, InstrumentApi } from '../src/api/instrumentApi'
import type { StimulusZoomCommand } from '../src/api/types'
import { instrumentStatus } from './displayFixtures'

test('Zoom status remains optional for old services and preserves independent restore availability', () => {
  const status = instrumentStatus()
  assert.equal(decodeStatus(status).stimulusZoom, undefined)
  const stimulusZoom = {
    available: false,
    canZoomOut: true,
    reason: 'Choose a Cartesian trace to zoom in',
    depth: 16,
  }
  assert.deepEqual(decodeStatus({ ...status, stimulusZoom }).stimulusZoom, stimulusZoom)
  assert.deepEqual(decodeStatus({
    ...status,
    stimulusZoom: { available: true, canZoomOut: false, reason: 'History changed', depth: 0 },
  }).stimulusZoom, { available: true, canZoomOut: false, reason: 'History changed', depth: 0 })
})

test('incomplete or malformed Zoom capability status fails closed instead of enabling guessed controls', () => {
  const status = instrumentStatus()
  const valid = { available: true, canZoomOut: true, reason: '', depth: 1 }
  for (const key of Object.keys(valid)) {
    const incomplete: Record<string, unknown> = { ...valid }
    delete incomplete[key]
    assert.throws(() => decodeStatus({ ...status, stimulusZoom: incomplete }), /stimulusZoom/)
  }
  for (const malformed of [null, [], true, { ...valid, available: 1 },
    { ...valid, canZoomOut: 'true' }, { ...valid, reason: null },
    ...[-1, 17, 1.5, '1', NaN].map(depth => ({ ...valid, depth })),
  ]) assert.throws(() => decodeStatus({ ...status, stimulusZoom: malformed }), /stimulusZoom/)
})

test('Zoom apply and restore use one accepted operation with both frozen revisions and the control session', async () => {
  const requests: { path: string; method: string | undefined; body: Record<string, unknown>; token: string | null }[] = []
  const api = new InstrumentApi({
    storage: { getItem: () => 'zoom-owner', setItem() {}, removeItem() {} },
    fetch: async (input, init) => {
      requests.push({
        path: String(input), method: init?.method,
        body: JSON.parse(String(init?.body)),
        token: new Headers(init?.headers).get('X-Vna-Session'),
      })
      return new Response(JSON.stringify({ operationId: 'zoom-operation', state: 'pending' }), { status: 202 })
    },
  })
  const commands: StimulusZoomCommand[] = [
    { action: 'apply', traceName: 'Transmit', startHz: 15e6, stopHz: 25e6, min: -45, max: -5 },
    { action: 'out' },
  ]
  for (const command of commands)
    assert.deepEqual(await api.stimulusZoom(command, '9007199254740993', '18446744073709551615'),
      { operationId: 'zoom-operation', state: 'pending' })
  assert.equal(requests.length, 2, 'A zoom rectangle cannot split frequency and scale into separate writes')
  for (const [index, request] of requests.entries()) {
    assert.ok(request.path.endsWith('/stimulus-zoom'))
    assert.equal(request.method, 'POST')
    assert.equal(request.token, 'zoom-owner')
    const { requestId, ...body } = request.body
    assert.equal(typeof requestId, 'string')
    assert.ok(String(requestId).length > 0)
    assert.deepEqual(body, { ...commands[index], expectedRevision: '9007199254740993', expectedDisplayRevision: '18446744073709551615' })
  }
  assert.notEqual(requests[0]!.body.requestId, requests[1]!.body.requestId)
})

test('Zoom rejects a synchronous HTTP success or incomplete accepted response', async () => {
  for (const [status, body] of [
    [200, { operationId: 'zoom-operation', state: 'pending' }],
    [202, { state: 'pending' }],
  ] as const) {
    const api = new InstrumentApi({
      storage: null,
      fetch: async () => new Response(JSON.stringify(body), { status }),
    })
    await assert.rejects(api.stimulusZoom({ action: 'out' }, '1', '2'),
      (error: unknown) => error instanceof ApiError && error.kind === 'protocol')
  }
})
