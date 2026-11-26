// markdown-it plugin: wraps every runnable SQL example (and its documented
// output block) in <LiveSql>. Runs at build time in Node; it must never import
// the engine. The original rendered markup stays in the component's default
// slot, so the page without JavaScript is unchanged.
import { extractExamples } from '../src/extract.mjs';

const ATTR_ESCAPES = { '&': '&amp;', '"': '&quot;', "'": '&#39;', '<': '&lt;', '>': '&gt;' };

export function escapeAttr(s) {
  return s.replace(/[&"'<>]/g, (c) => ATTR_ESCAPES[c]);
}

// docs-root-relative path without `.md`, the page id CI uses
export function pageId(relativePath) {
  return String(relativePath ?? '').replace(/\\/g, '/').replace(/\.md$/, '');
}

const isBox = (token) =>
  token?.type === 'fence' &&
  (token.content.split('\n').find((l) => l.trim() !== '') ?? '').startsWith('┌');

export function liveSql(md, opts = {}) {
  const tag = opts.component ?? 'LiveSql';
  md.core.ruler.push('live_sql', (state) => {
    const rel = state.env?.relativePath;
    if (!rel) return;
    // Use state.src: it is exactly the source markdown-it tokenised (VitePress
    // strips frontmatter first), so example.line and token.map agree.
    const examples = extractExamples(state.src, pageId(rel));
    if (examples.length === 0) return;
    const byLine = new Map(examples.map((ex) => [ex.line, ex]));
    const out = [];
    const tokens = state.tokens;
    for (let i = 0; i < tokens.length; i++) {
      const t = tokens[i];
      const ex = t.type === 'fence' && t.map ? byLine.get(t.map[0] + 1) : undefined;
      if (!ex) {
        out.push(t);
        continue;
      }
      const payload = { ...ex, source: t.content.replace(/\n$/, '') };
      const open = new state.Token('html_block', '', 0);
      open.content = `<${tag} :example="${escapeAttr(JSON.stringify(payload))}">\n`;
      out.push(open, t);
      if (isBox(tokens[i + 1])) out.push(tokens[++i]);
      const close = new state.Token('html_block', '', 0);
      close.content = `</${tag}>\n`;
      out.push(close);
    }
    state.tokens = out;
  });
}

export default liveSql;
