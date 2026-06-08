#!/usr/bin/env bash
set -ex

WORKSPACE=$(cd "$(dirname "$0")/../.." && pwd)
export WORKSPACE

if [ -n "$CERT_PASSWORD" ]
then
    security delete-keychain build.keychain || true
    security create-keychain -p 'Password123' build.keychain
    security default-keychain -s build.keychain
    security import "$WORKSPACE/tools/GHAction/Certificates.p12" -A -P "$CERT_PASSWORD"
    security unlock-keychain -p 'Password123' build.keychain
    security set-keychain-settings build.keychain
    security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k 'Password123' build.keychain > /dev/null

    mkdir -p "$HOME/Library/MobileDevice/Provisioning Profiles"
    for PLATFORM_DIR in iphone tvos
    do
        cp "$WORKSPACE/platform/$PLATFORM_DIR"/*.mobileprovision "$HOME/Library/MobileDevice/Provisioning Profiles/"
    done
else
    # No signing certificate available (e.g. Switch-only test workflow).
    # subrepos/enterprise/build.sh invokes xcodebuild without any overrides,
    # and the .xcodeproj files hardcode 'Developer ID Application: Corona Labs Inc'.
    # Inject command-line build settings via a PATH shim so all downstream
    # xcodebuild calls become signing-free.
    XCB_WRAP_DIR="$(mktemp -d -t xcb-wrap)"
    cat > "$XCB_WRAP_DIR/xcodebuild" <<'EOF'
#!/usr/bin/env bash
# Inject signing-disabled build settings as command-line args (highest precedence
# in xcodebuild's setting evaluation) for normal build invocations. Covers base
# + SDK-conditional identities and the automatic-signing inputs
# (DEVELOPMENT_TEAM / provisioning profile), which is what triggers
# GatherProvisioningInputs to look up a cert.
#
# Skip injection for xcodebuild "verbs" that don't accept build settings as
# command-line args (e.g. -create-xcframework, -version, -showsdks, -list,
# -exportArchive, -importArchive). Without this guard the shim breaks them
# with `error: invalid argument 'CODE_SIGN_IDENTITY='.`.
for arg in "$@"; do
    case "$arg" in
        -create-xcframework|-version|-showsdks|-showBuildSettings|-list|-exportArchive|-importArchive|-resolvePackageDependencies|-showBuildTimingSummary)
            exec /usr/bin/xcodebuild "$@"
            ;;
    esac
done
exec /usr/bin/xcodebuild "$@" \
    CODE_SIGN_IDENTITY= \
    "CODE_SIGN_IDENTITY[sdk=*]=" \
    CODE_SIGNING_REQUIRED=NO \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGN_STYLE=Manual \
    DEVELOPMENT_TEAM= \
    PROVISIONING_PROFILE_SPECIFIER= \
    PROVISIONING_PROFILE=
EOF
    chmod +x "$XCB_WRAP_DIR/xcodebuild"
    export PATH="$XCB_WRAP_DIR:$PATH"
fi

java -version
echo $JAVA_HOME
cd "${WORKSPACE}/subrepos/enterprise"

if ! ./build.sh
then
    BUILD_FAILED=YES
    echo "BUILD FAILED"
fi

if [ -n "$CERT_PASSWORD" ]
then
    security default-keychain -s login.keychain
    security delete-keychain build.keychain &> /dev/null || true
fi

if [ "$BUILD_FAILED" = "YES" ]
then
    exit 1
fi

mkdir -p "$WORKSPACE/output/"
mv build/CoronaEnterprise.tgz "$WORKSPACE/output/CoronaNative.tar.gz"

(
    cd "$WORKSPACE/platform/android/sdk/build/intermediates/merged_native_libs/release/mergeReleaseNativeLibs/out/lib/"
    zip -9 "$WORKSPACE/output/AndroidDebugSymbols.zip" -r .
)
