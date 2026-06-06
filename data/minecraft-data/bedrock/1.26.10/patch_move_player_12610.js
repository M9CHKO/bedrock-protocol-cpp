#!/usr/bin/env node
const fs = require('fs')
const path = require('path')

const VERSION = process.env.MC_VERSION || '1.26.10'
const DRY = process.env.DRY === '1'
const FORCE_CANONICAL = process.env.FORCE_CANONICAL === '1'

function log(...a) { console.log('[move_player_patch]', ...a) }
function die(...a) { console.error('[move_player_patch ERROR]', ...a); process.exit(1) }

function findPackageRoot(pkgName) {
  const entry = require.resolve(pkgName)
  let dir = path.dirname(entry)

  while (dir !== path.dirname(dir)) {
    if (fs.existsSync(path.join(dir, 'package.json'))) {
      const pkg = JSON.parse(fs.readFileSync(path.join(dir, 'package.json'), 'utf8'))
      if (pkg.name === pkgName) return dir
    }
    dir = path.dirname(dir)
  }

  throw new Error(`Cannot find package root for ${pkgName}`)
}

function findProtocolJson() {
  const roots = []

  try { roots.push(findPackageRoot('minecraft-data')) } catch (e) {}

  try {
    const bpRoot = findPackageRoot('bedrock-protocol')
    roots.push(path.join(bpRoot, 'node_modules', 'minecraft-data', 'minecraft-data'))
  } catch (e) {}

  const tried = []

  for (const root of roots) {
    const p = path.join(root, 'data', 'bedrock', VERSION, 'protocol.json')
    tried.push(p)
    if (fs.existsSync(p)) return p
  }

  die(
    'protocol.json not found for',
    VERSION,
    '\nTried:\n' + tried.join('\n') + '\nRun: npm update bedrock-protocol minecraft-data'
  )
}

function isContainerType(x) {
  return Array.isArray(x) && x[0] === 'container' && Array.isArray(x[1])
}

function fieldName(f) {
  return f && typeof f === 'object' ? f.name : undefined
}

function getFields(def) {
  if (!isContainerType(def)) return null
  return def[1]
}

function clone(x) {
  return JSON.parse(JSON.stringify(x))
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

function ensureTeleportType(types) {
  if (!types.move_player_teleport) {
    types.move_player_teleport = ['container', [
      { name: 'teleportation_cause', type: 'li32' },
      { name: 'teleportation_item', type: 'li32' }
    ]]
    return true
  }
  return false
}

function setFieldType(fields, names, type, changes, opts = {}) {
  const nameSet = new Set(names)
  let hit = false

  for (const f of fields) {
    if (!f || typeof f !== 'object') continue
    if (!nameSet.has(f.name)) continue

    hit = true
    const old = JSON.stringify(f.type)
    const next = JSON.stringify(type)

    if (old !== next) {
      if (
        opts.preserveEnum &&
        typeof f.type === 'string' &&
        !['varint', 'varint64', 'zigzag32', 'u8', 'u16', 'u32', 'li32'].includes(f.type)
      ) {
        changes.push(`preserved enum field ${f.name}: ${f.type}`)
      } else {
        f.type = clone(type)
        changes.push(`field ${f.name}: ${old} -> ${next}`)
      }
    }
  }

  return hit
}

function hasField(fields, name) {
  return fields.some(f => fieldName(f) === name)
}

function insertAfter(fields, afterName, field, changes) {
  if (hasField(fields, field.name)) return false
  const idx = fields.findIndex(f => fieldName(f) === afterName)
  if (idx >= 0) fields.splice(idx + 1, 0, field)
  else fields.push(field)
  changes.push(`added missing field ${field.name}`)
  return true
}

const protocolPath = findProtocolJson()
log('version:', VERSION)
log('protocol:', protocolPath)

const raw = fs.readFileSync(protocolPath, 'utf8')
const json = JSON.parse(raw)
json.types ||= {}

const backup = `${protocolPath}.bak_${Date.now()}`
const changes = []

if (FORCE_CANONICAL || !json.types.packet_move_player) {
  if (!json.types.packet_move_player) changes.push('packet_move_player missing: inserted canonical definition')
  else changes.push('FORCE_CANONICAL=1: replaced packet_move_player with canonical definition')

  json.types.packet_move_player = canonicalMovePlayer()
  ensureTeleportType(json.types) && changes.push('added move_player_teleport helper type')
} else {
  const fields = getFields(json.types.packet_move_player)

  if (!fields) {
    changes.push('packet_move_player was not a container: replaced with canonical definition')
    json.types.packet_move_player = canonicalMovePlayer()
    ensureTeleportType(json.types) && changes.push('added move_player_teleport helper type')
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
    insertAfter(fields, 'ridden_runtime_id', { name: 'tick', type: 'varint64' }, changes)
  }
}

log('current packet_move_player:')
console.log(JSON.stringify(json.types.packet_move_player, null, 2))

if (changes.length === 0) {
  log('No changes needed. Your packet_move_player already looks OK.')
  process.exit(0)
}

log('changes:')
for (const c of changes) log(' -', c)

if (DRY) {
  log('DRY=1, not writing file.')
  process.exit(0)
}

fs.copyFileSync(protocolPath, backup)
fs.writeFileSync(protocolPath, JSON.stringify(json, null, 2) + '\n')

log('backup:', backup)
log('patched:', protocolPath)
log('Now run your relay again. If protodef cache exists, remove node_modules/.cache or reinstall package cache if needed.')
