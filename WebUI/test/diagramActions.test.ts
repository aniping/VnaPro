import assert from 'node:assert/strict'
import test from 'node:test'
import { ref } from 'vue'
import { InstrumentApi } from '../src/api/instrumentApi'
import type { DisplayCommand } from '../src/api/types'
import { useWorkspace } from '../src/composables/useWorkspace'
import { instrumentStatus } from './displayFixtures'

test('default Add Trace, copied Trace plus Diagram and Delete Diagram each carry one transaction with both CAS revisions', async () => {
  const requests: { url: string; body: Record<string, unknown> }[] = []
  const api = new InstrumentApi({
    storage: null,
    fetch: async (url, init) => {
      requests.push({ url: String(url), body: JSON.parse(String(init?.body)) })
      return new Response(
        JSON.stringify({ operationId: 'op-1', state: 'pending' }),
        { status: 202 },
      )
    },
  })
  await api.createTrace('9', '10', { diagram: 3 })
  await api.cloneMeasurement('Trc5', '9', '10', {
    newDiagram: true,
    diagram: 3,
  })
  await api.deleteDiagram(3, '9', '10')
  assert.equal(requests.length, 3)
  assert.deepEqual(
    requests.map((item) => item.url),
    [
      '/api/v1/measurement-actions/add',
      '/api/v1/measurement-actions/clone',
      '/api/v1/measurement-actions/delete-diagram',
    ],
  )
  assert.equal(requests[0]!.body.sourceName, undefined)
  assert.equal(requests[0]!.body.newDiagram, false)
  assert.equal(requests[0]!.body.diagram, 3)
  assert.equal(requests[1]!.body.sourceName, 'Trc5')
  assert.equal(requests[1]!.body.newDiagram, true)
  for (const request of requests) {
    assert.equal(request.body.expectedRevision, '9')
    assert.equal(request.body.expectedDisplayRevision, '10')
    assert.equal(typeof request.body.requestId, 'string')
  }
})

test('New Trace creates the selected parameter and diagram in one request without inventing a name or feed operation', async () => {
  const requests: { url: string; body: Record<string, unknown> }[] = []
  const api = new InstrumentApi({
    storage: null,
    fetch: async (url, init) => {
      requests.push({ url: String(url), body: JSON.parse(String(init?.body)) })
      return new Response(
        JSON.stringify({ operationId: 'op-new', state: 'pending' }),
        { status: 202 },
      )
    },
  })
  await api.createTrace('9', '10', { diagram: 2, parameter: 'S44' })
  assert.equal(requests.length, 1)
  assert.equal(requests[0]!.url, '/api/v1/measurement-actions/add')
  assert.deepEqual(
    Object.keys(requests[0]!.body).sort(),
    [
      'diagram',
      'expectedDisplayRevision',
      'expectedRevision',
      'newDiagram',
      'parameter',
      'requestId',
    ].sort(),
  )
  assert.equal(requests[0]!.body.parameter, 'S44')
  assert.equal(requests[0]!.body.diagram, 2)
  assert.equal(requests[0]!.body.expectedRevision, '9')
  assert.equal(requests[0]!.body.expectedDisplayRevision, '10')
})

test('explicit diagram actions remain requests and never repartition local memberships or marker state', () => {
  const status = ref(instrumentStatus())
  const commands: DisplayCommand[] = []
  const workspace = useWorkspace(
    () => status.value,
    () => null,
    (command) => commands.push(command),
  )
  const before = JSON.parse(JSON.stringify(status.value.display))
  workspace.selectDiagram(3)
  workspace.setLayout('maximized', 3)
  workspace.overlayAll(3)
  workspace.splitAll()
  assert.deepEqual(commands, [
    { action: 'diagramSelect', diagram: 3 },
    { action: 'layout', layout: 'maximized', diagram: 3 },
    { action: 'overlay', diagram: 3 },
    { action: 'split' },
  ])
  assert.deepEqual(status.value.display, before)
})
