#!/usr/bin/env node
const fs = require('fs')
const path = require('path')

const VERSION = process.env.MC_VERSION || '1.26.10'
const DRY = process.env.DRY === '1'
const FORCE_CANONICAL = process.env.FORCE_CANONICAL === '1'
const TARGET = process.env.TARGET_PROTOCOL_JSON

function log(...a) { console.log('[move_player_patch]', ...a) }
function die(...a) { console.error('[move_player_patch ERROR]', ...a); process.exit(1) }

function existsFile(p) {
  try { return fs.existsSync(p) && fs.statSync(p).isFile() } catch (_) { return false }
}

function findProtocolJson() {
  const tried = []

  if (TARGET) {
    tried.push(TARGET)
    if (existsFile(TARGET)) return TARGET
  }

  const cwdProtocol = path.join(process.cwd(), 'protocol.json')
  tried.push(cwdProtocol)
  if (existsFile(cwdProtocol)) return cwdProtocol

  const common = [
    `/root/node_modules/minecraft-data/minecraft-data/data/bedrock/${VERSION}/protocol.json`,
    `/root/node_modules/minecraft-data/data/bedrock/${VERSION}/protocol.json`,
    `/root/node_modules/bedrock-protocol/node_modules/minecraft-data/minecraft-data/data/bedrock/${VERSION}/protocol.json`,
    `/root/node_modules/bedrock-protocol/node_modules/minecraft-data/data/bedrock/${VERSION}/protocol.json`,
    path.join(process.cwd(), 'node_modules', 'minecraft-data', 'minecraft-data', 'data', 'bedrock', VERSION, 'protocol.json'),
    path.join(process.cwd(), 'node_modules', 'minecraft-data', 'data', 'bedrock', VERSION, 'protocol.json')
  ]

  for (const p of common) {
    tried.push(p)
    if (existsFile(p)) return p
  }

  die(
    'protocol.json not found for',
    VERSION,
    '\nTried:\n' + tried.join('\n') +
    '\n\nRun this to find it:\nfind /root/node_modules -path "*/data/bedrock/' + VERSION + '/protocol.json" -print'
  )
}

function clone(x) {
  return JSON.parse(JSON.stringify(x))
}

function isContainerType(x) {
  return Array.isArray(x) && x[0] === 'container' && Array.isArray(x[1])
}

function getFields(def) {
  if (!isContainerType(def)) return null
  return def[1]
}

function fieldName(f) {
  return f && typeof f === 'object' ? f.name : undefined
}

function hasField(fields, name) {
  return fields.some(f => fieldName(f) === name)
}

function setFieldType(fields, names, type, changes, opts = {}) {
  const set = new Set(names)

  for (const f of fields) {
    if (!f || typeof f !== 'object') continue
    if (!set.has(f.name)) continue

    const old = JSON.stringify(f.type)
    const next = JSON.stringify(type)

    if (old === next) continue

    if (
      opts.preserveEnum &&
      typeof f.type === 'string' &&
      !['varint', 'varint64', 'zigzag32', 'u8', 'u16', 'u32', 'li32'].includes(f.type)
    ) {
      changes.push(`preserved enum field ${f.name}: ${f.type}`)
      continue
    }

    f.type = clone(type)
    changes.push(`field ${f.name}: ${old} -> ${next}`)
  }
}

function insertAfter(fields, afterName, field, changes) {
  if (hasField(fields, field.name)) return
  const idx = fields.findIndex(f => fieldName(f) === afterName)
  if (idx >= 0) fields.splice(idx + 1, 0, field)
  else fields.push(field)
  changes.push(`added missing field ${field.name}`)
}

function canonicalMovePlayer() {
  return ['container', [
    { name: 'runtime_id', type: 'varint64' },
    { name: 'position', type: 'vec3f' },
    { name: 'pitch', type: 'lf32' },
    { name: 'yaw', type: 'lf32' },
    { name: 'head_yaw', type: 'lf32' },
    { name: 'mode', type: 'u8' },
    { name: 'on_ground', type: 'bool' },
    { name: 'ridden_runtime_id', type: 'varint64' },
    {
      name: 'teleport',
      type: ['switch', {
        compareTo: 'mode',
        fields: {
          2: 'move_player_teleport',
          3: 'move_player_teleport',
          4: 'move_player_teleport'
        },
        default: 'void'
      }]
    },
    { name: 'tick', type: 'varint64' }
  ]]
}

function ensureTeleportType(types, changes) {
  if (types.move_player_teleport) return

  types.move_player_teleport = ['container', [
    { name: 'teleportation_cause', type: 'li32' },
    { name: 'teleportation_item', type: 'li32' }
  ]]

  changes.push('added move_player_teleport helper type')
}

const protocolPath = findProtocolJson()
log('version:', VERSION)
log('protocol:', protocolPath)

const raw = fs.readFileSync(protocolPath, 'utf8')
const json = JSON.parse(raw)
json.types ||= {}

const changes = []

if (FORCE_CANONICAL || !json.types.packet_move_player) {
  json.types.packet_move_player = canonicalMovePlayer()
  changes.push(FORCE_CANONICAL ? 'FORCE_CANONICAL=1: replaced packet_move_player' : 'packet_move_player missing: inserted canonical')
  ensureTeleportType(json.types, changes)
} else {
  const fields = getFields(json.types.packet_move_player)

  if (!fields) {
    json.types.packet_move_player = canonicalMovePlayer()
    changes.push('packet_move_player was not container: replaced with canonical')
    ensureTeleportType(json.types, changes)
  } else {
    setFieldType(fields, ['runtime_id', 'entity_runtime_id', 'entity_id'], 'varint64', changes)
    setFieldType(fields, ['position', 'pos'], 'vec3f', changes)
    setFieldType(fields, ['pitch', 'yaw', 'head_yaw'], 'lf32', changes)
    setFieldType(fields, ['mode'], 'u8', changes, { preserveEnum: true })
    setFieldType(fields, ['on_ground'], 'bool', changes)
    setFieldType(fields, ['ridden_runtime_id', 'riding_runtime_id'], 'varint64', changes)
    setFieldType(fields, ['tick', 'current_tick'], 'varint64', changes)

    insertAfter(fields, 'mode', { name: 'on_ground', type: 'bool' }, changes)
    insertAfter(fields, 'on_ground', { name: 'ridden_runtime_id', type: 'varint64' }, changes)

    if (!hasField(fields, 'teleport')) {
      insertAfter(fields, 'ridden_runtime_id', {
        name: 'teleport',
        type: ['switch', {
          compareTo: 'mode',
          fields: {
            2: 'move_player_teleport',
            3: 'move_player_teleport',
            4: 'move_player_teleport'
          },
          default: 'void'
        }]
      }, changes)
      ensureTeleportType(json.types, changes)
    }

    insertAfter(fields, 'teleport', { name: 'tick', type: 'varint64' }, changes)
  }
}

console.log('\n===== packet_move_player =====')
console.log(JSON.stringify(json.types.packet_move_player, null, 2))

if (changes.length === 0) {
  log('No changes needed.')
  process.exit(0)
}

console.log('\n===== changes =====')
for (const c of changes) log('-', c)

if (DRY) {
  log('DRY=1, not writing.')
  process.exit(0)
}

const backup = `${protocolPath}.bak_${Date.now()}`
fs.copyFileSync(protocolPath, backup)
fs.writeFileSync(protocolPath, JSON.stringify(json, null, 2) + '\n')

log('backup:', backup)
log('patched:', protocolPath)
