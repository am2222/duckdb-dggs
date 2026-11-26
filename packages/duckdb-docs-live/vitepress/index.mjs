// VitePress entry: register LiveSql and provide the site config in enhanceApp.
// The engine (engine.mjs and @duckdb/duckdb-wasm) is not imported here; LiveSql
// loads it with import() on the first Run.
import './style.css';
import LiveSql from './LiveSql.vue';
import BuildNotice from './BuildNotice.vue';

export { LiveSql, BuildNotice };
export { provideLiveSqlConfig, useLiveSqlConfig, LIVE_SQL_CONFIG, liveState, usePublished } from './config.mjs';
