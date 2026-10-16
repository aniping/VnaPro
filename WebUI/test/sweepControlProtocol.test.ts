import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeStatus } from '../src/api/decoders'
import { InstrumentApi } from '../src/api/instrumentApi'
import type { SweepControlCommand } from '../src/api/types'
import { instrumentStatus } from './displayFixtures'

test('stopped Single selection survives authoritative status decoding and invalid modes are rejected', () => {
  const status = instrumentStatus()
  status.config.selectedSweepMode = 'SING'
  status.config.sweepMode = 'HOLD'
  status.config.triggerSource = 'MAN'
  const decoded = decodeStatus(status)
  assert.equal(decoded.config.selectedSweepMode, 'SING')
  assert.equal(decoded.config.sweepMode, 'HOLD')
  assert.equal(decoded.config.triggerSource, 'MAN')
  assert.throws(() => decodeStatus({
    ...status, config: { ...status.config, selectedSweepMode: 'HOLD' },
  }), /selectedSweepMode/)
  delete status.config.selectedSweepMode
  assert.equal(decodeStatus(status).config.selectedSweepMode, undefined)
})

test('independent sweep commands use the owned accepted-operation protocol without combining settings and start', async () => {
  const requests: { path: string; body: Record<string, unknown>; token: string | null }[] = []
  const api = new InstrumentApi({
    storage: { getItem: () => 'sweep-owner', setItem() {}, removeItem() {} },
    fetch: async (input, init) => {
      requests.push({
        path: String(input),
        body: JSON.parse(String(init?.body)),
        token: new Headers(init?.headers).get('X-Vna-Session'),
      })
      return new Response(JSON.stringify({ operationId: 'accepted-sweep', state: 'pending' }), { status: 202 })
    },
  })
  const commands: SweepControlCommand[] = [
    { action: 'mode', mode: 'SING' },
    { action: 'source', source: 'MAN' },
    { action: 'start' },
    { action: 'trigger' },
    { action: 'stop' },
    { action: 'single' },
  ]
  for (const command of commands)
    assert.equal((await api.sweepControl(command)).state, 'pending')
  assert.equal(requests.length, commands.length)
  for (const [index, request] of requests.entries()) {
    assert.ok(request.path.endsWith('/sweep-control'))
    assert.equal(request.token, 'sweep-owner')
    const { requestId, ...command } = request.body
    assert.equal(typeof requestId, 'string')
    assert.ok(String(requestId).length > 0)
    assert.deepEqual(command, commands[index])
  }
  assert.equal(new Set(requests.map(({ body }) => body.requestId)).size, commands.length)
})
