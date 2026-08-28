import React, { useEffect } from 'react'

export default function SansSerifAgentSidebar() {
  useEffect(() => {
    const styleId = 'crab-plugin-sans-serif-agent-sidebar'
    let style = document.getElementById(styleId) as HTMLStyleElement | null
    if (!style) {
      style = document.createElement('style')
      style.id = styleId
      style.textContent = `
        .chat, .chat *, [data-panel="chat"], [data-panel="chat"] * {
          font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Oxygen, Ubuntu, Cantarell, "Open Sans", "Helvetica Neue", sans-serif !important;
        }
      `
      document.head.appendChild(style)
    }

    return () => {
      const el = document.getElementById(styleId)
      if (el) el.remove()
    }
  }, [])

  return null
}
