
set -e

TEST_CERT_DIR="${TEST_TMPDIR}"
mkdir -p "${TEST_CERT_DIR}"
cd "$TEST_CERT_DIR"

openssl genrsa -out ca_key.pem 2048
openssl req -new -key ca_key.pem -out ca_cert.csr -batch -sha256
openssl x509 -req -days 730 -in ca_cert.csr -signkey ca_key.pem -out ca_cert.pem
