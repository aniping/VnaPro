import { spawnSync } from 'node:child_process'
import { mkdirSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { resolve, relative } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../', import.meta.url))
const output = resolve(root, '.test-build')
if (relative(root, output) !== '.test-build') throw new Error('Invalid test output directory')
rmSync(output, { recursive: true, force: true })
const compiled = spawnSync(process.execPath, ['node_modules/typescript/bin/tsc', '-p', 'tsconfig.test.json'], { cwd: root, stdio: 'inherit' })
if (compiled.status !== 0) process.exit(compiled.status ?? 1)
mkdirSync(new URL('../.test-build/', import.meta.url), { recursive: true })
writeFileSync(new URL('../.test-build/package.json', import.meta.url), '{"type":"commonjs"}\n')
const testRoot = resolve(output, 'test')
const tests = readdirSync(testRoot).filter(name => name.endsWith('.test.js')).map(name => resolve(testRoot, name))
if (!tests.length) throw new Error('No compiled tests found')
const tested = spawnSync(process.execPath, ['--test', ...tests], { cwd: root, stdio: 'inherit' })
process.exit(tested.status ?? 1)
