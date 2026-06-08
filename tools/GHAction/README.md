# GitHub Actions Signing Assets

The macOS and iOS/tvOS GitHub Actions builds import
`tools/GHAction/Certificates.p12` when the `CERTIFICATES_P12_PASSWORD` secret
is set. The `.p12` must contain the private keys for the Apple signing
identities used by the build.

## Refresh Signing Assets

The refresh script creates these certificates through App Store Connect:

- Apple Development
- Developer ID Application

For iOS and tvOS template builds, it also recreates these provisioning profiles:

- `ios_Solar2D`, using a wildcard App ID, saved as
  `platform/iphone/ios.mobileprovision`
- `tvos_Solar2D`, using a wildcard App ID, saved as
  `platform/tvos/tvos.mobileprovision`

There are two modes:

```sh
zsh tools/GHAction/create_certificates_p12.sh --check
zsh tools/GHAction/create_certificates_p12.sh --refresh
```

`--check` validates the current local `Certificates.p12` and provisioning
profiles without creating anything.

`--refresh` creates those certificates and profiles through App Store Connect:

```sh
zsh tools/GHAction/create_certificates_p12.sh --refresh
```

This creates a new Apple Development certificate and a new Developer ID
Application certificate, exports them to `tools/GHAction/Certificates.p12`,
recreates the iOS/tvOS wildcard development provisioning profiles, and writes
them to the existing repo paths.

To also delete old remote certificates of the same types after the new ones are
created, pass:

```sh
zsh tools/GHAction/create_certificates_p12.sh --refresh --replace-certificates
```

Check the current local files without creating anything:

```sh
zsh tools/GHAction/create_certificates_p12.sh --check
```

The p12 password must match the GitHub Actions secret named
`CERTIFICATES_P12_PASSWORD`.
When `Certificates.p12` is regenerated with a different password, update that
GitHub secret before running signing builds. If the existing password is reused
for the new export, the secret does not need to change.

## App Store Connect Credentials

Save a personal-team App Store Connect team API key in macOS Keychain:

```sh
security add-generic-password -U -s "Solar2D_APP_STORE_CONNECT_API_KEY_KEY_ID" -a "Solar2D" -w "<key id>"
security add-generic-password -U -s "Solar2D_APP_STORE_CONNECT_API_KEY_ISSUER_ID" -a "Solar2D" -w "<issuer id>"
security add-generic-password -U -s "Solar2D_APP_STORE_CONNECT_API_KEY_CONTENT_B64" -a "Solar2D" -w "$(base64 -i AuthKey_<key id>.p8)"
security add-generic-password -U -s "Solar2D_CERTIFICATES_P12_PASSWORD" -a "Solar2D" -w "<p12 password>"
```

The API key needs access to certificates, identifiers, profiles, and devices.
It must be created under the personal Apple team that should own the Solar2D
certificates and profiles.

The refresh script reads `CERTIFICATES_P12_PASSWORD`, then a generic macOS
Keychain password with service `Solar2D_CERTIFICATES_P12_PASSWORD` and account
`Solar2D`.
