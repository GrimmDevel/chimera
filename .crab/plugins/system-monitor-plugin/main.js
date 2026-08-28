const os = require('node:os')

function getCpuSample() {
  const cpus = os.cpus()
  let user = 0
  let sys = 0
  let idle = 0
  for (const cpu of cpus) {
    user += cpu.times.user
    sys += cpu.times.sys
    idle += cpu.times.idle
  }
  const total = user + sys + idle
  return { idle, total, cores: cpus.length }
}

let lastSample = getCpuSample()

function calcRealCpuUsage() {
  const currentSample = getCpuSample()
  const idleDiff = currentSample.idle - lastSample.idle
  const totalDiff = currentSample.total - lastSample.total
  lastSample = currentSample
  if (totalDiff <= 0) return 0
  const usage = Math.round(100 * (1 - idleDiff / totalDiff))
  return Math.max(0, Math.min(100, usage))
}

module.exports = {
  get_system_metrics: async () => {
    const cpuLoad = calcRealCpuUsage()
    const totalMem = os.totalmem()
    const freeMem = os.freemem()
    const usedMem = totalMem - freeMem
    const ramPercent = Math.round((usedMem / totalMem) * 100)
    const memUsage = typeof process !== 'undefined' && process.memoryUsage ? process.memoryUsage() : { heapUsed: 0 }

    return JSON.stringify({
      cpuLoad,
      totalRamGb: Number((totalMem / (1024 * 1024 * 1024)).toFixed(2)),
      usedRamGb: Number((usedMem / (1024 * 1024 * 1024)).toFixed(2)),
      ramPercent,
      ideMemMb: Math.round((memUsage.heapUsed || 0) / (1024 * 1024)),
      cores: os.cpus().length,
      platform: os.platform(),
      uptimeHours: Number((os.uptime() / 3600).toFixed(1))
    }, null, 2)
  }
}
