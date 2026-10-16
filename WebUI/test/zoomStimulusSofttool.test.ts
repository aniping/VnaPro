import assert from 'node:assert/strict'
import test from 'node:test'
import { reactive, type VNode } from 'vue'
import { renderToString } from '@vue/server-renderer'
import { findElements, sfcHarness } from './sfcHarness'

function propsFor() {
  return reactive({
    canZoom: true, canZoomOut: true, busy: false, selecting: false, reason: '',
    choose: () => {}, zoomOut: async () => true,
  })
}

function button(render: () => VNode, id: 'in' | 'out') {
  const node = findElements(render(), (element) => element.props?.['data-testid'] === `stimulus-zoom-${id}`)[0]
  assert.ok(node)
  return node.props!
}

test('Zoom In toggles selection through the parent and explains real sweep range changes', async () => {
  const props = propsFor()
  let choices = 0
  props.choose = () => { choices++; props.selecting = !props.selecting }
  const view = await sfcHarness('src/components/softtools/ZoomStimulusSofttool.vue', props, {})
  try {
    assert.equal(button(view.render, 'in')['aria-pressed'], false)
    button(view.render, 'in').onClick()
    assert.equal(choices, 1)
    assert.equal(button(view.render, 'in')['aria-pressed'], true)
    assert.match(await renderToString(view.render()), /再次点击 Zoom In 取消/)
    button(view.render, 'in').onClick()
    assert.equal(choices, 2)
    assert.equal(button(view.render, 'in')['aria-pressed'], false)
    const html = await renderToString(view.render())
    assert.match(html, /真实扫频频段与纵轴范围/)
    assert.match(html, /扫描点数保持不变/)
    assert.match(html, /返回上一次缩放范围/)
  } finally { view.dispose() }
})

test('read-only, busy and pending Zoom Out cannot invoke selection or additional restoration', async () => {
  const props = propsFor()
  let choices = 0
  let restorations = 0
  let finish!: (value: boolean) => void
  props.choose = () => { choices++ }
  props.zoomOut = () => { restorations++; return new Promise((resolve) => { finish = resolve }) }
  const view = await sfcHarness('src/components/softtools/ZoomStimulusSofttool.vue', props, {})
  try {
    props.canZoom = false
    props.canZoomOut = false
    for (const id of ['in', 'out'] as const) {
      assert.equal(button(view.render, id).disabled, true)
      await button(view.render, id).onClick()
    }
    props.canZoom = true
    props.canZoomOut = true
    props.busy = true
    await button(view.render, 'in').onClick()
    await button(view.render, 'out').onClick()
    assert.equal(choices, 0)
    assert.equal(restorations, 0)
    props.busy = false
    const pending = button(view.render, 'out').onClick()
    for (const id of ['in', 'out'] as const) {
      assert.equal(button(view.render, id).disabled, true)
      await button(view.render, id).onClick()
    }
    assert.equal(restorations, 1)
    assert.equal(choices, 0)
    finish(false)
    await pending
    assert.equal(button(view.render, 'out').disabled, false)
    assert.equal(button(view.render, 'in').disabled, false)
  } finally { finish?.(false); view.dispose() }
})

test('Zoom Out has its own availability and failed restoration releases the local lock', async () => {
  const props = propsFor()
  props.canZoom = false
  props.reason = '圆图不支持框选，仍可恢复上次缩放。'
  props.zoomOut = async () => { throw new Error('Configuration revision changed') }
  const view = await sfcHarness('src/components/softtools/ZoomStimulusSofttool.vue', props, {})
  try {
    assert.equal(button(view.render, 'in').disabled, true)
    assert.equal(button(view.render, 'out').disabled, false)
    assert.match(await renderToString(view.render()), /圆图不支持框选/)
    await assert.rejects(button(view.render, 'out').onClick(), /revision changed/)
    assert.equal(button(view.render, 'out').disabled, false)
    assert.equal(props.selecting, false)
  } finally { view.dispose() }
})
