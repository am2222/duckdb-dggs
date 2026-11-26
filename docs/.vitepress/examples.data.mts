// Every runnable doc example on the site plus docs/wasm-known-failures.json,
// for /wasm-status. Same pages and ids as the CI runner (`--docs docs`).
import { createExamplesLoader } from 'duckdb-docs-live/vitepress/examples-loader'

export default createExamplesLoader('**/*.md', { exclude: ['superpowers/**', 'node_modules/**'] })
