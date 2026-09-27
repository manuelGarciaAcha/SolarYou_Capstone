# Running the host tests

These checks run on the computer instead of the esp. They check that the public header works from C and that the JSON serializer follows the draft
message rules. They do not communicate with physical sensors.

Run the commands from the `SolarYou_Sensor` folder after activating ESP-IDF 5.5.5.

## Check the C-compatible header

```powershell
gcc -std=c11 -Wall -Wextra -Werror `
  -Icomponents\sensor_service\include `
  -fsyntax-only host_tests\test_c_api_header.c
```

No output means the header passed the syntax check.

## Test the JSON serializer

This test compiles the firmware's `sensor_json.cpp` with the cJSON source from the active ESP-IDF installation:

```powershell
$cjsonDir = Join-Path $env:IDF_PATH 'components\json\cJSON'
$cjsonSource = Join-Path $cjsonDir 'cJSON.c'
$testObject = Join-Path $env:TEMP 'solaryou_cjson.o'
$testProgram = Join-Path $env:TEMP 'solaryou_json_tests.exe'

gcc -std=c11 -Wall -Wextra -Werror `
  "-I$cjsonDir" `
  -c $cjsonSource `
  -o $testObject

g++ -std=c++11 -Wall -Wextra -Werror `
  -Icomponents\sensor_service\include `
  "-I$cjsonDir" `
  components\sensor_service\sensor_json.cpp `
  host_tests\test_sensor_json.cpp `
  $testObject `
  -o $testProgram

& $testProgram
```

Expected output:

```text
PASS: draft nested sensor JSON semantics and buffer handling
```

The test checks nested objects, zero and negative values, invalid values that become `null`, raw light diagnostics, fault masks, 32-bit counters, invalid
arguments, and a buffer that is too small.
