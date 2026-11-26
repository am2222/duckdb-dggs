// Compare a documented expected output with a live result.
// Pure, dependency-free, no DOM/Node APIs.
import { numericallyEqual } from './sql.mjs';

const NUM_SRC = '-?\\d+(?:\\.\\d+)?(?:e[-+]?\\d+)?';

const normalise = (s) =>
  String(s)
    .replace(/(['"])(\w+)\1\s*:/g, '$2:')
    .replace(/\s+/g, ' ')
    .replace(/,\s*/g, ', ')
    .trim();

// Split expected text into literal text, number and wildcard parts.
function tokenise(expected) {
  const parts = [];
  const re = new RegExp(`${NUM_SRC}|\\(\\.\\.\\.\\)|\\.\\.\\.|\\u2026`, 'gi');
  let last = 0;
  let m;
  while ((m = re.exec(expected))) {
    if (m.index > last) parts.push({ t: 'text', v: expected.slice(last, m.index) });
    const v = m[0];
    parts.push(/^[-\d]/.test(v) ? { t: 'num', v } : { t: 'any' });
    last = m.index + v.length;
  }
  if (last < expected.length) parts.push({ t: 'text', v: expected.slice(last) });
  return parts;
}

function numberMatches(exp, act) {
  const isInt = (v) => /^[-+]?\d+$/.test(v);
  const canon = (v) => v.replace(/^\+/, '').replace(/^-(0+)$/, '$1');
  if (isInt(exp) && isInt(act)) return canon(exp) === canon(act);
  if (numericallyEqual(exp, act)) return true;
  const mant = exp.split(/e/i)[0];
  const dot = mant.indexOf('.');
  if (dot < 0 || /e/i.test(exp)) return false;
  const places = mant.length - dot - 1;
  const x = Number(act);
  if (!Number.isFinite(x)) return false;
  return Number(x.toFixed(places)) === Number(exp);
}

export function cellsMatch(expected, actual) {
  let exp = normalise(expected);
  const act = normalise(actual);
  // DuckDB renders VARCHAR values unquoted; docs quote them per value.
  if (!act.includes("'")) exp = exp.replace(/'([^']*)'/g, '$1');
  const parts = tokenise(exp);
  const numRe = new RegExp(NUM_SRC, 'iy');
  const go = (pi, pos) => {
    if (pi === parts.length) return pos === act.length;
    const p = parts[pi];
    if (p.t === 'text') return act.startsWith(p.v, pos) && go(pi + 1, pos + p.v.length);
    if (p.t === 'num') {
      numRe.lastIndex = pos;
      const m = numRe.exec(act);
      return !!m && numberMatches(p.v, m[0]) && go(pi + 1, pos + m[0].length);
    }
    for (let q = pos; q <= act.length; q++) if (go(pi + 1, q)) return true;
    return false;
  };
  return go(0, 0);
}

const splitCells = (line) =>
  line
    .trim()
    .replace(/^│/, '')
    .replace(/│$/, '')
    .split('│')
    .map((c) => c.trim());

export function parseBox(text) {
  const lines = text.split(/\r?\n/).filter((l) => l.trim() !== '');
  const names = lines.length > 1 ? splitCells(lines[1]) : [];
  const types = lines.length > 2 ? splitCells(lines[2]) : [];
  const start = lines.findIndex((l) => l.trim().startsWith('├')) + 1;
  const rows = [];
  let elided = false;
  if (start > 0) {
    for (let i = start; i < lines.length; i++) {
      const l = lines[i].trim();
      if (l.startsWith('└')) break;
      if (!l.startsWith('│')) continue;
      const cells = splitCells(l);
      if (cells.length && cells.every((c) => c === '·')) elided = true;
      else if (cells.length === 1 && /\d+ rows/.test(cells[0])) elided = true;
      else rows.push(cells);
    }
  }
  if (lines.some((l) => /\d+ rows/.test(l))) elided = true;
  return { names, types, rows, elided };
}

export function compareResult(expected, actual) {
  if (expected.kind === 'box') {
    const box = parseBox(expected.text);
    if (box.elided) return { status: 'shown' };
    if (box.rows.length !== actual.rows.length) {
      return {
        status: 'differs',
        detail: `expected ${box.rows.length} row(s), got ${actual.rows.length}`,
      };
    }
    for (let r = 0; r < box.rows.length; r++) {
      const e = box.rows[r];
      const a = actual.rows[r];
      const n = Math.max(e.length, a.length);
      for (let c = 0; c < n; c++) {
        if (e[c] === undefined || a[c] === undefined || !cellsMatch(e[c], a[c])) {
          return {
            status: 'differs',
            detail: `row ${r + 1}, column ${c + 1}: expected ${e[c] ?? '(missing)'}, got ${a[c] ?? '(missing)'}`,
          };
        }
      }
    }
    return { status: 'match' };
  }
  const got = actual.rows.map((r) => r.join(', ')).join('\n');
  if (cellsMatch(expected.text, got)) return { status: 'match' };
  return { status: 'differs', detail: `expected ${expected.text}, got ${got}` };
}
