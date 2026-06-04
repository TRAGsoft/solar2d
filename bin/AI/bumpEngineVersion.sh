#!/usr/bin/env zsh

set -e

SOLAR2D_PROJECT_DIR="${${0:A}%%/bin/*}"
BUILD_WORKFLOW=".github/workflows/build.yml"
BUILD_WORKFLOW_PATH="${SOLAR2D_PROJECT_DIR}/${BUILD_WORKFLOW}"

pushChanges=true
targetVersion=""

for arg in "$@"; do
	case "${arg}" in
		--no-push)
			pushChanges=false
			;;
		--help|-h)
			echo "Usage: zsh bin/AI/bumpEngineVersion.sh [version] [--no-push]"
			echo
			echo "Without a version, bumps the FORK_BUILD_NAME suffix by one letter."
			exit 0
			;;
		*)
			if [[ -n "${targetVersion}" ]]; then
				echo "Unexpected argument '${arg}'." 1>&2
				exit 1
			fi
			targetVersion="${arg}"
			;;
	esac
done

if [[ ! -f "${BUILD_WORKFLOW_PATH}" ]]; then
	echo "Missing ${BUILD_WORKFLOW}." 1>&2
	exit 1
fi

currentBuildNumber="$(awk '$1 == "BUILD_NUMBER:" {print $2; exit}' "${BUILD_WORKFLOW_PATH}")"
currentYear="$(awk '$1 == "YEAR:" {print $2; exit}' "${BUILD_WORKFLOW_PATH}")"
currentForkBuildName="$(awk '$1 == "FORK_BUILD_NAME:" {print $2; exit}' "${BUILD_WORKFLOW_PATH}")"

if [[ -z "${currentBuildNumber}" || -z "${currentYear}" || -z "${currentForkBuildName}" ]]; then
	echo "Could not read BUILD_NUMBER, YEAR, and FORK_BUILD_NAME from ${BUILD_WORKFLOW}." 1>&2
	exit 1
fi

currentVersion="${currentYear}.${currentBuildNumber}.${currentForkBuildName}"

increment_suffix() {
	local suffix="$1"
	local carry=1
	local result=""
	local alphabet="abcdefghijklmnopqrstuvwxyz"

	if [[ ! "${suffix}" =~ "^[a-z]+$" ]]; then
		echo "Cannot auto-bump suffix '${suffix}'. Pass an explicit version instead." 1>&2
		return 1
	fi

	for (( i = ${#suffix}; i >= 1; i-- )); do
		local char="${suffix[i]}"
		local index="${alphabet[(i)$char]}"

		if (( carry == 0 )); then
			result="${char}${result}"
		elif (( index == 26 )); then
			result="a${result}"
		else
			result="${alphabet[index + 1]}${result}"
			carry=0
		fi
	done

	if (( carry == 1 )); then
		result="a${result}"
	fi

	echo "${result}"
}

if [[ -z "${targetVersion}" ]]; then
	nextForkBuildName="$(increment_suffix "${currentForkBuildName}")"
	targetYear="${currentYear}"
	targetBuildNumber="${currentBuildNumber}"
	targetForkBuildName="${nextForkBuildName}"
	targetVersion="${targetYear}.${targetBuildNumber}.${targetForkBuildName}"
else
	if [[ ! "${targetVersion}" =~ "^[0-9]{4}\\.[0-9]+\\.[A-Za-z0-9][A-Za-z0-9._-]*$" ]]; then
		echo "Version '${targetVersion}' must look like 2026.3730.k." 1>&2
		exit 1
	fi

	targetYear="${targetVersion%%.*}"
	remainder="${targetVersion#*.}"
	targetBuildNumber="${remainder%%.*}"
	targetForkBuildName="${remainder#*.}"
fi

if ! git diff --quiet -- "${BUILD_WORKFLOW_PATH}" || ! git diff --cached --quiet -- "${BUILD_WORKFLOW_PATH}"; then
	echo "${BUILD_WORKFLOW} already has changes. Commit or stash them before bumping the engine version." 1>&2
	exit 1
fi

if [[ "${currentVersion}" == "${targetVersion}" ]]; then
	echo "No change: engine version is already ${targetVersion}."
	exit 0
fi

if git rev-parse -q --verify "refs/tags/${targetVersion}" >/dev/null; then
	echo "Tag '${targetVersion}' already exists." 1>&2
	exit 1
fi

TARGET_YEAR="${targetYear}" \
TARGET_BUILD_NUMBER="${targetBuildNumber}" \
TARGET_FORK_BUILD_NAME="${targetForkBuildName}" \
perl -0pi -e '
	s/^([[:space:]]*BUILD_NUMBER:[[:space:]]*)[0-9]+/$1$ENV{"TARGET_BUILD_NUMBER"}/m or exit 1;
	s/^([[:space:]]*YEAR:[[:space:]]*)[0-9]{4}/$1$ENV{"TARGET_YEAR"}/m or exit 1;
	s/^([[:space:]]*FORK_BUILD_NAME:[[:space:]]*)[A-Za-z0-9._-]+/$1$ENV{"TARGET_FORK_BUILD_NAME"}/m or exit 1;
' "${BUILD_WORKFLOW_PATH}"

git commit --only "${BUILD_WORKFLOW_PATH}" -m "${targetVersion}"
git tag "${targetVersion}"

if [[ "${pushChanges}" == true ]]; then
	currentBranch="$(git branch --show-current)"
	if [[ -z "${currentBranch}" ]]; then
		echo "Cannot push from detached HEAD. Commit and tag were created locally." 1>&2
		exit 1
	fi

	git push origin "HEAD:${currentBranch}"
	git push origin "refs/tags/${targetVersion}"
fi

echo "engineVersion: ${currentVersion} -> ${targetVersion}"
echo "Committed $(git rev-parse --short=10 HEAD) on $(git branch --show-current)."
echo "Tagged ${targetVersion}."
