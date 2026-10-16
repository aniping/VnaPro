import assert from 'node:assert/strict'
import test from 'node:test'
import {
  measuredTraceTitleLayout,
  type TraceTitleField,
} from '../src/plot/titleLayout'

const font = '11px Tahoma'
const allSections = {
  format: true,
  scale: true,
  reference: true,
  state: true,
}

/** 固定字宽模拟真实字体，避免测试依赖系统是否安装 Tahoma。 */
function measure(text: string, measuredFont: string): number {
  const size = Number(measuredFont.match(/([\d.]+)px/)?.[1] ?? 11)
  return (text.length * 5.7 * size) / 11
}

function field(
  kind: string,
  text: string,
  paddingLeft = 0,
  paddingRight = 4,
  measuredFont = font,
): TraceTitleField {
  return { kind, text, font: measuredFont, paddingLeft, paddingRight }
}

function title(
  name = 'Trc2',
  scale = '10 dB/',
  reference = 'Ref 0 dB',
  parameter = 'S21',
  format = 'dB Mag',
  measuredFont = font,
): TraceTitleField[] {
  return [
    field('trace-number', name, 4, 4, measuredFont),
    field('measurement-chip', parameter, 4, 4, measuredFont),
    field('trace-format', format, 4, 4, measuredFont),
    field('trace-scale', scale, 0, 4, measuredFont),
    field('trace-reference', reference, 0, 4, measuredFont),
  ]
}

function titleList(fields: TraceTitleField[], count = 4): TraceTitleField[][] {
  return Array.from({ length: count }, () => structuredClone(fields))
}

test('complete trace information takes priority over retaining four columns', () => {
  const ordinary = Array.from({ length: 17 }, (_, index) =>
    title(`Trc${index + 1}`),
  )
  const adjusted = structuredClone(ordinary)
  adjusted[16] = title('Trc17', '0.5 dB/', 'Ref -25.5 dB')
  const before = measuredTraceTitleLayout(806, ordinary, measure)
  const after = measuredTraceTitleLayout(806, adjusted, measure)

  assert.equal(before.columns, 4)
  assert.equal(before.compact, false)
  assert.equal(after.columns, 3)
  assert.equal(after.nameWidth, before.nameWidth)
  assert.equal(after.chipWidth, before.chipWidth)
  assert.deepEqual(before.sections, allSections)
  assert.deepEqual(after.sections, allSections)
})

test('decimal and scientific notation values reserve their complete actual width', () => {
  for (const [scale, reference, columns] of [
    ['0.00005 dB/', 'Ref -125.125 dB', 3],
    ['5e-7 dB/', 'Ref 9.064625e-1 dB', 3],
    ['100 dB/', 'Ref +10 dB', 3],
  ] as const) {
    const layout = measuredTraceTitleLayout(
      806,
      titleList(title('Trc17', scale, reference)),
      measure,
    )
    assert.equal(layout.columns, columns)
    assert.deepEqual(layout.sections, allSections)
  }
})

test('hiding a section never mutates displayed trace title fields', () => {
  const titles = titleList(title('Trc17', '0.5 dB/', 'Ref -25.5 dB'))
  const original = structuredClone(titles)
  const layout = measuredTraceTitleLayout(160, titles, measure)

  assert.deepEqual(titles, original)
  assert.equal(layout.sections.reference, false)
  assert.equal(titles[0]![3]!.text, '0.5 dB/')
  assert.equal(titles[0]![4]!.text, 'Ref -25.5 dB')
})

test('a medium diagram reduces columns before hiding any trace information', () => {
  const titles = [title(), title('Trc17', '0.5 dB/', 'Ref -25.5 dB')]
  const medium = measuredTraceTitleLayout(420, titles, measure)

  assert.equal(medium.columns, 1)
  assert.equal(medium.compact, false)
  assert.deepEqual(medium.sections, allSections)
})

test('sections are hidden only when one complete title exceeds the whole diagram', () => {
  const titles = [title(), title('Trc17', '0.5 dB/', 'Ref -25.5 dB')]
  const narrow = measuredTraceTitleLayout(160, titles, measure)

  assert.equal(narrow.columns, 1)
  assert.equal(narrow.compact, true)
  assert.deepEqual(narrow.sections, { ...allSections, reference: false })
})

test('a very narrow diagram retains complete names and parameters only', () => {
  const titles = [
    [...title(), field('trace-state', 'RAW')],
    [...title('Trc17', '0.5 dB/', 'Ref -25.5 dB'), field('trace-state', 'RAW')],
  ]
  const original = structuredClone(titles)
  const layout = measuredTraceTitleLayout(100, titles, measure)

  assert.equal(layout.columns, 1)
  assert.deepEqual(layout.sections, {
    format: false,
    scale: false,
    reference: false,
    state: false,
  })
  assert.equal(layout.nameWidth, Math.ceil(measure('Trc17', font) + 8))
  assert.equal(layout.chipWidth, Math.ceil(measure('S21', font) + 8))
  assert.deepEqual(titles, original)
})

test('hidden sections return when the diagram has enough room', () => {
  const titles = [
    ...titleList(title(), 3),
    title('Trc17', '0.5 dB/', 'Ref -25.5 dB'),
  ]
  const narrow = measuredTraceTitleLayout(160, titles, measure)
  const wide = measuredTraceTitleLayout(896, titles, measure)
  const fourSlots = measuredTraceTitleLayout(898, titles, measure)

  assert.equal(narrow.sections.reference, false)
  // 四个自然槽及三个 2px 间隔共需 898px，896px 只能容纳三列。
  assert.equal(wide.columns, 3)
  assert.deepEqual(wide.sections, allSections)
  assert.equal(fourSlots.columns, 4)
  assert.deepEqual(fourSlots.sections, allSections)
})

test('the longest title determines common section visibility for every entry', () => {
  const ordinary = title()
  const longer = title('Trc17', '0.5 dB/', 'Ref -25.5 dB')
  const forward = measuredTraceTitleLayout(
    806,
    [...titleList(ordinary, 3), longer],
    measure,
  )
  const reverse = measuredTraceTitleLayout(
    806,
    [longer, ...titleList(ordinary, 3)],
    measure,
  )

  assert.equal(forward.columns, 3)
  assert.deepEqual(forward.sections, allSections)
  assert.deepEqual(reverse, forward)
  assert.deepEqual(
    measuredTraceTitleLayout(806, [ordinary], measure).sections,
    allSections,
  )
})

test('a long scale value hides its entire section after hiding reference', () => {
  const scale = '0.123456789012345678901234567890 dB/'
  const titles = titleList(title('Trc17', scale, 'Ref -25.5 dB'))
  const layout = measuredTraceTitleLayout(160, titles, measure)

  assert.equal(layout.columns, 1)
  assert.deepEqual(layout.sections, {
    ...allSections,
    reference: false,
    scale: false,
  })
  assert.equal(titles[0]![3]!.text, scale)
  const wide = measuredTraceTitleLayout(806, titles, measure)
  assert.equal(wide.columns, 2)
  assert.deepEqual(wide.sections, allSections)
})

test('state is hidden after scale and before format when it does not fit', () => {
  const titles = [[...title('Trc17'), field('trace-state', 'RAW')]]
  const layout = measuredTraceTitleLayout(120, titles, measure)

  assert.equal(layout.columns, 1)
  assert.deepEqual(layout.sections, {
    format: true,
    scale: false,
    reference: false,
    state: false,
  })
  assert.equal(titles[0]![5]!.text, 'RAW')
})

test('larger annotation fonts still reduce title columns', () => {
  const standard = measuredTraceTitleLayout(
    806,
    titleList(title('Trc17')),
    measure,
  )
  const enlarged = measuredTraceTitleLayout(
    806,
    titleList(
      title('Trc17', '10 dB/', 'Ref 0 dB', 'S21', 'dB Mag', '16.5px Tahoma'),
    ),
    measure,
  )

  assert.equal(standard.columns, 4)
  assert.equal(enlarged.columns, 2)
  assert.deepEqual(enlarged.sections, allSections)
  assert.ok(enlarged.nameWidth > standard.nameWidth)
  assert.ok(enlarged.chipWidth > standard.chipWidth)
})

test('trace names and measurement chips align using their widest actual labels', () => {
  const titles = [title(), title('Trc17', '0.5 dB/', 'Ref -25.5 dB', 'a4,4')]
  const layout = measuredTraceTitleLayout(806, titles, measure)

  assert.equal(layout.nameWidth, Math.ceil(measure('Trc17', font) + 8))
  assert.equal(layout.chipWidth, Math.ceil(measure('a4,4', font) + 8))
})

test('digits in trace names retain their actual width', () => {
  const name = 'Trc1234567890'
  const layout = measuredTraceTitleLayout(806, titleList(title(name)), measure)

  assert.equal(layout.nameWidth, Math.ceil(measure(name, font) + 8))
  assert.equal(layout.columns, 3)
})

test('longer measurement and format labels still widen the layout', () => {
  const standard = measuredTraceTitleLayout(
    806,
    titleList(title('Trc17')),
    measure,
  )
  const longParameter = measuredTraceTitleLayout(
    806,
    titleList(title('Trc17', '10 dB/', 'Ref 0 dB', 'A1P1/B1P1')),
    measure,
  )
  const longFormat = measuredTraceTitleLayout(
    806,
    titleList(title('Trc17', '10 dB/', 'Ref 0 dB', 'S21', 'Smith R+jX')),
    measure,
  )

  assert.equal(standard.columns, 4)
  assert.equal(longParameter.columns, 3)
  assert.ok(longParameter.chipWidth > standard.chipWidth)
  assert.equal(longFormat.columns, 3)
  assert.deepEqual(longParameter.sections, allSections)
  assert.deepEqual(longFormat.sections, allSections)
})

test('section visibility uses the actual number of trace title columns', () => {
  const titles = [title('Trc17', '0.5 dB/', 'Ref -25.5 dB')]
  const layout = measuredTraceTitleLayout(806, titles, measure)

  assert.equal(layout.columns, 1)
  assert.deepEqual(layout.sections, allSections)
  assert.deepEqual(
    measuredTraceTitleLayout(806, titleList(titles[0]!, 2), measure),
    { ...layout, columns: 2 },
  )
  assert.deepEqual(
    measuredTraceTitleLayout(806, titleList(titles[0]!, 2), measure).sections,
    allSections,
  )
})

test('every displayed section fits completely inside its trace title column', () => {
  const titles = Array.from({ length: 17 }, (_, index) => [
    ...title(
      `Trc${index + 1}`,
      index === 16 ? '0.5 dB/' : '10 dB/',
      index === 16 ? 'Ref -25.5 dB' : 'Ref 0 dB',
    ),
    field('trace-state', 'RAW'),
  ])
  for (const available of [100, 160, 420, 806, 900]) {
    const layout = measuredTraceTitleLayout(available, titles, measure)
    const columnWidth = (available - (layout.columns - 1) * 4) / layout.columns
    const visibleKinds = new Set([
      'trace-number',
      'measurement-chip',
      ...(layout.sections.format ? ['trace-format'] : []),
      ...(layout.sections.scale ? ['trace-scale'] : []),
      ...(layout.sections.reference ? ['trace-reference'] : []),
      ...(layout.sections.state ? ['trace-state'] : []),
    ])

    for (const fields of titles) {
      const displayedWidth = fields
        .filter((item) => visibleKinds.has(item.kind))
        .reduce((sum, item) => {
          if (item.kind === 'trace-number') return sum + layout.nameWidth
          if (item.kind === 'measurement-chip') return sum + layout.chipWidth
          if (item.kind === 'trace-format') return sum + layout.formatWidth
          return (
            sum +
            measure(item.text, item.font) +
            item.paddingLeft +
            item.paddingRight
          )
        }, 0)
      assert.ok(
        displayedWidth <= layout.slotWidth,
        `At ${available}px, a complete title needs ${displayedWidth}px but its slot has ${layout.slotWidth}px`,
      )
      assert.ok(layout.slotWidth <= columnWidth)
    }
  }
})

test('natural title slots do not stretch with one two or seventeen entries', () => {
  const fields = title('Trc17', '0.5 dB/', 'Ref -25.5 dB')
  const single = measuredTraceTitleLayout(895, [fields], measure)
  const pair = measuredTraceTitleLayout(895, titleList(fields, 2), measure)
  const many = measuredTraceTitleLayout(895, titleList(fields, 17), measure)

  assert.equal(single.columns, 1)
  assert.equal(pair.columns, 2)
  assert.equal(many.columns, 3)
  assert.equal(single.slotWidth, 223)
  assert.equal(pair.slotWidth, single.slotWidth)
  assert.equal(many.slotWidth, single.slotWidth)
  assert.deepEqual(single.sections, allSections)
})

test('a wider diagram adds natural title slots without widening them', () => {
  const titles = titleList(title('Trc17', '0.5 dB/', 'Ref -25.5 dB'), 17)
  const original = measuredTraceTitleLayout(806, titles, measure)
  const enlarged = measuredTraceTitleLayout(1400, titles, measure)

  assert.equal(original.columns, 3)
  assert.equal(enlarged.columns, 6)
  assert.equal(original.slotWidth, 223)
  assert.equal(enlarged.slotWidth, original.slotWidth)
  assert.deepEqual(enlarged.sections, allSections)
})

test('format width is shared so SWR and dB magnitude scale fields start together', () => {
  const swr = title('Trc1', '1 /', 'Ref 1', 'S11', 'SWR')
  const magnitude = title('Trc2', '10 dB/', 'Ref 0 dB', 'S21', 'dB Mag')
  const titles = [swr, magnitude]
  const original = structuredClone(titles)
  const layout = measuredTraceTitleLayout(895, titles, measure)

  assert.equal(layout.nameWidth, Math.ceil(measure('Trc1', font) + 8))
  assert.equal(layout.chipWidth, Math.ceil(measure('S21', font) + 8))
  assert.equal(layout.formatWidth, Math.ceil(measure('dB Mag', font) + 8))
  assert.ok(layout.formatWidth > Math.ceil(measure('SWR', font) + 8))
  assert.equal(layout.slotWidth, 188)
  assert.deepEqual(titles, original)
})

test('natural title slot width shrinks only with whole hidden sections', () => {
  const titles = titleList(title('Trc17', '0.5 dB/', 'Ref -25.5 dB'))
  const narrow = measuredTraceTitleLayout(160, titles, measure)
  const veryNarrow = measuredTraceTitleLayout(100, titles, measure)

  assert.deepEqual(narrow.sections, { ...allSections, reference: false })
  assert.equal(narrow.slotWidth, 150)
  assert.equal(
    veryNarrow.slotWidth,
    veryNarrow.nameWidth + veryNarrow.chipWidth,
  )
  assert.ok(veryNarrow.slotWidth < narrow.slotWidth)
})

test('the empty title list uses the available width for its empty state', () => {
  const layout = measuredTraceTitleLayout(895, [], measure)

  assert.equal(layout.columns, 1)
  assert.equal(layout.slotWidth, 895)
  assert.equal(layout.formatWidth, 0)
  assert.deepEqual(layout.sections, allSections)
})
