# Kiseki Input

Kiseki Input is a cross-platform C++ CLI foundation for configuration-first input and screenshot automation workflows.

## Foundation Build

This build includes:

- C++ CLI executable: `kiseki`
- JSON configuration defaults, validation, and persistence
- Config-only local WebUI: `kiseki config-ui`
- Machine-readable capabilities: `kiseki capabilities`
- Human-readable diagnostics: `kiseki doctor`

Operational input simulation, screenshot capture, target resolution, notifications, and daemon behavior are implemented in separate feature slices after this foundation.

## Commands

```text
kiseki config path
kiseki config show
kiseki config validate
kiseki config-ui
kiseki capabilities
kiseki doctor
```

The WebUI exposes only:

```text
GET /api/config
PUT /api/config
GET /api/capabilities
```

It does not expose input, screenshot, notification, daemon, shell, or execution routes.

## Build

```bash
cmake -S . -B build -DKISEKI_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

When building with a multi-config generator, the executable is usually under `build/Debug/kiseki.exe` on Windows.
