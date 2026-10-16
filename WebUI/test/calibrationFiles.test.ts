import assert from 'node:assert/strict'
import test from 'node:test'
import { calibrationFileBlob, decodeCalibrationFile, readCalibrationFile } from '../src/api/calibrationFiles'

test('binary file transport preserves every byte across chunk boundaries', async () => {
  const data = Uint8Array.from({ length: 1024 * 1024 + 2 }, (_, index) => index % 256)
  const imported = await readCalibrationFile(new File([data], 'cal.vcal'))
  assert.equal(imported.action, 'importFile')
  if (imported.action !== 'importFile') assert.fail('binary import command')
  const decoded = decodeCalibrationFile({ ...imported, filename: 'vnapro-9007199254740993.vcal', byteLength: data.length })
  assert.deepEqual(new Uint8Array(await calibrationFileBlob(decoded).arrayBuffer()), data)
})

test('CSV keeps UTF-8 bytes and JSON files are rejected before reading', async () => {
  const csv = '\uFEFFvnapro_calibration_csv,1\r\nname,text:四端口\r\n'
  const command = await readCalibrationFile(new File([csv], 'terms.csv'))
  assert.deepEqual(command, { action: 'importFile', format: 'csv', encoding: 'utf8', content: csv.slice(1) })
  const file = decodeCalibrationFile({ ...command, content: csv, filename: 'vnapro-1.csv', byteLength: new TextEncoder().encode(csv).length })
  assert.equal(await calibrationFileBlob(file).text(), csv.slice(1), 'Blob.text strips a UTF-8 BOM')
  await assert.rejects(readCalibrationFile(new File(['{"schema":2,"name":"old"}'], 'old.json')), /请选择/)
  await assert.rejects(readCalibrationFile(new File([new Uint8Array([0xff])], 'invalid.csv')), /UTF-8/)
})

test('file envelope rejects unsafe names, encoding mismatches, noncanonical Base64 and oversized files', async () => {
  const binary = { format: 'binary', encoding: 'base64', filename: 'vnapro-1.vcal', byteLength: 1, content: 'AA==' }
  for (const patch of [{ filename: '../file.vcal' }, { encoding: 'utf8' }, { content: 'AB==' },
    { byteLength: 2 }, { byteLength: 8 * 1024 * 1024 + 1 }])
    assert.throws(() => decodeCalibrationFile({ ...binary, ...patch }))
  await assert.rejects(readCalibrationFile(new File(['native'], 'unverified.pcs')), /请选择/)
  await assert.rejects(readCalibrationFile(new File(['null'], 'invalid.json')), /请选择/)
})
