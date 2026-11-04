# Bundled browser vector decoder

`vector.js` is an ES2020 browser module bundled from:

- `@mapbox/vector-tile` 3.0.0 — BSD-3-Clause, https://github.com/mapbox/vector-tile-js
- `pbf` 5.1.2 — BSD-3-Clause, https://github.com/mapbox/pbf
- `@mapbox/point-geometry` 1.1.0 — ISC, https://github.com/mapbox/point-geometry

Unmodified license texts are next to the bundle. Esbuild 0.28.2 is an authoring tool only and is not shipped in the runtime.

Developer rebuild from `scripts/map-app`: `npm install` then `npm run build:vendor`.
Normal users launch the batch file. They do not install Node.js, npm, Python, a GIS program or API credentials.

The renderer uses OpenFreeMap vector data and native browser Canvas for Thai shaping, rather than fetching raster maps from OpenStreetMap's standard tile servers. It renders a simple street/land-use map, not the provider's full Liberty style or contour/elevation cartography.
