import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive, type VNode } from 'vue'
import type { InstrumentStatus, RunAction } from '../src/api/types'
import { instrumentStatus } from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

function sweepProps() {
  return reactive({
    status: instrumentStatus(),
    mode: 'sweep-control' as 'sweep-control' | 'trigger',
    canWrite: true,
    busy: false,
    action: async (_action: RunAction): Promise<InstrumentStatus | null> => null,
  })
}

function button(render: () => VNode, id: string) {
  const node = findElements(
    render(),
    (element) => element.props?.['data-testid'] === `sweep-${id}`,
  )[0]
  assert.ok(node, `Missing sweep control ${id}`)
  return node.props!
}

test('Sweep separates the one-sweep action from authoritative Continuous and Hold selection', async () => {
  const props = sweepProps()
  const calls: RunAction[] = []
  props.action = async (action) => {
    calls.push(action)
    return null
  }
  const view = await sfcHarness(
    'src/components/softtools/SweepSofttool.vue', props, {},
  )
  try {
    for (const mode of ['HOLD', 'CONT', 'SING'] as const) {
      props.status.config.sweepMode = mode
      const single = button(view.render, 'single')
      assert.equal(single['aria-pressed'], undefined)
      assert.equal(single.class, 'softtool-button')
      assert.equal(button(view.render, 'continuous')['aria-pressed'], mode === 'CONT')
      assert.equal(button(view.render, 'hold')['aria-pressed'], mode === 'HOLD')
    }
    props.status.config.sweepMode = 'CONT'
    const before = JSON.stringify(props.status)
    await button(view.render, 'single').onClick()
    await button(view.render, 'hold').onClick()
    assert.deepEqual(calls, ['single', 'hold'])
    assert.equal(JSON.stringify(props.status), before)
    assert.equal(button(view.render, 'continuous')['aria-pressed'], true)
    props.status.config.sweepMode = 'HOLD'
    assert.equal(button(view.render, 'hold')['aria-pressed'], true)
    assert.equal(button(view.render, 'continuous')['aria-pressed'], false)
  } finally {
    view.dispose()
  }
})

test('Sweep rejects read-only, busy and concurrent actions while preserving backend readback', async () => {
  const props = sweepProps()
  const calls: RunAction[] = []
  let finish: (value: null) => void = () => {}
  props.action = (action) => {
    calls.push(action)
    return new Promise<null>((resolve) => { finish = resolve })
  }
  const view = await sfcHarness(
    'src/components/softtools/SweepSofttool.vue', props, {},
  )
  try {
    for (const state of ['readOnly', 'busy'] as const) {
      props.canWrite = state !== 'readOnly'
      props.busy = state === 'busy'
      for (const id of ['single', 'continuous', 'hold']) {
        const control = button(view.render, id)
        assert.equal(control.disabled, true)
        await control.onClick()
      }
    }
    assert.deepEqual(calls, [])
    props.canWrite = true
    props.busy = false
    const pending = button(view.render, 'single').onClick()
    for (const id of ['single', 'continuous', 'hold']) {
      const control = button(view.render, id)
      assert.equal(control.disabled, true)
      await control.onClick()
    }
    assert.deepEqual(calls, ['single'])
    assert.equal(button(view.render, 'hold')['aria-pressed'], true)
    finish(null)
    await pending
    assert.equal(button(view.render, 'single').disabled, false)
  } finally {
    finish(null)
    view.dispose()
  }
})

test('Sweep releases its pending guard when the device action fails', async () => {
  const props = sweepProps()
  props.action = async () => { throw new Error('Device rejected request') }
  const view = await sfcHarness(
    'src/components/softtools/SweepSofttool.vue', props, {},
  )
  try {
    await assert.rejects(button(view.render, 'single').onClick(), /Device rejected/)
    assert.equal(button(view.render, 'single').disabled, false)
    assert.equal(button(view.render, 'hold')['aria-pressed'], true)
  } finally {
    view.dispose()
  }
})

test('Manual trigger only submits in the backend-armed manual state', async () => {
  const props = sweepProps()
  props.mode = 'trigger'
  const calls: RunAction[] = []
  props.action = async (action) => { calls.push(action); return null }
  const view = await sfcHarness(
    'src/components/softtools/SweepSofttool.vue', props, {},
  )
  try {
    for (const state of [
      { state: 'Running', source: 'IMM', sweep: 'CONT' },
      { state: 'Ready', source: 'MAN', sweep: 'CONT' },
      { state: 'Running', source: 'MAN', sweep: 'HOLD' },
    ] as const) {
      props.status.state = state.state
      props.status.config.triggerSource = state.source
      props.status.config.sweepMode = state.sweep
      assert.equal(button(view.render, 'trigger').disabled, true)
      await button(view.render, 'trigger').onClick()
    }
    assert.deepEqual(calls, [])
    props.status.config.sweepMode = 'CONT'
    assert.equal(button(view.render, 'trigger').disabled, false)
    await button(view.render, 'trigger').onClick()
    assert.deepEqual(calls, ['trigger'])
  } finally {
    view.dispose()
  }
})
