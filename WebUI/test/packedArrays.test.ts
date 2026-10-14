import assert from 'node:assert/strict'
import test from 'node:test'
import { array } from '../src/api/decodeValues'
import { decodeFrame } from '../src/api/decoders'
import { InstrumentApi } from '../src/api/instrumentApi'
import type { DisplayFormat } from '../src/domain/presentation'
import { acquiredFrame, projection, receiverTrace } from './displayFixtures'

/** 测试独立使用 Node 的 LE 写入，不复用生产解码过程。 */
function doubles(values: readonly (number | null)[]) {
  const bytes = Buffer.alloc(values.length * 8)
  values.forEach((value, index) => bytes.writeDoubleLE(value ?? NaN, index * 8))
  return {
    codec: 'f64le',
    count: values.length,
    data: bytes.toString('base64'),
  }
}

function booleans(values: readonly boolean[]) {
  return {
    codec: 'u8',
    count: values.length,
    data: Buffer.from(values.map((value) => Number(value))).toString('base64'),
  }
}

function strings(values: readonly string[]) {
  const dictionary = [...new Set(values)]
  return {
    codec: 'enum8',
    count: values.length,
    dictionary,
    data: Buffer.from(
      values.map((value) => dictionary.indexOf(value)),
    ).toString('base64'),
  }
}

function wire(value: unknown): unknown {
  if (Array.isArray(value)) {
    if (!value.length) return []
    if (value.every((entry) => entry === null || typeof entry === 'number'))
      return doubles(value)
    if (value.every((entry) => typeof entry === 'boolean'))
      return booleans(value)
    if (value.every((entry) => typeof entry === 'string')) return strings(value)
    return value.map(wire)
  }
  if (value === null || typeof value !== 'object') return value
  return Object.fromEntries(
    Object.entries(value).map(([key, item]) => [key, wire(item)]),
  )
}

test('f64le 无损保留双精度、负零和缺失值', () => {
  const values = [
    0,
    -0,
    1,
    -1,
    0.5,
    null,
    Number.MIN_VALUE,
    Number.MAX_VALUE,
    Math.PI,
  ]
  const decoded = array(doubles(values), 'points', values.length)
  assert.deepEqual(decoded, values)
  assert.equal(Object.is(decoded[1], -0), true)
})

test('u8 和 enum8 展开成 DTO 使用的布尔与状态数组', () => {
  assert.deepEqual(array(booleans([false, true, false]), 'valid', 3), [
    false,
    true,
    false,
  ])
  const states = ['finite', 'undefined', 'negativeInfinity', 'finite']
  assert.deepEqual(array(strings(states), 'states', 4), states)
  assert.deepEqual(array([], 'optional', 0), [])
  assert.deepEqual(
    array({ codec: 'f64le', count: 0, data: '' }, 'empty', 0),
    [],
  )
})

test('C++ 与 Python 独立生成的固定协议样本直接还原', () => {
  assert.deepEqual(
    array(
      {
        codec: 'f64le',
        count: 6,
        data: 'AAAAAAAAAAAAAAAAAAAAgAAAAAAAAPA/AAAAAAAA8L8AAAAAAADgPwAAAAAAAPh/',
      },
      'numbers',
      6,
    ),
    [0, -0, 1, -1, 0.5, null],
  )
  assert.deepEqual(
    array({ codec: 'u8', count: 4, data: 'AQABAQ==' }, 'valid', 4),
    [true, false, true, true],
  )
  assert.deepEqual(
    array(
      {
        codec: 'enum8',
        count: 4,
        dictionary: ['VALID', '无效', 'ZERO'],
        data: 'AAEAAg==',
      },
      'states',
      4,
    ),
    ['VALID', '无效', 'VALID', 'ZERO'],
  )
})

test('enum8 保留 256 项字典的最后一个索引和 Unicode 字符', () => {
  const dictionary = Array.from({ length: 256 }, (_, index) => `状态${index}`)
  assert.deepEqual(
    array({ codec: 'enum8', count: 1, dictionary, data: '/w==' }, 'states', 1),
    ['状态255'],
  )
})

const formats: readonly DisplayFormat[] = [
  'db',
  'magnitude',
  'phase',
  'real',
  'imag',
  'realimag',
  'smith',
  'swr',
  'unwrappedPhase',
  'logMagnitude',
  'delay',
  'delayDerivation',
  'polar',
  'invSmith',
]
for (const format of formats) {
  test(`${format} 紧凑完整帧与普通 JSON 数组逐值相同`, () => {
    const trace = receiverTrace(undefined, [0.2, 0.3, 0.4, 0.5])
    trace.projection = projection([1.25, null, -0, -12.3456789012345], format)
    trace.projection.valueStates[1] = 'negativeInfinity'
    const plain = acquiredFrame(trace)
    assert.deepEqual(decodeFrame(wire(plain)), decodeFrame(plain))
  })
}

test('紧凑状态仍接受原 DTO 语义校验，不能注入未知状态', () => {
  const trace = receiverTrace()
  const frame = wire(acquiredFrame(trace)) as Record<string, unknown>
  const entries = frame.traces as Record<string, unknown>[]
  const projection = entries[0]!.projection as Record<string, unknown>
  projection.valueStates = strings(['finite', 'invented'])
  assert.throws(() => decodeFrame(frame), /projection.valueState/)
})

test('紧凑数字不能替代布尔有效位，缺失波形不能标记为有效', () => {
  const first = acquiredFrame()
  const packedValidity = wire(first) as Record<string, unknown>
  const validityTrace = (packedValidity.traces as Record<string, unknown>[])[0]!
  validityTrace.valid = doubles([1, 1])
  assert.throws(() => decodeFrame(packedValidity), /trace.valid/)
  const packedMissing = wire(first) as Record<string, unknown>
  const missingTrace = (packedMissing.traces as Record<string, unknown>[])[0]!
  missingTrace.real = doubles([0.2, null])
  assert.throws(() => decodeFrame(packedMissing), /trace valid data/)
})

test('完整 10001 点紧凑帧保留尾部尖峰，不省略任何采样', () => {
  const values = Array.from({ length: 10001 }, (_, index) =>
    index === 9999 ? 1e9 : index / 10001,
  )
  const plain = acquiredFrame(receiverTrace(undefined, values))
  const decoded = decodeFrame(wire(plain))
  assert.equal(decoded.available, true)
  if (!decoded.available) return
  assert.equal(decoded.displayPointCount, 10001)
  assert.equal(decoded.decimated, false)
  assert.deepEqual(decoded.traces[0]!.real, values)
  assert.equal(decoded.traces[0]!.real[9999], 1e9)
  assert.equal(decoded.sampleIndexes[10000], 10000)
})

test('请求一次完整帧并支持尚未紧凑编码的旧 JSON 响应', async () => {
  const plain = acquiredFrame()
  const requests: string[] = []
  const api = new InstrumentApi({
    storage: null,
    fetch: async (input) => {
      requests.push(String(input))
      return new Response(
        JSON.stringify(requests.length === 1 ? wire(plain) : plain),
      )
    },
  })
  assert.deepEqual(await api.frame(), decodeFrame(plain))
  assert.deepEqual(await api.frame(), decodeFrame(plain))
  assert.deepEqual(requests, [
    '/api/v1/frame?encoding=packed',
    '/api/v1/frame?encoding=packed',
  ])
})

test('帧传输定位默认关闭，开启后只打印版本与容量耗时', async (context) => {
  const logs: unknown[][] = []
  context.mock.method(console, 'debug', (...args: unknown[]) => logs.push(args))
  const plain = acquiredFrame()
  const payload = JSON.stringify(wire(plain))
  const fetcher: typeof fetch = async () => new Response(payload)
  await new InstrumentApi({ storage: null, fetch: fetcher }).frame()
  assert.equal(logs.length, 0)
  await new InstrumentApi({
    storage: null,
    fetch: fetcher,
    diagnostics: true,
  }).frame()
  assert.equal(logs.length, 1)
  assert.equal(logs[0]![0], '[VnaPro][frame]')
  const metrics = logs[0]![1] as Record<string, unknown>
  assert.equal(metrics.frameSeq, plain.frameSeq)
  assert.equal(metrics.runId, plain.runId)
  assert.equal(metrics.displayRevision, plain.displayRevision)
  assert.equal(metrics.payloadBytes, Buffer.byteLength(payload, 'utf8'))
  assert.ok(Number(metrics.receiveMs) >= 0)
  assert.ok(Number(metrics.jsonMs) >= 0)
  assert.ok(Number(metrics.decodeMs) >= 0)
  assert.ok(!JSON.stringify(logs).includes('real'))
  assert.ok(!JSON.stringify(logs).includes('session'))
})

test('损坏紧凑帧打印定位阶段但不泄漏响应数据', async (context) => {
  const logs: unknown[][] = []
  context.mock.method(console, 'debug', (...args: unknown[]) => logs.push(args))
  const api = new InstrumentApi({
    storage: null,
    diagnostics: true,
    fetch: async () => new Response('{"private":"raw-data"}'),
  })
  await assert.rejects(api.frame(), /frame.available/)
  assert.equal(logs.length, 1)
  assert.equal(logs[0]![0], '[VnaPro][frame][failed]')
  assert.equal((logs[0]![1] as Record<string, unknown>).stage, 'decode')
  assert.equal((logs[0]![1] as Record<string, unknown>).kind, 'protocol')
  assert.ok(!JSON.stringify(logs).includes('raw-data'))
})

test('浏览器 debug=frames 参数开启定位，显式 false 可关闭', async (context) => {
  const logs: unknown[][] = []
  context.mock.method(console, 'debug', (...args: unknown[]) => logs.push(args))
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'location')
  Object.defineProperty(globalThis, 'location', {
    configurable: true,
    value: { search: '?debug=frames' },
  })
  context.after(() => {
    if (previous) Object.defineProperty(globalThis, 'location', previous)
    else Reflect.deleteProperty(globalThis, 'location')
  })
  const fetcher: typeof fetch = async () =>
    new Response(JSON.stringify(wire(acquiredFrame())))
  await new InstrumentApi({ storage: null, fetch: fetcher }).frame()
  assert.equal(logs.length, 1)
  await new InstrumentApi({
    storage: null,
    fetch: fetcher,
    diagnostics: false,
  }).frame()
  assert.equal(logs.length, 1)
})

const invalidPackets: readonly [string, unknown][] = [
  ['未知编码', { codec: 'float', count: 1, data: 'AA==' }],
  ['负长度', { codec: 'u8', count: -1, data: '' }],
  ['小数长度', { codec: 'u8', count: 1.5, data: 'AA==' }],
  ['字符串长度', { codec: 'u8', count: '1', data: 'AA==' }],
  ['不安全长度', { codec: 'u8', count: Number.MAX_SAFE_INTEGER + 1, data: '' }],
  ['超过调用者上限', { codec: 'u8', count: 4, data: 'AAAAAA==' }],
  ['短字节流', { codec: 'f64le', count: 1, data: 'AA==' }],
  ['长字节流', { codec: 'u8', count: 1, data: 'AAAA' }],
  ['Base64 缺少填充', { codec: 'u8', count: 1, data: 'AA' }],
  ['Base64 包含空白', { codec: 'u8', count: 1, data: 'AA==\n' }],
  ['Base64 错误填充', { codec: 'u8', count: 1, data: '=A==' }],
  ['Base64 非规范尾位', { codec: 'u8', count: 1, data: 'AB==' }],
  ['Base64 非字符串', { codec: 'u8', count: 1, data: ['AA=='] }],
  ['布尔值越界', { codec: 'u8', count: 1, data: 'Ag==' }],
  ['缺少字典', { codec: 'enum8', count: 1, data: 'AA==' }],
  [
    '字典值非字符串',
    { codec: 'enum8', count: 1, dictionary: [1], data: 'AA==' },
  ],
  [
    '重复字典项',
    {
      codec: 'enum8',
      count: 1,
      dictionary: ['finite', 'finite'],
      data: 'AA==',
    },
  ],
  [
    '字典索引越界',
    { codec: 'enum8', count: 1, dictionary: ['finite'], data: 'AQ==' },
  ],
  [
    '非空数组空字典',
    { codec: 'enum8', count: 1, dictionary: [], data: 'AA==' },
  ],
  [
    '字典超过 256 项',
    {
      codec: 'enum8',
      count: 1,
      dictionary: Array.from({ length: 257 }, (_, index) => String(index)),
      data: 'AA==',
    },
  ],
  ['正无穷', doubles([Infinity])],
  ['负无穷', doubles([-Infinity])],
]
for (const [name, packet] of invalidPackets) {
  test(`拒绝损坏紧凑数组：${name}`, () => {
    assert.throws(() => array(packet, 'points', 3), /服务数据格式无效：points/)
  })
}
