import DefaultTheme from 'vitepress/theme'
import type { Theme } from 'vitepress'
import { LiveSql, WasmStatus, provideLiveSqlConfig } from 'duckdb-docs-live/vitepress'
import liveSqlConfig from '../live-sql.config'
// the same list CI and /wasm-status apply: listed examples show "known issue"
import knownFailures from '../../wasm-known-failures.json'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('LiveSql', LiveSql)
    app.component('WasmStatus', WasmStatus)
    provideLiveSqlConfig(app, { ...liveSqlConfig, knownFailures })
  },
} satisfies Theme
