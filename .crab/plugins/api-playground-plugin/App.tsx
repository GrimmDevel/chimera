import React, { useState } from 'react'

export type HttpMethod = 'GET' | 'POST' | 'PUT' | 'PATCH' | 'DELETE' | 'HEAD' | 'OPTIONS'
export type BodyType = 'none' | 'json' | 'urlencoded' | 'raw'
export type AuthType = 'none' | 'bearer' | 'basic' | 'apikey'

export interface KeyValueParam {
  id: string
  key: string
  value: string
  enabled: boolean
}

export interface HistoryItem {
  id: string
  timestamp: string
  method: HttpMethod
  url: string
  status: number | string
  statusText: string
  durationMs: number
  headers: Record<string, string>
  body: string
  bodyType: BodyType
  requestHeaders: KeyValueParam[]
}

export interface ResponseDetails {
  status: number | string
  statusText: string
  durationMs: number
  sizeBytes: number
  headers: Record<string, string>
  data: string
  ok: boolean
}

export const CrabManApp: React.FC = () => {
  // Main Request State
  const [method, setMethod] = useState<HttpMethod>('GET')
  const [url, setUrl] = useState<string>('https://jsonplaceholder.typicode.com/todos/1')
  const [activeReqTab, setActiveReqTab] = useState<'params' | 'headers' | 'body' | 'auth'>('params')
  
  // Params & Headers
  const [queryParams, setQueryParams] = useState<KeyValueParam[]>([
    { id: '1', key: '', value: '', enabled: true }
  ])
  const [headers, setHeaders] = useState<KeyValueParam[]>([
    { id: '1', key: 'Content-Type', value: 'application/json', enabled: true },
    { id: '2', key: 'Accept', value: 'application/json', enabled: true }
  ])

  // Body
  const [bodyType, setBodyType] = useState<BodyType>('none')
  const [bodyText, setBodyText] = useState<string>('{\n  "title": "CrabMan REST",\n  "completed": false\n}')
  const [formParams, setFormParams] = useState<KeyValueParam[]>([
    { id: '1', key: '', value: '', enabled: true }
  ])

  // Auth
  const [authType, setAuthType] = useState<AuthType>('none')
  const [bearerToken, setBearerToken] = useState<string>('')
  const [basicUser, setBasicUser] = useState<string>('')
  const [basicPass, setBasicPass] = useState<string>('')
  const [apiKeyName, setApiKeyName] = useState<string>('X-API-Key')
  const [apiKeyValue, setApiKeyValue] = useState<string>('')
  const [apiKeyLocation, setApiKeyLocation] = useState<'header' | 'query'>('header')

  // Execution & Response State
  const [loading, setLoading] = useState<boolean>(false)
  const [response, setResponse] = useState<ResponseDetails | null>(null)
  const [activeResTab, setActiveResTab] = useState<'pretty' | 'raw' | 'headers'>('pretty')
  const [copied, setCopied] = useState<boolean>(false)

  // History & Sidebar
  const [history, setHistory] = useState<HistoryItem[]>([])
  const [showHistory, setShowHistory] = useState<boolean>(false)

  // Sync URL query string when queryParams change
  const updateUrlWithParams = (newParams: KeyValueParam[], currentUrl: string) => {
    try {
      const baseUrl = currentUrl.split('?')[0]
      const active = newParams.filter((p) => p.enabled && p.key.trim() !== '')
      if (active.length === 0) return baseUrl

      const searchParams = new URLSearchParams()
      active.forEach((p) => searchParams.append(p.key.trim(), p.value))
      return `${baseUrl}?${searchParams.toString()}`
    } catch {
      return currentUrl
    }
  }

  const handleQueryParamChange = (index: number, field: 'key' | 'value' | 'enabled', val: any) => {
    const updated = [...queryParams]
    updated[index] = { ...updated[index], [field]: val }

    if (index === updated.length - 1 && (updated[index].key || updated[index].value)) {
      updated.push({ id: Date.now().toString(), key: '', value: '', enabled: true })
    }

    setQueryParams(updated)
    setUrl(updateUrlWithParams(updated, url))
  }

  const removeQueryParam = (index: number) => {
    if (queryParams.length <= 1) {
      setQueryParams([{ id: Date.now().toString(), key: '', value: '', enabled: true }])
      return
    }
    const updated = queryParams.filter((_, i) => i !== index)
    setQueryParams(updated)
    setUrl(updateUrlWithParams(updated, url))
  }

  const handleHeaderChange = (index: number, field: 'key' | 'value' | 'enabled', val: any) => {
    const updated = [...headers]
    updated[index] = { ...updated[index], [field]: val }
    if (index === updated.length - 1 && (updated[index].key || updated[index].value)) {
      updated.push({ id: Date.now().toString(), key: '', value: '', enabled: true })
    }
    setHeaders(updated)
  }

  const removeHeader = (index: number) => {
    if (headers.length <= 1) {
      setHeaders([{ id: Date.now().toString(), key: '', value: '', enabled: true }])
      return
    }
    setHeaders(headers.filter((_, i) => i !== index))
  }

  const handleFormParamChange = (index: number, field: 'key' | 'value' | 'enabled', val: any) => {
    const updated = [...formParams]
    updated[index] = { ...updated[index], [field]: val }
    if (index === updated.length - 1 && (updated[index].key || updated[index].value)) {
      updated.push({ id: Date.now().toString(), key: '', value: '', enabled: true })
    }
    setFormParams(updated)
  }

  const formatJsonBody = () => {
    try {
      const parsed = JSON.parse(bodyText)
      setBodyText(JSON.stringify(parsed, null, 2))
    } catch (err) {
      alert('Invalid JSON: ' + (err as Error).message)
    }
  }

  const sendRequest = async () => {
    const targetUrl = url.trim()
    if (!targetUrl) return

    setLoading(true)
    const startTime = Date.now()

    try {
      const requestHeaders: Record<string, string> = {}
      headers.forEach((h) => {
        if (h.enabled && h.key.trim() !== '') {
          requestHeaders[h.key.trim()] = h.value
        }
      })

      let finalUrl = targetUrl
      if (authType === 'bearer' && bearerToken.trim()) {
        requestHeaders['Authorization'] = `Bearer ${bearerToken.trim()}`
      } else if (authType === 'basic' && (basicUser || basicPass)) {
        const credentials = btoa(`${basicUser}:${basicPass}`)
        requestHeaders['Authorization'] = `Basic ${credentials}`
      } else if (authType === 'apikey' && apiKeyName.trim() && apiKeyValue) {
        if (apiKeyLocation === 'header') {
          requestHeaders[apiKeyName.trim()] = apiKeyValue
        } else {
          const separator = finalUrl.includes('?') ? '&' : '?'
          finalUrl = `${finalUrl}${separator}${encodeURIComponent(apiKeyName.trim())}=${encodeURIComponent(apiKeyValue)}`
        }
      }

      let requestBody: any = undefined
      if (method !== 'GET' && method !== 'HEAD') {
        if (bodyType === 'json' || bodyType === 'raw') {
          requestBody = bodyText
        } else if (bodyType === 'urlencoded') {
          const params = new URLSearchParams()
          formParams.forEach((f) => {
            if (f.enabled && f.key.trim() !== '') {
              params.append(f.key.trim(), f.value)
            }
          })
          requestBody = params.toString()
          if (!requestHeaders['Content-Type']) {
            requestHeaders['Content-Type'] = 'application/x-www-form-urlencoded'
          }
        }
      }

      const options: RequestInit = {
        method,
        headers: requestHeaders,
        body: requestBody
      }

      const res = await fetch(finalUrl, options)
      const duration = Date.now() - startTime
      const rawText = await res.text()

      const resHeaders: Record<string, string> = {}
      res.headers.forEach((val, key) => {
        resHeaders[key] = val
      })

      let formattedData = rawText
      try {
        formattedData = JSON.stringify(JSON.parse(rawText), null, 2)
      } catch {}

      const newResponse: ResponseDetails = {
        status: res.status,
        statusText: res.statusText,
        durationMs: duration,
        sizeBytes: new Blob([rawText]).size,
        headers: resHeaders,
        data: formattedData,
        ok: res.ok
      }

      setResponse(newResponse)

      const histEntry: HistoryItem = {
        id: Date.now().toString(),
        timestamp: new Date().toLocaleTimeString(),
        method,
        url: finalUrl,
        status: res.status,
        statusText: res.statusText,
        durationMs: duration,
        headers: resHeaders,
        body: requestBody || '',
        bodyType,
        requestHeaders: headers
      }
      setHistory((prev) => [histEntry, ...prev.slice(0, 19)])
    } catch (err) {
      setResponse({
        status: 'ERR',
        statusText: 'Network Error',
        durationMs: Date.now() - startTime,
        sizeBytes: 0,
        headers: {},
        data: `Error executing HTTP request:\n${(err as Error).message}`,
        ok: false
      })
    } finally {
      setLoading(false)
    }
  }

  const restoreFromHistory = (item: HistoryItem) => {
    setMethod(item.method)
    setUrl(item.url)
    setBodyType(item.bodyType)
    if (typeof item.body === 'string') setBodyText(item.body)
    if (item.requestHeaders && item.requestHeaders.length > 0) {
      setHeaders(item.requestHeaders)
    }
  }

  const applyPreset = (
    m: HttpMethod,
    u: string,
    bType: BodyType = 'none',
    bText = '',
    presetHeaders: KeyValueParam[] = []
  ) => {
    setMethod(m)
    setUrl(u)
    setBodyType(bType)
    if (bText) setBodyText(bText)
    if (presetHeaders.length > 0) {
      setHeaders(presetHeaders)
    }
  }

  const copyResponse = () => {
    if (!response?.data) return
    navigator.clipboard.writeText(response.data).then(() => {
      setCopied(true)
      setTimeout(() => setCopied(false), 2000)
    })
  }

  return (
    <div className="crabman-app">
      {/* Flat Header */}
      <div className="app-header">
        <div className="brand">
          <span className="brand-title">CrabMan REST Client</span>
        </div>
        <div className="header-actions">
          <button
            className={`btn-history ${showHistory ? 'active' : ''}`}
            onClick={() => setShowHistory(!showHistory)}
          >
            History ({history.length})
          </button>
        </div>
      </div>

      <div className="app-layout">
        {/* History Sidebar */}
        {showHistory && (
          <div className="history-sidebar">
            <div className="sidebar-header">
              <span>History</span>
              {history.length > 0 && (
                <button className="btn-clear" onClick={() => setHistory([])}>
                  Clear
                </button>
              )}
            </div>
            <div className="history-list">
              {history.length === 0 ? (
                <div className="empty-history">No history in this session</div>
              ) : (
                history.map((h) => (
                  <div
                    key={h.id}
                    className="history-card"
                    onClick={() => restoreFromHistory(h)}
                  >
                    <div className="history-card-top">
                      <span className={`method-badge ${h.method}`}>{h.method}</span>
                      <span className="status-code">{h.status}</span>
                    </div>
                    <div className="history-url" title={h.url}>
                      {h.url}
                    </div>
                    <div className="history-time">
                      {h.timestamp} • {h.durationMs}ms
                    </div>
                  </div>
                ))
              )}
            </div>
          </div>
        )}

        {/* Main Work Area */}
        <div className="main-content">
          {/* Flat Address Bar */}
          <div className="address-bar">
            <select
              className={`method-select ${method}`}
              value={method}
              onChange={(e) => setMethod(e.target.value as HttpMethod)}
            >
              <option value="GET">GET</option>
              <option value="POST">POST</option>
              <option value="PUT">PUT</option>
              <option value="PATCH">PATCH</option>
              <option value="DELETE">DELETE</option>
              <option value="HEAD">HEAD</option>
              <option value="OPTIONS">OPTIONS</option>
            </select>

            <input
              type="text"
              className="url-input"
              value={url}
              onChange={(e) => setUrl(e.target.value)}
              placeholder="https://api.example.com/v1/resource"
            />

            <button className="btn-send" onClick={sendRequest} disabled={loading}>
              {loading ? 'Sending...' : 'Send'}
            </button>
          </div>

          {/* Quiet Presets Bar */}
          <div className="presets-bar">
            <span className="presets-label">Presets:</span>
            <button
              className="preset-chip"
              onClick={() => applyPreset('GET', 'https://api.github.com/users/octocat')}
            >
              GitHub API
            </button>
            <button
              className="preset-chip"
              onClick={() => applyPreset('GET', 'https://jsonplaceholder.typicode.com/posts/1')}
            >
              JSONPlaceholder GET
            </button>
            <button
              className="preset-chip"
              onClick={() =>
                applyPreset(
                  'POST',
                  'https://jsonplaceholder.typicode.com/posts',
                  'json',
                  JSON.stringify({ title: 'CrabMan REST', body: 'Postman Client', userId: 1 }, null, 2)
                )
              }
            >
              JSONPlaceholder POST
            </button>
          </div>

          {/* Request Config Tabs */}
          <div className="req-config-panel">
            <div className="tabs-header">
              <button
                className={`tab-btn ${activeReqTab === 'params' ? 'active' : ''}`}
                onClick={() => setActiveReqTab('params')}
              >
                Params ({queryParams.filter((p) => p.key).length})
              </button>
              <button
                className={`tab-btn ${activeReqTab === 'headers' ? 'active' : ''}`}
                onClick={() => setActiveReqTab('headers')}
              >
                Headers ({headers.filter((h) => h.key).length})
              </button>
              <button
                className={`tab-btn ${activeReqTab === 'body' ? 'active' : ''}`}
                onClick={() => setActiveReqTab('body')}
              >
                Body {bodyType !== 'none' && `(${bodyType})`}
              </button>
              <button
                className={`tab-btn ${activeReqTab === 'auth' ? 'active' : ''}`}
                onClick={() => setActiveReqTab('auth')}
              >
                Auth {authType !== 'none' && `(${authType})`}
              </button>
            </div>

            <div className="tab-body">
              {activeReqTab === 'params' && (
                <div className="kv-table">
                  <div className="table-header">
                    <span>Key</span>
                    <span>Value</span>
                    <span>Action</span>
                  </div>
                  {queryParams.map((param, index) => (
                    <div key={param.id} className="table-row">
                      <input
                        type="checkbox"
                        checked={param.enabled}
                        onChange={(e) => handleQueryParamChange(index, 'enabled', e.target.checked)}
                      />
                      <input
                        type="text"
                        placeholder="Key"
                        value={param.key}
                        onChange={(e) => handleQueryParamChange(index, 'key', e.target.value)}
                      />
                      <input
                        type="text"
                        placeholder="Value"
                        value={param.value}
                        onChange={(e) => handleQueryParamChange(index, 'value', e.target.value)}
                      />
                      <button className="btn-del" onClick={() => removeQueryParam(index)}>
                        ✕
                      </button>
                    </div>
                  ))}
                </div>
              )}

              {activeReqTab === 'headers' && (
                <div className="kv-table">
                  <div className="table-header">
                    <span>Key</span>
                    <span>Value</span>
                    <span>Action</span>
                  </div>
                  {headers.map((h, index) => (
                    <div key={h.id} className="table-row">
                      <input
                        type="checkbox"
                        checked={h.enabled}
                        onChange={(e) => handleHeaderChange(index, 'enabled', e.target.checked)}
                      />
                      <input
                        type="text"
                        placeholder="Header Name"
                        value={h.key}
                        onChange={(e) => handleHeaderChange(index, 'key', e.target.value)}
                      />
                      <input
                        type="text"
                        placeholder="Value"
                        value={h.value}
                        onChange={(e) => handleHeaderChange(index, 'value', e.target.value)}
                      />
                      <button className="btn-del" onClick={() => removeHeader(index)}>
                        ✕
                      </button>
                    </div>
                  ))}
                </div>
              )}

              {activeReqTab === 'body' && (
                <div className="body-tab">
                  <div className="body-type-selector">
                    <label>
                      <input
                        type="radio"
                        name="bodyType"
                        checked={bodyType === 'none'}
                        onChange={() => setBodyType('none')}
                      />
                      none
                    </label>
                    <label>
                      <input
                        type="radio"
                        name="bodyType"
                        checked={bodyType === 'json'}
                        onChange={() => setBodyType('json')}
                      />
                      json
                    </label>
                    <label>
                      <input
                        type="radio"
                        name="bodyType"
                        checked={bodyType === 'urlencoded'}
                        onChange={() => setBodyType('urlencoded')}
                      />
                      x-www-form-urlencoded
                    </label>
                    <label>
                      <input
                        type="radio"
                        name="bodyType"
                        checked={bodyType === 'raw'}
                        onChange={() => setBodyType('raw')}
                      />
                      raw
                    </label>

                    {bodyType === 'json' && (
                      <button className="btn-format" onClick={formatJsonBody}>
                        Format JSON
                      </button>
                    )}
                  </div>

                  {bodyType === 'none' && (
                    <div className="empty-state">No body for this request</div>
                  )}

                  {(bodyType === 'json' || bodyType === 'raw') && (
                    <textarea
                      className="code-editor"
                      value={bodyText}
                      onChange={(e) => setBodyText(e.target.value)}
                      placeholder='{ "key": "value" }'
                    />
                  )}

                  {bodyType === 'urlencoded' && (
                    <div className="kv-table">
                      <div className="table-header">
                        <span>Key</span>
                        <span>Value</span>
                        <span>Action</span>
                      </div>
                      {formParams.map((f, index) => (
                        <div key={f.id} className="table-row">
                          <input
                            type="checkbox"
                            checked={f.enabled}
                            onChange={(e) => handleFormParamChange(index, 'enabled', e.target.checked)}
                          />
                          <input
                            type="text"
                            placeholder="Key"
                            value={f.key}
                            onChange={(e) => handleFormParamChange(index, 'key', e.target.value)}
                          />
                          <input
                            type="text"
                            placeholder="Value"
                            value={f.value}
                            onChange={(e) => handleFormParamChange(index, 'value', e.target.value)}
                          />
                          <button
                            className="btn-del"
                            onClick={() =>
                              setFormParams(formParams.filter((_, i) => i !== index))
                            }
                          >
                            ✕
                          </button>
                        </div>
                      ))}
                    </div>
                  )}
                </div>
              )}

              {activeReqTab === 'auth' && (
                <div className="auth-tab">
                  <div className="auth-row">
                    <label>Auth Type:</label>
                    <select
                      value={authType}
                      onChange={(e) => setAuthType(e.target.value as AuthType)}
                    >
                      <option value="none">No Auth</option>
                      <option value="bearer">Bearer Token</option>
                      <option value="basic">Basic Auth</option>
                      <option value="apikey">API Key</option>
                    </select>
                  </div>

                  {authType === 'bearer' && (
                    <div className="auth-fields">
                      <label>Token:</label>
                      <input
                        type="password"
                        placeholder="Bearer token string"
                        value={bearerToken}
                        onChange={(e) => setBearerToken(e.target.value)}
                      />
                    </div>
                  )}

                  {authType === 'basic' && (
                    <div className="auth-fields">
                      <label>User:</label>
                      <input
                        type="text"
                        placeholder="Username"
                        value={basicUser}
                        onChange={(e) => setBasicUser(e.target.value)}
                      />
                      <label>Pass:</label>
                      <input
                        type="password"
                        placeholder="Password"
                        value={basicPass}
                        onChange={(e) => setBasicPass(e.target.value)}
                      />
                    </div>
                  )}

                  {authType === 'apikey' && (
                    <div className="auth-fields">
                      <label>Key Name:</label>
                      <input
                        type="text"
                        placeholder="X-API-Key"
                        value={apiKeyName}
                        onChange={(e) => setApiKeyName(e.target.value)}
                      />
                      <label>Value:</label>
                      <input
                        type="password"
                        placeholder="Secret key"
                        value={apiKeyValue}
                        onChange={(e) => setApiKeyValue(e.target.value)}
                      />
                      <label>Add to:</label>
                      <select
                        value={apiKeyLocation}
                        onChange={(e) => setApiKeyLocation(e.target.value as any)}
                      >
                        <option value="header">Headers</option>
                        <option value="query">Query Params</option>
                      </select>
                    </div>
                  )}
                </div>
              )}
            </div>
          </div>

          {/* Response Viewer Panel */}
          <div className="response-panel">
            <div className="res-header-bar">
              <div className="res-tabs">
                <button
                  className={`res-tab ${activeResTab === 'pretty' ? 'active' : ''}`}
                  onClick={() => setActiveResTab('pretty')}
                >
                  Body
                </button>
                <button
                  className={`res-tab ${activeResTab === 'raw' ? 'active' : ''}`}
                  onClick={() => setActiveResTab('raw')}
                >
                  Raw
                </button>
                <button
                  className={`res-tab ${activeResTab === 'headers' ? 'active' : ''}`}
                  onClick={() => setActiveResTab('headers')}
                >
                  Headers ({response ? Object.keys(response.headers).length : 0})
                </button>
              </div>

              {response && (
                <div className="res-meta">
                  <span className={`status-pill ${response.ok ? 's-ok' : 's-err'}`}>
                    {response.status} {response.statusText}
                  </span>
                  <span className="meta-item">{response.durationMs} ms</span>
                  <span className="meta-item">
                    {(response.sizeBytes / 1024).toFixed(2)} KB
                  </span>
                  <button className="btn-copy" onClick={copyResponse}>
                    {copied ? 'Copied' : 'Copy'}
                  </button>
                </div>
              )}
            </div>

            <div className="res-body">
              {!response ? (
                <div className="response-placeholder">
                  <span>Click <strong>Send</strong> to execute request</span>
                </div>
              ) : activeResTab === 'pretty' || activeResTab === 'raw' ? (
                <pre className="json-output">{response.data}</pre>
              ) : (
                <div className="headers-output">
                  <div className="table-header">
                    <span>Header</span>
                    <span>Value</span>
                  </div>
                  {Object.entries(response.headers).map(([k, v]) => (
                    <div key={k} className="table-row">
                      <span className="h-key">{k}</span>
                      <span className="h-val">{v}</span>
                    </div>
                  ))}
                </div>
              )}
            </div>
          </div>
        </div>
      </div>
    </div>
  )
}
