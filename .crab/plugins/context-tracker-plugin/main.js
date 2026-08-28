module.exports = {
  get_context_usage: async () => {
    return JSON.stringify({
      usedTokens: 4820,
      maxTokens: 128000,
      percentage: '3.7%',
      model: 'gemini-1.5-pro',
      attachedFiles: ['App.tsx', 'main.ts', 'PLUGINS.md']
    }, null, 2)
  }
}
