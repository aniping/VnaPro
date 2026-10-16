import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, reactive, type Ref } from 'vue'
import { InstrumentApi } from '../src/api/instrumentApi'
import type { ClaimResponse, FrameResponse } from '../src/api/types'
import { useInstrument } from '../src/composables/useInstrument'
import type { DisplayScale } from '../src/domain/presentation'
import type { SelectableCurve } from '../src/plot/traceHitTest'
import {
  acquiredFrame,
  instrumentStatus,
  presentation,
  receiverTrace,
  reflection,
  transmission,
} from './displayFixtures'
import { findElements, sfcHarness } from './sfcHarness'

async function twoTracePane() {
  const frame = acquiredFrame()
  frame.traces.push(receiverTrace(transmission))
  const props = reactive({
    number: 1,
    layout: 'single',
    frame,
    measurements: [reflection, transmission],
    allMeasurements: [reflection, transmission],
    presentations: { Reflect: presentation(), Transmit: presentation() },
    activeTraceName: 'Reflect',
    primaryTraceName: 'Reflect',
    selectedDiagram: 1,
    displayRevision: '7',
    config: instrumentStatus().config,
    frameCondition: { kind: 'current', label: '', detail: '' },
    canEditDisplay: true,
    canContinueReferenceDrag: true,
    commitMarker: async () => false,
    commitReferencePosition: async () => true,
  })
  const view = await sfcHarness(
    'src/components/diagrams/DiagramPane.vue',
    props,
    {
      window: { devicePixelRatio: 1 },
      requestAnimationFrame: () => 1,
      cancelAnimationFrame() {},
    },
  )
  function handle(name: string) {
    const match = findElements(
      view.render(),
      (node) =>
        typeof node.type === 'object' &&
        Reflect.get(node.type, 'name') === 'ReferencePositionHandle' &&
        node.props?.name === name,
    )[0]
    assert.ok(match, `${name} must have a real template handle`)
    return match.props!
  }
  const bindings = view.bindings as unknown as {
    axisScale: Ref<DisplayScale>
    curves: Ref<SelectableCurve[]>
  }
  const scale = (name: string) => {
    const curve = bindings.curves.value.find(
      (item) => item.name === name && item.kind === 'cartesian',
    )!
    assert.equal(curve.kind, 'cartesian')
    return curve.scale
  }
  return { ...view, props, handle, scale, bindings }
}

test('同窗参考把手只允许当前 owner 预览，其他把手的迟到事件不能夺取或释放归属', async () => {
  const pane = await twoTracePane()
  try {
    assert.equal(pane.handle('Reflect')['can-edit-display'], true)
    assert.equal(pane.handle('Transmit')['can-edit-display'], true)
    pane.handle('Reflect').onActive(true)
    await nextTick()
    assert.equal(pane.handle('Reflect')['can-edit-display'], true)
    assert.equal(pane.handle('Transmit')['can-edit-display'], false)
    pane.handle('Reflect').onPreview({ name: 'Reflect', referencePosition: 7 })
    await nextTick()
    assert.equal(pane.bindings.axisScale.value.referencePosition, 7)

    const other = pane.handle('Transmit')
    other.onActive(true)
    other.onPreview({ name: 'Transmit', referencePosition: 3 })
    other.onActive(false)
    other.onPreview(null)
    await nextTick()
    assert.equal(pane.bindings.axisScale.value.referencePosition, 7)
    assert.equal(pane.scale('Transmit').referencePosition, 9)
    assert.equal(pane.handle('Reflect')['can-edit-display'], true)
    assert.equal(pane.handle('Transmit')['can-edit-display'], false)

    pane.handle('Reflect').onPreview(null)
    pane.handle('Reflect').onActive(false)
    await nextTick()
    assert.equal(pane.bindings.axisScale.value.referencePosition, 9)
    assert.equal(pane.handle('Transmit')['can-edit-display'], true)
    assert.deepEqual(pane.emitted, [])
  } finally {
    pane.dispose()
  }
})

test('松手后的 pending 预览仍占用所属迹线，非 owner 清理及新 begin 不能覆盖，读回清理后才能接手', async () => {
  const pane = await twoTracePane()
  try {
    const before = JSON.stringify(pane.props)
    const owner = pane.handle('Reflect')
    owner.onActive(true)
    owner.onPreview({ name: 'Reflect', referencePosition: 6 })
    owner.onActive(false)
    await nextTick()
    assert.equal(pane.bindings.axisScale.value.referencePosition, 6)
    assert.equal(pane.handle('Transmit')['can-edit-display'], false)
    pane.handle('Transmit').onPreview(null)
    pane.handle('Transmit').onActive(true)
    pane.handle('Transmit').onPreview({ name: 'Transmit', referencePosition: 2 })
    await nextTick()
    assert.equal(pane.bindings.axisScale.value.referencePosition, 6)
    assert.equal(pane.scale('Transmit').referencePosition, 9)

    owner.onPreview(null)
    await nextTick()
    assert.equal(pane.handle('Transmit')['can-edit-display'], true)
    const next = pane.handle('Transmit')
    next.onActive(true)
    next.onPreview({ name: 'Transmit', referencePosition: 4 })
    await nextTick()
    assert.equal(pane.handle('Reflect')['can-edit-display'], false)
    assert.equal(pane.scale('Transmit').referencePosition, 4)
    assert.equal(pane.bindings.axisScale.value.referencePosition, 9)
    owner.onActive(false)
    owner.onPreview(null)
    await nextTick()
    assert.equal(pane.scale('Transmit').referencePosition, 4)
    assert.equal(pane.handle('Reflect')['can-edit-display'], false)
    assert.equal(JSON.stringify(pane.props), before)
  } finally {
    pane.dispose()
  }
})

function deferred<T>() {
  let resolve!: (value: T) => void
  const promise = new Promise<T>((accept) => {
    resolve = accept
  })
  return { promise, resolve }
}

class RenewalGateway extends InstrumentApi {
  current = instrumentStatus()
  claims = 0
  pendingClaim: ReturnType<typeof deferred<ClaimResponse>> | null = null
  enteredClaim: ReturnType<typeof deferred<void>> | null = null

  constructor() {
    super({
      storage: null,
      fetch: async () => {
        throw new Error('Unexpected request')
      },
    })
  }
  override async status() {
    return structuredClone(this.current)
  }
  override async frame(): Promise<FrameResponse> {
    return { available: false, current: false, reason: 'No frame' }
  }
  override claim(): Promise<ClaimResponse> {
    this.claims += 1
    this.enteredClaim?.resolve()
    return (
      this.pendingClaim?.promise ??
      Promise.resolve({
        sessionId: 'test-token',
        hasControl: true,
        leaseSeconds: 30,
      })
    )
  }
  override async release() {}
}

test('后台自动续租保持显示编辑和已有手势可用，真实忙碌、失控及释放立即关闭信号', async () => {
  const api = new RenewalGateway()
  let time = 1000
  const model = useInstrument({
    api,
    pollMs: 1e6,
    retryMs: 1e6,
    now: () => time,
  })
  try {
    assert.equal(model.canContinueDisplayGesture.value, false)
    model.start()
    await model.refresh()
    assert.equal(model.canEditDisplay.value, true)
    assert.equal(model.canContinueDisplayGesture.value, true)
    api.pendingClaim = deferred<ClaimResponse>()
    api.enteredClaim = deferred<void>()
    time += 12000
    const renewing = model.refresh()
    await api.enteredClaim.promise
    assert.equal(api.claims, 2, 'must exercise the automatic renewal path')
    // 同 token 续租只延长租约，不应短暂关闭显示编辑权限。
    assert.equal(model.controlBusy.value, false)
    assert.equal(model.canEditDisplay.value, true)
    assert.equal(model.canContinueDisplayGesture.value, true)
    api.pendingClaim.resolve({
      sessionId: 'test-token',
      hasControl: true,
      leaseSeconds: 30,
    })
    await renewing
    assert.equal(model.controlBusy.value, false)
    assert.equal(model.canEditDisplay.value, true)
    assert.equal(model.canContinueDisplayGesture.value, true)

    for (const lock of [model.writeInFlight, model.uncertainWrite]) {
      lock.value = true
      assert.equal(model.canContinueDisplayGesture.value, false)
      lock.value = false
      assert.equal(model.canContinueDisplayGesture.value, true)
    }
    model.pendingOperation.value = {
      id: 'pending-display',
      label: 'Apply display',
      kind: 'display',
      ownerSessionId: 'test-token',
      readingBack: true,
    }
    assert.equal(model.canContinueDisplayGesture.value, false)
    model.pendingOperation.value = null
    assert.equal(model.canContinueDisplayGesture.value, true)
    const saved = model.status.value!
    for (const invalid of [
      { ...saved, pending: true },
      { ...saved, state: 'Faulted' as const },
      { ...saved, control: { ...saved.control, hasControl: false } },
      { ...saved, control: { ...saved.control, releasing: true } },
    ]) {
      model.status.value = invalid
      assert.equal(model.canContinueDisplayGesture.value, false)
      model.status.value = saved
      assert.equal(model.canContinueDisplayGesture.value, true)
    }
    model.connected.value = false
    assert.equal(model.canContinueDisplayGesture.value, false)
    model.connected.value = true
    await model.release()
    assert.equal(model.canContinueDisplayGesture.value, false)
    assert.equal(model.canEditDisplay.value, false)
  } finally {
    api.pendingClaim?.resolve({
      sessionId: 'test-token',
      hasControl: true,
      leaseSeconds: 30,
    })
    model.stop()
  }
})
