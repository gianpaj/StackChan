# Original firmware build baseline

## Decision and scope

Local Ito speech uses the original StackChan application, codec and avatar.
The standalone green-dot firmware remains the device rollback target.
The implementation branch is `feat/local-ito`, based on official 1.6.0 source
`94856a77876255ceee36f2a7ffae40f572eeb1f2`. The supplied checkout and its
untracked files remain untouched.

Git dependencies are pinned in `firmware/repos.json`. Registry dependencies
are pinned in `firmware/dependencies.lock`. The bootstrap must accept a fully
applied patch or fail. Silently skipping an incompatible patch can produce a
build from the wrong source. Repeated bootstrap with existing commits and
patches does not require network access.

## Verification

An unmodified firmware build completed with ESP-IDF 5.5.4. Build artifacts,
SDK configuration, linker map and logs are retained under
`firmware/build/baseline-artifacts/` and `firmware/build/baseline-*.log`.
No device writes were part of this baseline.

- App: 3,730,352 bytes, SHA256
  `0b7d391a8f60543e5b3b9942619c43fd0a66b9e8032728913309e9e2f80b9f39`.
- Generated assets: 2,298,029 bytes, SHA256
  `2b6fa2d4c2c630c692380e27a46c0e7d46b57bfd7a3c5cf94b0a6fa2ed29637e`.
- Linker DIRAM use: 210,111 bytes, including 137,135 bytes of code,
  36,404 bytes of data, 35,544 bytes of BSS and 1,028 vector bytes.
- Runtime free memory and the largest internal allocation remain to be measured.
  The linker summary does not establish that the 239,948-byte Ito hot arena fits.

The bootstrap was checked with temporary local Git repositories for initial
clone, patch application, repeated offline use and conflicting patch rejection.
All six checked-out dependencies passed repeated verification without mutation.
