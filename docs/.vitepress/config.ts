import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'duck_dggs',
  description: 'DuckDB extension for Discrete Global Grid Systems powered by DGGRID v8',
  base: '/duckdb-dggs/',
  head: [
    ['link', { rel: 'icon', href: '/duckdb-dggs/favicon.svg' }]
  ],
  themeConfig: {
    logo: '/duck_dggs_icon.svg',
    nav: [
      { text: 'Guide', link: '/guide/getting-started' },
      { text: 'Functions', link: '/functions/utility' },
      { text: 'GitHub', link: 'https://github.com/am2222/duckdb-dggs' }
    ],
    sidebar: [
      {
        text: 'Guide',
        items: [
          { text: 'Getting Started', link: '/guide/getting-started' },
          { text: 'Grid Configuration', link: '/guide/grid-configuration' },
        ]
      },
      {
        text: 'Functions',
        items: [
          { text: 'Utility', link: '/functions/utility' },
          { text: 'Grid Statistics', link: '/functions/grid-statistics' },
          { text: 'Transforms - from GEO', link: '/functions/transforms-geo' },
          { text: 'Transforms - from SEQNUM', link: '/functions/transforms-seqnum' },
          { text: 'Transforms - from Q2DI', link: '/functions/transforms-q2di' },
          { text: 'Transforms - from Q2DD', link: '/functions/transforms-q2dd' },
          { text: 'Transforms - from PROJTRI', link: '/functions/transforms-projtri' },
          { text: 'Neighbors', link: '/functions/neighbors' },
          { text: 'Parent / Child', link: '/functions/parent-child' },
          { text: 'Hierarchical Indexes', link: '/functions/hierarchical-indexes' },
        ]
      }
    ],
    search: {
      provider: 'local'
    },
    outline: [2, 3],
  }
})
