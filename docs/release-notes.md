Download the installer matching your operating system and CPU from **Assets**:

- **Windows x64:** `.exe` installer; `.zip` for a portable installation.
- **macOS 14+:** `Darwin-arm64.pkg` for Apple Silicon or `Darwin-x86_64.pkg` for Intel. Archives are also available.
- **Linux:** `.deb` for Ubuntu 22.04+ / compatible Debian systems; `.tar.gz` for a custom prefix. Choose `x86_64` or `aarch64`.

Every package has a matching `.sha256` file. Windows packages include the optional IbInputSimulator DLL; device-specific driver prerequisites remain separate.

After installation, open a new terminal and run `kiseki --version`. To begin CUA operation, run `kiseki background cua setup`: it installs a missing official Cua Driver and checks for updates at workflow startup at most once per 24 hours (`kiseki background cua update` checks now). Kiseki-started installs and updates do not connect other agents to the Driver, run its desktop self-test, edit shell PATH on update, or register the Windows elevated logon task. macOS requires the normal Accessibility and Screen Recording grants.

CUA convenience commands follow Cua Driver 0.49.9: element actions send `element_token` (`--element-index` needs `--snapshot-id`), `state` exposes the Driver's bounds (`--max-elements`, `--max-image-dimension`, `--since`, …) and takes no screenshot without `--output`, `text` keeps the Driver's batched typing unless `--delay-ms` is given, `draw` batches its segments through `run_steps`, JPEG capture is offered where the Driver supports it, and `session state` was removed with the Driver's `get_session_state`. See `docs/cua.md`.

Read the [installation guide](https://github.com/win10ogod/kiseki-input/blob/master/docs/install.md) or open `share/kiseki/install.html` in your installation for step-by-step instructions, agent skill locations, and troubleshooting.
