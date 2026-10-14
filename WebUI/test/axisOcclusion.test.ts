import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test, { type TestContext } from 'node:test'
import { parse } from '@vue/compiler-sfc'
import { paintSmithDiagram } from '../src/plot/smithRenderer'
import { paintMixedDiagram } from '../src/plot/mixedRenderer'
import { plotRect } from '../src/plot/canvasRenderer'

function probe(context: TestContext) {
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'window')
  Object.defineProperty(globalThis, 'window', {
    configurable: true,
    value: { devicePixelRatio: 1 },
  })
  context.after(() => {
    if (previous) Object.defineProperty(globalThis, 'window', previous)
    else Reflect.deleteProperty(globalThis, 'window')
  })
  const operations: string[] = []
  const drawing = {
    strokeStyle: '',
    fillStyle: '',
    setTransform() {},
    clearRect() {},
    setLineDash() {},
    fillRect() {
      operations.push('mask')
    },
    beginPath() {},
    lineTo() {},
    moveTo() {},
    closePath() {},
    fill() {},
    save() {},
    rect() {},
    clip() {},
    restore() {},
    arc() {},
    fillText() {
      operations.push('label')
    },
    measureText(text: string) {
      return { width: text.length * 6 }
    },
    stroke() {
      operations.push(this.strokeStyle === '#60e600' ? 'trace' : 'grid')
    },
  }
  const canvas = {
    width: 0,
    height: 0,
    style: {},
    getContext: () => drawing,
  } as unknown as HTMLCanvasElement
  return { canvas, operations }
}

test('矩形刻度文字的 DOM 层不能带遮住测量迹线的黑底', () => {
  const { descriptor } = parse(
    readFileSync('src/components/diagrams/DiagramPane.vue', 'utf8'),
  )
  const css = descriptor.styles.map((style) => style.content).join('\n')
  const rule = css.match(/\.y-tick\s*\{([^}]+)\}/)?.[1]
  assert.ok(rule)
  assert.match(rule, /background:\s*transparent/)
})

for (const mixed of [false, true]) {
  test(`${mixed ? '混合' : '矩形'}绘图的刻度黑底位于全部网格之后、全部迹线之前`, (context) => {
    const drawing = probe(context)
    const diagram = {
      width: 700,
      height: 500,
      rect: plotRect(700, 500),
      xMin: 10e6,
      xMax: 20e6,
      references: [],
      axisFormat: 'db' as const,
      radius: 1,
      backgrounds: mixed
        ? [
            { format: 'db' as const, radius: 1 },
            { format: 'smith' as const, radius: 1 },
          ]
        : [{ format: 'db' as const, radius: 1 }],
      labelMasks: [{ left: 5, top: 45, width: 30, height: 14 }],
      curves: [
        {
          kind: 'cartesian' as const,
          format: 'db' as const,
          color: '#60e600',
          active: true,
          frequencyHz: [10e6, 20e6],
          values: [0, 0],
          scale: { min: -90, max: 10 },
          dashed: false,
        },
      ],
    }
    paintMixedDiagram(drawing.canvas, diagram, {
      left: 0,
      top: 0,
      width: 700,
      height: 500,
    })
    const masks = drawing.operations.reduce(
      (count, operation) => count + Number(operation === 'mask'),
      0,
    )
    assert.ok(masks > 1, '必须保留抹掉字框下方网格的黑底')
    assert.ok(
      drawing.operations.lastIndexOf('mask') >
        drawing.operations.lastIndexOf('grid'),
    )
    assert.ok(
      drawing.operations.lastIndexOf('mask') <
        drawing.operations.indexOf('trace'),
    )
  })
}

for (const mixed of [false, true]) {
  test(`${mixed ? '混合网格' : 'Smith'}刻度黑底仅遮网格，不能擦掉穿过字框的迹线`, (context) => {
    const drawing = probe(context)
    const curve = {
      color: '#60e600',
      active: true,
      points: [
        { real: -1, imaginary: 0 },
        { real: 1, imaginary: 0 },
      ],
    }
    const bounds = { left: 0, top: 0, width: 700, height: 500 }
    if (mixed) {
      paintMixedDiagram(
        drawing.canvas,
        {
          width: 700,
          height: 500,
          rect: plotRect(700, 500),
          xMin: 10e6,
          xMax: 20e6,
          references: [],
          axisFormat: 'smith',
          radius: 1,
          backgrounds: [
            { format: 'db', radius: 1 },
            { format: 'smith', radius: 1 },
          ],
          curves: [{ ...curve, kind: 'circular', format: 'smith', radius: 1 }],
        },
        bounds,
      )
    } else {
      paintSmithDiagram(
        drawing.canvas,
        {
          width: 700,
          height: 500,
          radius: 1,
          curves: [curve],
        },
        bounds,
      )
    }
    const trace = drawing.operations.indexOf('trace')
    assert.ok(trace >= 0)
    assert.ok(drawing.operations.includes('label'))
    assert.ok(
      !drawing.operations.slice(trace + 1).includes('mask'),
      '已绘制的测量曲线被随后填充的刻度黑框擦除',
    )
  })
}
