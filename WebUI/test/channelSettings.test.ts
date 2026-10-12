import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import test, { type TestContext } from 'node:test'
import { effectScope, nextTick, reactive, type Ref } from 'vue'
import { compileScript, parse } from 'vue/compiler-sfc'
import { ModuleKind, transpileModule } from 'typescript'
import type {
  InstrumentStatus,
  SweepConfiguration,
  SweepPatch,
} from '../src/api/types'
import { sweepWithinCapabilities } from '../src/domain/sweep'
import { frequencyText } from '../src/plot/formatting'
import { instrumentStatus } from './displayFixtures'

type SettingsMode = 'frequency' | 'points' | 'bandwidth'
interface SettingsProps {
  status: InstrumentStatus
  mode: SettingsMode
  canConfigure: boolean
  busy: boolean
  apply: (
    patch: SweepPatch,
    revision: string,
  ) => Promise<InstrumentStatus | null>
}
interface SettingsSetup {
  draft: SweepConfiguration
  dirty: Ref<boolean>
  revisionChanged: Ref<boolean>
  update: () => void
  discard: () => void
  setCenter: (value: number) => void
  setSpan: (value: number) => void
  commit: () => Promise<void>
}

// 执行真实单文件组件的 setup，验证提交边界；不复制组件内的草稿控制逻辑。
const path = resolve('src/components/softtools/ChannelSettingsSofttool.vue')
const { descriptor } = parse(readFileSync(path, 'utf8'), { filename: path })
const compiled = compileScript(descriptor, { id: 'channel-settings-test' })
const script = transpileModule(compiled.content, {
  compilerOptions: { module: ModuleKind.CommonJS },
}).outputText
const moduleValue = { exports: {} }
new Function('require', 'module', 'exports', script)(
  (name: string) => {
    if (name === 'vue') return require('vue')
    if (name === '../../domain/sweep') return { sweepWithinCapabilities }
    if (name === '../../plot/formatting') return { frequencyText }
    if (name.endsWith('FrequencyField.vue')) return { default: {} }
    throw new Error(`Unexpected component dependency: ${name}`)
  },
  moduleValue,
  moduleValue.exports,
)
const component = moduleValue.exports as {
  default: {
    setup: (
      props: SettingsProps,
      context: { expose: () => void },
    ) => SettingsSetup
  }
}

function settingsFixture(context: TestContext, mode: SettingsMode) {
  const calls: { patch: SweepPatch; revision: string }[] = []
  const props = reactive<SettingsProps>({
    status: instrumentStatus(),
    mode,
    canConfigure: true,
    busy: false,
    apply: async (patch, revision) => {
      calls.push({ patch, revision })
      return null
    },
  })
  const scope = effectScope()
  const setup = scope.run(() =>
    component.default.setup(props, { expose() {} }),
  )!
  context.after(() => scope.stop())
  return { props, setup, calls }
}

test('each channel menu sends only its own edited fields and preserves CAS revision', async (context) => {
  for (const mode of ['frequency', 'points', 'bandwidth'] as const) {
    const { props, setup, calls } = settingsFixture(context, mode)
    const revision = props.status.revision
    Object.assign(setup.draft, {
      startHz: NaN,
      stopHz: NaN,
      points: NaN,
      ifBandwidthHz: NaN,
    })
    if (mode === 'frequency') {
      setup.draft.startHz = 20e6
      setup.draft.stopHz = 30e6
    } else if (mode === 'points') setup.draft.points = 401
    else setup.draft.ifBandwidthHz = 10000
    setup.update()
    await setup.commit()
    assert.deepEqual(calls, [
      {
        patch:
          mode === 'frequency'
            ? { startHz: 20e6, stopHz: 30e6 }
            : mode === 'points'
              ? { points: 401 }
              : { ifBandwidthHz: 10000 },
        revision,
      },
    ])
  }
})

test('Center and Span submit raw intent without hidden sweep or bandwidth values', async (context) => {
  const { setup, calls } = settingsFixture(context, 'frequency')
  setup.setCenter(30e6)
  setup.setSpan(20e6)
  await setup.commit()
  assert.deepEqual(calls[0]!.patch, { centerHz: 30e6, spanHz: 20e6 })
})

test('a dirty menu keeps its draft through polling and reloads only when requested', async (context) => {
  const frequency = settingsFixture(context, 'frequency')
  const points = settingsFixture(context, 'points')
  frequency.setup.draft.startHz = 20e6
  frequency.setup.update()
  const updated = structuredClone(instrumentStatus())
  updated.revision = '99'
  updated.config.startHz = 15e6
  updated.config.points = 401
  frequency.props.status = updated
  points.props.status = updated
  await nextTick()
  assert.equal(frequency.setup.draft.startHz, 20e6)
  assert.equal(frequency.setup.revisionChanged.value, true)
  assert.equal(points.setup.draft.points, 401)
  frequency.setup.discard()
  assert.equal(frequency.setup.draft.startHz, 15e6)
  assert.equal(frequency.setup.dirty.value, false)
})

test('Points preserves HOLD permission and rejects invalid drafts before requesting work', async (context) => {
  const { props, setup, calls } = settingsFixture(context, 'points')
  setup.draft.points = 401
  setup.update()
  props.canConfigure = false
  await setup.commit()
  assert.equal(calls.length, 0)
  props.canConfigure = true
  setup.draft.points = 0
  setup.update()
  await setup.commit()
  assert.equal(calls.length, 0)
})

test('edits made while a configuration request completes remain an unapplied draft', async (context) => {
  const { props, setup } = settingsFixture(context, 'points')
  let finish!: (status: InstrumentStatus) => void
  props.apply = () =>
    new Promise((resolve) => {
      finish = resolve
    })
  setup.draft.points = 401
  setup.update()
  const committing = setup.commit()
  setup.draft.points = 601
  setup.update()
  const applied = instrumentStatus()
  applied.revision = '2'
  applied.config.points = 401
  finish(applied)
  await committing
  assert.equal(setup.draft.points, 601)
  assert.equal(setup.dirty.value, true)
  setup.draft.points = 401
  setup.update()
  assert.equal(setup.dirty.value, false)
})
