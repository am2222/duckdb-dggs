// Markdown example extractor. Pure, dependency-free, no DOM/Node APIs.

const slug = (s) => s.toLowerCase().replace(/[^a-z0-9_]+/g, '-');

// Update single-quote state across one line of SQL (ignores `--` comments).
function scanQuotes(line, inQuote) {
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (c === "'") inQuote = !inQuote;
    else if (!inQuote && c === '-' && line[i + 1] === '-') break;
  }
  return inQuote;
}

const isArrow = (l) => /^--\s*→/.test(l);

function splitStatements(lines) {
  const statements = [];
  let cur = [];
  let inQuote = false;
  const flush = () => {
    if (cur.some((l) => l.trim() !== '')) {
      statements.push({ sql: cur.join('\n').trim(), expected: null });
    }
    cur = [];
  };
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (!inQuote && isArrow(line)) {
      flush();
      const last = statements[statements.length - 1];
      const parts = [line.replace(/^--\s*→\s*/, '').trimEnd()];
      while (
        i + 1 < lines.length &&
        /^--/.test(lines[i + 1]) &&
        !isArrow(lines[i + 1])
      ) {
        i++;
        parts.push(lines[i].replace(/^--\s?/, '').trimEnd());
      }
      if (last) last.expected = { kind: 'arrow', text: parts.join('\n') };
      continue;
    }
    cur.push(line);
    inQuote = scanQuotes(line, inQuote);
    if (!inQuote && line.trimEnd().endsWith(';')) flush();
  }
  flush();
  return statements;
}

export function extractExamples(markdown, page) {
  const lines = markdown.split(/\r?\n/);
  const examples = [];
  const counters = new Map();
  let heading = ''; // nearest `##` text
  let h1 = ''; // nearest `#` text, fallback when no `##` yet
  let hasH2 = false;

  const readFence = (start) => {
    // lines[start] is the opening fence; returns { info, body, end }
    const info = lines[start].replace(/^```/, '').trim();
    const body = [];
    let j = start + 1;
    while (j < lines.length && !/^```\s*$/.test(lines[j])) body.push(lines[j++]);
    return { info, body, end: j };
  };

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const h = /^(#{1,2})\s+(.*?)\s*#*\s*$/.exec(line);
    if (h) {
      if (h[1].length === 2) {
        heading = h[2];
        hasH2 = true;
      } else {
        h1 = h[2];
        heading = '';
        hasH2 = false;
      }
      continue;
    }
    if (!line.startsWith('```')) continue;
    const { info, body, end } = readFence(i);
    const words = info.split(/\s+/);
    const fenceLine = i + 1;
    const nextI = end;
    if (words[0] !== 'sql') {
      i = nextI;
      continue;
    }
    const mode = words.includes('norun')
      ? 'norun'
      : words.includes('run')
        ? 'run'
        : 'compare';
    const statements = splitStatements(body);

    // A following box-table fence belongs to the last statement.
    let k = end + 1;
    while (k < lines.length && lines[k].trim() === '') k++;
    if (k < lines.length && lines[k].startsWith('```')) {
      const box = readFence(k);
      const first = box.body.find((l) => l.trim() !== '');
      if (first && first.startsWith('┌') && statements.length) {
        statements[statements.length - 1].expected = {
          kind: 'box',
          text: box.body.join('\n').trim(),
        };
      }
    }

    const hasExpected = statements.some((s) => s.expected);
    if (statements.length && (mode !== 'compare' || hasExpected)) {
      const shown = hasH2 ? heading : h1 || 'top';
      const key = slug(shown);
      const n = (counters.get(key) || 0) + 1;
      counters.set(key, n);
      examples.push({
        id: `${page}#${key}/${n}`,
        page,
        heading: shown,
        line: fenceLine,
        mode,
        statements,
        needs: statements.some((s) => /\bLOAD\s+spatial\b/i.test(s.sql))
          ? ['spatial']
          : [],
      });
    }
    i = nextI;
  }
  return examples;
}
