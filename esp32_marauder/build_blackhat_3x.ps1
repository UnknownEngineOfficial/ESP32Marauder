$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $projectRoot

arduino-cli compile `
  --library "$repoRoot\libraries\ESPAsyncWebServer" `
  --library "$projectRoot\libraries\LinkedList" `
  --library "$projectRoot\libraries\ArduinoJson" `
  --library "$projectRoot\libraries\NimBLE-Arduino" `
  --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=min_spiffs" `
  "$projectRoot"
