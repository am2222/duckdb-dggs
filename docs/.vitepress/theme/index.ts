import DefaultTheme from 'vitepress/theme'
import type { Theme } from 'vitepress'
import { LiveSql, provideLiveSqlConfig } from 'duckdb-docs-live/vitepress'
import liveSqlConfig from '../live-sql.config'

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component('LiveSql', LiveSql)
    provideLiveSqlConfig(app, liveSqlConfig)
  },
} satisfies Theme
