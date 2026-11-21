/** How a transcript line of the agent run is shown (lines come from AgentRun: prompts, texts, tool calls, status). */
export type AgentLineKind = 'user' | 'tool' | 'done' | 'error' | 'text';
export function agentLineKind(line: string): AgentLineKind {
  if (line.startsWith('› ')) return 'user';
  if (line.startsWith('→ ')) return 'tool';
  if (/^агент закончил$|^agent finished$/i.test(line)) return 'done';
  if (/^(ошибка агента|агент остановлен|не удалось запустить)/i.test(line) || /^error\b/i.test(line)) return 'error';
  return 'text';
}
