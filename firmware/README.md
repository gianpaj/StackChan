
## Build

### Fetch Dependencies

Run these commands from `firmware/`. `repos.json` pins Git dependencies to
full commit IDs. The bootstrap reuses local commits and checks that the Xiaozhi
patch applies or is already applied. Conflicting dependency changes stop it.
Keep `dependencies.lock` to preserve the ESP-IDF component versions.

```bash
python3 ./fetch_repos.py
```

### Tool Chains

[ESP-IDF v5.5.4](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/index.html)

### Build

```bash
idf.py build
```

### Host-side tests

The motion coordinate helpers can be tested without ESP-IDF hardware:

```bash
cmake -S tests -B build-host-tests
cmake --build build-host-tests
ctest --test-dir build-host-tests --output-on-failure
```

### Flash

```bash
idf.py flash
```

### Local speech profile

See [Local Ito speech](LOCAL_ITO.md) for the on-device USB speech build, commands
and storage layout.
