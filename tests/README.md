# prism tests

Regression suite for the prism renderer. Each `cases/*.test` file renders a scene
headlessly, compares it to a ground-truth image, and appends one entry per test to
`results.json`. `dashboard.html` plots that history.

## Running

```bash
scripts/build.sh            # build prism first (tests use build/bin.exe)
tests/run_tests.sh          # run every enabled case and record results
tests/run_tests.sh cornell  # only cases whose name contains "cornell"
tests/run_tests.sh -n       # run without recording
tests/run_tests.sh -u       # (re)generate ground truths from the current build
```

On Windows use `tests\run_tests.bat` with the same options (it needs PowerShell,
which ships with Windows). Run `-h` for all options.
