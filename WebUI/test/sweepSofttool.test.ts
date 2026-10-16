import assert from 'node:assert/strict'
import test from 'node:test'
import { isVNode, reactive, type VNode } from 'vue'
import { renderToString } from '@vue/server-renderer'
import type { InstrumentStatus, SweepControlCommand } from '../src/api/types'
import type { useInstrument } from '../src/composables/useInstrument'
import { instrumentStatus } from './displayFixtures'
import { findComponent, findElements, sfcHarness } from './sfcHarness'

function sweepProps() {
  const status = instrumentStatus()
  status.config.selectedSweepMode = 'CONT'
  return reactive({
    status: status as InstrumentStatus | null,
    mode: 'sweep-control' as 'sweep-control' | 'trigger',
    canWrite: true,
    busy: false,
    action: async (_command: SweepControlCommand): Promise<InstrumentStatus | null> => null,
  })
}

function control(render: () => VNode, id: string) {
  const node = findElements(
    render(),
    (element) => element.props?.['data-testid'] === `sweep-${id}`,
  )[0]
  assert.ok(node, `Missing sweep control ${id}`)
  return node
}

function button(render: () => VNode, id: string) {
  return control(render, id).props!
}

function text(node: VNode): string {
  if (typeof node.children === 'string') return node.children
  return Array.isArray(node.children)
    ? node.children.map((child) => isVNode(child) ? text(child) : String(child)).join('')
    : ''
}

async function viewFor(props: ReturnType<typeof sweepProps>) {
  return sfcHarness('src/components/softtools/SweepSofttool.vue', props, {})
}

test('Sweep mode selection is separate from its single Start Sweep action and preserves authoritative selection', async () => {
  const props = sweepProps()
  const calls: SweepControlCommand[] = []
  props.action = async (command) => { calls.push({ ...command }); return null }
  const view = await viewFor(props)
  try {
    const status = props.status!
    status.config.triggerSource = 'MAN'
    for (const selected of ['CONT', 'SING'] as const) {
      status.config.selectedSweepMode = selected
      for (const runtime of ['HOLD', 'CONT', 'SING'] as const) {
        status.config.sweepMode = runtime
        assert.equal(button(view.render, 'continuous')['aria-pressed'], selected === 'CONT')
        assert.equal(button(view.render, 'single')['aria-pressed'], selected === 'SING')
        assert.equal(button(view.render, 'start')['aria-pressed'], undefined)
        assert.equal(text(control(view.render, 'start')), 'Start Sweep')
        assert.equal(findElements(view.render(), (node) => node.props?.['data-testid'] === 'sweep-start').length, 1)
        assert.equal(findElements(view.render(), (node) => node.props?.['data-testid'] === 'sweep-stop').length, 0)
      }
    }
    status.config.selectedSweepMode = 'CONT'
    status.config.sweepMode = 'HOLD'
    const before = JSON.stringify(status)
    await button(view.render, 'single').onClick()
    await button(view.render, 'start').onClick()
    assert.deepEqual(calls, [{ action: 'mode', mode: 'SING' }, { action: 'start' }])
    assert.equal(JSON.stringify(status), before, 'Do not select optimistically or force Free Run')
    status.config.selectedSweepMode = 'SING'
    status.config.sweepMode = 'SING'
    status.state = 'Running'
    assert.equal(text(control(view.render, 'start')), 'Start Sweep')
    await button(view.render, 'start').onClick()
    assert.deepEqual(calls.at(-1), { action: 'start' })
    status.config.sweepMode = 'HOLD'
    status.state = 'Ready'
    assert.equal(button(view.render, 'single')['aria-pressed'], true)
    assert.equal(text(control(view.render, 'status')), 'Stopped')
  } finally { view.dispose() }
})

test('Continuous selection sends only a mode command and waits for authoritative run readback', async () => {
  const props = sweepProps()
  props.status!.config.selectedSweepMode = 'SING'
  const calls: SweepControlCommand[] = []
  props.action = async (command) => { calls.push({ ...command }); return null }
  const view = await viewFor(props)
  try {
    const status = props.status!
    const before = JSON.stringify(status)
    await button(view.render, 'continuous').onClick()
    assert.deepEqual(calls, [{ action: 'mode', mode: 'CONT' }])
    assert.equal(JSON.stringify(status), before)
    assert.equal(text(control(view.render, 'status')), 'Stopped')
    assert.equal(button(view.render, 'continuous')['aria-pressed'], false)
    status.config.selectedSweepMode = 'CONT'
    status.config.sweepMode = 'CONT'
    status.state = 'Running'
    assert.equal(button(view.render, 'continuous')['aria-pressed'], true)
    assert.equal(text(control(view.render, 'status')), 'Sweeping continuously')
    assert.equal(text(control(view.render, 'start')), 'Start Sweep')
    await button(view.render, 'continuous').onClick()
    assert.equal(calls.length, 1, 'Preserve the existing guard for an already selected mode')
    status.config.triggerSource = 'MAN'
    assert.equal(text(control(view.render, 'status')), 'Waiting for manual trigger')
  } finally { view.dispose() }
})

test('Trigger In exposes source selection and Manual Trigger without start or stop controls', async () => {
  const props = sweepProps()
  props.mode = 'trigger'
  const calls: SweepControlCommand[] = []
  props.action = async (command) => { calls.push({ ...command }); return null }
  const view = await viewFor(props)
  try {
    const status = props.status!
    status.config.selectedSweepMode = 'SING'
    for (const state of ['Ready', 'Running'] as const) {
      status.state = state
      const html = await renderToString(view.render())
      assert.doesNotMatch(html, /data-testid="sweep-(?:start|stop)"/)
      assert.doesNotMatch(html, /(?:Start|Stop|Restart) Sweep/)
    }
    status.state = 'Ready'
    const before = JSON.stringify(status)
    await button(view.render, 'manual').onClick()
    await button(view.render, 'trigger').onClick()
    assert.deepEqual(calls, [{ action: 'source', source: 'MAN' }])
    assert.equal(JSON.stringify(status), before)
    status.config.triggerSource = 'MAN'
    assert.equal(button(view.render, 'manual')['aria-pressed'], true)
    assert.equal(button(view.render, 'trigger').disabled, true)
    await button(view.render, 'trigger').onClick()
    assert.equal(calls.length, 1, 'Selecting Manual is not enough when the instrument is stopped')
    status.state = 'Running'
    status.config.sweepMode = 'SING'
    assert.equal(button(view.render, 'trigger').disabled, false)
    assert.equal(text(control(view.render, 'status')), 'Waiting for manual trigger')
    await button(view.render, 'manual').onClick()
    assert.equal(calls.length, 1, 'Selecting the active source must not stop the run')
    await button(view.render, 'trigger').onClick()
    assert.deepEqual(calls.at(-1), { action: 'trigger' })
    status.state = 'Ready'
    status.config.sweepMode = 'HOLD'
    assert.equal(button(view.render, 'trigger').disabled, true)
    assert.equal(status.config.selectedSweepMode, 'SING')
    const completedCalls = calls.length
    await button(view.render, 'trigger').onClick()
    assert.equal(calls.length, completedCalls, 'Manual Trigger must not start another Single implicitly')
    await button(view.render, 'free-run').onClick()
    assert.deepEqual(calls.at(-1), { action: 'source', source: 'IMM' })
    assert.equal(status.config.triggerSource, 'MAN', 'Wait for source readback')
    assert.ok(calls.every((command) => command.action === 'source' || command.action === 'trigger'))
  } finally { view.dispose() }
})

test('Running Free Run switches to Manual and triggers from readback without an extra Start command', async () => {
  const props = sweepProps()
  props.mode = 'trigger'
  props.status!.state = 'Running'
  props.status!.config.sweepMode = 'CONT'
  const calls: SweepControlCommand[] = []
  props.action = async (command) => { calls.push({ ...command }); return null }
  const view = await viewFor(props)
  try {
    const status = props.status!
    assert.equal(text(control(view.render, 'status')), 'Sweeping continuously')
    assert.equal(button(view.render, 'trigger').disabled, true)
    const before = JSON.stringify(status)
    await button(view.render, 'manual').onClick()
    assert.deepEqual(calls, [{ action: 'source', source: 'MAN' }])
    assert.equal(JSON.stringify(status), before, 'Source selection waits for authoritative run state')
    status.config.triggerSource = 'MAN'
    assert.equal(text(control(view.render, 'status')), 'Waiting for manual trigger')
    assert.equal(button(view.render, 'trigger').disabled, false)
    await button(view.render, 'trigger').onClick()
    assert.deepEqual(calls, [{ action: 'source', source: 'MAN' }, { action: 'trigger' }])
    assert.equal(status.config.selectedSweepMode, 'CONT')
    status.state = 'Ready'
    status.config.sweepMode = 'HOLD'
    await button(view.render, 'free-run').onClick()
    assert.deepEqual(calls.at(-1), { action: 'source', source: 'IMM' })
    status.config.triggerSource = 'IMM'
    assert.equal(text(control(view.render, 'status')), 'Stopped')
    assert.doesNotMatch(text(view.render()), /到 Sweep Control/)
    assert.ok(calls.every((command) => command.action === 'source' || command.action === 'trigger'))
  } finally { view.dispose() }
})

test('Sweep rejects read-only, busy and concurrent mode, source and action requests', async () => {
  const props = sweepProps()
  const calls: SweepControlCommand[] = []
  let finish: (value: null) => void = () => {}
  props.action = (command) => {
    calls.push({ ...command })
    return new Promise<null>((resolve) => { finish = resolve })
  }
  props.status!.state = 'Running'
  props.status!.config.sweepMode = 'CONT'
  props.status!.config.triggerSource = 'MAN'
  const view = await viewFor(props)
  async function assertBlocked() {
    for (const mode of ['sweep-control', 'trigger'] as const) {
      props.mode = mode
      for (const id of mode === 'trigger'
        ? ['free-run', 'manual', 'trigger']
        : ['single', 'continuous', 'start']) {
        assert.equal(button(view.render, id).disabled, true, id)
        await button(view.render, id).onClick()
      }
    }
  }
  try {
    props.canWrite = false
    await assertBlocked()
    props.canWrite = true
    props.busy = true
    await assertBlocked()
    assert.deepEqual(calls, [])
    props.busy = false
    props.mode = 'sweep-control'
    const pending = button(view.render, 'single').onClick()
    await assertBlocked()
    assert.deepEqual(calls, [{ action: 'mode', mode: 'SING' }])
    finish(null)
    await pending
    assert.equal(button(view.render, 'free-run').disabled, false)
  } finally { finish(null); view.dispose() }
})

test('Sweep releases pending guard after failure without changing mode, source or run state', async () => {
  const props = sweepProps()
  const before = JSON.stringify(props.status)
  props.action = async () => { throw new Error('Device rejected request') }
  const view = await viewFor(props)
  try {
    await assert.rejects(button(view.render, 'single').onClick(), /Device rejected/)
    assert.equal(button(view.render, 'single').disabled, false)
    assert.equal(JSON.stringify(props.status), before)
    await assert.rejects(button(view.render, 'start').onClick(), /Device rejected/)
    assert.equal(button(view.render, 'start').disabled, false)
    props.mode = 'trigger'
    await assert.rejects(button(view.render, 'manual').onClick(), /Device rejected/)
    assert.equal(button(view.render, 'manual').disabled, false)
    assert.equal(JSON.stringify(props.status), before)
  } finally { view.dispose() }
})

test('Offline and old-service controls are disabled without inferring selection from runtime mode', async () => {
  const props = sweepProps()
  const calls: SweepControlCommand[] = []
  props.action = async (command) => { calls.push({ ...command }); return null }
  const view = await viewFor(props)
  try {
    delete props.status!.config.selectedSweepMode
    props.status!.config.sweepMode = 'CONT'
    assert.equal(button(view.render, 'continuous')['aria-pressed'], false)
    for (const id of ['continuous', 'single', 'start']) {
      assert.equal(button(view.render, id).disabled, true)
      await button(view.render, id).onClick()
    }
    assert.match(text(view.render()), /请更新服务/)
    props.mode = 'trigger'
    for (const id of ['manual', 'free-run', 'trigger']) {
      assert.equal(button(view.render, id).disabled, true)
      await button(view.render, id).onClick()
    }
    props.status = null
    await button(view.render, 'manual').onClick()
    assert.equal(text(control(view.render, 'status')), 'Waiting for instrument…')
    assert.deepEqual(calls, [])
  } finally { view.dispose() }
})

test('Unsupported ZNB controls are disabled with readable reasons referenced by aria-describedby', async () => {
  const props = sweepProps()
  const view = await viewFor(props)
  try {
    for (const mode of ['sweep-control', 'trigger'] as const) {
      props.mode = mode
      const html = await renderToString(view.render())
      const ids = mode === 'sweep-control'
        ? ['count', 'restart-manager', 'all-continuous', 'all-hold', 'controller', 'pipelining']
        : ['external', 'multiple', 'sequence', 'delay', 'signal-type', 'trigger-manager']
      for (const id of ids) {
        // 连续静态控件会被 Vue 优化为静态 HTML；从真实渲染结果验证其可读说明。
        const node = [...html.matchAll(/<button\b([^>]*)>([\s\S]*?)<\/button>/g)]
          .find((match) => match[1]!.includes(`data-testid="sweep-${id}"`))
        assert.ok(node, `Missing unsupported control ${id}`)
        assert.match(node[1]!, /\bdisabled(?:\s|$)/, `${id} must be disabled`)
        const descriptionId = /aria-describedby="([^"]+)"/.exec(node[1]!)?.[1]
        assert.ok(descriptionId, `${id} needs a readable explanation, not only a tooltip`)
        const reason = new RegExp(`<p[^>]*id="${descriptionId}"[^>]*>([\\s\\S]*?)<\\/p>`).exec(html)
        assert.ok(reason)
        assert.ok(reason[1]!.length > 8)
      }
      if (mode === 'trigger') {
        assert.match(html, /<span>Sequence<\/span><strong>Sweep<\/strong>/)
        assert.match(html, /<span>Delay<\/span><strong>0 s<\/strong>/)
      }
    }
  } finally { view.dispose() }
})

test('App connects Sweep and toolbar restart to the independent sweep command API', async () => {
  const view = await sfcHarness('src/App.vue', {}, { setTimeout, clearTimeout, fetch: async () => new Response('{}') })
  try {
    const instrument = view.bindings.instrument as ReturnType<typeof useInstrument>
    assert.equal(findComponent(view.render(), 'InstrumentToolbar')!.props!['sweep-supported'], false)
    instrument.status.value = instrumentStatus()
    instrument.status.value.config.selectedSweepMode = 'CONT'
    assert.equal(findComponent(view.render(), 'InstrumentToolbar')!.props!['sweep-supported'], true)
    const host = findComponent(view.render(), 'SofttoolHost')!
    const slot = (host.children as { default: () => VNode[] }).default()
    const sweep = slot.map((node) => findComponent(node, 'SweepSofttool')).find(Boolean)!
    assert.equal(sweep.props!.action, instrument.sweepControl)
    const calls: SweepControlCommand[] = []
    instrument.sweepControl = async (command) => { calls.push({ ...command }); return null }
    for (const mode of ['CONT', 'SING'] as const) {
      for (const source of ['IMM', 'MAN'] as const) {
        instrument.status.value.config.selectedSweepMode = mode
        instrument.status.value.config.triggerSource = source
        const before: string = JSON.stringify(instrument.status.value.config)
        await findComponent(view.render(), 'InstrumentToolbar')!.props!.onRestart()
        assert.deepEqual(calls.at(-1), { action: 'start' })
        assert.equal(JSON.stringify(instrument.status.value.config), before)
      }
    }
    assert.equal(calls.length, 4)
  } finally { view.dispose() }
})

test('Legacy service disables only toolbar restart and explains the upgrade requirement', async () => {
  const props = reactive({
    layout: 'single', hasTrace: true, canAutoscale: true, canEditDisplay: true,
    markerEnabled: false, canExport: true, canWrite: true, sweepSupported: false,
    busy: false, canDelete: true, canAddTrace: true,
  })
  const view = await sfcHarness('src/components/instrument/InstrumentToolbar.vue', props, {})
  const toolbarButton = (id: string) => findElements(view.render(),
    (node) => node.props?.['data-testid'] === `toolbar-${id}`)[0]!.props!
  try {
    assert.equal(toolbarButton('restart').disabled, true)
    assert.match(toolbarButton('restart').title, /请更新服务/)
    toolbarButton('restart').onClick()
    assert.deepEqual(view.emitted, [])
    assert.equal(toolbarButton('delete-trace').disabled, false)
    props.sweepSupported = true
    assert.equal(toolbarButton('restart').disabled, false)
    assert.equal(toolbarButton('restart')['aria-label'], 'Restart sweep in active channel')
    assert.match(toolbarButton('restart').title, /保留扫描模式与触发源/)
    toolbarButton('restart').onClick()
    assert.deepEqual(view.emitted, [['restart']])
    props.busy = true
    assert.equal(toolbarButton('restart').disabled, true)
    toolbarButton('restart').onClick()
    props.busy = false
    props.canWrite = false
    assert.equal(toolbarButton('restart').disabled, true)
    toolbarButton('restart').onClick()
    assert.deepEqual(view.emitted, [['restart']])
  } finally { view.dispose() }
})
