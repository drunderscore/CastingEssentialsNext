$filename = $args[0]

$name = "CastingEssentialsRed";
$shortstat = (git diff --shortstat 2>$null) | Out-String
$version = (git describe --always --tag --match "release-*" 2>$null) | Out-String
$version = $version.Trim().Substring(8)

$dir = Split-Path -path "$filename"
if(!(Test-Path "$filename" -PathType Any)) {
	New-Item -path "$dir" -type directory >$null
}

if ($shortstat) {
	$version = "${version}-dirty";
}

$current = "";
if(Test-Path "$filename" -PathType Leaf) {
	$current = Get-Content -Path "$filename"
}

$suffix = $args[1]
if ($suffix) {
	$version = "${version}${suffix}"
}

$target = "extern const char* const PLUGIN_NAME = ""$name"";
extern const char* const PLUGIN_VERSION_ID = ""$version"";
extern const char* const PLUGIN_FULL_VERSION = ""$name $version"";"

if ($target -ne $current) {
	echo $target | Out-File "$filename"
}
