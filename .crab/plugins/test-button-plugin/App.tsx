import React, { useState } from 'react'

export const TestButtonApp: React.FC = () => {
  const [clickedCount, setClickedCount] = useState(0)

  const handleClick = () => {
    const next = clickedCount + 1
    setClickedCount(next)
    alert(`🎉 Тестовая кнопка из плагина работает! Кликов: ${next}`)
  }

  return (
    <button
      className="titlebar-test-btn"
      onClick={handleClick}
      title="Тестовая кнопка плагина рядом с настройками"
    >
      🧪 Тест ({clickedCount})
    </button>
  )
}

export default TestButtonApp
