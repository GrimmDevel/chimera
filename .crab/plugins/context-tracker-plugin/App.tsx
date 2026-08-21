import React, { useState, useEffect } from 'react'

export interface ContextStats {
  usedTokens: number
  maxTokens: number
  modelName: string
  attachedFiles: string[]
  promptTokens: number
  completionTokens: number
  estimatedCost: number
}

export const ContextTrackerApp: React.FC = () => {
  const [open, setOpen] = useState(false)
  const [stats, setStats] = useState<ContextStats>({
    usedTokens: 4820,
    maxTokens: 128000,
    modelName: 'gemini-1.5-pro',
    attachedFiles: ['App.tsx', 'main.ts', 'PLUGINS.md'],
    promptTokens: 4100,
    completionTokens: 720,
    estimatedCost: 0.007
  })

  // Periodically refresh context stats estimation
  useEffect(() => {
    const interval = setInterval(() => {
      // Simulate real-time context token updates
      setStats((prev) => {
        const delta = Math.floor(Math.random() * 20) - 5
        const newUsed = Math.max(1000, prev.usedTokens + delta)
        return {
          ...prev,
          usedTokens: newUsed,
          promptTokens: Math.floor(newUsed * 0.85),
          completionTokens: Math.floor(newUsed * 0.15)
        }
      })
    }, 4000)
    return () => clearInterval(interval)
  }, [])

  const percent = Math.min(100, Math.round((stats.usedTokens / stats.maxTokens) * 100))
  
  const getProgressColor = (p: number) => {
    if (p > 85) return '#f38ba8' // Red warning
    if (p > 60) return '#f9e2af' // Yellow warning
    return '#89b4fa' // Normal blue
  }

  const formatTokens = (num: number) => {
    if (num >= 1000) return `${(num / 1000).toFixed(1)}k`
    return num.toString()
  }

  return (
    <div className="ctx-tracker-root">
      {/* Status Bar Pill */}
      <div className="ctx-pill" onClick={() => setOpen(!open)} title="Нажмите для деталей контекста ИИ">
        <span className="ctx-icon">🧠</span>
        <span className="ctx-text">
          {formatTokens(stats.usedTokens)} / {formatTokens(stats.maxTokens)}
        </span>
        <div className="ctx-bar-bg">
          <div
            className="ctx-bar-fill"
            style={{
              width: `${percent}%`,
              background: getProgressColor(percent)
            }}
          />
        </div>
        <span className="ctx-percent">{percent}%</span>
      </div>

      {/* Detail Modal / Dropdown */}
      {open && (
        <div className="ctx-dropdown">
          <div className="ctx-header">
            <span className="ctx-title">Context Usage Tracker</span>
            <button className="ctx-close" onClick={() => setOpen(false)}>✕</button>
          </div>

          <div className="ctx-body">
            <div className="ctx-row">
              <span className="ctx-label">Active Model:</span>
              <span className="ctx-val model">{stats.modelName}</span>
            </div>

            <div className="ctx-row">
              <span className="ctx-label">Used Tokens:</span>
              <span className="ctx-val">{stats.usedTokens.toLocaleString()} / {stats.maxTokens.toLocaleString()}</span>
            </div>

            <div className="ctx-row">
              <span className="ctx-label">Prompt / Completion:</span>
              <span className="ctx-val">{stats.promptTokens.toLocaleString()} / {stats.completionTokens.toLocaleString()}</span>
            </div>

            <div className="ctx-row">
              <span className="ctx-label">Est. Session Cost:</span>
              <span className="ctx-val cost">${stats.estimatedCost.toFixed(4)}</span>
            </div>

            {/* Meter Bar */}
            <div className="ctx-meter-container">
              <div className="ctx-meter-label">
                <span>Context Window Fill</span>
                <span>{percent}%</span>
              </div>
              <div className="ctx-meter-bg">
                <div
                  className="ctx-meter-fill"
                  style={{
                    width: `${percent}%`,
                    background: getProgressColor(percent)
                  }}
                />
              </div>
            </div>

            {/* Attached Files List */}
            <div className="ctx-files-section">
              <div className="ctx-files-title">
                <span>Context Files ({stats.attachedFiles.length})</span>
              </div>
              <div className="ctx-files-list">
                {stats.attachedFiles.map((file) => (
                  <div key={file} className="ctx-file-item">
                    📄 {file}
                  </div>
                ))}
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

export default ContextTrackerApp
