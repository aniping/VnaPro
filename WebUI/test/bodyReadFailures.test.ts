import assert from 'node:assert/strict'
import test from 'node:test'
import { ApiError, InstrumentApi } from '../src/api/instrumentApi'

/** 响应头已到达，正文仍停在网络流内；abort 只能由请求信号触发。 */
function blockedBody(status: number) {
  let started!: () => void
  const reading = new Promise<void>((resolve) => {
    started = resolve
  })
  const requests: { url: string; method: string }[] = []
  const fetcher: typeof fetch = async (input, options) => {
    requests.push({ url: String(input), method: options?.method ?? 'GET' })
    const signal = options?.signal
    assert.ok(signal)
    const stream = new ReadableStream<Uint8Array>(
      {
        start(controller) {
          signal.addEventListener(
            'abort',
            () => {
              controller.error(
                new DOMException('Body read aborted', 'AbortError'),
              )
            },
            { once: true },
          )
        },
        pull() {
          started()
          return new Promise<void>(() => {})
        },
      },
      { highWaterMark: 0 },
    )
    return new Response(stream, { status })
  }
  return { fetcher, reading, requests }
}

for (const diagnostics of [false, true]) {
  for (const action of ['frame', 'configure'] as const) {
    for (const failure of ['timeout', 'cancelled'] as const) {
      test(`${action} 正文读取 ${failure} 保留正确错误分类（诊断 ${diagnostics}）`, async (context) => {
        const logs: unknown[][] = []
        context.mock.method(console, 'debug', (...args: unknown[]) =>
          logs.push(args),
        )
        const body = blockedBody(action === 'frame' ? 200 : 202)
        const api = new InstrumentApi({
          storage: null,
          fetch: body.fetcher,
          diagnostics,
          timeoutMs: failure === 'timeout' ? 50 : 1000,
        })
        const request =
          action === 'frame'
            ? api.frame()
            : api.configure({ points: 4000 }, '1')
        const rejected = assert.rejects(request, (error) => {
          assert.ok(error instanceof ApiError)
          assert.equal(
            error.kind,
            failure === 'timeout' ? 'transport' : 'cancelled',
          )
          assert.equal(
            error.message,
            failure === 'timeout' ? '采集服务响应超时' : '界面请求已停止',
          )
          return true
        })
        await body.reading
        if (failure === 'cancelled') api.abortRequests()
        await rejected
        // 正文取消后仍不可确定服务器是否已处理写入，因此不能自行重发。
        assert.deepEqual(body.requests, [
          {
            url:
              action === 'frame'
                ? '/api/v1/frame?encoding=packed'
                : '/api/v1/config',
            method: action === 'frame' ? 'GET' : 'POST',
          },
        ])
        if (diagnostics && action === 'frame') {
          assert.equal(logs.length, 1)
          const metric = logs[0]![1] as Record<string, unknown>
          assert.equal(metric.stage, 'receive')
          assert.equal(
            metric.kind,
            failure === 'timeout' ? 'transport' : 'cancelled',
          )
        } else {
          assert.equal(logs.length, 0)
        }
      })
    }
  }
}

for (const diagnostics of [false, true]) {
  for (const action of ['frame', 'configure'] as const) {
    for (const failure of ['network', 'syntax', 'apiError'] as const) {
      test(`${action} 正文 ${failure} 与 JSON 语法错误分开定位（诊断 ${diagnostics}）`, async (context) => {
        const logs: unknown[][] = []
        context.mock.method(console, 'debug', (...args: unknown[]) =>
          logs.push(args),
        )
        const original = new ApiError('Original API error', 503, 'BUSY', 'http')
        let requests = 0
        const api = new InstrumentApi({
          storage: null,
          diagnostics,
          fetch: async () => {
            requests++
            const status = action === 'frame' ? 200 : 202
            if (failure === 'syntax')
              return new Response('{"private-raw-data":', { status })
            return new Response(
              new ReadableStream<Uint8Array>(
                {
                  pull(controller) {
                    controller.error(
                      failure === 'network'
                        ? new TypeError('Network body terminated')
                        : original,
                    )
                  },
                },
                { highWaterMark: 0 },
              ),
              { status },
            )
          },
        })
        const expectedKind =
          failure === 'network'
            ? 'transport'
            : failure === 'syntax'
              ? 'protocol'
              : 'http'
        const request =
          action === 'frame'
            ? api.frame()
            : api.configure({ points: 4000 }, '1')
        await assert.rejects(request, (error) => {
          assert.ok(error instanceof ApiError)
          assert.equal(error.kind, expectedKind)
          if (failure === 'apiError') assert.equal(error, original)
          return true
        })
        assert.equal(requests, 1)
        if (diagnostics && action === 'frame') {
          assert.equal(logs.length, 1)
          const metric = logs[0]![1] as Record<string, unknown>
          assert.equal(metric.stage, failure === 'syntax' ? 'json' : 'receive')
          assert.equal(metric.kind, expectedKind)
          assert.ok(!JSON.stringify(logs).includes('private-raw-data'))
        } else {
          assert.equal(logs.length, 0)
        }
      })
    }
  }
}
