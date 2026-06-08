#!/usr/bin/env ruby
# frozen_string_literal: true

require "base64"
require "fileutils"
require "json"
require "net/http"
require "openssl"
require "open3"
require "rexml/document"
require "tempfile"
require "time"
require "uri"

PROJECT_DIR = File.expand_path("../..", __dir__)
API_BASE = "https://api.appstoreconnect.apple.com"
DEFAULT_EXPIRY_WINDOW_DAYS = 45
KEYCHAIN_ACCOUNT = "Solar2D"
P12_PASSWORD_SERVICE = "Solar2D_CERTIFICATES_P12_PASSWORD"
P12_PATH = "tools/GHAction/Certificates.p12"

CertificateSpec = Struct.new(
    :label,
    :certificate_type,
    :expected_name_fragment,
    keyword_init: true
)

ProfileSpec = Struct.new(
    :label,
    :path,
    :profile_name,
    :profile_type,
    :bundle_identifier,
    :bundle_name,
    :device_platforms,
    :device_classes,
    :certificate_label,
    keyword_init: true
)

CERTIFICATE_SPECS = [
    CertificateSpec.new(
        label: "Apple Development",
        certificate_type: "DEVELOPMENT",
        expected_name_fragment: "Apple Development:"
    ),
    CertificateSpec.new(
        label: "Developer ID Application",
        certificate_type: "DEVELOPER_ID_APPLICATION",
        expected_name_fragment: "Developer ID Application:"
    )
].freeze

PROFILE_SPECS = [
    ProfileSpec.new(
        label: "iOS development",
        path: "platform/iphone/ios.mobileprovision",
        profile_name: "ios_Solar2D",
        profile_type: "IOS_APP_DEVELOPMENT",
        bundle_identifier: "*",
        bundle_name: "XC Wildcard",
        device_platforms: ["IOS"],
        device_classes: ["IPHONE", "IPAD"],
        certificate_label: "Apple Development"
    ),
    ProfileSpec.new(
        label: "tvOS development",
        path: "platform/tvos/tvos.mobileprovision",
        profile_name: "tvos_Solar2D",
        profile_type: "TVOS_APP_DEVELOPMENT",
        bundle_identifier: "*",
        bundle_name: "XC Wildcard",
        device_platforms: ["TV_OS"],
        device_classes: ["APPLE_TV"],
        certificate_label: "Apple Development"
    )
].freeze

def usage
    <<~TEXT
        Usage:
          zsh tools/GHAction/create_certificates_p12.sh --check [--expires-within-days N]
          zsh tools/GHAction/create_certificates_p12.sh --refresh [--expires-within-days N] [--replace-certificates]

        --check validates the current Certificates.p12 and provisioning profiles.
        --refresh creates new Apple Development and Developer ID Application certificates,
        exports them to tools/GHAction/Certificates.p12, recreates the iOS/tvOS wildcard
        development profiles, and writes them to platform/iphone and platform/tvos.

        App Store Connect credentials are read from environment variables or macOS Keychain:
          Solar2D_APP_STORE_CONNECT_API_KEY_KEY_ID
          Solar2D_APP_STORE_CONNECT_API_KEY_ISSUER_ID
          Solar2D_APP_STORE_CONNECT_API_KEY_CONTENT_B64

        The p12 password is read from CERTIFICATES_P12_PASSWORD or macOS
        Keychain service:
          #{P12_PASSWORD_SERVICE}
    TEXT
end

def fail_with(message)
    warn message
    exit 1
end

def redacted_command(args)
    redact_next_argument = false
    args.map do |arg|
        if redact_next_argument
            redact_next_argument = false
            "<redacted>"
        elsif ["-P", "-passin", "-passout", "--password"].include?(arg)
            redact_next_argument = true
            arg
        else
            arg
        end
    end.join(" ")
end

def shell_capture(*args, allow_failure: false, stdin_data: nil)
    stdout, stderr, status = Open3.capture3(*args, stdin_data: stdin_data)
    if !status.success? && !allow_failure
        fail_with("#{redacted_command(args)} failed:\n#{stderr.empty? ? stdout : stderr}")
    end

    [stdout, stderr, status]
end

def keychain_secret(service_name)
    stdout, _stderr, status = shell_capture(
        "security", "find-generic-password", "-a", KEYCHAIN_ACCOUNT, "-s", service_name, "-w",
        allow_failure: true
    )
    status.success? ? stdout.strip : nil
end

def env_or_keychain_secret(env_names, keychain_names)
    env_names.each do |name|
        return ENV[name] if ENV.key?(name) && !ENV[name].empty?
    end

    keychain_names.each do |name|
        secret = keychain_secret(name)
        return secret if secret && !secret.empty?
    end

    nil
end

def p12_password
    password = env_or_keychain_secret(["CERTIFICATES_P12_PASSWORD"], [P12_PASSWORD_SERVICE])
    return password if password && !password.empty?

    fail_with("Missing p12 password. Set CERTIFICATES_P12_PASSWORD or save #{P12_PASSWORD_SERVICE} in Keychain account #{KEYCHAIN_ACCOUNT}.")
end

def parse_plist_element(element)
    case element.name
    when "dict"
        result = {}
        children = element.elements.to_a
        index = 0
        while index < children.length
            key_element = children[index]
            value_element = children[index + 1]
            result[key_element.text.to_s] = parse_plist_element(value_element)
            index += 2
        end
        result
    when "array"
        element.elements.map { |child| parse_plist_element(child) }
    when "string", "key"
        element.text.to_s
    when "data"
        element.text.to_s.gsub(/\s+/, "")
    when "date"
        Time.parse(element.text.to_s)
    when "integer"
        element.text.to_i
    when "true"
        true
    when "false"
        false
    else
        element.text.to_s
    end
end

def parse_plist(xml)
    document = REXML::Document.new(xml)
    plist_root = document.root
    data_element = plist_root.elements.to_a.first
    parse_plist_element(data_element)
end

def profile_xml_from_strings(path)
    stdout, _stderr, status = shell_capture("strings", path, allow_failure: true)
    return nil unless status.success?

    lines = stdout.lines
    start_index = lines.index { |line| line.include?("<?xml") }
    end_index = lines.index { |line| line.include?("</plist>") }
    return nil unless start_index && end_index && end_index >= start_index

    lines[start_index..end_index].join.sub(/^.*<\?xml/, "<?xml")
end

def decode_profile(path)
    absolute_path = File.join(PROJECT_DIR, path)
    return { path: path, error: "missing" } unless File.exist?(absolute_path)

    stdout, stderr, status = shell_capture(
        "openssl", "cms", "-inform", "DER", "-verify", "-noverify", "-in", absolute_path,
        allow_failure: true
    )
    xml = status.success? ? stdout : profile_xml_from_strings(absolute_path)
    return { path: path, error: stderr.empty? ? "could not decode profile" : stderr.strip } unless xml && !xml.empty?

    plist = parse_plist(xml)
    certificates = Array(plist["DeveloperCertificates"]).map do |certificate_data|
        OpenSSL::X509::Certificate.new(Base64.decode64(certificate_data))
    end

    {
        path: path,
        plist: plist,
        name: plist["Name"],
        uuid: plist["UUID"],
        expiration_date: plist["ExpirationDate"],
        application_identifier: plist.dig("Entitlements", "application-identifier"),
        team_identifiers: plist["TeamIdentifier"],
        provisioned_devices: Array(plist["ProvisionedDevices"]),
        certificate_fingerprints: certificates.map { |certificate| certificate_sha1(certificate) },
        certificate_summaries: certificates.map do |certificate|
            {
                subject: certificate.subject.to_s,
                expires_at: certificate.not_after,
                fingerprint: certificate_sha1(certificate)
            }
        end
    }
rescue StandardError => error
    { path: path, error: error.message }
end

def certificate_sha1(certificate)
    OpenSSL::Digest::SHA1.hexdigest(certificate.to_der).upcase
end

def int_to_fixed_bytes(value, byte_count)
    hex = value.to_s(16)
    hex = "0#{hex}" if hex.length.odd?
    bytes = [hex].pack("H*")
    bytes = bytes.byteslice(1, bytes.length - 1) while bytes.length > byte_count
    ("\x00".b * (byte_count - bytes.length)) + bytes
end

def base64url(data)
    Base64.strict_encode64(data).tr("+/", "-_").delete("=")
end

class AppStoreConnectClient
    def initialize
        @key_id = env_or_keychain_secret(
            ["APP_STORE_CONNECT_API_KEY_KEY_ID", "Solar2D_APP_STORE_CONNECT_API_KEY_KEY_ID"],
            ["Solar2D_APP_STORE_CONNECT_API_KEY_KEY_ID"]
        )
        @issuer_id = env_or_keychain_secret(
            ["APP_STORE_CONNECT_API_KEY_ISSUER_ID", "Solar2D_APP_STORE_CONNECT_API_KEY_ISSUER_ID"],
            ["Solar2D_APP_STORE_CONNECT_API_KEY_ISSUER_ID"]
        )
        key_content = env_or_keychain_secret(
            ["APP_STORE_CONNECT_API_KEY_KEY", "APP_STORE_CONNECT_API_KEY_CONTENT_B64", "Solar2D_APP_STORE_CONNECT_API_KEY_CONTENT_B64"],
            ["Solar2D_APP_STORE_CONNECT_API_KEY_CONTENT_B64"]
        )

        fail_with("Missing App Store Connect API key id") if @key_id.nil?
        fail_with("Missing App Store Connect API issuer id") if @issuer_id.nil?
        fail_with("Missing App Store Connect API private key content") if key_content.nil?

        key_pem = key_content.include?("BEGIN PRIVATE KEY") ? key_content : Base64.decode64(key_content)
        @private_key = OpenSSL::PKey.read(key_pem)
    end

    def get(path, query = {})
        request_json(Net::HTTP::Get, path, query: query)
    end

    def post(path, body)
        request_json(Net::HTTP::Post, path, body: body)
    end

    def delete(path)
        request_json(Net::HTTP::Delete, path, expect_json: false)
    end

    def get_all(path, query = {})
        data = []
        next_url = nil

        loop do
            response = if next_url
                request_json(Net::HTTP::Get, next_url, absolute_url: true)
            else
                request_json(Net::HTTP::Get, path, query: query)
            end
            data.concat(Array(response["data"]))
            next_url = response.dig("links", "next")
            break if next_url.nil? || next_url.empty?
        end

        data
    end

    private

    def request_json(request_class, path, query: {}, body: nil, absolute_url: false, expect_json: true)
        uri = absolute_url ? URI(path) : URI("#{API_BASE}#{path}")
        uri.query = URI.encode_www_form(query) if !absolute_url && !query.empty?

        request = request_class.new(uri)
        request["Authorization"] = "Bearer #{jwt}"
        request["Content-Type"] = "application/json"
        request.body = JSON.generate(body) if body

        response = Net::HTTP.start(uri.hostname, uri.port, use_ssl: uri.scheme == "https") do |http|
            http.request(request)
        end

        unless response.code.to_i.between?(200, 299)
            fail_with("App Store Connect #{request.method} #{uri} failed with #{response.code}:\n#{response.body}")
        end

        expect_json && !response.body.to_s.empty? ? JSON.parse(response.body) : {}
    end

    def jwt
        header = { alg: "ES256", kid: @key_id, typ: "JWT" }
        payload = {
            iss: @issuer_id,
            exp: Time.now.to_i + 20 * 60,
            aud: "appstoreconnect-v1"
        }
        signing_input = "#{base64url(JSON.generate(header))}.#{base64url(JSON.generate(payload))}"
        digest = OpenSSL::Digest::SHA256.digest(signing_input)
        signature_der = @private_key.dsa_sign_asn1(digest)
        signature_sequence = OpenSSL::ASN1.decode(signature_der)
        raw_signature = signature_sequence.value.map { |integer| int_to_fixed_bytes(integer.value, 32) }.join
        "#{signing_input}.#{base64url(raw_signature)}"
    end
end

def create_csr(common_name)
    private_key = OpenSSL::PKey::RSA.new(2048)
    csr = OpenSSL::X509::Request.new
    csr.version = 0
    csr.subject = OpenSSL::X509::Name.parse("/CN=#{common_name}")
    csr.public_key = private_key.public_key
    csr.sign(private_key, OpenSSL::Digest::SHA256.new)
    [private_key, csr]
end

def create_certificate(client, spec)
    private_key, csr = create_csr("Solar2D #{spec.label} #{Time.now.utc.strftime("%Y%m%d%H%M%S")}")
    response = client.post(
        "/v1/certificates",
        {
            data: {
                type: "certificates",
                attributes: {
                    certificateType: spec.certificate_type,
                    csrContent: csr.to_pem
                }
            }
        }
    )

    certificate = response["data"]
    content = certificate.dig("attributes", "certificateContent")
    fail_with("Apple did not return certificateContent for #{spec.label}") if content.nil? || content.empty?

    parsed_certificate = OpenSSL::X509::Certificate.new(Base64.decode64(content))
    {
        spec: spec,
        id: certificate["id"],
        private_key: private_key,
        certificate: parsed_certificate,
        fingerprint: certificate_sha1(parsed_certificate),
        expires_at: parsed_certificate.not_after,
        subject: parsed_certificate.subject.to_s
    }
end

def remote_certificates(client, certificate_type)
    client.get_all(
        "/v1/certificates",
        {
            "filter[certificateType]" => certificate_type,
            "fields[certificates]" => "name,certificateType,displayName,serialNumber,platform,expirationDate,certificateContent,activated",
            "limit" => "200"
        }
    )
end

def delete_old_certificates(client, created_certificates)
    created_by_type = created_certificates.group_by { |certificate| certificate[:spec].certificate_type }
    created_by_type.each do |certificate_type, certificates|
        keep_ids = certificates.map { |certificate| certificate[:id] }
        remote_certificates(client, certificate_type).each do |remote_certificate|
            next if keep_ids.include?(remote_certificate["id"])

            puts "Deleting old #{certificate_type} certificate #{remote_certificate["id"]}"
            client.delete("/v1/certificates/#{remote_certificate["id"]}")
        end
    end
end

def create_bundle_id(client, spec)
    response = client.post(
        "/v1/bundleIds",
        {
            data: {
                type: "bundleIds",
                attributes: {
                    identifier: spec.bundle_identifier,
                    name: spec.bundle_name,
                    platform: "IOS"
                }
            }
        }
    )
    response["data"]
end

def find_or_create_bundle_id(client, spec)
    bundle_ids = client.get_all(
        "/v1/bundleIds",
        {
            "filter[identifier]" => spec.bundle_identifier,
            "fields[bundleIds]" => "identifier,name,platform,seedId",
            "limit" => "200"
        }
    )
    existing = bundle_ids.find { |bundle_id| bundle_id.dig("attributes", "identifier") == spec.bundle_identifier }
    return existing if existing

    puts "Creating wildcard bundle id #{spec.bundle_identifier} (#{spec.bundle_name})"
    create_bundle_id(client, spec)
end

def enabled_devices(client, spec)
    client.get_all(
        "/v1/devices",
        {
            "fields[devices]" => "name,platform,udid,status,deviceClass",
            "limit" => "200"
        }
    ).select do |device|
        attrs = device["attributes"] || {}
        next false unless attrs["status"] == "ENABLED"

        platform = attrs["platform"].to_s
        device_class = attrs["deviceClass"].to_s
        spec.device_platforms.include?(platform) || spec.device_classes.include?(device_class)
    end
end

def find_existing_profiles(client, spec)
    client.get_all(
        "/v1/profiles",
        {
            "filter[name]" => spec.profile_name,
            "filter[profileType]" => spec.profile_type,
            "filter[profileState]" => "ACTIVE",
            "fields[profiles]" => "name,profileType,profileState,profileContent,uuid,expirationDate,bundleId,devices,certificates",
            "include" => "certificates,devices,bundleId",
            "limit" => "200"
        }
    ).select { |profile| profile.dig("attributes", "name") == spec.profile_name }
end

def create_profile(client, spec, bundle_id, certificate, devices)
    relationships = {
        bundleId: { data: { type: "bundleIds", id: bundle_id["id"] } },
        certificates: { data: [{ type: "certificates", id: certificate[:id] }] },
        devices: { data: devices.map { |device| { type: "devices", id: device["id"] } } }
    }

    response = client.post(
        "/v1/profiles",
        {
            data: {
                type: "profiles",
                attributes: {
                    name: spec.profile_name,
                    profileType: spec.profile_type
                },
                relationships: relationships
            }
        }
    )
    response["data"]
end

def download_profile(client, profile, destination_path)
    profile_id = profile["id"]
    response = client.get(
        "/v1/profiles/#{profile_id}",
        {
            "fields[profiles]" => "name,profileContent,uuid,expirationDate,profileType"
        }
    )
    content = response.dig("data", "attributes", "profileContent")
    fail_with("Profile #{profile_id} did not include profileContent") if content.nil? || content.empty?

    absolute_path = File.join(PROJECT_DIR, destination_path)
    FileUtils.mkdir_p(File.dirname(absolute_path))
    File.binwrite(absolute_path, Base64.decode64(content))
end

def recreate_profiles(client, created_certificates)
    development_certificate = created_certificates.find { |certificate| certificate[:spec].label == "Apple Development" }
    fail_with("No newly created Apple Development certificate is available for profile creation") unless development_certificate

    PROFILE_SPECS.each do |spec|
        puts "Refreshing #{spec.label} profile"
        devices = enabled_devices(client, spec)
        fail_with("No enabled App Store Connect devices found for #{spec.label}; development profiles require at least one registered device.") if devices.empty?

        bundle_id = find_or_create_bundle_id(client, spec)

        find_existing_profiles(client, spec).each do |profile|
            puts "Deleting old #{spec.label} profile #{profile["id"]}"
            client.delete("/v1/profiles/#{profile["id"]}")
        end

        profile = create_profile(client, spec, bundle_id, development_certificate, devices)
        download_profile(client, profile, spec.path)
        decoded = decode_profile(spec.path)
        fail_with("#{spec.label} profile was written but could not be decoded: #{decoded[:error]}") if decoded[:error]

        puts "#{spec.label}: wrote #{spec.path}, expires #{decoded[:expiration_date].utc.iso8601}"
    end
end

def export_created_certificates_to_p12(created_certificates, password)
    temp_dir = Dir.mktmpdir("solar2d-signing-assets")
    temp_keychain = File.join(temp_dir, "solar2d-signing.keychain-db")
    temp_password = "solar2d-#{Process.pid}-#{Time.now.to_i}"

    shell_capture("security", "create-keychain", "-p", temp_password, temp_keychain)
    shell_capture("security", "unlock-keychain", "-p", temp_password, temp_keychain)

    created_certificates.each do |created_certificate|
        cert_path = File.join(temp_dir, "#{created_certificate[:spec].label.gsub(/\W+/, "_")}.cer")
        key_path = File.join(temp_dir, "#{created_certificate[:spec].label.gsub(/\W+/, "_")}.key")
        p12_path = File.join(temp_dir, "#{created_certificate[:spec].label.gsub(/\W+/, "_")}.p12")

        File.write(cert_path, created_certificate[:certificate].to_pem)
        File.write(key_path, created_certificate[:private_key].to_pem)

        shell_capture(
            "openssl", "pkcs12", "-export",
            "-inkey", key_path,
            "-in", cert_path,
            "-out", p12_path,
            "-passout", "pass:#{password}",
            "-name", created_certificate[:spec].label
        )
        shell_capture("security", "import", p12_path, "-k", temp_keychain, "-A", "-P", password)
    end

    absolute_output = File.join(PROJECT_DIR, P12_PATH)
    FileUtils.mkdir_p(File.dirname(absolute_output))
    shell_capture(
        "security", "export",
        "-k", temp_keychain,
        "-t", "identities",
        "-f", "pkcs12",
        "-o", absolute_output,
        "-P", password
    )

    puts "Wrote #{P12_PATH}"
ensure
    shell_capture("security", "delete-keychain", temp_keychain, allow_failure: true) if temp_keychain
    FileUtils.rm_rf(temp_dir) if temp_dir
end

def verify_p12(password)
    absolute_path = File.join(PROJECT_DIR, P12_PATH)
    return ["#{P12_PATH}: missing"] unless File.exist?(absolute_path)

    temp_dir = Dir.mktmpdir("solar2d-p12-check")
    temp_keychain = File.join(temp_dir, "check.keychain-db")
    temp_password = "solar2d-check-#{Process.pid}-#{Time.now.to_i}"

    shell_capture("security", "create-keychain", "-p", temp_password, temp_keychain)
    shell_capture("security", "unlock-keychain", "-p", temp_password, temp_keychain)
    _stdout, stderr, status = shell_capture(
        "security", "import", absolute_path, "-k", temp_keychain, "-P", password, "-A",
        allow_failure: true
    )
    return ["#{P12_PATH}: import failed (#{stderr.strip})"] unless status.success?

    stdout, _stderr, _status = shell_capture("security", "find-identity", "-p", "codesigning", "-v", temp_keychain, allow_failure: true)
    identities = stdout.lines.grep(/".+"/).map(&:strip)
    messages = identities.map { |identity| "#{P12_PATH}: #{identity}" }

    CERTIFICATE_SPECS.each do |spec|
        unless identities.any? { |identity| identity.include?(spec.expected_name_fragment) }
            messages << "#{P12_PATH}: missing #{spec.expected_name_fragment}"
        end
    end

    messages
ensure
    shell_capture("security", "delete-keychain", temp_keychain, allow_failure: true) if temp_keychain
    FileUtils.rm_rf(temp_dir) if temp_dir
end

def profile_status(spec, decoded, expiry_window_seconds)
    return ["#{spec.label}: #{spec.path} is not readable (#{decoded[:error]})"] if decoded[:error]

    messages = []
    expires_at = decoded[:expiration_date]
    expected_app_id_suffix = ".#{spec.bundle_identifier}"

    if expires_at <= Time.now
        messages << "#{spec.label}: expired on #{expires_at.utc.iso8601}"
    elsif expires_at <= Time.now + expiry_window_seconds
        messages << "#{spec.label}: expires soon on #{expires_at.utc.iso8601}"
    else
        messages << "#{spec.label}: profile valid until #{expires_at.utc.iso8601}"
    end

    messages << "#{spec.label}: name is #{decoded[:name]}"
    messages << "#{spec.label}: app identifier is #{decoded[:application_identifier]}"
    unless decoded[:application_identifier].to_s.end_with?(expected_app_id_suffix)
        messages << "#{spec.label}: expected wildcard app identifier ending in #{expected_app_id_suffix}"
    end

    messages
end

mode = nil
expiry_window_days = DEFAULT_EXPIRY_WINDOW_DAYS
replace_certificates = false
arguments = ARGV.dup
until arguments.empty?
    argument = arguments.shift
    case argument
    when "--check"
        mode = :check
    when "--refresh"
        mode = :refresh
    when "--replace-certificates"
        replace_certificates = true
    when "--expires-within-days"
        value = arguments.shift
        fail_with("--expires-within-days requires a positive integer") unless value && value.match?(/\A[0-9]+\z/)
        expiry_window_days = value.to_i
    when "--help", "-h"
        puts usage
        exit 0
    else
        fail_with("Unknown argument #{argument}\n\n#{usage}")
    end
end

fail_with(usage) if mode.nil?

expiry_window_seconds = expiry_window_days * 24 * 60 * 60
password = p12_password

if mode == :refresh
    client = AppStoreConnectClient.new
    created_certificates = CERTIFICATE_SPECS.map do |spec|
        puts "Creating #{spec.label} certificate"
        created = create_certificate(client, spec)
        puts "#{spec.label}: #{created[:fingerprint]} expires #{created[:expires_at].utc.iso8601}"
        created
    end

    export_created_certificates_to_p12(created_certificates, password)
    recreate_profiles(client, created_certificates)
    delete_old_certificates(client, created_certificates) if replace_certificates
end

messages = []
messages.concat(verify_p12(password))

PROFILE_SPECS.each do |spec|
    decoded = decode_profile(spec.path)
    messages.concat(profile_status(spec, decoded, expiry_window_seconds))
end

messages.each { |message| puts message }

failure_patterns = [
    "missing",
    "import failed",
    "not readable",
    "expired on",
    "expires soon",
    "expected wildcard"
]

exit 1 if messages.any? { |message| failure_patterns.any? { |pattern| message.include?(pattern) } }
