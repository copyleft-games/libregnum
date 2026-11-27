#!/bin/bash
# fake-ffmpeg.sh: a stand-in ffmpeg for the video exporter tests.
#
# tests/test-reel.c copies this script into a fresh temp directory per test
# (so each copy has its own path, and the exporter's per-path probe cache
# starts empty) next to these files:
#
#   encoders.txt  printed for `-encoders` (an `ffmpeg -hide_banner -encoders`
#                 listing); a missing file makes the probe fail
#   works         encoder names whose one-frame trial encode succeeds
#
# It records what it was asked in the same directory:
#
#   probes.log    one line per `-encoders` probe
#   trials.log    one line per trial encode (the encoder name)
#   argv.txt      the argv of the real encode, one argument per line
#
# A real encode reads stdin to EOF (the frames) and exits 0.

dir="$(dirname "$(readlink -f "${0}")")"

encoder_arg () {
	local prev=''
	local arg
	for arg in "$@"
	do
		if [[ "${prev}" == '-c:v' ]]
		then
			printf '%s\n' "${arg}"
			return
		fi
		prev="${arg}"
	done
}

if [[ " $* " == *' -encoders '* ]]
then
	echo probe >> "${dir}/probes.log"
	[[ -f "${dir}/encoders.txt" ]] || exit 1
	cat "${dir}/encoders.txt"
	exit 0
fi

if [[ " $* " == *' lavfi '* ]]
then
	enc="$(encoder_arg "$@")"
	echo "${enc}" >> "${dir}/trials.log"
	if [[ -f "${dir}/works" ]] && grep -qx -- "${enc}" "${dir}/works"
	then
		exit 0
	fi
	exit 1
fi

printf '%s\n' "$@" > "${dir}/argv.txt"
cat > /dev/null
exit 0
