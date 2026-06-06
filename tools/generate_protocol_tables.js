#!/usr/bin/env node
'use strict'

const fs = require('fs')
const path = require('path')

function die (msg) {
  console.error('[GEN ERROR]', msg)
  process.exit(1)
}

function exists (p) {
  try { return fs.existsSync(p) } catch { return false }
}

function readJson (p) {
  try {
    return JSON.parse(fs.readFileSync(p, 'utf8'))
  } catch (e) {
    die('failed to read json: ' + p + '\n' + e.stack)
  }
}

function esc (s) {
  return String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"')
}

function ident (version) {
  return String(version).replace(/[^A-Za-z0-9_]/g, '_')
}

function findDataRoot () {
  const argRoot = process.argv[3]
  const candidates = [
    argRoot,
    path.join(process.cwd(), 'data', 'bedrock'),
    path.join(process.cwd(), 'bedrock'),
    path.join(process.cwd(), 'node_modules', 'minecraft-data', 'data', 'bedrock')
  ].filter(Boolean)

  for (const c of candidates) {
    if (exists(c)) return c
  }

  die('bedrock data root not found. Put bedrock.zip in project root and unzip, or use node_modules/minecraft-data.')
}

function listVersions (root, requested) {
  const dirs = fs.readdirSync(root)
    .filter(v => exists(path.join(root, v, 'protocol.json')) && exists(path.join(root, v, 'version.json')))
    .sort((a, b) => {
      const pa = a.split('.').map(Number)
      const pb = b.split('.').map(Number)
      for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
        const da = pa[i] || 0
        const db = pb[i] || 0
        if (da !== db) return da - db
      }
      return a.localeCompare(b)
    })

  if (requested === 'all') return dirs
  if (!dirs.includes(requested)) {
    die(`version ${requested} not found in ${root}`)
  }
  return [requested]
}

function parseMcpePacketInfo (protocolJson) {
  const mcpe = protocolJson.types && protocolJson.types.mcpe_packet
  if (!Array.isArray(mcpe) || mcpe[0] !== 'container' || !Array.isArray(mcpe[1])) {
    die('protocol.json has unsupported mcpe_packet layout')
  }

  const fields = mcpe[1]
  const nameField = fields.find(f => f && f.name === 'name')
  const paramsField = fields.find(f => f && f.name === 'params')

  if (!nameField || !Array.isArray(nameField.type) || nameField.type[0] !== 'mapper') {
    die('mcpe_packet.name mapper not found')
  }

  const mappings = nameField.type[1] && nameField.type[1].mappings
  if (!mappings) die('mcpe_packet.name mappings not found')

  let paramsTypes = {}
  if (paramsField && Array.isArray(paramsField.type) && paramsField.type[0] === 'switch') {
    paramsTypes = paramsField.type[1].fields || {}
  }

  return Object.keys(mappings)
    .map(k => ({
      id: Number(k),
      name: mappings[k],
      paramsType: paramsTypes[mappings[k]] || ''
    }))
    .filter(p => Number.isFinite(p.id))
    .sort((a, b) => a.id - b.id)
}

function generate (root, versions) {
  const includeDir = path.join(process.cwd(), 'include', 'bedrock', 'generated')
  const srcDir = path.join(process.cwd(), 'src', 'generated')
  fs.mkdirSync(includeDir, { recursive: true })
  fs.mkdirSync(srcDir, { recursive: true })

  const versionData = []

  for (const version of versions) {
    const dir = path.join(root, version)
    const protocolJson = readJson(path.join(dir, 'protocol.json'))
    const versionJson = readJson(path.join(dir, 'version.json'))
    const packets = parseMcpePacketInfo(protocolJson)

    versionData.push({
      minecraftVersion: version,
      protocolVersion: Number(versionJson.version || versionJson.protocol || 0),
      packets
    })

    console.log(`[GEN] ${version}: protocol=${versionJson.version} packets=${packets.length}`)
  }

  const hpp = `#pragma once

#include <bedrock/protocol/GeneratedProtocolRegistry.hpp>

namespace bedrock {

const ProtocolVersionInfo* generatedProtocolVersionByName(const std::string& minecraftVersion);
std::vector<std::string> generatedProtocolVersionNames();

} // namespace bedrock
`

  let cpp = `#include <bedrock/generated/GeneratedProtocolTables.hpp>

#include <algorithm>
#include <array>
#include <string_view>

namespace bedrock {

`

  for (const v of versionData) {
    const id = ident(v.minecraftVersion)
    cpp += `static const ProtocolPacketInfo PACKETS_${id}[] = {\n`
    for (const p of v.packets) {
      cpp += `    { ${p.id}u, "${esc(p.name)}", "${esc(p.paramsType)}" },\n`
    }
    cpp += `};\n\n`
    cpp += `static const ProtocolVersionInfo VERSION_${id} = {\n`
    cpp += `    "${esc(v.minecraftVersion)}",\n`
    cpp += `    ${v.protocolVersion}u,\n`
    cpp += `    PACKETS_${id},\n`
    cpp += `    sizeof(PACKETS_${id}) / sizeof(PACKETS_${id}[0])\n`
    cpp += `};\n\n`
  }

  cpp += `static const ProtocolVersionInfo* const ALL_VERSIONS[] = {\n`
  for (const v of versionData) {
    cpp += `    &VERSION_${ident(v.minecraftVersion)},\n`
  }
  cpp += `};\n\n`

  cpp += `const ProtocolVersionInfo* generatedProtocolVersionByName(const std::string& minecraftVersion) {
    for (const ProtocolVersionInfo* version : ALL_VERSIONS) {
        if (version && minecraftVersion == version->minecraftVersion) {
            return version;
        }
    }
    return nullptr;
}

std::vector<std::string> generatedProtocolVersionNames() {
    std::vector<std::string> out;
    for (const ProtocolVersionInfo* version : ALL_VERSIONS) {
        if (version) out.emplace_back(version->minecraftVersion);
    }
    return out;
}

const ProtocolVersionInfo* getGeneratedProtocolVersion(const std::string& minecraftVersion) {
    return generatedProtocolVersionByName(minecraftVersion);
}

std::vector<std::string> getGeneratedProtocolVersions() {
    return generatedProtocolVersionNames();
}

} // namespace bedrock
`

  fs.writeFileSync(path.join(includeDir, 'GeneratedProtocolTables.hpp'), hpp)
  fs.writeFileSync(path.join(srcDir, 'GeneratedProtocolTables.cpp'), cpp)
}

const requested = process.argv[2] || '1.21.100'
const root = findDataRoot()
console.log('[GEN] data root =', root)
generate(root, listVersions(root, requested))
