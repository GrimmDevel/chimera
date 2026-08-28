module.exports = {
  execute_crabman_request: async (args) => {
    const method = (args.method || 'GET').toUpperCase()
    const url = args.url || ''
    if (!url) return 'Error: URL is required'

    try {
      const options = {
        method,
        headers: {
          'Content-Type': 'application/json',
          'User-Agent': 'CrabMan-API-Client/1.0'
        }
      }
      if ((method === 'POST' || method === 'PUT') && args.body) {
        options.body = typeof args.body === 'string' ? args.body : JSON.stringify(args.body)
      }

      const startTime = Date.now()
      const res = await fetch(url, options)
      const duration = Date.now() - startTime
      const text = await res.text()

      let parsedBody
      try {
        parsedBody = JSON.parse(text)
      } catch {
        parsedBody = text
      }

      return JSON.stringify(
        {
          ok: res.ok,
          status: res.status,
          statusText: res.statusText,
          durationMs: duration,
          response: parsedBody
        },
        null,
        2
      )
    } catch (err) {
      return JSON.stringify({ ok: false, error: err.message }, null, 2)
    }
  }
}
