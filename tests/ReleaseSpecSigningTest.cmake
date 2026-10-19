if (NOT DEFINED VIX_RELEASE_TOOL OR NOT EXISTS "${VIX_RELEASE_TOOL}")
  message(FATAL_ERROR "VIX_RELEASE_TOOL must name the built release signer")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(root "${CMAKE_CURRENT_BINARY_DIR}/release-spec-signing-${suffix}")
file(MAKE_DIRECTORY "${root}")
file(WRITE "${root}/payload.txt" "versioned target SDK archive fixture\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar czf archive.tar.gz --format=gnutar payload.txt
  WORKING_DIRECTORY "${root}"
  RESULT_VARIABLE archive_result
  OUTPUT_QUIET ERROR_VARIABLE archive_error)
if (NOT archive_result EQUAL 0)
  message(FATAL_ERROR "could not create release-spec fixture archive: ${archive_error}")
endif()

file(WRITE "${root}/trust.toml"
  "format = 1\n\n[[trusted-key]]\nkey-id = \"runtime-test\"\nalgorithm = \"ed25519\"\npublic-key = \"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a\"\n")
file(WRITE "${root}/release-spec.toml"
  "version = \"2.4.0\"\nchannel = \"stable\"\nsequence = 8\n\n"
  "[[artifact]]\nid = \"host-linux\"\nhost = \"x86_64-linux-gnu\"\n"
  "location = \"https://releases.invalid/host.tar.gz\"\narchive = \"tar.gz\"\nfile = \"archive.tar.gz\"\n\n"
  "[[component]]\nid = \"wasi-sdk-p2\"\nkind = \"target-sdk\"\ntarget = \"wasm32-wasip2\"\n"
  "location = \"https://releases.invalid/wasi-sdk.tar.gz\"\narchive = \"tar.gz\"\nfile = \"archive.tar.gz\"\n"
  "install-path = \"target-sdks\"\n")

set(test_private_key
  "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "VIX_RELEASE_KEY_ID=runtime-test"
    "VIX_RELEASE_SIGNING_KEY_HEX=${test_private_key}"
    "VIX_RELEASE_TRUST_FILE=${root}/trust.toml"
    "${VIX_RELEASE_TOOL}" --spec release-spec.toml
  WORKING_DIRECTORY "${root}"
  RESULT_VARIABLE signing_result
  OUTPUT_VARIABLE signing_output
  ERROR_VARIABLE signing_error)
if (NOT signing_result EQUAL 0)
  message(FATAL_ERROR "release signer rejected a valid multi-component spec: ${signing_output}${signing_error}")
endif()
file(READ "${root}/release-manifest.toml" manifest)
file(READ "${root}/release-signature.toml" signature)
if (NOT manifest MATCHES "target = \"wasm32-wasip2\"" OR
    NOT manifest MATCHES "kind = \"target-sdk\"" OR
    NOT signature MATCHES "algorithm = \"ed25519\"")
  message(FATAL_ERROR "signed release metadata omitted the requested target component or signature")
endif()

file(READ "${root}/release-spec.toml" invalid_spec)
string(REPLACE "file = \"archive.tar.gz\"" "file = \"../archive.tar.gz\"" invalid_spec "${invalid_spec}")
file(WRITE "${root}/release-spec-invalid.toml" "${invalid_spec}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "VIX_RELEASE_KEY_ID=runtime-test"
    "VIX_RELEASE_SIGNING_KEY_HEX=${test_private_key}"
    "VIX_RELEASE_TRUST_FILE=${root}/trust.toml"
    "${VIX_RELEASE_TOOL}" --spec release-spec-invalid.toml
  WORKING_DIRECTORY "${root}"
  RESULT_VARIABLE invalid_result
  OUTPUT_QUIET ERROR_QUIET)
if (invalid_result EQUAL 0)
  message(FATAL_ERROR "release signer accepted a traversal path in a local archive reference")
endif()

string(REPLACE "target = \"wasm32-wasip2\"" "target = \"wasm32-wasi\"" invalid_spec "${invalid_spec}")
file(WRITE "${root}/release-spec-preview1.toml" "${invalid_spec}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "VIX_RELEASE_KEY_ID=runtime-test"
    "VIX_RELEASE_SIGNING_KEY_HEX=${test_private_key}"
    "VIX_RELEASE_TRUST_FILE=${root}/trust.toml"
    "${VIX_RELEASE_TOOL}" --spec release-spec-preview1.toml
  WORKING_DIRECTORY "${root}"
  RESULT_VARIABLE preview1_result
  OUTPUT_QUIET ERROR_QUIET)
if (preview1_result EQUAL 0)
  message(FATAL_ERROR "release signer accepted the obsolete WASI Preview 1 target identity")
endif()
