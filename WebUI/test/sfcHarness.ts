import { readFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import { basename, dirname, resolve } from 'node:path'
import { runInNewContext } from 'node:vm'
import { compileScript, compileTemplate, parse } from '@vue/compiler-sfc'
import { build } from 'esbuild'
import { effectScope, isVNode, proxyRefs, type VNode } from 'vue'

interface SetupComponent {
  setup: (
    props: unknown,
    context: { expose: () => void; emit: (...args: unknown[]) => void },
  ) => Record<string, unknown>
}

/** 编译真实 SFC 的 setup 和模板，检查事件生产者传给子组件的属性。 */
export async function sfcHarness(
  relativePath: string,
  props: unknown,
  globals: Record<string, unknown>,
) {
  const path = resolve(process.cwd(), relativePath)
  const { descriptor } = parse(readFileSync(path, 'utf8'), { filename: path })
  const script = compileScript(descriptor, { id: 'interaction-test' })
  const template = compileTemplate({
    id: 'interaction-test',
    filename: path,
    source: descriptor.template!.content,
    compilerOptions: { bindingMetadata: script.bindings },
  })
  if (template.errors.length) throw new Error(template.errors.join('\n'))
  const bundled = await build({
    stdin: {
      contents: `${script.content}\n${template.code}`,
      resolveDir: dirname(path),
      loader: 'ts',
    },
    bundle: true,
    platform: 'node',
    format: 'cjs',
    external: ['vue'],
    write: false,
    plugins: [
      {
        name: 'child-components',
        setup(plugin) {
          plugin.onLoad({ filter: /\.vue$/ }, (args) => ({
            contents: `export default { name: ${JSON.stringify(basename(args.path, '.vue'))} }`,
            loader: 'js',
          }))
        },
      },
    ],
  })
  const module = { exports: {} as Record<string, unknown> }
  runInNewContext(bundled.outputFiles[0]!.text, {
    module,
    exports: module.exports,
    require: createRequire(resolve(process.cwd(), 'package.json')),
    console,
    TextEncoder,
    TextDecoder,
    atob,
    btoa,
    ...globals,
  })
  const component = module.exports.default as SetupComponent
  const render = module.exports.render as (...args: unknown[]) => VNode
  const scope = effectScope()
  const emitted: unknown[][] = []
  // 此夹具不挂载 DOM；生命周期在实际浏览器验收中覆盖。
  const warn = console.warn
  let bindings: Record<string, unknown>
  try {
    console.warn = (...args: unknown[]) => {
      if (!String(args[0]).includes('no active component instance'))
        warn(...args)
    }
    bindings = scope.run(() =>
      component.setup(props, {
        expose() {},
        emit: (...args) => emitted.push(args),
      }),
    )!
  } finally {
    console.warn = warn
  }
  return {
    bindings,
    emitted,
    render: () => render({}, [], props, proxyRefs(bindings), {}, {}),
    dispose: () => scope.stop(),
  }
}

export function findComponent(node: VNode, name: string): VNode | undefined {
  if (
    typeof node.type === 'object' &&
    'name' in node.type &&
    node.type.name === name
  )
    return node
  if (!Array.isArray(node.children)) return undefined
  for (const child of node.children) {
    if (!isVNode(child)) continue
    const match = findComponent(child, name)
    if (match) return match
  }
  return undefined
}

export function findElements(
  node: VNode,
  predicate: (node: VNode) => boolean,
): VNode[] {
  const matches = predicate(node) ? [node] : []
  if (!Array.isArray(node.children)) return matches
  return matches.concat(
    node.children.flatMap((child) =>
      isVNode(child) ? findElements(child, predicate) : [],
    ),
  )
}
